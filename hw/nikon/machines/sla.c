/*
 * Nikon SLA loaded ARM software domain, independent of the MAS address space.
 * Firmware owns its MMU, banked SPs, COPY/ZERO and secondary-core release.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "qemu/error-report.h"
#include "qapi/error.h"
#include "hw/core/boards.h"
#include "hw/core/irq.h"
#include "hw/core/or-irq.h"
#include "hw/core/split-irq.h"
#include "hw/arm/machines-qom.h"
#include "hw/core/qdev-properties.h"
#include "hw/core/qdev-properties-system.h"
#include "hw/intc/arm_gic.h"
#include "hw/nikon/bundle.h"
#include "hw/nikon/devices.h"
#include "hw/nikon/events.h"
#include "hw/nikon/notifier.h"
#include "hw/nikon/counter.h"
#include "hw/nikon/command.h"
#include "hw/nikon/lanes.h"
#include "hw/nikon/storage.h"
#include "hw/nikon/sla.h"
#include "system/address-spaces.h"
#include "system/reset.h"
#include "system/system.h"
#include "target/arm/cpu.h"

#define TYPE_NIKON_SLA_MACHINE MACHINE_TYPE_NAME("nikonsla")
OBJECT_DECLARE_SIMPLE_TYPE(NikonSLADomain, NIKON_SLA_DOMAIN)

#define SLA_MAX_CPUS 2
#define SLA_CBAR 0xf8100000ULL
#define SLA_TIMER_HZ 48000000ULL
#define SLA_STACK_SIZE 0x4000
#define SLA_RAM_COUNT (6 * SLA_MAX_CPUS + 9)

typedef struct NikonSLANor {
    MemoryRegion region;
    NikonSLADomain *domain;
    hwaddr base;
} NikonSLANor;

struct NikonSLADomain {
    SysBusDevice parent_obj;
    ARMCPU cpu[SLA_MAX_CPUS];
    unsigned num_cpus;
    Chardev *serial;
    MemoryRegion system;
    AddressSpace as;
    qemu_irq peer_ready;
    NikonSLANor image, boot_nor;
    MemoryRegion uncovered, bridge_window;
    MemoryRegion ram[SLA_RAM_COUNT], storage[SLA_RAM_COUNT];
    unsigned ram_count;
    DeviceState *memc;
    DeviceState *bridge;
    GBytes *payload;
    GBytes *boot_prefix;
    bool reported_error;
    bool initialized;
};

typedef struct NikonSLAMachine {
    MachineState parent_obj;
    MemoryRegion view;
    char *boot_prefix;
} NikonSLAMachine;

static DeviceState *sla_device(NikonSLADomain *s, const char *name,
                               const char *type)
{
    DeviceState *dev = qdev_new(type);

    object_property_add_child(OBJECT(s), name, OBJECT(dev));
    return dev;
}

static void sla_map(NikonSLADomain *s, DeviceState *dev, unsigned region,
                    hwaddr base)
{
    memory_region_add_subregion(&s->system, base,
                               sysbus_mmio_get_region(SYS_BUS_DEVICE(dev), region));
}

static DeviceState *sla_simple(NikonSLADomain *s, const char *name,
                               const char *type, hwaddr base)
{
    DeviceState *dev = sla_device(s, name, type);

    sysbus_realize_and_unref(SYS_BUS_DEVICE(dev), &error_fatal);
    sla_map(s, dev, 0, base);
    return dev;
}

static void sla_peer_output(void *opaque, int n, int level)
{
    NikonSLADomain *s = opaque;

    qemu_set_irq(s->peer_ready, level);
}

static MemTxResult sla_uncovered_read(void *opaque, hwaddr addr,
                                     uint64_t *value, unsigned size,
                                     MemTxAttrs attrs)
{
    NikonSLADomain *s = opaque;

    nikon_access_error(&s->reported_error, addr, size, false, 0,
                       "SLA uncovered input");
    *value = 0; /* Invalid output: the transaction fails. */
    return MEMTX_ERROR;
}

static MemTxResult sla_uncovered_write(void *opaque, hwaddr addr,
                                      uint64_t value, unsigned size,
                                      MemTxAttrs attrs)
{
    NikonSLADomain *s = opaque;

    nikon_access_error(&s->reported_error, addr, size, true, value,
                       "SLA uncovered output");
    return MEMTX_ERROR;
}

static const MemoryRegionOps sla_uncovered_ops = {
    .read_with_attrs = sla_uncovered_read,
    .write_with_attrs = sla_uncovered_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 8, .unaligned = true },
    .impl = { .min_access_size = 1, .max_access_size = 8, .unaligned = true },
};

