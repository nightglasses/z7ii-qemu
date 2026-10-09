/* Nikon finite functional device.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/main-loop.h"
#include "migration/vmstate.h"
#include "hw/core/sysbus.h"
#include "hw/core/irq.h"
#include "hw/nikon/devices.h"

OBJECT_DECLARE_SIMPLE_TYPE(NikonCompletionState, NIKON_COMPLETION)

struct NikonCompletionState {
    SysBusDevice parent_obj;
    MemoryRegion regs;
    qemu_irq irq;
    QEMUBH *bh;
    uint32_t request;
    uint32_t applied_domains;
    bool irq_enabled;
    bool done;
    bool pending;
    bool reported_error;
};

static MemTxResult nikon_completion_error(NikonCompletionState *s, hwaddr offset,
                                        unsigned size, bool write,
                                        uint64_t value, const char *reason)
{
    nikon_access_error(&s->reported_error, s->regs.addr + offset, size,
                      write, value, reason);
    return MEMTX_ERROR;
}

static void nikon_completion_irq(NikonCompletionState *s)
{
    qemu_set_irq(s->irq, s->irq_enabled && s->done);
}

static void nikon_completion_apply(void *opaque)
{
    NikonCompletionState *s = opaque;
    uint32_t bit = 1U << ((s->request >> 12) & 7);

    if (!s->pending) {
        return;
    }
    /* Functional request effects; physical domains and latency are unknown. */
    if (s->request & 0x100) {
        s->applied_domains |= bit;
    } else {
        s->applied_domains &= ~bit;
    }
    s->pending = false;
    s->done = true;
    qemu_log_mask(LOG_GUEST_ERROR,
                  "nikon-completion: deferred request=0x%08x applied=0x%08x "
                  "done=1\n", s->request, s->applied_domains);
    nikon_completion_irq(s);
}

static MemTxResult nikon_completion_read(void *opaque, hwaddr offset,
                                       uint64_t *value, unsigned size,
                                       MemTxAttrs attrs)
{
    NikonCompletionState *s = opaque;

    *value = 0;
    if (size != 4 ||
        (offset != 0 && offset != 4 && offset != 8 && offset != 12)) {
        return nikon_completion_error(s, offset, size, false, 0,
                                     "completion-access-not-supported");
    }
    if (offset == 12) {
        *value = s->applied_domains;
        return MEMTX_OK;
    }
    *value = offset == 0 ? s->request :
             offset == 4 ? s->done : s->irq_enabled;
    return MEMTX_OK;
}

static MemTxResult nikon_completion_write(void *opaque, hwaddr offset,
                                        uint64_t value, unsigned size,
                                        MemTxAttrs attrs)
{
    NikonCompletionState *s = opaque;
    unsigned domain = (value >> 12) & 7;

    if (size != 4 ||
        (offset != 0 && offset != 4 && offset != 8 && offset != 16)) {
        return nikon_completion_error(s, offset, size, true, value,
                                     "completion-access-not-supported");
    }
    if (s->pending) {
        return nikon_completion_error(
            s, offset, size, true, value,
            "completion-concurrent-request-not-supported");
    }
    switch (offset) {
    case 0:
        /* Observed domain and request-mode profiles, not full IP legality. */
        if ((value & ~0x7110U) ||
            (domain != 0 && domain != 1 && domain != 3)) {
            return nikon_completion_error(
                s, offset, size, true, value,
                "completion-request-profile-not-supported");
        }
        s->request = value;
        break;
    case 4:
        if (value != 1) {
            return nikon_completion_error(s, offset, size, true, value,
                                         "completion-ack-not-supported");
        }
        /* W1C diagnostic binding; no host RTOS flag mutation. */
        s->done = false;
        nikon_completion_irq(s);
        break;
    case 8:
        if (value > 1) {
            return nikon_completion_error(
                s, offset, size, true, value,
                "completion-irq-profile-not-supported");
        }
        s->irq_enabled = value;
        nikon_completion_irq(s);
        break;
    case 16:
        if (value != 0xa1) {
            return nikon_completion_error(s, offset, size, true, value,
                                         "completion-command-not-supported");
        }
        s->pending = true;
        s->done = false;
        nikon_completion_irq(s);
        qemu_bh_schedule(s->bh);
        break;
    }
    return MEMTX_OK;
}

static const MemoryRegionOps nikon_completion_ops = {
    .read_with_attrs = nikon_completion_read,
    .write_with_attrs = nikon_completion_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 8, .unaligned = true },
    .impl = { .min_access_size = 1, .max_access_size = 8, .unaligned = true },
};

static void nikon_completion_reset(DeviceState *dev)
{
    NikonCompletionState *s = NIKON_COMPLETION(dev);

    qemu_bh_cancel(s->bh);
    s->pending = false;
    /* Coherent virtual domain-off/reset profile, not a measured Nikon reset. */
    s->request = 0;
    s->irq_enabled = false;
    s->done = false;
    s->applied_domains = 0;
    s->reported_error = false;
    nikon_completion_irq(s);
}

static void nikon_completion_init(Object *obj)
{
    NikonCompletionState *s = NIKON_COMPLETION(obj);

    s->bh = qemu_bh_new(nikon_completion_apply, s);
    memory_region_init_io(&s->regs, obj, &nikon_completion_ops, s,
                          "nikon.completion", 0x14);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->regs);
    sysbus_init_irq(SYS_BUS_DEVICE(obj), &s->irq);
}

static void nikon_completion_finalize(Object *obj)
{
    NikonCompletionState *s = NIKON_COMPLETION(obj);

    qemu_bh_delete(s->bh);
}

static const VMStateDescription nikon_completion_vmstate = {
    .name = "nikon-completion",
    .unmigratable = true,
};

static void nikon_completion_class_init(ObjectClass *oc, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->vmsd = &nikon_completion_vmstate;
    device_class_set_legacy_reset(dc, nikon_completion_reset);
    dc->user_creatable = false;
}

static const TypeInfo nikon_completion_type = {
    .name = TYPE_NIKON_COMPLETION,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(NikonCompletionState),
    .instance_init = nikon_completion_init,
    .instance_finalize = nikon_completion_finalize,
    .class_init = nikon_completion_class_init,
};

static void nikon_completion_register_types(void)
{
    type_register_static(&nikon_completion_type);
}
type_init(nikon_completion_register_types)
