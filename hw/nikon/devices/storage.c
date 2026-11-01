/*
 * Source-bound NVMe endpoint using QEMU's existing controller and block layer.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "qapi/error.h"
#include "hw/core/qdev-properties.h"
#include "hw/core/qdev-properties-system.h"
#include "hw/nikon/storage.h"
#include "hw/pci-host/gpex.h"
#include "hw/pci/pci_host.h"
#include "hw/core/irq.h"
#include "system/address-spaces.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "qemu/timer.h"
#include "migration/vmstate.h"

PCIDevice *nikon_storage_attach(PCIBus *bus, int devfn, BlockBackend *media,
                               const char *serial, Error **errp)
{
    PCIDevice *controller;
    DeviceState *dev;

    if (!bus || !media || !serial || !serial[0]) {
        error_setg(errp, "NVMe storage requires a PCI bus, media and serial");
        return NULL;
    }

    controller = pci_new(devfn, "nvme");
    dev = DEVICE(controller);
    qdev_prop_set_string(dev, "serial", serial);
    if (!qdev_prop_set_drive_err(dev, "drive", media, errp)) {
        object_unref(OBJECT(controller));
        return NULL;
    }
    /* Keep the mature controller's namespace, queue, DMA, interrupt, reset,
     * cancellation and error machinery. The original driver uses queue IDs
     * 0, 1 and 2; upstream defaults support all of them without a new profile.
     */
    if (!pci_realize_and_unref(controller, bus, errp)) {
        return NULL;
    }
    return controller;
}

/* The original compact PCIIO provider uses CFG0 target zero for the external
 * endpoint. Feeding that value to a generic ECAM/DWC bridge would address its
 * root device. These finite host ports preserve the original selector protocol
 * while the PCI device, config semantics, BAR and all block I/O remain native.
 */
typedef struct StorageWindow {
    uint32_t type, control, base, upper, limit, target, target_upper;
} StorageWindow;

#define TYPE_NIKON_STORAGE_BRIDGE "nikon.storage.bridge"
OBJECT_DECLARE_SIMPLE_TYPE(StorageBridge, NIKON_STORAGE_BRIDGE)

struct StorageBridge {
    GPEXHost parent_obj;
    char *root_path;
};

struct NikonStorageHost {
    SysBusDevice parent_obj;
    StorageBridge bridge;
    PCIDevice *endpoint;
    BlockBackend *media;
    char *media_id;
    MemoryRegion *dma_memory;
    MemoryRegion dbi, config, memory, phy, platform;
    MemoryRegion dma_root, dma_alias, msi;
    AddressSpace dma, pci_memory;
    qemu_irq irq[4];
    qemu_irq present;
    qemu_irq intx_inputs[4];
    QEMUTimer *transition;
    StorageWindow window[2];
    uint32_t viewport, msi_address, msi_upper, msi_enable, msi_mask, msi_status;
    uint32_t dbi_168, dbi_80c, dbi_8a8, dbi_8bc;
    uint32_t transaction_errors;
    uint32_t phy_registers[0x400 / 4];
    uint32_t platform_registers[4];
    uint32_t pending24, pending2c, cancel_event;
    uint8_t intx, state;
    bool cancelling;
    bool dma_initialized;
};

static const char *storage_root_bus_path(PCIHostState *host, PCIBus *rootbus)
{
    StorageBridge *s = NIKON_STORAGE_BRIDGE(host);

    return s->root_path;
}

static void storage_bridge_finalize(Object *obj)
{
    g_free(NIKON_STORAGE_BRIDGE(obj)->root_path);
}

static void storage_bridge_class_init(ObjectClass *klass, const void *data)
{
    PCIHostBridgeClass *hc = PCI_HOST_BRIDGE_CLASS(klass);

    /* GPEX's fixed 0000:00 path is for a single root. Each embedded CPU-domain
     * host needs its own migration identity without changing guest PCI layout.
     */
    hc->root_bus_path = storage_root_bus_path;
}

static const TypeInfo storage_bridge_type = {
    .name = TYPE_NIKON_STORAGE_BRIDGE,
    .parent = TYPE_GPEX_HOST,
    .instance_size = sizeof(StorageBridge),
    .instance_finalize = storage_bridge_finalize,
    .class_init = storage_bridge_class_init,
};