static MemTxResult sla_rom_read(void *opaque, hwaddr offset, uint64_t *value,
                               unsigned size, MemTxAttrs attrs)
{
    NikonSLANor *nor = opaque;

    *value = 0;
    return nikon_memc_nor_access(nor->domain->memc, nor->base + offset,
                                 size, false, 0);
}

static MemTxResult sla_rom_write(void *opaque, hwaddr offset, uint64_t value,
                                unsigned size, MemTxAttrs attrs)
{
    NikonSLANor *nor = opaque;

    return nikon_memc_nor_access(nor->domain->memc, nor->base + offset,
                                 size, true, value);
}

static const MemoryRegionOps sla_rom_ops = {
    .read_with_attrs = sla_rom_read,
    .write_with_attrs = sla_rom_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 8, .unaligned = true },
    .impl = { .min_access_size = 1, .max_access_size = 8, .unaligned = true },
};

static void sla_nor(NikonSLADomain *s, NikonSLANor *nor, const char *name,
                    hwaddr base, GBytes *bytes)
{
    gsize size;
    const void *data = g_bytes_get_data(bytes, &size);

    nor->domain = s;
    nor->base = base;
    memory_region_init_rom_device(&nor->region, OBJECT(s), &sla_rom_ops, nor,
                                  name, size, &error_fatal);
    memcpy(memory_region_get_ram_ptr(&nor->region), data, size);
    memory_region_add_subregion(&s->system, base, &nor->region);
}

static void sla_reset(void *opaque)
{
    NikonSLADomain *s = opaque;
    unsigned i;

    s->reported_error = false;
    for (i = 0; i < s->num_cpus; i++) {
        cpu_reset(CPU(&s->cpu[i]));
        cpu_set_pc(CPU(&s->cpu[i]), NIKON_SLA_ENTRY);
    }
}

static void sla_ram(NikonSLADomain *s, DeviceState *mxic, const char *name,
                    hwaddr base, uint64_t size)
{
    unsigned index = s->ram_count++;
    unsigned page_offset = base & 0xfff;
    g_autofree char *storage_name = g_strdup_printf("%s.storage", name);

    assert(index < SLA_RAM_COUNT);
    memory_region_init_ram(&s->storage[index], OBJECT(s), storage_name,
                           size + page_offset, &error_fatal);
    memory_region_init_alias(&s->ram[index], OBJECT(s), name, &s->storage[index],
                             page_offset, size);
    memory_region_add_subregion(&s->system, base, &s->ram[index]);
    if (mxic) {
        nikon_mxic_attach_ram(mxic, &s->ram[index], base);
    }
}

