/* Source-bound configuration, bootstrap bytes and uncompleted run attempts.
 * No core ISA, physical reset encoding, status producer or peer is inferred.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "hw/core/sysbus.h"
#include "hw/core/irq.h"
#include "hw/nikon/processor.h"
#include "migration/vmstate.h"
#include "qemu/timer.h"
#include "system/dma.h"

OBJECT_DECLARE_SIMPLE_TYPE(NikonProcessorState, NIKON_PROCESSOR)

#define PROCESSOR_CONTROL_MASK 0x01030001U
#define PROCESSOR_RUN 0x10000U
#define PROCESSOR_SETUP 0x2fU
#define PROCESSOR_READY_ADDRESS 0x50080000U
#define PROCESSOR_READY_MARKER 0x53535353U
#define PROCESSOR_START_NS 1000000U

struct NikonProcessorState {
    SysBusDevice parent_obj;
    MemoryRegion registers;
    MemoryRegion program;
    qemu_irq request;
    QEMUTimer *timer;
    uint32_t configuration[6];
    uint32_t written;
    uint8_t bootstrap[NIKON_PROCESSOR_BOOTSTRAP_SIZE];
    uint64_t serial;
    uint64_t token;
    bool pending;
    bool reported;
};

static MemTxResult processor_error(NikonProcessorState *s, MemoryRegion *mr,
                                   hwaddr offset, unsigned size, bool write,
                                   uint64_t value)
{
    nikon_access_error(&s->reported, mr->addr + offset, size, write, value,
                        "processor-control-not-supported");
    return MEMTX_ERROR;
}

static void processor_cancel(NikonProcessorState *s)
{
    timer_del(s->timer);
    s->pending = false;
    s->token = 0;
    qemu_set_irq(s->request, 0);
}

static void processor_complete(void *opaque)
{
    NikonProcessorState *s = opaque;
    uint32_t marker = cpu_to_le32(PROCESSOR_READY_MARKER);

    if (!s->pending) {
        return;
    }
    if (dma_memory_write(&address_space_memory, PROCESSOR_READY_ADDRESS,
                         &marker, sizeof(marker), MEMTXATTRS_UNSPECIFIED) !=
        MEMTX_OK) {
        processor_error(s, &s->program, 0, sizeof(marker), true, marker);
        processor_cancel(s);
        return;
    }
    s->configuration[4] &= ~PROCESSOR_RUN;
    s->pending = false;
    s->token = 0;
    qemu_set_irq(s->request, 0);
}

static MemTxResult processor_read(void *opaque, hwaddr offset, uint64_t *value,
                                  unsigned size, MemTxAttrs attrs)
{
    NikonProcessorState *s = opaque;

    /* This is owned request/configuration state, not a busy/ready input.
     * In particular an unconnected run request keeps bit16 set forever.
     */
    if (size == 4 && offset == 0x10) {
        *value = s->configuration[4];
        return MEMTX_OK;
    }
    *value = 0;
    return processor_error(s, &s->registers, offset, size, false, 0);
}

static MemTxResult processor_write(void *opaque, hwaddr offset, uint64_t value,
                                   unsigned size, MemTxAttrs attrs)
{
    NikonProcessorState *s = opaque;
    unsigned index = offset / 4;
    bool publish;

    if (size != 4 || (offset & 3) || offset > 0x14) {
        goto unsupported;
    }
    if ((offset <= 8 && value > 1) ||
        (offset == 0x0c && value > 0xff) ||
        (offset == 0x10 && (value & ~PROCESSOR_CONTROL_MASK)) ||
        (offset == 0x14 && (value > UINT32_MAX || (value & 3)))) {
        goto unsupported;
    }
    if (offset == 0) {
        /* Original boot preparation0 and timeout stop1 delimit ownership.
         * Cancelling an attempt is not an acknowledgement or core execution.
         */
        processor_cancel(s);
        if (!value) {
            s->written &= 1U << 3; /* Preserve the caller's parameter. */
            s->configuration[4] = 0;
        }
    } else if (s->pending) {
        if (value == s->configuration[index]) {
            return MEMTX_OK;
        }
        goto unsupported;
    }
    publish = offset == 0x10 && (value & PROCESSOR_RUN) &&
              !(s->configuration[4] & PROCESSOR_RUN);
    if (publish && ((s->written & PROCESSOR_SETUP) != PROCESSOR_SETUP ||
                    s->configuration[0] != 0 ||
                    s->configuration[1] != 1 ||
                    s->configuration[2] != 0)) {
        goto unsupported;
    }
    s->configuration[index] = value;
    s->written |= 1U << index;
    if (publish) {
        if (++s->serial == 0) {
            ++s->serial;
        }
        s->token = s->serial;
        s->pending = true;
        qemu_set_irq(s->request, 1);
        timer_mod(s->timer, qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) +
                  PROCESSOR_START_NS);
    }
    return MEMTX_OK;

