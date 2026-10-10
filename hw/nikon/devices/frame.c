/* Diagnostic descriptor-only CMOS -> processing -> display contract.
 * This aperture is test-only and is not a C1.70 physical register claim.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "qapi/error.h"
#include "hw/core/sysbus.h"
#include "migration/vmstate.h"
#include "hw/nikon/frame.h"
#include "hw/nikon/devices.h"

OBJECT_DECLARE_SIMPLE_TYPE(NikonFrameState, NIKON_FRAME_CONTRACT)

enum { CAPTURE_BASE = 0, PROCESS_BASE = 0x40, DISPLAY_BASE = 0x80,
       STATUS = 0xc0, FRAME_ERROR = 0xc4, COMMAND = 0xc8, GENERATION = 0xcc };
enum { CAPTURE_SUBMIT = 1, CAPTURE_COMPLETE, PROCESS_SUBMIT,
       PROCESS_COMPLETE, DISPLAY_SUBMIT, DISPLAY_COMPLETE, CONTRACT_RESET };

struct NikonFrameState {
    SysBusDevice parent_obj;
    MemoryRegion regs;
    NikonFrameDescriptor capture, process, display;
    NikonFrameStage stage;
    uint32_t error, generation;
    bool reported_error;
};

static bool descriptor_valid(const NikonFrameDescriptor *d, Error **errp)
{
    uint64_t row_bytes = (uint64_t)d->width * d->bytes_per_pixel;
    uint64_t extent = d->height ? (uint64_t)(d->height - 1) * d->stride +
                                 row_bytes : 0;

    if (!d->width || !d->height || !d->stride || !d->bytes_per_pixel ||
        !d->destination || row_bytes > d->stride || extent > UINT32_MAX) {
        error_setg(errp, "frame descriptor geometry is incomplete or invalid");
        return false;
    }
    /* format/Bayer/bit-depth/packing/flags remain opaque source words. */
    return true;
}

static bool expect(NikonFrameState *s, NikonFrameStage expected, Error **errp)
{
    if (s->stage == expected) return true;
    s->stage = NIKON_FRAME_ERROR;
    s->error = 0x100 | expected;
    error_setg(errp, "frame stage %u does not accept this handoff", expected);
    return false;
}

bool nikon_frame_capture_submit(DeviceState *dev,
                                const NikonFrameDescriptor *d, Error **errp)
{
    NikonFrameState *s = NIKON_FRAME_CONTRACT(dev);
    if (!expect(s, NIKON_FRAME_UNBOUND, errp) || !descriptor_valid(d, errp)) return false;
    s->capture = *d; s->stage = NIKON_FRAME_CAPTURE_PENDING; s->error = 0;
    return true;
}

bool nikon_frame_capture_complete(DeviceState *dev, Error **errp)
{
    NikonFrameState *s = NIKON_FRAME_CONTRACT(dev);
    if (!expect(s, NIKON_FRAME_CAPTURE_PENDING, errp)) return false;
    s->stage = NIKON_FRAME_CAPTURED; return true;
}

bool nikon_frame_process_submit(DeviceState *dev,
                                const NikonFrameDescriptor *d, Error **errp)
{
    NikonFrameState *s = NIKON_FRAME_CONTRACT(dev);
    if (!expect(s, NIKON_FRAME_CAPTURED, errp) || !descriptor_valid(d, errp)) return false;
    s->process = *d; s->stage = NIKON_FRAME_PROCESS_PENDING; s->error = 0;
    return true;
}

bool nikon_frame_process_complete(DeviceState *dev, Error **errp)
{
    NikonFrameState *s = NIKON_FRAME_CONTRACT(dev);
    if (!expect(s, NIKON_FRAME_PROCESS_PENDING, errp)) return false;
    s->stage = NIKON_FRAME_PROCESSED; return true;
}

bool nikon_frame_display_submit(DeviceState *dev,
                                const NikonFrameDescriptor *d, Error **errp)
{
    NikonFrameState *s = NIKON_FRAME_CONTRACT(dev);
    if (!expect(s, NIKON_FRAME_PROCESSED, errp) || !descriptor_valid(d, errp)) return false;
    s->display = *d; s->stage = NIKON_FRAME_DISPLAY_PENDING; s->error = 0;
    return true;
}

bool nikon_frame_display_complete(DeviceState *dev, Error **errp)
{
    NikonFrameState *s = NIKON_FRAME_CONTRACT(dev);
    if (!expect(s, NIKON_FRAME_DISPLAY_PENDING, errp)) return false;
    s->stage = NIKON_FRAME_DISPLAYED; return true;
}

void nikon_frame_reset(DeviceState *dev)
{
    NikonFrameState *s = NIKON_FRAME_CONTRACT(dev);
    memset(&s->capture, 0, sizeof(s->capture));
    memset(&s->process, 0, sizeof(s->process));
    memset(&s->display, 0, sizeof(s->display));
    s->stage = NIKON_FRAME_UNBOUND; s->error = 0; s->generation++;
    s->reported_error = false;
}

NikonFrameStage nikon_frame_stage(DeviceState *dev) { return NIKON_FRAME_CONTRACT(dev)->stage; }
const NikonFrameDescriptor *nikon_frame_capture_descriptor(DeviceState *dev) { return &NIKON_FRAME_CONTRACT(dev)->capture; }
const NikonFrameDescriptor *nikon_frame_process_descriptor(DeviceState *dev) { return &NIKON_FRAME_CONTRACT(dev)->process; }
const NikonFrameDescriptor *nikon_frame_display_descriptor(DeviceState *dev) { return &NIKON_FRAME_CONTRACT(dev)->display; }