static void sla_domain_realize(DeviceState *dev, Error **errp)
{
    NikonSLADomain *s = NIKON_SLA_DOMAIN(dev);
    DeviceState *mxic, *gic, *fill, *gpio, *completion, *hdmac[2], *usio[3];
    SysBusDevice *gic_bus;
    size_t size;
    unsigned i;
    static const hwaddr stack_top[] = {
        0xc1920000, 0xc1918000, 0xc1910000,
        0xc1908000, 0xc1900000, 0xc18f8000,
    };

    size = g_bytes_get_size(s->payload);
    if (size != NIKON_SLA_ROM_SIZE) {
        error_setg(errp, "SLA requires the complete C1.70 component");
        return;
    }

    memory_region_init(&s->system, OBJECT(s), "nikon.sla.domain", 1ULL << 32);
    address_space_init(&s->as, &s->system, "nikon.sla.domain");
    s->initialized = true;

    memory_region_init_io(&s->uncovered, OBJECT(s), &sla_uncovered_ops, s,
                          "nikon.sla.uncovered", 1ULL << 32);
    memory_region_add_subregion_overlap(&s->system, 0,
                                        &s->uncovered, -1);
    s->memc = sla_simple(s, "memc", TYPE_NIKON_MEMC, 0x04264000);
    sla_nor(s, &s->image, "nikon.sla.image", NIKON_SLA_ROM_BASE, s->payload);
    if (s->boot_prefix) {
        sla_nor(s, &s->boot_nor, "nikon.sla.external-boot-prefix",
                NIKON_SLA_BOOT_PREFIX_BASE, s->boot_prefix);
    }

    mxic = sla_simple(s, "mxic", TYPE_NIKON_MXIC, 0x20016000);
    /* Original COPY targets only: no initialized resources are host copied. */
    sla_ram(s, mxic, "nikon.sla.copy1", NIKON_SLA_COPY1_BASE,
            NIKON_SLA_COPY1_SIZE);
    sla_ram(s, mxic, "nikon.sla.copy2", NIKON_SLA_COPY2_BASE,
            NIKON_SLA_COPY2_SIZE);
    /* Original fill rounds 0x8b71d4 bytes up to 0x4000-byte blocks. */
    sla_ram(s, mxic, "nikon.sla.bss", 0xc1017d80, 0x8b8000);
    /* Original secondary table COPY and startup caller range. */
    sla_ram(s, mxic, "nikon.sla.table", 0xc18e4000, 0x4000);
    /* Source config bounds consumed by the native kernel arena initializer. */
    sla_ram(s, mxic, "nikon.sla.kernel-arena", 0xc1940000, 0x36c0000);
    sla_ram(s, NULL, "nikon.sla.temporary-stack", 0x04008e00, 0x100);
    sla_ram(s, NULL, "nikon.sla.release", 0x04008ffc, 4);
    /* Source header/sender extent: 0x18 + 0xc0 * 0x108, guest owns contents. */
    sla_ram(s, mxic, "nikon.sla.peer-ring", 0xc60f0000, 0xc618);
    /* 100658e4 record19 and 10065ad8's next-base classifier: normal7MiB RAM.
     * This is allocation backing only; guest/external clone bytes are absent. */
    sla_ram(s, mxic, "nikon.sla.local-code", NIKON_SLA_LOCAL_CODE_BASE,
            NIKON_SLA_LOCAL_CODE_SIZE);
    for (i = 0; i < s->num_cpus; i++) {
        unsigned mode;
        g_autofree char *name = g_strdup_printf("cpu%u", i);

        for (mode = 0; mode < ARRAY_SIZE(stack_top); mode++) {
            g_autofree char *stack = g_strdup_printf("nikon.sla.cpu%u.stack%u",
                                                    i, mode);
            sla_ram(s, mxic, stack, stack_top[mode] - (i + 1) * SLA_STACK_SIZE,
                    SLA_STACK_SIZE);
        }
        object_initialize_child(OBJECT(s), name, &s->cpu[i],
                                ARM_CPU_TYPE_NAME("cortex-a7"));
        object_property_set_link(OBJECT(&s->cpu[i]), "memory",
                                 OBJECT(&s->system), &error_fatal);
        qdev_prop_set_uint64(DEVICE(&s->cpu[i]), "mp-affinity", i);
        qdev_prop_set_uint64(DEVICE(&s->cpu[i]), "reset-cbar", SLA_CBAR);
        qdev_prop_set_uint64(DEVICE(&s->cpu[i]), "cntfrq", SLA_TIMER_HZ);
        qdev_realize(DEVICE(&s->cpu[i]), NULL, &error_fatal);
    }

    sla_simple(s, "clock", TYPE_NIKON_CLOCK_GATES, 0x04281000);
    sla_simple(s, "sysconfig", TYPE_NIKON_SYSCONFIG, 0x04280000);
    gpio = sla_simple(s, "gpio", TYPE_NIKON_GPIO, 0x04282000);
    qdev_connect_gpio_out_named(gpio, "pad", 8 * 16 + 10,
                               qdev_get_gpio_in_named(dev, "peer-source", 0));
    /* COPY1 c1000000 supplies the counter used by source 1002012c/10020158. */
    sla_simple(s, "timestamp", TYPE_NIKON_TIMESTAMP, 0x04266000);
    completion = sla_simple(s, "completion", TYPE_NIKON_COMPLETION, 0x04287000);
    for (i = 0; i < ARRAY_SIZE(usio); i++) {
        g_autofree char *name = i ? g_strdup_printf("usio%u", i) :
                                   g_strdup("usio");

        usio[i] = sla_device(s, name, TYPE_NIKON_USIO);
        /* Callback10020294 uses port0; COPY1 C10059FC supplies offsets0/10/20. */
        if (!i && s->serial) {
            qdev_prop_set_chr(usio[i], "chardev", s->serial);
        }
        sysbus_realize_and_unref(SYS_BUS_DEVICE(usio[i]), &error_fatal);
        sla_map(s, usio[i], 0, 0x2c090000 + i * 0x10);
    }
    {
        DeviceState *config = sla_device(s, "mxic-config", TYPE_NIKON_MXIC_CONFIG);

        sysbus_realize_and_unref(SYS_BUS_DEVICE(config), &error_fatal);
        sla_map(s, config, 0, 0x20010000);
        sla_map(s, config, 1, 0x24074000);
    }
    for (i = 0; i < 2; i++) {
        g_autofree char *name = g_strdup_printf("sdi%u", i);
        DeviceState *sdi = sla_device(s, name, TYPE_NIKON_SDI);

        qdev_prop_set_uint32(sdi, "bank", i);
        sysbus_realize_and_unref(SYS_BUS_DEVICE(sdi), &error_fatal);
        sla_map(s, sdi, 0, 0x20020000 + i * 0x20000);
    }

    gic = sla_device(s, "gic", TYPE_ARM_GIC);
    gic_bus = SYS_BUS_DEVICE(gic);
    qdev_prop_set_uint32(gic, "num-cpu", s->num_cpus);
    qdev_prop_set_uint32(gic, "first-cpu-index", CPU(&s->cpu[0])->cpu_index);
    qdev_prop_set_uint32(gic, "num-irq", 256);
    /* 10036cc0 writes GICD_ISACTIVER; upstream implements it in GICv2. */
    qdev_prop_set_uint32(gic, "revision", 2);
    qdev_prop_set_bit(gic, "has-security-extensions", true);
    qdev_prop_set_bit(gic, "has-virtualization-extensions", false);
    sysbus_realize_and_unref(gic_bus, &error_fatal);
    sla_map(s, gic, 0, SLA_CBAR + 0x1000);
    sla_map(s, gic, 1, SLA_CBAR + 0x2000);
    /* Second-phase1005bf0e/1005bf4c registers and enables IRQ250. */
    sysbus_connect_irq(SYS_BUS_DEVICE(completion), 0,
                       qdev_get_gpio_in(gic, 250 - 32));
    /* Source10058be2 registers RX/TX pairs186/187,188/189,190/191. */
    for (i = 0; i < ARRAY_SIZE(usio); i++) {
        sysbus_connect_irq(SYS_BUS_DEVICE(usio[i]), 0,
                           qdev_get_gpio_in(gic, 186 + i * 2 - 32));
        sysbus_connect_irq(SYS_BUS_DEVICE(usio[i]), 1,
                           qdev_get_gpio_in(gic, 187 + i * 2 - 32));
    }
    {
        DeviceState *events = sla_simple(s, "events", TYPE_NIKON_EVENTS,
                                         0x2c012000);

        /* 10057956/10057970: object104 and IRQ158; no event provider attached. */
        sysbus_connect_irq(SYS_BUS_DEVICE(events), 0,
                           qdev_get_gpio_in(gic, 158 - 32));
    }
    {
        DeviceState *notifier = sla_simple(s, "notifier", TYPE_NIKON_NOTIFIER,
                                           0x2c0b0000);
        DeviceState *counter = sla_simple(s, "counter", TYPE_NIKON_COUNTER,
                                          0x2c015000);

        /* Official1005fe2c/100604fc registrations; providers stay disconnected. */
        sysbus_connect_irq(SYS_BUS_DEVICE(notifier), 0,
                           qdev_get_gpio_in(gic, 218 - 32));
        for (i = 0; i < 3; i++) {
            sysbus_connect_irq(SYS_BUS_DEVICE(counter), i,
                               qdev_get_gpio_in(gic, 169 + i - 32));
        }
    }
    {
        DeviceState *signal = sla_device(s, "signal", TYPE_NIKON_SIGNAL);
        DeviceState *lanes = sla_device(s, "lanes", TYPE_NIKON_LANES);
        DeviceState *signal_or = sla_device(s, "signal-or", TYPE_OR_IRQ);
        DeviceState *storage = sla_device(s, "storage", TYPE_NIKON_STORAGE_HOST);
        static const hwaddr storage_base[] = {
            [NIKON_STORAGE_DBI] = 0x2a900000,
            [NIKON_STORAGE_CONFIG] = 0x2a800000,
            [NIKON_STORAGE_MEMORY] = 0x2a810000,
            [NIKON_STORAGE_PHY] = 0x2b200000,
        };
        static const unsigned storage_irq[] = { 242, 243, 244, 246 };

        /* SOURCE11 1008987c registers IRQ237; 1008989e writes both inputs. */
        sysbus_realize_and_unref(SYS_BUS_DEVICE(signal), &error_fatal);
        sla_map(s, signal, 0, 0x2b000000);
        sla_map(s, signal, 1, 0x28000000);
        qdev_prop_set_uint16(signal_or, "num-lines", 5);
        qdev_realize_and_unref(signal_or, NULL, &error_fatal);
        for (i = 0; i < 5; i++) {
            sysbus_connect_irq(SYS_BUS_DEVICE(signal), i,
                               qdev_get_gpio_in(signal_or, i));
        }
        qdev_connect_gpio_out(signal_or, 0, qdev_get_gpio_in(gic, 237 - 32));
        /* 10089b06 selects one of two write-only configuration apertures. */
        sysbus_realize_and_unref(SYS_BUS_DEVICE(lanes), &error_fatal);
        sla_map(s, lanes, 0, 0x2b001000);
        sla_map(s, lanes, 1, 0x2b001100);
        /* Lane writes create no IRQ; only the real storage link feeds route3. */
        /* SOURCE11 10088a82/10093476 select root mode through COPY1 words0. */
        object_property_set_link(OBJECT(storage), "memory", OBJECT(&s->system),
                                 &error_fatal);
        sysbus_realize_and_unref(SYS_BUS_DEVICE(storage), &error_fatal);
        for (i = 0; i < ARRAY_SIZE(storage_base); i++) {
            sla_map(s, storage, i, storage_base[i]);
        }
        for (i = 0; i < ARRAY_SIZE(storage_irq); i++) {
            sysbus_connect_irq(SYS_BUS_DEVICE(storage), i,
                               qdev_get_gpio_in(gic, storage_irq[i] - 32));
        }
        qdev_connect_gpio_out_named(storage, "link", 0,
                                   qdev_get_gpio_in_named(signal, "signal", 3));
        /* No drive/endpoint or unsupported MAS platform aperture is supplied. */
    }
    {
        DeviceState *staging = sla_simple(s, "staging",
                                          TYPE_NIKON_STAGED_CONFIGURATION,
                                          0x20202000);

        /* SOURCE11 10093e0e registers14e/IRQ123; 10093dc0 ACKs actual status. */
        sysbus_connect_irq(SYS_BUS_DEVICE(staging), 0,
                           qdev_get_gpio_in(gic, 123 - 32));
        /* External event causes remain disconnected. */
    }
    {
        static const hwaddr base[] = { 0x04250000, 0x24061000, 0x2c041000 };
        static const unsigned irq[] = {
            49, 50, 51, 52, 53, 54, 55, 56,
            57, 58, 59, 60, 61, 62, 63, 64,
            227, 228, 247, 248,
        };
        DeviceState *command = sla_device(s, "command", TYPE_NIKON_COMMAND);

        /* Source100608c6 and complete three-by25 ROM register pointer words. */
        sysbus_realize_and_unref(SYS_BUS_DEVICE(command), &error_fatal);
        for (i = 0; i < ARRAY_SIZE(base); i++) {
            sla_map(s, command, i, base[i]);
        }
        for (i = 0; i < ARRAY_SIZE(irq); i++) {
            sysbus_connect_irq(SYS_BUS_DEVICE(command), i,
                               qdev_get_gpio_in(gic, irq[i] - 32));
        }
        /* Requests carry opaque guest words; no message provider is attached. */
    }
    /* Official 1006ade8/1006ad78: IRQ127, pending/ACK at 2013d020/d028. */
    s->bridge = sla_simple(s, "bridge", TYPE_NIKON_BRIDGE, 0x20130000);
    sysbus_connect_irq(SYS_BUS_DEVICE(s->bridge), 0,
                       qdev_get_gpio_in(gic, 127 - 32));
    /* Class0 source converter 10060f9c and CPU accessors 10064728/1006474a.
     * This IO alias forwards only valid guest-configured mapping records. */
    memory_region_init_alias(&s->bridge_window, OBJECT(s),
        "nikon.sla.peer-window", sysbus_mmio_get_region(SYS_BUS_DEVICE(s->bridge), 2),
        0x40000000, 0x10000000);
    memory_region_add_subregion(&s->system, 0x40000000, &s->bridge_window);
    {
        DeviceState *exiu = sla_simple(s, "exiu", TYPE_NIKON_EXIU, 0x0426f000);

        /* COPY GPIO objects54..5b: own port5[7..14], EXIU0..7, IRQ65..72. */
        for (i = 0; i < 8; i++) {
            qdev_connect_gpio_out_named(gpio, "pad", 5 * 16 + 7 + i,
                                       qdev_get_gpio_in(exiu, i));
            sysbus_connect_irq(SYS_BUS_DEVICE(exiu), i,
                               qdev_get_gpio_in(gic, 65 + i - 32));
        }
    }
    {
        DeviceState *copy = sla_device(s, "copy", TYPE_NIKON_COPY);

        /* 10067016 registers COPY1 C1005C48's four IRQ45..48 records. */
        object_property_set_link(OBJECT(copy), "memory", OBJECT(&s->system),
                                 &error_fatal);
        sysbus_realize_and_unref(SYS_BUS_DEVICE(copy), &error_fatal);
        sla_map(s, copy, 0, 0x04251000);
        for (i = 0; i < 4; i++) {
            sysbus_connect_irq(SYS_BUS_DEVICE(copy), i,
                               qdev_get_gpio_in(gic, 45 + i - 32));
        }
    }
    /* COPY1 C1005004: four 0x30-byte I2C records, initializer 1005f910. */
    for (i = 0; i < 4; i++) {
        g_autofree char *name = g_strdup_printf("i2c%u", i);
        DeviceState *i2c = sla_device(s, name, TYPE_NIKON_I2C);

        qdev_prop_set_uint32(i2c, "instance", i);
        sysbus_realize_and_unref(SYS_BUS_DEVICE(i2c), &error_fatal);
        sla_map(s, i2c, 0, 0x2c018000 + i * 0x1000);
        sysbus_connect_irq(SYS_BUS_DEVICE(i2c), 0,
                           qdev_get_gpio_in(gic, 214 + i - 32));
        /* Controller-local I2CBus remains disconnected; no DMA is supplied. */
    }
    {
        DeviceState *timing = sla_device(s, "timing", TYPE_NIKON_TIMING);

        /* COPY1 C1005904/C10054B0 and ROM1016AFBC: twelve local channels. */
        sysbus_realize_and_unref(SYS_BUS_DEVICE(timing), &error_fatal);
        sla_map(s, timing, 0, 0x2c010000);
        sla_map(s, timing, 1, 0x2c011000);
        for (i = 0; i < 12; i++) {
            sysbus_connect_irq(SYS_BUS_DEVICE(timing), i,
                               qdev_get_gpio_in(gic, 172 + i - 32));
        }
        /* Mature nominal3GHz profile; capture/compare have no external peer. */
    }
    {
        DeviceState *audio = sla_simple(s, "audio", TYPE_NIKON_AUDIO,
                                        0x2c080000);

        /* 10057b7e/10057bc8: two local records and supported release-only init. */
        sysbus_connect_irq(SYS_BUS_DEVICE(audio), 0,
                           qdev_get_gpio_in(gic, 167 - 32));
        sysbus_connect_irq(SYS_BUS_DEVICE(audio), 1,
                           qdev_get_gpio_in(gic, 168 - 32));
        /* DMA selectors overlap SPI: no unqualified bank/mux wiring is added. */
    }
    /* COPY1 C1004DB8: two banks, eight 0x24-byte channel records each. */
    for (i = 0; i < ARRAY_SIZE(hdmac); i++) {
        g_autofree char *name = g_strdup_printf("hdmac%u", i);
        unsigned channel;

        hdmac[i] = sla_device(s, name, TYPE_NIKON_HDMAC);
        qdev_prop_set_uint32(hdmac[i], "bank", i);
        object_property_set_link(OBJECT(hdmac[i]), "memory", OBJECT(&s->system),
                                 &error_fatal);
        sysbus_realize_and_unref(SYS_BUS_DEVICE(hdmac[i]), &error_fatal);
        sla_map(s, hdmac[i], 0, 0x2c020000 + i * 0x10000);
        for (channel = 0; channel < 8; channel++) {
            sysbus_connect_irq(SYS_BUS_DEVICE(hdmac[i]), channel,
                qdev_get_gpio_in(gic, (i ? 142 : 150) + channel - 32));
        }
    }
    /* COPY1 C10015E8: eleven 0x58-byte SPI registry records, 1005ec84. */
    for (i = 0; i < 11; i++) {
        g_autofree char *name = g_strdup_printf("spi%u", i);
        DeviceState *spi = sla_device(s, name, TYPE_NIKON_SPI);

        qdev_prop_set_uint32(spi, "channel", i);
        qdev_prop_set_bit(spi, "dma", true);
        sysbus_realize_and_unref(SYS_BUS_DEVICE(spi), &error_fatal);
        sla_map(s, spi, 0, 0x2c0a0000 + i * 0x100);
        /* Both registered source callbacks enter 1005f2f8. */
        sysbus_connect_irq(SYS_BUS_DEVICE(spi), 0,
                           qdev_get_gpio_in(gic, 0xc1 + i * 2 - 32));
        sysbus_connect_irq(SYS_BUS_DEVICE(spi), 1,
                           qdev_get_gpio_in(gic, 0xc0 + i * 2 - 32));
        /* C1007238 records provide TX/RX selectors, consumed by 1005c786/70e.
         * Shared selector fanout is the functional request wiring assumption. */
        for (unsigned direction = 0; direction < 2; direction++) {
            unsigned request = 3 + i * 2 + direction;
            g_autofree char *split_name = g_strdup_printf("spi%u-demand%u",
                                                          i, direction);
            DeviceState *split = sla_device(s, split_name, TYPE_SPLIT_IRQ);

            qdev_prop_set_uint16(split, "num-lines", 2);
            qdev_realize_and_unref(split, NULL, &error_fatal);
            qdev_connect_gpio_out(split, 0,
                                 qdev_get_gpio_in(hdmac[0], request));
            qdev_connect_gpio_out(split, 1,
                                 qdev_get_gpio_in(hdmac[1], request));
            qdev_connect_gpio_out_named(spi, "dma", direction,
                                        qdev_get_gpio_in(split, 0));
        }
    }
    fill = sla_device(s, "fill", TYPE_NIKON_FILL);
    object_property_set_link(OBJECT(fill), "memory", OBJECT(&s->system),
                             &error_fatal);
    sysbus_realize_and_unref(SYS_BUS_DEVICE(fill), &error_fatal);
    sla_map(s, fill, 0, 0x20070000);
    sysbus_connect_irq(SYS_BUS_DEVICE(fill), 0,
                       qdev_get_gpio_in(gic, 103 - 32));
    sysbus_connect_irq(SYS_BUS_DEVICE(fill), 1,
                       qdev_get_gpio_in(gic, 104 - 32));
    for (i = 0; i < s->num_cpus; i++) {
        DeviceState *cpu = DEVICE(&s->cpu[i]);
        const int timer_irq[] = {
            [GTIMER_PHYS] = 30, [GTIMER_VIRT] = 27,
            [GTIMER_HYP] = 26, [GTIMER_SEC] = 29,
        };
        unsigned irq;

        for (irq = 0; irq < ARRAY_SIZE(timer_irq); irq++) {
            qdev_connect_gpio_out(cpu, irq,
                qdev_get_gpio_in(gic, 256 - 32 + i * 32 + timer_irq[irq]));
        }
        sysbus_connect_irq(gic_bus, i, qdev_get_gpio_in(cpu, ARM_CPU_IRQ));
        sysbus_connect_irq(gic_bus, i + s->num_cpus,
                           qdev_get_gpio_in(cpu, ARM_CPU_FIQ));
        sysbus_connect_irq(gic_bus, i + 2 * s->num_cpus,
                           qdev_get_gpio_in(cpu, ARM_CPU_VIRQ));
        sysbus_connect_irq(gic_bus, i + 3 * s->num_cpus,
                           qdev_get_gpio_in(cpu, ARM_CPU_VFIQ));
    }
    qemu_register_reset(sla_reset, s);
    error_report("nikonsla: loaded C1.70 SLA ARM entry=1002bcd4; "
                 "functional Cortex-A7 cpus=%u CBAR=f8100000 GICv2=256 "
                 "cntfrq=48000000; virtual stack slots=16KiB "
                 "temporary caller stack=256B; guest MMU/COPY/ZERO/release; "
                 "loaded software entry; no calibration input or MAS peer ACK",
                 s->num_cpus);
}