#define STORAGE_CONFIG_BASE 0x2a800000u
#define STORAGE_BAR_BASE 0x2a810000u
#define STORAGE_BAR_SIZE 0x000f0000u
#define STORAGE_EVENT24 0xf0000000u
#define STORAGE_EVENT2C ((1u << 10) | (1u << 14) | (1u << 18) | \
                         (1u << 26) | (1u << 30))

static void storage_irqs(NikonStorageHost *s)
{
    /* f2/f4 have no invented event producer. Source reset keeps them low. */
    qemu_set_irq(s->irq[0], 0);
    qemu_set_irq(s->irq[1], !!(s->pending24 | s->pending2c));
    qemu_set_irq(s->irq[2], 0);
    qemu_set_irq(s->irq[3], !!(s->msi_status & s->msi_enable & ~s->msi_mask));
}

static bool storage_window_valid(NikonStorageHost *s, unsigned index)
{
    StorageWindow *w = &s->window[index];
    return s->endpoint && s->state == 0x11 &&
           (w->control & (1u << 31)) && !w->upper && !w->target_upper &&
           (index ? w->type == 0 && w->base == STORAGE_BAR_BASE &&
                    w->limit == 0x2a8fffff && w->target == STORAGE_BAR_BASE :
                    w->type == 4 && w->base == STORAGE_CONFIG_BASE &&
                    w->limit == 0x2a80ffff && w->target == 0);
}

static MemTxResult storage_result(NikonStorageHost *s, MemTxResult result)
{
    /*
     * 105187e4 checks bit14 after all twelve native config/BAR wrappers.
     * Mapping a failed host transaction to this bit is a finite convention,
     * not physical PCIe Completion Timeout or an NVMe command error.
     */
    if (result != MEMTX_OK) {
        s->transaction_errors |= 1U << 14;
    }
    return result;
}

static void storage_intx(void *opaque, int pin, int level)
{
    NikonStorageHost *s = opaque;
    bool prior = s->intx != 0;

    s->intx = (s->intx & ~(1u << pin)) | (!!level << pin);
    if (s->state == 0x11 && !prior && s->intx) {
        /* 10517a18 dispatches bit28 as completion and bit29 as abort. A
         * consumed CQ's ordinary INTx release is not a hardware abort.
         */
        s->pending24 |= 1u << 28;
    }
    storage_irqs(s);
}

static AddressSpace *storage_dma(PCIBus *bus, void *opaque, int devfn)
{
    NikonStorageHost *s = opaque;
    return &s->dma;
}

static const PCIIOMMUOps storage_iommu = {
    .get_address_space = storage_dma,
};

static MemTxResult storage_msi_read(void *opaque, hwaddr addr, uint64_t *data,
                                  unsigned size, MemTxAttrs attrs)
{
    /* MSI is a PCI write transaction, never fabricated readable RAM. */
    *data = 0;
    return MEMTX_ERROR;
}

static MemTxResult storage_msi_write(void *opaque, hwaddr addr, uint64_t data,
                                   unsigned size, MemTxAttrs attrs)
{
    NikonStorageHost *s = opaque;
    if (s->state != 0x11 || data >= 32) {
        return MEMTX_ERROR;
    }
    s->msi_status |= (1u << data) & s->msi_enable;
    storage_irqs(s);
    return MEMTX_OK;
}

static const MemoryRegionOps storage_msi_ops = {
    .read_with_attrs = storage_msi_read,
    .write_with_attrs = storage_msi_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 4, .max_access_size = 4 },
    .impl = { .min_access_size = 4, .max_access_size = 4 },
};

static void storage_msi_address(NikonStorageHost *s)
{
    /* Overlay exists only in the PCI DMA view: CPU RAM at b1800740 is intact. */
    memory_region_set_enabled(&s->msi, false);
    memory_region_set_address(&s->msi,
        ((uint64_t)s->msi_upper << 32) | s->msi_address);
    memory_region_set_enabled(&s->msi,
        !(s->msi_address & 3) && (s->msi_address || s->msi_upper));
}

static bool storage_dbi_accepts(void *opaque, hwaddr addr, unsigned size,
                                bool is_write, MemTxAttrs attrs)
{
    if (addr < 0x100) {
        return (size == 1 || size == 2 || size == 4) &&
               !(addr & (size - 1)) && addr + size <= 0x100;
    }
    if (size != 4 || (addr & 3)) {
        return false;
    }
    switch (addr) {
    case 0x104:
        return !is_write;
    case 0x168: case 0x80c:
    case 0x820: case 0x824: case 0x828: case 0x82c: case 0x830:
    case 0x8a8: case 0x8bc:
    case 0x900: case 0x904: case 0x908: case 0x90c: case 0x910:
    case 0x914: case 0x918: case 0x91c:
        return true;
    default:
        return false;
    }
}