static uint32_t *word(NikonFrameDescriptor *d, unsigned index)
{
    switch (index) {
    case 0: return (uint32_t *)&d->source; case 1: return ((uint32_t *)&d->source) + 1;
    case 2: return (uint32_t *)&d->destination; case 3: return ((uint32_t *)&d->destination) + 1;
    case 4: return &d->width; case 5: return &d->height; case 6: return &d->stride;
    case 7: return &d->bytes_per_pixel; case 8: return &d->format; case 9: return &d->bayer;
    case 10: return &d->bit_depth; case 11: return &d->packing; case 12: return &d->flags;
    case 13: return (uint32_t *)&d->sequence; case 14: return ((uint32_t *)&d->sequence) + 1;
    default: return NULL;
    }
}

static uint32_t *field(NikonFrameState *s, hwaddr offset)
{
    unsigned base, index;
    NikonFrameDescriptor *d;

    if (offset < CAPTURE_BASE + 0x3c) { base = CAPTURE_BASE; d = &s->capture; }
    else if (offset >= PROCESS_BASE && offset < PROCESS_BASE + 0x3c) { base = PROCESS_BASE; d = &s->process; }
    else if (offset >= DISPLAY_BASE && offset < DISPLAY_BASE + 0x3c) { base = DISPLAY_BASE; d = &s->display; }
    else return NULL;
    index = (offset - base) / 4;
    return word(d, index);
}

static MemTxResult frame_error(NikonFrameState *s, hwaddr offset, unsigned size,
                               bool write, uint64_t value, const char *reason)
{
    nikon_access_error(&s->reported_error, s->regs.addr + offset, size, write, value, reason);
    return MEMTX_ERROR;
}

static MemTxResult frame_read(void *opaque, hwaddr offset, uint64_t *value,
                              unsigned size, MemTxAttrs attrs)
{
    NikonFrameState *s = opaque;
    uint32_t *ptr;
    if (size != 4 || (offset & 3)) return frame_error(s, offset, size, false, 0, "frame-access-width");
    if (offset == STATUS) *value = s->stage;
    else if (offset == FRAME_ERROR) *value = s->error;
    else if (offset == GENERATION) *value = s->generation;
    else if ((ptr = field(s, offset))) *value = *ptr;
    else return frame_error(s, offset, size, false, 0, "frame-read-unknown");
    return MEMTX_OK;
}

static MemTxResult command(NikonFrameState *s, uint32_t value)
{
    Error *err = NULL; bool ok;
    switch (value) {
    case CAPTURE_SUBMIT: ok = nikon_frame_capture_submit(DEVICE(s), &s->capture, &err); break;
    case CAPTURE_COMPLETE: ok = nikon_frame_capture_complete(DEVICE(s), &err); break;
    case PROCESS_SUBMIT: ok = nikon_frame_process_submit(DEVICE(s), &s->process, &err); break;
    case PROCESS_COMPLETE: ok = nikon_frame_process_complete(DEVICE(s), &err); break;
    case DISPLAY_SUBMIT: ok = nikon_frame_display_submit(DEVICE(s), &s->display, &err); break;
    case DISPLAY_COMPLETE: ok = nikon_frame_display_complete(DEVICE(s), &err); break;
    case CONTRACT_RESET: nikon_frame_reset(DEVICE(s)); ok = true; break;
    default: return frame_error(s, COMMAND, 4, true, value, "frame-command-unknown");
    }
    if (!ok) { error_free(err); return MEMTX_ERROR; }
    error_free(err); return MEMTX_OK;
}

static MemTxResult frame_write(void *opaque, hwaddr offset, uint64_t value,
                               unsigned size, MemTxAttrs attrs)
{
    NikonFrameState *s = opaque;
    uint32_t *ptr;
    if (size != 4 || (offset & 3)) return frame_error(s, offset, size, true, value, "frame-access-width");
    if (offset == COMMAND) return command(s, value);
    if (offset == STATUS || offset == FRAME_ERROR || offset == GENERATION)
        return frame_error(s, offset, size, true, value, "frame-read-only");
    ptr = field(s, offset);
    if (!ptr) return frame_error(s, offset, size, true, value, "frame-write-unknown");
    *ptr = value; return MEMTX_OK;
}

static const MemoryRegionOps frame_ops = {
    .read_with_attrs = frame_read, .write_with_attrs = frame_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 4, .max_access_size = 4 },
    .impl = { .min_access_size = 4, .max_access_size = 4 },
};

static void frame_reset_device(DeviceState *dev) { nikon_frame_reset(dev); }
static void frame_init(Object *obj)
{
    NikonFrameState *s = NIKON_FRAME_CONTRACT(obj);
    memory_region_init_io(&s->regs, obj, &frame_ops, s, "nikon.frame-contract", NIKON_FRAME_CONTRACT_REGS);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->regs); nikon_frame_reset(DEVICE(s));
}
static const VMStateDescription frame_vmstate = { .name = "nikon-frame-contract", .unmigratable = true };
static void frame_class_init(ObjectClass *oc, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc); dc->vmsd = &frame_vmstate;
    dc->user_creatable = false; device_class_set_legacy_reset(dc, frame_reset_device);
}
static const TypeInfo frame_type = {
    .name = TYPE_NIKON_FRAME_CONTRACT, .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(NikonFrameState), .instance_init = frame_init,
    .class_init = frame_class_init,
};
static void frame_register_types(void) { type_register_static(&frame_type); }
type_init(frame_register_types)