static void sla_finalize(Object *object)
{
    NikonSLADomain *s = NIKON_SLA_DOMAIN(object);

    if (s->initialized) {
        qemu_unregister_reset(sla_reset, s);
        address_space_destroy(&s->as);
    }
    g_clear_pointer(&s->payload, g_bytes_unref);
    g_clear_pointer(&s->boot_prefix, g_bytes_unref);
}

static void sla_domain_init(Object *object)
{
    NikonSLADomain *s = NIKON_SLA_DOMAIN(object);

    qdev_init_gpio_out_named(DEVICE(s), &s->peer_ready, "peer-ready", 1);
    qdev_init_gpio_in_named(DEVICE(s), sla_peer_output, "peer-source", 1);
}

static void sla_domain_class_init(ObjectClass *oc, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->realize = sla_domain_realize;
    dc->user_creatable = false;
    dc->hotpluggable = false;
}

DeviceState *nikon_sla_create(Object *parent, const char *name,
                             GBytes *payload, GBytes *boot_prefix,
                             unsigned num_cpus,
                             Chardev *serial, Error **errp)
{
    DeviceState *dev;
    NikonSLADomain *s;

    if (!payload || num_cpus < 1 || num_cpus > SLA_MAX_CPUS) {
        error_setg(errp, "SLA requires its complete payload and one or two CPUs");
        return NULL;
    }
    if (boot_prefix &&
        g_bytes_get_size(boot_prefix) != NIKON_SLA_BOOT_PREFIX_SIZE) {
        error_setg(errp, "SLA boot-prefix input must contain exactly 128KiB");
        return NULL;
    }
    dev = qdev_new(TYPE_NIKON_SLA_DOMAIN);
    s = NIKON_SLA_DOMAIN(dev);
    s->payload = g_bytes_ref(payload);
    s->boot_prefix = boot_prefix ? g_bytes_ref(boot_prefix) : NULL;
    s->num_cpus = num_cpus;
    s->serial = serial;
    object_property_add_child(parent, name, OBJECT(dev));
    if (!sysbus_realize(SYS_BUS_DEVICE(dev), errp)) {
        object_property_del(parent, name);
        object_unref(OBJECT(dev));
        return NULL;
    }
    object_unref(OBJECT(dev));
    return dev; /* The parent owns the domain, including all peer interfaces. */
}