static uint64_t storage_dbi_read(void *opaque, hwaddr addr, unsigned size)
{
    NikonStorageHost *s = opaque;
    StorageWindow *w = &s->window[s->viewport];
    if (addr < 0x100) {
        uint32_t value = pci_host_config_read_common(
            PCI_DEVICE(&s->bridge.parent_obj.gpex_root), addr, 0x100, size);
        /* Software's link guard consumes this negotiated-speed field. The
         * virtual profile is 2 only after actual attached endpoint training.
         */
        if (addr <= 0x98 && addr + size > 0x98) {
            unsigned shift = (0x98 - addr) * 8;
            value = (value & ~(15u << shift)) |
                    ((s->state == 0x11 ? 2u : 0u) << shift);
        }
        return value;
    }
    switch (addr) {
    case 0x104: return s->transaction_errors;
    case 0x168: return s->dbi_168;
    case 0x80c: return s->dbi_80c;
    case 0x820: return s->msi_address;
    case 0x824: return s->msi_upper;
    case 0x828: return s->msi_enable;
    case 0x82c: return s->msi_mask;
    case 0x830: return s->msi_status;
    case 0x8a8: return s->dbi_8a8;
    case 0x8bc: return s->dbi_8bc;
    case 0x900: return s->viewport;
    case 0x904: return w->type;
    case 0x908: return w->control;
    case 0x90c: return w->base;
    case 0x910: return w->upper;
    case 0x914: return w->limit;
    case 0x918: return w->target;
    case 0x91c: return w->target_upper;
    default: g_assert_not_reached();
    }
}

static void storage_dbi_write(void *opaque, hwaddr addr, uint64_t value,
                              unsigned size)
{
    NikonStorageHost *s = opaque;
    StorageWindow *w = &s->window[s->viewport];
    if (addr < 0x100) {
        pci_host_config_write_common(PCI_DEVICE(&s->bridge.parent_obj.gpex_root),
                                     addr, 0x100, value, size);
        return;
    }
    switch (addr) {
    case 0x168: s->dbi_168 = value; break;
    case 0x80c: s->dbi_80c = value; break;
    case 0x820: s->msi_address = value; storage_msi_address(s); break;
    case 0x824: s->msi_upper = value; storage_msi_address(s); break;
    case 0x828: s->msi_enable = value; break;
    case 0x82c: s->msi_mask = value; break;
    case 0x830: s->msi_status &= ~value; break;
    case 0x8a8: s->dbi_8a8 = value; break;
    case 0x8bc: s->dbi_8bc = value; break;
    case 0x900:
        if (value > 1) {
            qemu_log_mask(LOG_GUEST_ERROR, "Nikon storage invalid iATU viewport\n");
            return;
        }
        s->viewport = value;
        break;
    case 0x904: w->type = value; break;
    case 0x908: w->control = value; break;
    case 0x90c: w->base = value; break;
    case 0x910: w->upper = value; break;
    case 0x914: w->limit = value; break;
    case 0x918: w->target = value; break;
    case 0x91c: w->target_upper = value; break;
    default: g_assert_not_reached();
    }
    storage_irqs(s);
}

static const MemoryRegionOps storage_dbi_ops = {
    .read = storage_dbi_read,
    .write = storage_dbi_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4,
               .accepts = storage_dbi_accepts },
    .impl = { .min_access_size = 1, .max_access_size = 4 },
};

static MemTxResult storage_config_read(void *opaque, hwaddr addr, uint64_t *data,
                                     unsigned size, MemTxAttrs attrs)
{
    NikonStorageHost *s = opaque;
    if (!storage_window_valid(s, 0) || addr + size > 0x1000) {
        *data = 0;
        return storage_result(s, MEMTX_ERROR);
    }
    *data = pci_host_config_read_common(s->endpoint, addr, 0x1000, size);
    return MEMTX_OK;
}

static MemTxResult storage_config_write(void *opaque, hwaddr addr, uint64_t data,
                                      unsigned size, MemTxAttrs attrs)
{
    NikonStorageHost *s = opaque;
    if (!storage_window_valid(s, 0) || addr + size > 0x1000) {
        return storage_result(s, MEMTX_ERROR);
    }
    pci_host_config_write_common(s->endpoint, addr, 0x1000, data, size);
    return MEMTX_OK;
}