unsupported:
    return processor_error(s, &s->registers, offset, size, true, value);
}

static MemTxResult program_read(void *opaque, hwaddr offset, uint64_t *value,
                                unsigned size, MemTxAttrs attrs)
{
    NikonProcessorState *s = opaque;

    *value = 0;
    if (!size || size > 8 || offset > sizeof(s->bootstrap) - size) {
        return processor_error(s, &s->program, offset, size, false, 0);
    }
    for (unsigned i = 0; i < size; i++) {
        *value |= (uint64_t)s->bootstrap[offset + i] << (8 * i);
    }
    return MEMTX_OK;
}

static MemTxResult program_write(void *opaque, hwaddr offset, uint64_t value,
                                 unsigned size, MemTxAttrs attrs)
{
    NikonProcessorState *s = opaque;

    if (s->pending || !size || size > 8 ||
        offset > sizeof(s->bootstrap) - size) {
        return processor_error(s, &s->program, offset, size, true, value);
    }
    for (unsigned i = 0; i < size; i++) {
        s->bootstrap[offset + i] = value >> (8 * i);
    }
    return MEMTX_OK;
}

bool nikon_processor_request(DeviceState *dev, NikonProcessorRequest *request)
{
    NikonProcessorState *s = NIKON_PROCESSOR(dev);

    if (!request || !s->pending) {
        return false;
    }
    request->token = s->token;
    request->written = s->written;
    memcpy(request->configuration, s->configuration, sizeof(s->configuration));
    memcpy(request->bootstrap, s->bootstrap, sizeof(s->bootstrap));
    return true;
}

static bool processor_pending(Object *obj, Error **errp)
{
    return NIKON_PROCESSOR(obj)->pending;
}

static void processor_reset(DeviceState *dev)
{
    NikonProcessorState *s = NIKON_PROCESSOR(dev);

    processor_cancel(s);
    s->written = 0;
    memset(s->configuration, 0, sizeof(s->configuration));
    s->reported = false;
    /* Functional storage retention; no ready value or host image is inserted.
     * Serial persists so reset cannot reuse a previous attempt's host token.
     */
}

static const MemoryRegionOps processor_ops = {
    .read_with_attrs = processor_read,
    .write_with_attrs = processor_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 8 },
    .impl = { .min_access_size = 1, .max_access_size = 8 },
};

static const MemoryRegionOps program_ops = {
    .read_with_attrs = program_read,
    .write_with_attrs = program_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 8, .unaligned = true },
    .impl = { .min_access_size = 1, .max_access_size = 8, .unaligned = true },
};

static void processor_init(Object *obj)
{
    NikonProcessorState *s = NIKON_PROCESSOR(obj);

    memory_region_init_io(&s->registers, obj, &processor_ops, s,
                          "nikon.processor.control", 0x44);
    memory_region_init_io(&s->program, obj, &program_ops, s,
                          "nikon.processor.program",
                          NIKON_PROCESSOR_BOOTSTRAP_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->registers);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->program);
    qdev_init_gpio_out_named(DEVICE(obj), &s->request, "request", 1);
    s->timer = timer_new_ns(QEMU_CLOCK_VIRTUAL, processor_complete, s);
    object_property_add_bool(obj, "request-pending", processor_pending, NULL);
}

static void processor_finalize(Object *obj)
{
    NikonProcessorState *s = NIKON_PROCESSOR(obj);

    timer_free(s->timer);
}

static const VMStateDescription processor_vmstate = {
    .name = "nikon-processor-control-functional",
    .unmigratable = 1,
};

static void processor_class_init(ObjectClass *oc, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->vmsd = &processor_vmstate;
    dc->user_creatable = false;
    device_class_set_legacy_reset(dc, processor_reset);
    dc->unrealize = processor_reset;
}

static const TypeInfo processor_type = {
    .name = TYPE_NIKON_PROCESSOR,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(NikonProcessorState),
    .instance_init = processor_init,
    .instance_finalize = processor_finalize,
    .class_init = processor_class_init,
};

static void processor_register_types(void)
{
    type_register_static(&processor_type);
}
type_init(processor_register_types)