AddressSpace *nikon_sla_address_space(DeviceState *domain)
{
    return &NIKON_SLA_DOMAIN(domain)->as;
}

DeviceState *nikon_sla_bridge(DeviceState *domain)
{
    return NIKON_SLA_DOMAIN(domain)->bridge;
}

static void sla_init(MachineState *machine)
{
    NikonSLAMachine *m = (NikonSLAMachine *)machine;
    DeviceState *domain;
    NikonBundle *bundle;
    GBytes *payload;
    g_autoptr(GBytes) boot_prefix = NULL;

    if (strcmp(machine->cpu_type, ARM_CPU_TYPE_NAME("cortex-a7"))) {
        error_report("nikonsla requires the functional Cortex-A7 profile");
        exit(EXIT_FAILURE);
    }
    bundle = nikon_bundle_load(machine->firmware, &error_fatal);
    payload = nikon_bundle_component(bundle, NIKON_COMPONENT_SLA);
    boot_prefix = nikon_boot_prefix_load(m->boot_prefix, &error_fatal);
    if (boot_prefix) {
        error_report("nikonsla: explicit external boot-prefix input=%s; "
                     "erased is counterfactual FF NOR, not original BootROM",
                     m->boot_prefix);
    }
    domain = nikon_sla_create(OBJECT(machine), "sla", payload, boot_prefix,
                              machine->smp.cpus, serial_hd(0), &error_fatal);
    g_bytes_unref(payload);
    nikon_bundle_free(bundle);
    /* Standalone board view for QEMU tools; embedded domains expose no alias. */
    memory_region_init_alias(&m->view, OBJECT(machine), "nikon.sla.board",
                             &NIKON_SLA_DOMAIN(domain)->system, 0, 1ULL << 32);
    memory_region_add_subregion(get_system_memory(), 0, &m->view);
}