static const MemoryRegionOps storage_config_ops = {
    .read_with_attrs = storage_config_read,
    .write_with_attrs = storage_config_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4 },
    .impl = { .min_access_size = 1, .max_access_size = 4 },
};

static MemTxResult storage_memory_read(void *opaque, hwaddr addr,
                                      uint64_t *data, unsigned size,
                                      MemTxAttrs attrs)
{
    NikonStorageHost *s = opaque;
    uint64_t value = 0;
    MemTxResult result;

    *data = 0;
    if (!storage_window_valid(s, 1)) {
        return storage_result(s, MEMTX_ERROR);
    }
    result = address_space_read(&s->pci_memory, STORAGE_BAR_BASE + addr,
                                attrs, &value, size);
    *data = le64_to_cpu(value);
    return storage_result(s, result);
}

static MemTxResult storage_memory_write(void *opaque, hwaddr addr,
                                       uint64_t data, unsigned size,
                                       MemTxAttrs attrs)
{
    NikonStorageHost *s = opaque;
    uint64_t value = cpu_to_le64(data);

    if (!storage_window_valid(s, 1)) {
        return storage_result(s, MEMTX_ERROR);
    }
    return storage_result(s,
        address_space_write(&s->pci_memory, STORAGE_BAR_BASE + addr,
                            attrs, &value, size));
}

static const MemoryRegionOps storage_memory_ops = {
    .read_with_attrs = storage_memory_read,
    .write_with_attrs = storage_memory_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 8 },
    .impl = { .min_access_size = 1, .max_access_size = 8 },
};

static bool storage_phy_accepts(void *opaque, hwaddr addr, unsigned size,
                                bool is_write, MemTxAttrs attrs)
{
    if (size != 4 || (addr & 3)) {
        return false;
    }
    switch (addr) {
    case 0: case 4: case 8: case 0xc: case 0x10: case 0x14: case 0x18:
    case 0x24: case 0x28: case 0x2c: case 0x3c: case 0x4c: case 0x50:
    case 0x54: case 0x58: case 0x5c:
    case 0x100: case 0x104: case 0x118: case 0x11c: case 0x120:
    case 0x124: case 0x128: case 0x12c: case 0x130: case 0x134:
    case 0x138: case 0x13c: case 0x140: case 0x144: case 0x148:
    case 0x14c: case 0x150: case 0x154: case 0x158: case 0x15c:
    case 0x210: case 0x218: case 0x300:
        return true;
    default:
        return false;
    }
}

static void storage_stop(NikonStorageHost *s)
{
    timer_del(s->transition);
    s->state = 0;
    s->cancelling = false;
    if (s->endpoint) {
        /* Native NVMe reset cancels queued commands and block AIO before a
         * cancellation completion is published. No fabricated write success.
         */
        pci_device_reset(s->endpoint);
    }
    s->intx = 0;
    s->pending24 = s->pending2c = s->msi_status = 0;
    storage_irqs(s);
}

static bool storage_training_requested(NikonStorageHost *s)
{
    return s->endpoint && (s->phy_registers[0] & 0x1f) == 0x14 &&
           (s->phy_registers[0xc / 4] & 3) == 3 &&
           (s->phy_registers[0x50 / 4] & (1u << 8));
}

static void storage_transition(void *opaque)
{
    NikonStorageHost *s = opaque;
    if (s->cancelling) {
        s->state = 0x15;
        s->pending2c |= s->cancel_event;
        if (s->cancel_event == (1u << 26)) {
            s->phy_registers[0x3c / 4] &= ~(1u << 4);
        } else {
            s->phy_registers[0x50 / 4] &= ~(1u << 16);
        }
        s->cancelling = false;
    } else if (storage_training_requested(s)) {
        s->state = 0x11;
    }
    storage_irqs(s);
}

static uint64_t storage_phy_read(void *opaque, hwaddr addr, unsigned size)
{
    NikonStorageHost *s = opaque;
    switch (addr) {
    case 0x24: return s->phy_registers[addr / 4] | s->pending24;
    case 0x2c: return s->phy_registers[addr / 4] | s->pending2c;
    case 0x58: return (s->phy_registers[addr / 4] & ~(0x3fu << 16)) |
                      (s->state << 16);
    case 0x210: return s->state == 0x11;
    default: return s->phy_registers[addr / 4];
    }
}

static void storage_phy_write(void *opaque, hwaddr addr, uint64_t value,
                              unsigned size)
{
    NikonStorageHost *s = opaque;
    uint32_t prior = s->phy_registers[addr / 4];
    if (addr == 0x58 || addr == 0x5c || addr == 0x210 || addr == 0x218) {
        qemu_log_mask(LOG_GUEST_ERROR, "Nikon storage write to status %" HWADDR_PRIx "\n", addr);
        return;
    }
    if (addr == 0x24) {
        s->pending24 &= ~value;
        value &= ~STORAGE_EVENT24;
    } else if (addr == 0x2c) {
        s->pending2c &= ~value;
        value &= ~STORAGE_EVENT2C;
    }
    s->phy_registers[addr / 4] = value;
    if ((addr == 0x300 && (value & 1)) ||
        (s->state == 0x11 && !storage_training_requested(s))) {
        storage_stop(s);
    } else if (((addr == 0x3c && (value & ~prior & (1u << 4))) ||
                (addr == 0x50 && (value & ~prior & (1u << 16)))) &&
               s->state == 0x11) {
        storage_stop(s);
        s->cancelling = true;
        s->cancel_event = addr == 0x3c ? (1u << 26) : (1u << 30);
        timer_mod(s->transition, qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + 1000000);
    } else if (addr == 0 && !s->cancelling && s->state != 0x11 &&
               storage_training_requested(s) && !timer_pending(s->transition)) {
        timer_mod(s->transition, qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + 1000000);
    }
    storage_irqs(s);
}

static const MemoryRegionOps storage_phy_ops = {
    .read = storage_phy_read,
    .write = storage_phy_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 4, .max_access_size = 4,
               .accepts = storage_phy_accepts },
    .impl = { .min_access_size = 4, .max_access_size = 4 },
};

static uint64_t storage_platform_read(void *opaque, hwaddr addr, unsigned size)
{
    NikonStorageHost *s = opaque;
    return s->platform_registers[addr / 4];
}

static MemTxResult storage_platform_write(void *opaque, hwaddr addr,
                                        uint64_t value, unsigned size,
                                        MemTxAttrs attrs)
{
    NikonStorageHost *s = opaque;
    static const uint32_t masks[] = { 0x101, 1, UINT32_MAX, 0x110000 };
    if (value & ~masks[addr / 4]) {
        return MEMTX_ERROR;
    }
    /* Complete10516a28/3e/4c source fields. No readiness/clock identity or
     * completion event is inferred from these configuration writes.
     */
    s->platform_registers[addr / 4] = value;
    return MEMTX_OK;
}

static const MemoryRegionOps storage_platform_ops = {
    .read = storage_platform_read,
    .write_with_attrs = storage_platform_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 4, .max_access_size = 4 },
    .impl = { .min_access_size = 4, .max_access_size = 4 },
};

static void storage_reset(DeviceState *dev)
{
    NikonStorageHost *s = NIKON_STORAGE_HOST(dev);
    storage_stop(s);
    memset(s->window, 0, sizeof(s->window));
    memset(s->phy_registers, 0, sizeof(s->phy_registers));
    memset(s->platform_registers, 0, sizeof(s->platform_registers));
    s->viewport = s->msi_address = s->msi_upper = 0;
    s->cancel_event = 0;
    s->msi_enable = s->msi_mask = s->dbi_8a8 = s->dbi_8bc = 0;
    s->dbi_168 = s->dbi_80c = 0;
    s->transaction_errors = 0;
    memory_region_set_enabled(&s->msi, false);
    /* Original1051635a checks insertion before starting link training. */
    qemu_set_irq(s->present, s->media && blk_is_inserted(s->media));
}