static char *sla_get_boot_prefix(Object *obj, Error **errp)
{
    NikonSLAMachine *s = (NikonSLAMachine *)obj;

    return g_strdup(s->boot_prefix ? s->boot_prefix : "");
}

static void sla_set_boot_prefix(Object *obj, const char *value, Error **errp)
{
    NikonSLAMachine *s = (NikonSLAMachine *)obj;

    g_free(s->boot_prefix);
    s->boot_prefix = g_strdup(value);
}

static void sla_machine_finalize(Object *obj)
{
    g_free(((NikonSLAMachine *)obj)->boot_prefix);
}

static void sla_class_init(ObjectClass *oc, const void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);

    mc->desc = "Nikon SLA loaded ARM software domain";
    mc->init = sla_init;
    mc->default_cpu_type = ARM_CPU_TYPE_NAME("cortex-a7");
    mc->default_ram_size = 0;
    mc->max_cpus = SLA_MAX_CPUS;
    mc->ignore_memory_transaction_failures = false;
    object_class_property_add_str(oc, "boot-prefix", sla_get_boot_prefix,
                                  sla_set_boot_prefix);
}

static const TypeInfo sla_machine = {
    .name = TYPE_NIKON_SLA_MACHINE,
    .parent = TYPE_MACHINE,
    .instance_size = sizeof(NikonSLAMachine),
    .instance_finalize = sla_machine_finalize,
    .class_init = sla_class_init,
    .interfaces = arm_machine_interfaces,
};

static const TypeInfo sla_domain = {
    .name = TYPE_NIKON_SLA_DOMAIN,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(NikonSLADomain),
    .instance_init = sla_domain_init,
    .instance_finalize = sla_finalize,
    .class_init = sla_domain_class_init,
};

static void sla_register_types(void)
{
    type_register_static(&sla_domain);
    type_register_static(&sla_machine);
}
type_init(sla_register_types)