static void storage_realize(DeviceState *dev, Error **errp)
{
    NikonStorageHost *s = NIKON_STORAGE_HOST(dev);
    PCIBus *bus;
    unsigned i;

    if (!s->dma_memory) {
        error_setg(errp, "Nikon storage requires its CPU domain memory link");
        return;
    }
    if (s->media_id) {
        s->media = blk_by_name(s->media_id);
        if (!s->media) {
            error_setg(errp, "Nikon storage backend '%s' does not exist", s->media_id);
            return;
        }
    }
    s->bridge.root_path = object_get_canonical_path(OBJECT(&s->bridge));
    qdev_prop_set_bit(DEVICE(&s->bridge), "allow-unmapped-accesses", false);
    if (!sysbus_realize(SYS_BUS_DEVICE(&s->bridge), errp)) {
        return;
    }
    bus = PCI_HOST_BRIDGE(&s->bridge)->bus;
    memory_region_init(&s->dma_root, OBJECT(s), "storage.dma", UINT64_MAX);
    memory_region_init_alias(&s->dma_alias, OBJECT(s), "storage.identity",
                             s->dma_memory, 0, UINT64_MAX);
    memory_region_add_subregion(&s->dma_root, 0, &s->dma_alias);
    memory_region_init_io(&s->msi, OBJECT(s), &storage_msi_ops, s,
                          "storage.msi", 4);
    memory_region_set_enabled(&s->msi, false);
    memory_region_add_subregion_overlap(&s->dma_root, 0, &s->msi, 1);
    address_space_init(&s->dma, &s->dma_root, "storage.pci");
    address_space_init(&s->pci_memory, &s->bridge.parent_obj.io_mmio,
                        "storage.pci-memory");
    s->dma_initialized = true;
    pci_setup_iommu(bus, &storage_iommu, s);
    for (i = 0; i < 4; i++) {
        gpex_set_irq_num(&s->bridge.parent_obj, i, i);
        s->intx_inputs[i] = qemu_allocate_irq(storage_intx, s, i);
        sysbus_connect_irq(SYS_BUS_DEVICE(&s->bridge), i, s->intx_inputs[i]);
    }
    if (s->media) {
        /* Private placement, not a claim about original physical BDF. */
        s->endpoint = nikon_storage_attach(bus, PCI_DEVFN(1, 0), s->media,
                                           "NIGHTGLASS", errp);
    }
}

static void storage_init(Object *obj)
{
    NikonStorageHost *s = NIKON_STORAGE_HOST(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);
    unsigned i;
    object_initialize_child(obj, "pci", &s->bridge, TYPE_NIKON_STORAGE_BRIDGE);
    memory_region_init_io(&s->dbi, obj, &storage_dbi_ops, s, "storage.dbi", 0x1000);
    memory_region_init_io(&s->config, obj, &storage_config_ops, s,
                          "storage.config", 0x10000);
    memory_region_init_io(&s->memory, obj, &storage_memory_ops, s,
                          "storage.memory", STORAGE_BAR_SIZE);
    memory_region_init_io(&s->phy, obj, &storage_phy_ops, s, "storage.phy", 0x400);
    memory_region_init_io(&s->platform, obj, &storage_platform_ops, s,
                          "storage.platform", 0x10);
    sysbus_init_mmio(sbd, &s->dbi);
    sysbus_init_mmio(sbd, &s->config);
    sysbus_init_mmio(sbd, &s->memory);
    sysbus_init_mmio(sbd, &s->phy);
    sysbus_init_mmio(sbd, &s->platform);
    for (i = 0; i < 4; i++) {
        sysbus_init_irq(sbd, &s->irq[i]);
    }
    qdev_init_gpio_out_named(DEVICE(obj), &s->present, "present", 1);
    s->transition = timer_new_ns(QEMU_CLOCK_VIRTUAL, storage_transition, s);
}

static void storage_finalize(Object *obj)
{
    NikonStorageHost *s = NIKON_STORAGE_HOST(obj);
    unsigned i;
    timer_free(s->transition);
    if (s->dma_initialized) {
        address_space_destroy(&s->dma);
        address_space_destroy(&s->pci_memory);
    }
    for (i = 0; i < 4; i++) {
        qemu_free_irq(s->intx_inputs[i]);
    }
}

static const Property storage_properties[] = {
    DEFINE_PROP_STRING("drive", NikonStorageHost, media_id),
    DEFINE_PROP_LINK("memory", NikonStorageHost, dma_memory,
                     TYPE_MEMORY_REGION, MemoryRegion *),
};

static const VMStateDescription storage_vmstate = {
    .name = TYPE_NIKON_STORAGE_HOST,
    .unmigratable = true,
};

static void storage_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    dc->realize = storage_realize;
    dc->desc = "Nikon source-local PCI storage host";
    dc->user_creatable = false;
    dc->vmsd = &storage_vmstate;
    device_class_set_legacy_reset(dc, storage_reset);
    device_class_set_props(dc, storage_properties);
}

static const TypeInfo storage_type = {
    .name = TYPE_NIKON_STORAGE_HOST,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(NikonStorageHost),
    .instance_init = storage_init,
    .instance_finalize = storage_finalize,
    .class_init = storage_class_init,
};

static void storage_register(void)
{
    type_register_static(&storage_bridge_type);
    type_register_static(&storage_type);
}

type_init(storage_register)
