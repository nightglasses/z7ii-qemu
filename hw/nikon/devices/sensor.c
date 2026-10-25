/*
 * Explicit external CMOS frame input and causal physical-memory readout.
 * No sensor slave, controller MMIO, pixel encoding or guest IRQ is guessed.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu/timer.h"
#include "io/channel-file.h"
#include "migration/vmstate.h"
#include "hw/core/irq.h"
#include "hw/core/qdev-properties.h"
#include "hw/core/sysbus.h"
#include "system/address-spaces.h"
#include "system/dma.h"
#include "system/reset.h"
#include "hw/nikon/sensor.h"
#include "hw/nikon/statistics.h"
#include "hw/nikon/devices.h"

#define SENSOR_CHUNK 4096
OBJECT_DECLARE_SIMPLE_TYPE(NikonSensorState, NIKON_SENSOR)

struct NikonSensorState {
    DeviceState parent_obj;
    char *filename;
    QIOChannelFile *input;
    QEMUTimer *timer;
    qemu_irq output[NIKON_SENSOR_OUTPUTS];
    NikonSensorFrame configuration, active;
    uint32_t phase, row, column;
    uint64_t written, generation;
    int64_t deadline;
};

static bool nikon_sensor_get_provider_bound(Object *obj, Error **errp)
{
    return NIKON_SENSOR(obj)->input != NULL;
}

static bool nikon_sensor_get_configured(Object *obj, Error **errp)
{
    const NikonSensorFrame *f = &NIKON_SENSOR(obj)->configuration;

    return f->row_bytes && f->rows && f->stride >= f->row_bytes &&
           f->exposure_ns && f->readout_ns;
}

bool nikon_sensor_provider_bound(DeviceState *dev)
{
    return NIKON_SENSOR(dev)->input != NULL;
}

bool nikon_sensor_configured(DeviceState *dev)
{
    return nikon_sensor_get_configured(OBJECT(dev), NULL);
}

static void nikon_sensor_outputs(NikonSensorState *s)
{
    qemu_set_irq(s->output[NIKON_SENSOR_EXPOSURE_ACTIVE],
                 s->phase == NIKON_SENSOR_EXPOSING);
    qemu_set_irq(s->output[NIKON_SENSOR_READOUT_ACTIVE],
                 s->phase == NIKON_SENSOR_READING);
    qemu_set_irq(s->output[NIKON_SENSOR_FRAME_DONE],
                 s->phase == NIKON_SENSOR_DONE);
    qemu_set_irq(s->output[NIKON_SENSOR_FRAME_ERROR],
                 s->phase == NIKON_SENSOR_ERROR);
}

static void nikon_sensor_finish(NikonSensorState *s, uint32_t phase)
{
    timer_del(s->timer);
    s->generation++;
    s->phase = phase;
    nikon_sensor_outputs(s);
}

void nikon_sensor_cancel_frame(DeviceState *dev)
{
    NikonSensorState *s = NIKON_SENSOR(dev);

    nikon_sensor_finish(s, NIKON_SENSOR_IDLE);
}

static void nikon_sensor_schedule(NikonSensorState *s, uint64_t delay)
{
    s->deadline += delay;
    timer_mod(s->timer, s->deadline);
}

static bool nikon_sensor_writable(hwaddr address, uint64_t length)
{
    while (length) {
        MemoryRegionSection section = memory_region_find(get_system_memory(),
                                                         address, length);
        uint64_t span;
        bool writable;

        if (!section.mr) {
            return false;
        }
        span = int128_get64(section.size);
        writable = section.offset_within_address_space == address && span &&
                   memory_access_is_direct(section.mr, true,
                                            MEMTXATTRS_UNSPECIFIED) &&
                   !section.readonly;
        memory_region_unref(section.mr);
        if (!writable) {
            return false;
        }
        address += span;
        length -= span;
    }
    return true;
}

static int64_t nikon_sensor_frame_length(NikonSensorState *s)
{
    struct stat info;

    if (!s->input) {
        return -ENOMEDIUM;
    }
    if (fstat(s->input->fd, &info) < 0) {
        return -errno;
    }
    return info.st_size;
}

static void nikon_sensor_readout(void *opaque)
{
    NikonSensorState *s = opaque;
    uint8_t bytes[SENSOR_CHUNK];
    uint64_t generation = s->generation;
    uint64_t address;
    uint64_t length = (uint64_t)s->active.row_bytes * s->active.rows;
    uint32_t count;
    MemTxAttrs attrs = MEMTXATTRS_UNSPECIFIED;
    MemTxResult result;
    Error *error = NULL;
    QIOChannelFile *input;
    int64_t input_length;
    int ret;

    if (s->phase == NIKON_SENSOR_EXPOSING) {
        s->phase = NIKON_SENSOR_READING;
        nikon_sensor_outputs(s);
        /* A controller may cancel through a real connected output callback. */
        if (s->generation == generation) {
            nikon_sensor_schedule(s, s->active.readout_ns);
        }
        return;
    }
    if (s->phase != NIKON_SENSOR_READING) {
        return;
    }
    input_length = nikon_sensor_frame_length(s);
    if (s->generation != generation || s->phase != NIKON_SENSOR_READING) {
        return;
    }
    if (input_length != (int64_t)length) {
        nikon_sensor_finish(s, NIKON_SENSOR_ERROR);
        return;
    }
    count = MIN(SENSOR_CHUNK, s->active.row_bytes - s->column);
    address = s->active.destination + (uint64_t)s->row * s->active.stride +
              s->column;
    input = s->input;
    object_ref(OBJECT(input));
    ret = qio_channel_read_all(QIO_CHANNEL(input), bytes, count, &error);
    object_unref(OBJECT(input));
    error_free(error);
    if (s->generation != generation || s->phase != NIKON_SENSOR_READING) {
        return;
    }
    if (ret < 0) {
        nikon_sensor_finish(s, NIKON_SENSOR_ERROR);
        return;
    }
    input_length = nikon_sensor_frame_length(s);
    if (s->generation != generation || s->phase != NIKON_SENSOR_READING) {
        return;
    }
    if (input_length != (int64_t)length) {
        nikon_sensor_finish(s, NIKON_SENSOR_ERROR);
        return;
    }
    /* QEMU may ignore ROM writes with MEMTX_OK: require actual writable RAM. */
    if (!nikon_sensor_writable(address, count)) {
        nikon_sensor_finish(s, NIKON_SENSOR_ERROR);
        return;
    }
    attrs.memory = true;
    result = dma_memory_write(&address_space_memory, address, bytes, count,
                               attrs);
    if (s->generation != generation || s->phase != NIKON_SENSOR_READING) {
        return;
    }
    if (result != MEMTX_OK) {
        nikon_sensor_finish(s, NIKON_SENSOR_ERROR);
        return;
    }
    s->written += count;
    s->column += count;
    if (s->column == s->active.row_bytes) {
        s->column = 0;
        s->row++;
    }
    if (s->row == s->active.rows) {
        nikon_sensor_finish(s, NIKON_SENSOR_DONE);
    } else {
        nikon_sensor_schedule(s, s->active.readout_ns);
    }
}

bool nikon_sensor_begin_frame(DeviceState *dev, const NikonSensorFrame *frame,
                              Error **errp)
{
    NikonSensorState *s = NIKON_SENSOR(dev);
    NikonSensorFrame request = *frame;
    uint64_t length, span, steps;
    uint64_t generation = s->generation;
    int64_t now;
    int64_t input_length;

    frame = &request;
    if (!dev->realized) {
        error_setg(errp, "sensor must be realized before capture");
        return false;
    }
    if (s->phase == NIKON_SENSOR_EXPOSING ||
        s->phase == NIKON_SENSOR_READING) {
        error_setg(errp, "sensor capture is already active");
        return false;
    }
    if (!frame->row_bytes || !frame->rows ||
        frame->stride < frame->row_bytes || !frame->readout_ns ||
        !frame->exposure_ns) {
        error_setg(errp, "sensor requires nonzero byte geometry and timing");
        return false;
    }
    length = (uint64_t)frame->row_bytes * frame->rows;
    span = (uint64_t)(frame->rows - 1) * frame->stride + frame->row_bytes;
    steps = (((uint64_t)frame->row_bytes + SENSOR_CHUNK - 1) / SENSOR_CHUNK) *
            frame->rows;
    if (frame->destination >= (1ULL << 32) ||
        span > (1ULL << 32) - frame->destination) {
        error_setg(errp, "sensor physical destination overflows");
        return false;
    }
    if (!s->input) {
        error_setg(errp, "sensor has no supplied frame");
        return false;
    }
    input_length = nikon_sensor_frame_length(s);
    if (input_length < 0 || length != (uint64_t)input_length) {
        error_setg(errp, "sensor frame length does not match byte geometry");
        return false;
    }
    if (s->generation != generation) {
        error_setg(errp, "sensor changed during capture preflight");
        return false;
    }
    now = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
    if (now < 0 || frame->exposure_ns > INT64_MAX - now ||
        frame->readout_ns > (INT64_MAX - now - frame->exposure_ns) / steps) {
        error_setg(errp, "sensor virtual deadline overflows");
        return false;
    }
    if (lseek(s->input->fd, 0, SEEK_SET) < 0) {
        error_setg_errno(errp, errno, "could not rewind sensor frame");
        return false;
    }
    s->active = *frame;
    s->generation++;
    s->row = s->column = 0;
    s->written = 0;
    s->phase = NIKON_SENSOR_EXPOSING;
    s->deadline = now;
    nikon_sensor_schedule(s, frame->exposure_ns);
    nikon_sensor_outputs(s);
    return true;
}

static bool nikon_sensor_capture(Object *obj, Error **errp)
{
    NikonSensorState *s = NIKON_SENSOR(obj);

    return s->phase == NIKON_SENSOR_EXPOSING ||
           s->phase == NIKON_SENSOR_READING;
}

static void nikon_sensor_set_capture(Object *obj, bool value, Error **errp)
{
    NikonSensorState *s = NIKON_SENSOR(obj);

    if (value) {
        nikon_sensor_begin_frame(DEVICE(s), &s->configuration, errp);
    } else {
        nikon_sensor_cancel_frame(DEVICE(s));
    }
}

static void nikon_sensor_realize(DeviceState *dev, Error **errp)
{
    NikonSensorState *s = NIKON_SENSOR(dev);
    struct stat info;

    if (s->filename) {
        s->input = qio_channel_file_new_path(s->filename,
                                            O_RDONLY | O_NONBLOCK, 0, errp);
        if (!s->input) {
            return;
        }
        if (fstat(s->input->fd, &info) < 0) {
            error_setg_errno(errp, errno, "could not inspect sensor frame");
            object_unref(OBJECT(s->input));
            s->input = NULL;
            return;
        }
        if (!S_ISREG(info.st_mode)) {
            error_setg(errp, "sensor frame must be a regular byte file");
            object_unref(OBJECT(s->input));
            s->input = NULL;
            return;
        }
    }
    /* This external endpoint is not a child of an invented controller bus. */
    qemu_register_resettable(OBJECT(dev));
}

static void nikon_sensor_unrealize(DeviceState *dev)
{
    NikonSensorState *s = NIKON_SENSOR(dev);

    nikon_sensor_cancel_frame(dev);
    qemu_unregister_resettable(OBJECT(dev));
    if (s->input) {
        object_unref(OBJECT(s->input));
        s->input = NULL;
    }
}

static void nikon_sensor_reset(DeviceState *dev)
{
    NikonSensorState *s = NIKON_SENSOR(dev);

    nikon_sensor_cancel_frame(dev);
    s->row = s->column = 0;
    s->written = 0;
}

static void nikon_sensor_init(Object *obj)
{
    NikonSensorState *s = NIKON_SENSOR(obj);

    s->timer = timer_new_ns(QEMU_CLOCK_VIRTUAL, nikon_sensor_readout, s);
    qdev_init_gpio_out(DEVICE(s), s->output, NIKON_SENSOR_OUTPUTS);
    object_property_add_bool(obj, "capture", nikon_sensor_capture,
                              nikon_sensor_set_capture);
    object_property_add_uint32_ptr(obj, "phase", &s->phase, OBJ_PROP_FLAG_READ);
    object_property_add_uint64_ptr(obj, "bytes-written", &s->written,
                                   OBJ_PROP_FLAG_READ);
    object_property_add_bool(obj, "provider-bound",
                             nikon_sensor_get_provider_bound, NULL);
    object_property_add_bool(obj, "configured", nikon_sensor_get_configured,
                             NULL);
}

static void nikon_sensor_finalize(Object *obj)
{
    NikonSensorState *s = NIKON_SENSOR(obj);

    timer_free(s->timer);
}

static const Property nikon_sensor_properties[] = {
    DEFINE_PROP_STRING("frame", NikonSensorState, filename),
    DEFINE_PROP_UINT32("row-bytes", NikonSensorState,
                       configuration.row_bytes, 0),
    DEFINE_PROP_UINT32("rows", NikonSensorState, configuration.rows, 0),
    DEFINE_PROP_UINT32("stride", NikonSensorState, configuration.stride, 0),
    DEFINE_PROP_UINT64("destination", NikonSensorState,
                       configuration.destination, 0),
    DEFINE_PROP_UINT64("exposure-ns", NikonSensorState,
                       configuration.exposure_ns, 0),
    DEFINE_PROP_UINT64("readout-ns", NikonSensorState,
                       configuration.readout_ns, 0),
};

static const VMStateDescription nikon_sensor_vmstate = {
    .name = "nikon-sensor",
    .unmigratable = true,
};

static void nikon_sensor_class_init(ObjectClass *oc, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->realize = nikon_sensor_realize;
    dc->unrealize = nikon_sensor_unrealize;
    dc->vmsd = &nikon_sensor_vmstate;
    device_class_set_props(dc, nikon_sensor_properties);
    device_class_set_legacy_reset(dc, nikon_sensor_reset);
}

static const TypeInfo nikon_sensor_type = {
    .name = TYPE_NIKON_SENSOR,
    .parent = TYPE_DEVICE,
    .instance_size = sizeof(NikonSensorState),
    .instance_init = nikon_sensor_init,
    .instance_finalize = nikon_sensor_finalize,
    .class_init = nikon_sensor_class_init,
};

/* Original SDES Driver register protocol. This controller does not derive a
 * frame request from unidentified numeric parameters or fabricate an event.
 */
#define SDES_WORDS (0x554 / 4)
#define SDES_STATUS_WORDS 11
OBJECT_DECLARE_SIMPLE_TYPE(NikonSdesState, NIKON_SDES)

typedef struct NikonSdesBank {
    NikonSdesState *s;
    MemoryRegion memory;
    unsigned index;
} NikonSdesBank;

struct NikonSdesState {
    SysBusDevice parent_obj;
    NikonSdesBank bank[NIKON_SDES_REGIONS];
    qemu_irq irq[NIKON_SDES_EVENTS];
    uint32_t configuration[NIKON_SDES_REGIONS][SDES_WORDS];
    bool known[NIKON_SDES_REGIONS][SDES_WORDS];
    uint32_t pending[SDES_STATUS_WORDS];
    uint32_t levels[SDES_STATUS_WORDS], seen[SDES_STATUS_WORDS];
    uint8_t event_levels;
    bool reported_error;
};

typedef struct NikonSdesStatus {
    unsigned bank;
    hwaddr offset;
    enum NikonSdesEvent event;
} NikonSdesStatus;

static const NikonSdesStatus nikon_sdes_status[] = {
    { NIKON_SDES_STATUS, 0x14, NIKON_SDES_STATUS_EVENT },
    { NIKON_SDES_PARAMETERS, 0x108, NIKON_SDES_PRIMARY_EVENT },
    { NIKON_SDES_PARAMETERS, 0x24, NIKON_SDES_PRIMARY_DETAIL_EVENT },
    { NIKON_SDES_PARAMETERS, 0x110, NIKON_SDES_PRIMARY_DETAIL_EVENT },
    { NIKON_SDES_PARAMETERS, 0x118, NIKON_SDES_PRIMARY_DETAIL_EVENT },
    { NIKON_SDES_PARAMETERS, 0x11c, NIKON_SDES_PRIMARY_DETAIL_EVENT },
    { NIKON_SDES_PARAMETERS, 0x508, NIKON_SDES_SECONDARY_EVENT },
    { NIKON_SDES_PARAMETERS, 0x424, NIKON_SDES_SECONDARY_DETAIL_EVENT },
    { NIKON_SDES_PARAMETERS, 0x510, NIKON_SDES_SECONDARY_DETAIL_EVENT },
    { NIKON_SDES_PARAMETERS, 0x518, NIKON_SDES_SECONDARY_DETAIL_EVENT },
    { NIKON_SDES_PARAMETERS, 0x51c, NIKON_SDES_SECONDARY_DETAIL_EVENT },
};

/* Bounds of values constructed by the saved software, not physical IP masks. */
static bool nikon_sdes_config(unsigned bank, hwaddr offset, uint32_t *mask)
{
    *mask = UINT32_MAX;
    if (bank == NIKON_SDES_CONFIGURATION) {
        if ((offset >= 0xa0 && offset <= 0xac) ||
            (offset >= 0x104 && offset <= 0x120) ||
            (offset >= 0x204 && offset <= 0x220)) {
            return true;
        }
        if (offset >= 0x14 && offset <= 0x20) {
            *mask = 0x3fffffff;
            return true;
        }
        if ((offset >= 0xb8 && offset <= 0xd4) || offset == 0xe0 ||
            offset == 0xe4 || (offset >= 0x26c && offset <= 0x274)) {
            *mask = 0x00ff00ff;
            return true;
        }
        if ((offset >= 0x400 && offset <= 0x410) ||
            (offset >= 0x420 && offset <= 0x430) ||
            (offset >= 0x440 && offset <= 0x450) ||
            (offset >= 0x460 && offset <= 0x470) ||
            (offset >= 0x4a0 && offset <= 0x4b0)) {
            *mask = 0xff;
            return true;
        }
        switch (offset) {
        case 0:
            *mask = 0x00f0ffff;
            break;
        case 4: case 0x10: case 0x24: case 0x28: case 0x2c: case 0x3c:
        case 0x64: case 0x7c: case 0x80: case 0x8c: case 0xb4:
        case 0xe8: case 0xec: case 0xf4: case 0xf8: case 0xfc: case 0x100:
        case 0x12c: case 0x130: case 0x134: case 0x200: case 0x224:
        case 0x228: case 0x234: case 0x238: case 0x240: case 0x244:
        case 0x250: case 0x258: case 0x25c: case 0x260: case 0x264:
        case 0x268:
            break;
        case 0x30: case 0x34: case 0x38: case 0x44: case 0x5c:
        case 0x70: case 0x74: case 0x78: case 0x84: case 0x94:
        case 0x22c: case 0x230: case 0x23c: case 0x24c: case 0x254:
        case 0x27c: case 0x284: case 0x288: case 0x28c:
            *mask = 0xffff;
            break;
        case 0x40: case 0x48: case 0x60: case 0x68: case 0x6c:
        case 0x88: case 0x90: case 0x280:
            *mask = 0xff;
            break;
        case 0x138: case 0x13c:
            *mask = 1;
            break;
        case 0x278:
            *mask = 0xff3ff3ff;
            break;
        case 0x248:
            *mask = 0xffff00ff;
            break;
        default:
            return false;
        }
        return true;
    }
    if (bank == NIKON_SDES_PARAMETERS) {
        if (offset >= 0x400) {
            offset -= 0x400;
        }
        switch (offset) {
        case 0x14:
            *mask = 0x00ffffff;
            break;
        case 0x18: case 0x1c: case 0x20: case 0x104: case 0x134:
            break;
        case 0x128: case 0x12c: case 0x13c: case 0x140:
            *mask = 0xffff;
            break;
        case 0x130:
            *mask = 0xff;
            break;
        case 0x138:
            *mask = 0x7fff;
            break;
        case 0x150:
            *mask = 1;
            break;
        default:
            return false;
        }
        return true;
    }
    return false;
}

static void nikon_sdes_irqs(NikonSdesState *s)
{
    uint32_t pending[NIKON_SDES_EVENTS] = { 0 };
    unsigned i;

    for (i = 0; i < ARRAY_SIZE(nikon_sdes_status); i++) {
        pending[nikon_sdes_status[i].event] |= s->pending[i];
    }
    for (i = 0; i < NIKON_SDES_EVENTS; i++) {
        qemu_set_irq(s->irq[i], !!pending[i]);
    }
}

bool nikon_sdes_publish(DeviceState *dev, enum NikonSdesEvent event,
                        const uint32_t *words, size_t count, Error **errp)
{
    NikonSdesState *s = NIKON_SDES(dev);
    size_t expected = 0, n = 0;
    unsigned i;

    if ((unsigned)event >= NIKON_SDES_EVENTS || !words) {
        error_setg(errp, "invalid SDES external event");
        return false;
    }
    for (i = 0; i < ARRAY_SIZE(nikon_sdes_status); i++) {
        expected += nikon_sdes_status[i].event == event;
    }
    if (count != expected) {
        error_setg(errp, "SDES external event needs %zu complete status words",
                   expected);
        return false;
    }
    for (i = 0; i < ARRAY_SIZE(nikon_sdes_status); i++) {
        if (nikon_sdes_status[i].event == event) {
            s->pending[i] |= words[n++];
        }
    }
    nikon_sdes_irqs(s);
    return true;
}

static void nikon_sdes_data(void *opaque, int index, int level)
{
    NikonSdesState *s = opaque;
    unsigned word = index / 32;
    uint32_t bit = 1U << (index % 32);

    s->levels[word] = (s->levels[word] & ~bit) | (level ? bit : 0);
    s->seen[word] |= bit;
}

static void nikon_sdes_event(void *opaque, int event, int level)
{
    NikonSdesState *s = opaque;
    uint32_t words[4];
    unsigned i, count = 0;
    bool rising = level && !(s->event_levels & (1U << event));

    s->event_levels = (s->event_levels & ~(1U << event)) | (!!level << event);
    if (!rising) {
        return;
    }
    for (i = 0; i < ARRAY_SIZE(nikon_sdes_status); i++) {
        if (nikon_sdes_status[i].event == event) {
            if (s->seen[i] != UINT32_MAX) {
                return;
            }
            words[count++] = s->levels[i];
        }
    }
    nikon_sdes_publish(DEVICE(s), event, words, count, &error_abort);
    for (i = 0; i < ARRAY_SIZE(nikon_sdes_status); i++) {
        if (nikon_sdes_status[i].event == event) {
            s->seen[i] = 0;
        }
    }
}

static MemTxResult nikon_sdes_error(NikonSdesBank *bank, hwaddr offset,
                                   unsigned size, bool write, uint64_t value)
{
    nikon_access_error(&bank->s->reported_error, bank->memory.addr + offset,
                      size, write, value,
                      "sdes-access-or-unobserved-reset-value-not-supported");
    return MEMTX_ERROR;
}

static MemTxResult nikon_sdes_read(void *opaque, hwaddr offset, uint64_t *value,
                                  unsigned size, MemTxAttrs attrs)
{
    NikonSdesBank *bank = opaque;
    NikonSdesState *s = bank->s;
    uint32_t mask;
    unsigned i;

    *value = 0;
    if (size != 4 || (offset & 3)) {
        return nikon_sdes_error(bank, offset, size, false, 0);
    }
    for (i = 0; i < ARRAY_SIZE(nikon_sdes_status); i++) {
        if (nikon_sdes_status[i].bank == bank->index &&
            nikon_sdes_status[i].offset == offset) {
            *value = s->pending[i];
            return MEMTX_OK;
        }
    }
    if (nikon_sdes_config(bank->index, offset, &mask) &&
        s->known[bank->index][offset / 4]) {
        *value = s->configuration[bank->index][offset / 4];
        return MEMTX_OK;
    }
    return nikon_sdes_error(bank, offset, size, false, 0);
}

static MemTxResult nikon_sdes_write(void *opaque, hwaddr offset, uint64_t value,
                                   unsigned size, MemTxAttrs attrs)
{
    NikonSdesBank *bank = opaque;
    NikonSdesState *s = bank->s;
    uint32_t mask;
    unsigned i;

    if (size != 4 || (offset & 3)) {
        return nikon_sdes_error(bank, offset, size, true, value);
    }
    for (i = 0; i < ARRAY_SIZE(nikon_sdes_status); i++) {
        if (nikon_sdes_status[i].bank == bank->index &&
            nikon_sdes_status[i].offset == offset) {
            s->pending[i] &= ~value;
            nikon_sdes_irqs(s);
            return MEMTX_OK;
        }
    }
    if (!nikon_sdes_config(bank->index, offset, &mask) || (value & ~mask)) {
        return nikon_sdes_error(bank, offset, size, true, value);
    }
    s->configuration[bank->index][offset / 4] = value;
    s->known[bank->index][offset / 4] = true;
    return MEMTX_OK;
}

static const MemoryRegionOps nikon_sdes_ops = {
    .read_with_attrs = nikon_sdes_read,
    .write_with_attrs = nikon_sdes_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 4, .max_access_size = 4,
               .unaligned = false },
};

static void nikon_sdes_reset(DeviceState *dev)
{
    NikonSdesState *s = NIKON_SDES(dev);
    unsigned bank, offset;
    uint32_t mask;

    memset(s->configuration, 0, sizeof(s->configuration));
    memset(s->known, 0, sizeof(s->known));
    /* Explicit functional cold-zero profile for known control registers.
     * Unknown data/status apertures do not acquire a reset value here.
     */
    for (bank = 0; bank < NIKON_SDES_REGIONS; bank++) {
        for (offset = 0; offset < SDES_WORDS * 4; offset += 4) {
            s->known[bank][offset / 4] =
                nikon_sdes_config(bank, offset, &mask);
        }
    }
    memset(s->pending, 0, sizeof(s->pending));
    memset(s->levels, 0, sizeof(s->levels));
    memset(s->seen, 0, sizeof(s->seen));
    s->event_levels = 0;
    s->reported_error = false;
    nikon_sdes_irqs(s);
}

static void nikon_sdes_init(Object *obj)
{
    NikonSdesState *s = NIKON_SDES(obj);
    static const unsigned sizes[] = { 0x500, 0x2c, 0x554 };
    static const char * const names[] = {
        "nikon-sdes-configuration", "nikon-sdes-status", "nikon-sdes-parameters",
    };
    unsigned i;

    for (i = 0; i < NIKON_SDES_REGIONS; i++) {
        s->bank[i].s = s;
        s->bank[i].index = i;
        memory_region_init_io(&s->bank[i].memory, obj, &nikon_sdes_ops,
                              &s->bank[i], names[i], sizes[i]);
        sysbus_init_mmio(SYS_BUS_DEVICE(s), &s->bank[i].memory);
    }
    for (i = 0; i < NIKON_SDES_EVENTS; i++) {
        sysbus_init_irq(SYS_BUS_DEVICE(s), &s->irq[i]);
    }
    qdev_init_gpio_in_named(DEVICE(s), nikon_sdes_data, "status-data",
                            SDES_STATUS_WORDS * 32);
    qdev_init_gpio_in_named(DEVICE(s), nikon_sdes_event, "status-publish",
                            NIKON_SDES_EVENTS);
}

static const VMStateDescription nikon_sdes_vmstate = {
    .name = "nikon-sdes",
    .unmigratable = true,
};

static void nikon_sdes_class_init(ObjectClass *oc, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->vmsd = &nikon_sdes_vmstate;
    device_class_set_legacy_reset(dc, nikon_sdes_reset);
}

static const TypeInfo nikon_sdes_type = {
    .name = TYPE_NIKON_SDES,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(NikonSdesState),
    .instance_init = nikon_sdes_init,
    .class_init = nikon_sdes_class_init,
};

/* Capture configuration packet10238fbc and handlers1020a836..1020a9b2.
 * Data status has an explicit provider. Finite stop fences below do no DMA.
 */
#define CAPTURE_SIZE 0x1180
#define CAPTURE_PACKET_BASE 0x1d00
#define CAPTURE_PACKET_SIZE 0x204
#define CAPTURE_WORDS ((CAPTURE_PACKET_BASE + CAPTURE_PACKET_SIZE) / 4)
#define CAPTURE_STATUS_WORDS 8
OBJECT_DECLARE_SIMPLE_TYPE(NikonCaptureControl, NIKON_CAPTURE_CONTROL)

/*
 * 1023858a and10239298..10239548 identify upper quiescence causes;
 * 1020a71a identifies separate low-level stop causes. No pixel work implied.
 */
static const struct {
    uint32_t unit;
    uint32_t causes[CAPTURE_STATUS_WORDS];
} nikon_capture_stops[] = {
    { 1,     { [0] = 0x200 } },
    { 2,     { [7] = 0x38 } },
    { 4,     { [0] = 0x8000 } },
    { 8,     { [0] = 0x8000 } },
    { 0x10,  { [0] = 0x80000000, [1] = 1 } },
    { 0x20,  { [0] = 0x18000000 } },
    { 0x40,  { [0] = 0x20000 } },
    { 0x80,  { [0] = 0x80000 } },
    { 0x200, { [1] = 0x180 } },
    { 0x400, { [1] = 0x18 } },
    { 0x800, { [1] = 0x1800 } },
};

typedef struct NikonCaptureStop {
    NikonCaptureControl *s;
    QEMUTimer *timer;
    unsigned index;
} NikonCaptureStop;

struct NikonCaptureControl {
    SysBusDevice parent_obj;
    MemoryRegion memory;
    MemoryRegion packet_memory;
    qemu_irq irq[NIKON_CAPTURE_EVENTS];
    qemu_irq command_data[64], command_publish[2];
    qemu_irq run_state[12];
    uint32_t configuration[CAPTURE_WORDS];
    bool known[CAPTURE_WORDS];
    uint32_t pending[CAPTURE_STATUS_WORDS];
    uint32_t levels[CAPTURE_STATUS_WORDS], seen[CAPTURE_STATUS_WORDS];
    uint32_t event_levels;
    NikonCaptureStop stop[ARRAY_SIZE(nikon_capture_stops)];
    uint32_t drained_shared;
    bool reported_error;
};

static const unsigned nikon_capture_events[CAPTURE_STATUS_WORDS] = {
    0, 0, 1, 2, 2, 3, 3, 4,
};

static bool nikon_capture_config(unsigned offset, uint32_t *mask)
{
    *mask = UINT32_MAX;
    /* 102094aa/102093ec/10209578 store every word of this complete packet. */
    if (offset >= CAPTURE_PACKET_BASE &&
        offset < CAPTURE_PACKET_BASE + CAPTURE_PACKET_SIZE) {
        unsigned word = offset - CAPTURE_PACKET_BASE;

        if ((word >= 0xc && word <= 0x20) ||
            (word >= 0x1dc && word <= 0x1e4)) {
            *mask = 0xffff;
        } else {
            switch (word) {
            case 0x08: case 0x1d8:
                *mask = 0xfffff;
                break;
            case 0xcc: case 0x174: case 0x1e8:
                *mask = 0xffffff;
                break;
            case 0x1d4:
                *mask = 0xffff;
                break;
            case 0x1f8:
                *mask = 0xffff00ff;
                break;
            case 0x1fc:
                *mask = 0xff;
                break;
            case 0x200:
                *mask = 0x11;
                break;
            }
        }
        return true;
    }
    /* Complete original stores, not an enclosing peripheral aperture.
     * Six seven-word subtables at871c..87c0 fill the middle of this range.
     */
    if (offset == 0x500 || (offset >= 0x508 && offset <= 0x81c) ||
        (offset >= 0xa00 && offset <= 0xafc) ||
        (offset >= 0xc00 && offset <= 0xe3c) ||
        (offset >= 0xf00 && offset <= 0x100c) ||
        (offset >= 0x1100 && offset <= 0x117c)) {
        return true;
    }
    if (offset >= 0x200 && offset <= 0x2e0 && !(offset & 0x1f)) {
        return true;
    }
    switch (offset) {
    case 0: case 4: case 0x10: case 0x14: case 0x18:
        *mask = 0xfff;
        return true;
    case 8: case 0xc:
        /* All eleven recovered unit bits, not reserved bit8. */
        *mask = 0xeff;
        return true;
    /* 1020bfc2/1020ab3c: raw word parameters, separate boolean enable. */
    case 0x20: case 0x24: case 0x60:
    case 0x8c: case 0x100: case 0x104:
        return true;
    case 0x28:
        *mask = 1;
        return true;
    case 0x70:
        /* 1020bfde: boolean bits5/1 and the two-bit selector at bits4:3. */
        *mask = 0x3a;
        return true;
    case 0x88:
        *mask = 0xff;
        return true;
    default:
        return false;
    }
}

bool nikon_capture_configuration(DeviceState *dev, unsigned offset,
                                 uint32_t *value)
{
    NikonCaptureControl *s = NIKON_CAPTURE_CONTROL(dev);
    uint32_t mask;

    if (!value || offset >= CAPTURE_WORDS * 4 || (offset & 3) ||
        !nikon_capture_config(offset, &mask) || !s->known[offset / 4]) {
        return false;
    }
    *value = s->configuration[offset / 4];
    return true;
}

static void nikon_capture_irqs(NikonCaptureControl *s)
{
    uint32_t active[NIKON_CAPTURE_EVENTS] = { 0 };
    unsigned i;

    /* Functional mask law:1 enables. Explicit cold-zero masks allow original
     * RMW while leaving all interrupts disabled until configured.
     */
    for (i = 0; i < CAPTURE_STATUS_WORDS; i++) {
        unsigned offset = 0x200 + i * 0x20;

        if (s->known[offset / 4]) {
            active[nikon_capture_events[i]] |=
                s->pending[i] & s->configuration[offset / 4];
        }
    }
    for (i = 0; i < NIKON_CAPTURE_EVENTS; i++) {
        qemu_set_irq(s->irq[i], !!active[i]);
    }
}

static void nikon_capture_shared(NikonCaptureControl *s)
{
    if (!s->drained_shared || (s->configuration[8 / 4] & 0xc)) {
        return;
    }
    for (unsigned i = 0; i < ARRAY_SIZE(s->stop); i++) {
        if ((nikon_capture_stops[i].unit & 0xc) &&
            timer_pending(s->stop[i].timer)) {
            return;
        }
    }
    s->drained_shared = 0;
    s->pending[0] |= 0x8000;
    nikon_capture_irqs(s);
}

static void nikon_capture_stopped(void *opaque)
{
    NikonCaptureStop *stop = opaque;
    NikonCaptureControl *s = stop->s;
    unsigned i = stop->index;

    if (nikon_capture_stops[i].unit & 0xc) {
        /* Keep expired identities until the shared drain can be delivered. */
        s->drained_shared |= nikon_capture_stops[i].unit;
        nikon_capture_shared(s);
        return;
    }
    if (s->configuration[8 / 4] & nikon_capture_stops[i].unit) {
        return;
    }
    for (unsigned word = 0; word < CAPTURE_STATUS_WORDS; word++) {
        s->pending[word] |= nikon_capture_stops[i].causes[word];
    }
    nikon_capture_irqs(s);
}

static void nikon_capture_cancel(NikonCaptureControl *s, uint32_t units)
{
    s->configuration[8 / 4] &= ~units;
    s->configuration[0xc / 4] = s->configuration[8 / 4];
    s->drained_shared &= ~units;
    for (unsigned i = 0; i < ARRAY_SIZE(s->stop); i++) {
        if (units & nikon_capture_stops[i].unit) {
            timer_del(s->stop[i].timer);
        }
    }
}

static void nikon_capture_run_state(NikonCaptureControl *s)
{
    for (unsigned i = 0; i < ARRAY_SIZE(s->run_state); i++) {
        qemu_set_irq(s->run_state[i], !!(s->configuration[8 / 4] & (1U << i)));
    }
}

bool nikon_capture_data_pending(DeviceState *dev)
{
    NikonCaptureControl *s = NIKON_CAPTURE_CONTROL(dev);

    /* Mode1 masks per-frame raw6 and consumes endpoint ring completion. */
    return s->pending[0] & s->configuration[0x200 / 4] & 0x40;
}

bool nikon_capture_front_pending(DeviceState *dev)
{
    NikonCaptureControl *s = NIKON_CAPTURE_CONTROL(dev);

    return (s->pending[0] & 0x58) || (s->pending[4] & 0x80);
}

bool nikon_capture_publish(DeviceState *dev, enum NikonCaptureEvent event,
                           const uint32_t *words, size_t count, Error **errp)
{
    NikonCaptureControl *s = NIKON_CAPTURE_CONTROL(dev);
    size_t expected = 0, n = 0;
    unsigned i;

    if ((unsigned)event >= NIKON_CAPTURE_EVENTS || !words) {
        error_setg(errp, "invalid capture control external event");
        return false;
    }
    for (i = 0; i < CAPTURE_STATUS_WORDS; i++) {
        expected += nikon_capture_events[i] == event;
    }
    if (count != expected) {
        error_setg(errp, "capture event needs %zu complete status words",
                   expected);
        return false;
    }
    for (i = 0; i < CAPTURE_STATUS_WORDS; i++) {
        if (nikon_capture_events[i] == event) {
            s->pending[i] |= words[n++];
        }
    }
    nikon_capture_irqs(s);
    return true;
}

static void nikon_capture_data(void *opaque, int index, int level)
{
    NikonCaptureControl *s = opaque;
    uint32_t bit = 1U << (index % 32);
    unsigned word = index / 32;

    s->levels[word] = (s->levels[word] & ~bit) | (level ? bit : 0);
    s->seen[word] |= bit;
}

static void nikon_capture_event(void *opaque, int event, int level)
{
    NikonCaptureControl *s = opaque;
    uint32_t words[2];
    unsigned i, count = 0;
    bool rising = level && !(s->event_levels & (1U << event));

    s->event_levels = (s->event_levels & ~(1U << event)) | (!!level << event);
    if (!rising) {
        return;
    }
    for (i = 0; i < CAPTURE_STATUS_WORDS; i++) {
        if (nikon_capture_events[i] == event) {
            if (s->seen[i] != UINT32_MAX) {
                return;
            }
            words[count++] = s->levels[i];
        }
    }
    nikon_capture_publish(DEVICE(s), event, words, count, &error_abort);
    for (i = 0; i < CAPTURE_STATUS_WORDS; i++) {
        if (nikon_capture_events[i] == event) {
            s->seen[i] = 0;
        }
    }
}

static MemTxResult nikon_capture_error(NikonCaptureControl *s, hwaddr offset,
                                      unsigned size, bool write, uint64_t value)
{
    nikon_access_error(&s->reported_error, s->memory.addr + offset,
                      size, write, value,
                      "capture-control-access-or-reset-value-not-supported");
    return MEMTX_ERROR;
}

static MemTxResult nikon_capture_read(void *opaque, hwaddr offset,
                                     uint64_t *value, unsigned size,
                                     MemTxAttrs attrs)
{
    NikonCaptureControl *s = opaque;
    uint32_t word;

    *value = 0;
    if (size != 4 || (offset & 3)) {
        return nikon_capture_error(s, offset, size, false, 0);
    }
    if (offset >= 0x210 && offset <= 0x2f0 && (offset & 0x1f) == 0x10) {
        *value = s->pending[(offset - 0x210) / 0x20];
        return MEMTX_OK;
    }
    if (nikon_capture_configuration(DEVICE(s), offset, &word)) {
        *value = word;
        return MEMTX_OK;
    }
    return nikon_capture_error(s, offset, size, false, 0);
}

static MemTxResult nikon_capture_write(void *opaque, hwaddr offset,
                                      uint64_t value, unsigned size,
                                      MemTxAttrs attrs)
{
    NikonCaptureControl *s = opaque;
    uint32_t mask;
    unsigned i;

    if (size != 4 || (offset & 3)) {
        return nikon_capture_error(s, offset, size, true, value);
    }
    if (offset >= 0x210 && offset <= 0x2f0 && (offset & 0x1f) == 0x10) {
        s->pending[(offset - 0x210) / 0x20] &= ~value;
        nikon_capture_irqs(s);
        return MEMTX_OK;
    }
    if (offset == 8 || offset == 0xc) {
        if (value & ~0xeffU) {
            return nikon_capture_error(s, offset, size, true, value);
        }
        if (offset == 8) {
            nikon_capture_cancel(s, value);
            /* Run intent alone implies no pixels or normal completion. */
            s->configuration[8 / 4] |= value & s->configuration[0x10 / 4] &
                                        ~s->configuration[0];
        } else {
            s->configuration[8 / 4] &= ~value;
            for (i = 0; i < ARRAY_SIZE(s->stop); i++) {
                if ((value & nikon_capture_stops[i].unit) &&
                    !(s->drained_shared & nikon_capture_stops[i].unit) &&
                    !timer_pending(s->stop[i].timer)) {
                    /* Hypothesis: idempotent quiescence takes1us, no data. */
                    timer_mod(s->stop[i].timer,
                              qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + 1000);
                }
            }
        }
        /* Functional readback mirrors run state, not the last stop mask. */
        s->configuration[0xc / 4] = s->configuration[8 / 4];
        nikon_capture_run_state(s);
        nikon_capture_shared(s);
        return MEMTX_OK;
    }
    if (offset == 0 || offset == 4 || offset == 0x10 || offset == 0x14) {
        unsigned first = offset < 8 ? 0 : 4;
        uint32_t padding = value & ~0xfffU;
        uint32_t bits = value & 0xfff;
        uint32_t state = s->configuration[first];

        /*
         * 1020a50c/1020a518 expose paired set/clear commands. 10239740
         * submits a complemented unit mask, whose upper bits are padding.
         * Keep a finite mask/complement grammar, not arbitrary upper bits.
         */
        if (padding && padding != ~0xfffU) {
            return nikon_capture_error(s, offset, size, true, value);
        }
        if (offset == 0 || offset == 0x10) {
            state |= bits;
        } else {
            state &= ~bits;
        }
        /* Functional readback mirrors effective state through both aliases. */
        s->configuration[first] = s->configuration[first + 1] = state;
        s->known[first] = s->known[first + 1] = true;
        if (offset == 0 || offset == 0x14) {
            nikon_capture_cancel(s, bits);
            nikon_capture_run_state(s);
            nikon_capture_shared(s);
        }
        return MEMTX_OK;
    }
    if (!nikon_capture_config(offset, &mask) || (value & ~mask)) {
        return nikon_capture_error(s, offset, size, true, value);
    }
    s->configuration[offset / 4] = value;
    s->known[offset / 4] = true;
    if (offset == 0x100 || offset == 0x104) {
        unsigned command = (offset - 0x100) / 4;

        /* Synchronous explicit request only. A provider can inspect known
         * configuration through the public API; no provider is connected by
         * default and no status is generated from this pulse.
         */
        for (i = 0; i < 32; i++) {
            qemu_set_irq(s->command_data[command * 32 + i],
                         !!(value & (1U << i)));
        }
        qemu_set_irq(s->command_publish[command], 1);
        qemu_set_irq(s->command_publish[command], 0);
    }
    nikon_capture_irqs(s);
    return MEMTX_OK;
}

static const MemoryRegionOps nikon_capture_ops = {
    .read_with_attrs = nikon_capture_read,
    .write_with_attrs = nikon_capture_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 4, .max_access_size = 4,
               .unaligned = false },
};

static MemTxResult nikon_capture_packet_read(void *opaque, hwaddr offset,
                                            uint64_t *value, unsigned size,
                                            MemTxAttrs attrs)
{
    return nikon_capture_read(opaque, CAPTURE_PACKET_BASE + offset,
                              value, size, attrs);
}

static MemTxResult nikon_capture_packet_write(void *opaque, hwaddr offset,
                                             uint64_t value, unsigned size,
                                             MemTxAttrs attrs)
{
    return nikon_capture_write(opaque, CAPTURE_PACKET_BASE + offset,
                               value, size, attrs);
}

static const MemoryRegionOps nikon_capture_packet_ops = {
    .read_with_attrs = nikon_capture_packet_read,
    .write_with_attrs = nikon_capture_packet_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 4, .max_access_size = 4,
               .unaligned = false },
};

static void nikon_capture_reset(DeviceState *dev)
{
    NikonCaptureControl *s = NIKON_CAPTURE_CONTROL(dev);
    unsigned i;
    uint32_t mask;

    memset(s->configuration, 0, sizeof(s->configuration));
    memset(s->known, 0, sizeof(s->known));
    for (i = 0; i < CAPTURE_WORDS; i++) {
        s->known[i] = nikon_capture_config(i * 4, &mask);
    }
    memset(s->pending, 0, sizeof(s->pending));
    memset(s->levels, 0, sizeof(s->levels));
    memset(s->seen, 0, sizeof(s->seen));
    s->event_levels = 0;
    s->reported_error = false;
    nikon_capture_cancel(s, 0xeff);
    nikon_capture_run_state(s);
    for (i = 0; i < ARRAY_SIZE(s->command_data); i++) {
        qemu_set_irq(s->command_data[i], 0);
    }
    for (i = 0; i < ARRAY_SIZE(s->command_publish); i++) {
        qemu_set_irq(s->command_publish[i], 0);
    }
    nikon_capture_irqs(s);
}

static void nikon_capture_init(Object *obj)
{
    NikonCaptureControl *s = NIKON_CAPTURE_CONTROL(obj);
    unsigned i;

    for (i = 0; i < ARRAY_SIZE(s->stop); i++) {
        s->stop[i].s = s;
        s->stop[i].index = i;
        s->stop[i].timer = timer_new_ns(QEMU_CLOCK_VIRTUAL,
                                       nikon_capture_stopped, &s->stop[i]);
    }
    memory_region_init_io(&s->memory, obj, &nikon_capture_ops, s,
                          "nikon-capture-control", CAPTURE_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(s), &s->memory);
    memory_region_init_io(&s->packet_memory, obj, &nikon_capture_packet_ops, s,
                          "nikon-capture-packet", CAPTURE_PACKET_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(s), &s->packet_memory);
    for (i = 0; i < NIKON_CAPTURE_EVENTS; i++) {
        sysbus_init_irq(SYS_BUS_DEVICE(s), &s->irq[i]);
    }
    qdev_init_gpio_in_named(DEVICE(s), nikon_capture_data, "status-data",
                            CAPTURE_STATUS_WORDS * 32);
    qdev_init_gpio_in_named(DEVICE(s), nikon_capture_event, "status-publish",
                            NIKON_CAPTURE_EVENTS);
    qdev_init_gpio_out_named(DEVICE(s), s->command_data, "command-data", 64);
    qdev_init_gpio_out_named(DEVICE(s), s->command_publish, "command-publish", 2);
    qdev_init_gpio_out_named(DEVICE(s), s->run_state, "run-state", 12);
}

static void nikon_capture_finalize(Object *obj)
{
    NikonCaptureControl *s = NIKON_CAPTURE_CONTROL(obj);

    for (unsigned i = 0; i < ARRAY_SIZE(s->stop); i++) {
        timer_free(s->stop[i].timer);
    }
}

static const VMStateDescription nikon_capture_vmstate = {
    .name = "nikon-capture-control",
    .unmigratable = true,
};

static void nikon_capture_class_init(ObjectClass *oc, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->vmsd = &nikon_capture_vmstate;
    dc->unrealize = nikon_capture_reset;
    device_class_set_legacy_reset(dc, nikon_capture_reset);
}

static const TypeInfo nikon_capture_type = {
    .name = TYPE_NIKON_CAPTURE_CONTROL,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(NikonCaptureControl),
    .instance_init = nikon_capture_init,
    .instance_finalize = nikon_capture_finalize,
    .class_init = nikon_capture_class_init,
};

/* Buffer selectors1020c334..1020c366 and readout packet1020c788..1020c7c6.
 * Geometry and address words are retained without guessing pixel semantics.
 */
#define READOUT_WORDS (0x404 / 4)
OBJECT_DECLARE_SIMPLE_TYPE(NikonReadoutControl, NIKON_READOUT_CONTROL)

typedef struct NikonReadoutBank {
    MemoryRegion memory;
    NikonReadoutControl *s;
    unsigned region;
} NikonReadoutBank;

struct NikonReadoutControl {
    SysBusDevice parent_obj;
    NikonReadoutBank bank[NIKON_READOUT_REGIONS];
    uint32_t configuration[NIKON_READOUT_REGIONS][READOUT_WORDS];
    uint32_t committed[READOUT_WORDS];
    uint32_t submitted[READOUT_WORDS];
    bool commit_selected[2];
    bool committed_valid;
    bool running;
    bool request_valid;
    bool provider_active;
    uint8_t fields_written;
    uint8_t committed_fields;
    uint8_t submitted_fields;
    bool known[NIKON_READOUT_REGIONS][READOUT_WORDS];
    uint32_t observed[11];
    bool observed_known[11];
    qemu_irq request;
    qemu_irq ready;
    qemu_irq completion;
    qemu_irq cancel;
    uint64_t requests;
    uint64_t generation;
    uint64_t ready_generation;
    uint64_t completion_generation;
    uint64_t provider_errors;
    bool reported_error;
};

/* Complete finite transport profile, including an explicitly written mode0. */
static const unsigned nikon_readout_fields[] = {
    0x394, 0x3c0, 0x3e8, 0x3ec, 0x3f0, 0x3f4, 0x3f8, 0x3fc,
};

/* Exact data words consumed by102092fa. Selector2 shares69870 with0. */
static const unsigned nikon_readout_observed[] = {
    0x70, 0x74, 0x78, 0x7c, 0x178, 0x17c,
    0x274, 0x278, 0x27c, 0x378, 0x37c,
};
static const unsigned nikon_readout_queries[4][4] = {
    { 0, 1, 2, 3 }, { 4, 5 }, { 0, 6, 7, 8 }, { 9, 10 },
};
static const unsigned nikon_readout_query_words[] = { 4, 2, 4, 2 };

static bool nikon_readout_config(unsigned region, unsigned offset,
                                 uint32_t *mask)
{
    *mask = UINT32_MAX;
    if (region == NIKON_READOUT_BUFFERS) {
        if ((offset >= 0x50 && offset <= 0x6c) ||
            (offset >= 0x150 && offset <= 0x16c) ||
            (offset >= 0x250 && offset <= 0x26c) ||
            (offset >= 0x350 && offset <= 0x36c)) {
            return true;
        }
        switch (offset) {
        case 0x14: case 0x40: case 0x214: case 0x240: case 0x280: case 0x380:
            return true;
        case 8: case 0x108: case 0x208: case 0x308:
            *mask = 0xffff;
            return true;
        case 0x18: case 0x44: case 0x48: case 0x4c:
        case 0x218: case 0x224: case 0x244: case 0x248: case 0x24c:
            *mask = 0xff;
            return true;
        case 0x3c: case 0x23c:
            *mask = 0x1ff;
            return true;
        default:
            return false;
        }
    }
    if (region != NIKON_READOUT_PARAMETERS) {
        return false;
    }
    if (offset <= 0x200) {
        *mask = 0xffff;
        return true;
    }
    if (offset >= 0x310 && offset <= 0x3fc) {
        switch (offset) {
        case 0x324: case 0x360: case 0x3fc:
            *mask = 0x00ff00ff;
            break;
        case 0x328: case 0x338: case 0x33c: case 0x340:
        case 0x354: case 0x368: case 0x3a4: case 0x3c0:
            *mask = 0xff;
            break;
        case 0x334: case 0x34c: case 0x370: case 0x384: case 0x3dc:
            *mask = 0xffff;
            break;
        case 0x350: case 0x358: case 0x35c: case 0x36c: case 0x3b8:
            *mask = 0xffffff;
            break;
        case 0x364:
            *mask = 0xffff00ff;
            break;
        case 0x388: case 0x38c: case 0x390:
            *mask = 0x03ff03ff;
            break;
        }
        return true;
    }
    if (offset == 0x400) {
        *mask = 1;
        return true;
    }
    return false;
}

bool nikon_readout_configuration(DeviceState *dev, enum NikonReadoutRegion region,
                                 unsigned offset, uint32_t *value)
{
    NikonReadoutControl *s = NIKON_READOUT_CONTROL(dev);
    uint32_t mask;

    if (!value || (unsigned)region >= NIKON_READOUT_REGIONS ||
        offset >= READOUT_WORDS * 4 || (offset & 3) ||
        !nikon_readout_config(region, offset, &mask) ||
        !s->known[region][offset / 4]) {
        return false;
    }
    *value = s->configuration[region][offset / 4];
    return true;
}

bool nikon_readout_request(DeviceState *dev, NikonReadoutRequest *request)
{
    NikonReadoutControl *s = NIKON_READOUT_CONTROL(dev);

    if (!request || !s->request_valid) {
        return false;
    }
    request->generation = s->generation;
    request->ready = s->submitted[0x3c0 / 4];
    request->completion_marker =
        s->submitted[0x400 / 4];
    return true;
}

bool nikon_readout_c170_mapping(DeviceState *dev, uint64_t generation,
                                NikonReadoutC170Mapping *mapping,
                                Error **errp)
{
    NikonReadoutControl *s = NIKON_READOUT_CONTROL(dev);
    uint32_t destination, stride, span, secondary, secondary_stride, format;
    unsigned mode;

    if (!mapping || !s->request_valid || !s->running ||
        !generation || generation != s->generation) {
        error_setg(errp, "C1.70 readout request generation is stale");
        return false;
    }
    destination = s->submitted[0x3e8 / 4];
    stride = s->submitted[0x3ec / 4];
    span = s->submitted[0x3f0 / 4];
    secondary = s->submitted[0x3f4 / 4];
    secondary_stride =
        s->submitted[0x3f8 / 4];
    format = s->submitted[0x3fc / 4];

    /*
     * 1020a2ae packs B26B489E into394[23:16]. 102391b2 consumes
     * raw0.bit3 in mode0 and bit5 in mode1. Their use after actual transport
     * completion is an explicit functional profile, not a physical claim.
     */
    mode = (s->submitted[0x394 / 4] >> 16) & 0xff;

    if (s->submitted_fields != 0xff ||
        s->submitted[0x3c0 / 4] != 1 || mode > 1) {
        error_setg(errp, "C1.70 functional readout enable/mode is unsupported");
        return false;
    }

    if (format != NIKON_READOUT_C170_FORMAT_PRIMARY &&
        format != NIKON_READOUT_C170_FORMAT_ALTERNATE) {
        error_setg(errp, "C1.70 functional readout format 0x%08x is unsupported",
                   format);
        return false;
    }
    if (!destination || !secondary || stride < 256 || (stride & 0xff) ||
        (stride & 1) || !span || span > 64U * 1024 * 1024 || span % stride ||
        secondary_stride < 256 || (secondary_stride & 0xff) ||
        destination > UINT32_MAX - (span - 1) ||
        (uint64_t)secondary_stride * (span / stride) > UINT32_MAX ||
        secondary > UINT32_MAX -
                    ((uint64_t)secondary_stride * (span / stride) - 1)) {
        error_setg(errp, "C1.70 functional readout geometry is invalid");
        return false;
    }
    mapping->generation = generation;
    mapping->destination = destination;
    mapping->stride = stride;
    mapping->span = span;
    mapping->secondary_destination = secondary;
    mapping->secondary_stride = secondary_stride;
    mapping->format = format;
    mapping->width = stride / 2;
    mapping->height = span / stride;
    mapping->completion_cause = mode ? 0x20 : 8;
    return true;
}

bool nikon_readout_publish_ready(DeviceState *dev, uint64_t generation)
{
    NikonReadoutControl *s = NIKON_READOUT_CONTROL(dev);

    if (!s->request_valid || !generation || generation != s->generation) {
        return false;
    }
    s->ready_generation = generation;
    s->provider_active = true;
    qemu_set_irq(s->ready, 1);
    qemu_set_irq(s->ready, 0);
    return true;
}

bool nikon_readout_publish_completion(DeviceState *dev, uint64_t generation)
{
    NikonReadoutControl *s = NIKON_READOUT_CONTROL(dev);

    if (!s->request_valid || !generation || generation != s->generation ||
        s->ready_generation != generation) {
        return false;
    }
    s->completion_generation = generation;
    s->provider_active = false;
    /*
     * The bundle pipeline joins this observation to capture IRQ94 only
     * after actual CMOS transport. This interface itself has no guest IRQ.
     */
    qemu_set_irq(s->completion, 1);
    qemu_set_irq(s->completion, 0);
    return true;
}

void nikon_readout_release(DeviceState *dev, uint64_t generation)
{
    NikonReadoutControl *s = NIKON_READOUT_CONTROL(dev);

    if (generation == s->generation) {
        s->provider_active = false;
        s->request_valid = false;
        s->ready_generation = 0;
        s->completion_generation = 0;
    }
}

static void nikon_readout_provider_ready(void *opaque, int n, int level)
{
    NikonReadoutControl *s = opaque;
    NikonReadoutRequest request;

    if (level && (!nikon_readout_request(DEVICE(s), &request) ||
                  !nikon_readout_publish_ready(DEVICE(s), request.generation))) {
        s->provider_errors++;
    }
}

static void nikon_readout_provider_completion(void *opaque, int n, int level)
{
    NikonReadoutControl *s = opaque;
    NikonReadoutRequest request;

    if (level && (!nikon_readout_request(DEVICE(s), &request) ||
                  !nikon_readout_publish_completion(DEVICE(s),
                                                     request.generation))) {
        s->provider_errors++;
    }
}

bool nikon_readout_publish(DeviceState *dev, unsigned selector,
                           const uint32_t *words, size_t count, Error **errp)
{
    NikonReadoutControl *s = NIKON_READOUT_CONTROL(dev);
    unsigned i;

    if (selector >= ARRAY_SIZE(nikon_readout_queries) || !words ||
        count != nikon_readout_query_words[selector]) {
        error_setg(errp, "readout observation needs its exact complete query");
        return false;
    }
    for (i = 0; i < count; i++) {
        unsigned word = nikon_readout_queries[selector][i];

        s->observed[word] = words[i];
        s->observed_known[word] = true;
    }
    return true;
}

static MemTxResult nikon_readout_error(NikonReadoutBank *bank, hwaddr offset,
                                      unsigned size, bool write, uint64_t value)
{
    nikon_access_error(&bank->s->reported_error, bank->memory.addr + offset,
                      size, write, value,
                      "readout-control-access-or-external-data-not-supported");
    return MEMTX_ERROR;
}

static MemTxResult nikon_readout_read(void *opaque, hwaddr offset,
                                     uint64_t *value, unsigned size,
                                     MemTxAttrs attrs)
{
    NikonReadoutBank *bank = opaque;
    NikonReadoutControl *s = bank->s;
    uint32_t word;
    unsigned i;

    *value = 0;
    if (size != 4 || (offset & 3)) {
        return nikon_readout_error(bank, offset, size, false, 0);
    }
    if (bank->region == NIKON_READOUT_BUFFERS) {
        for (i = 0; i < ARRAY_SIZE(nikon_readout_observed); i++) {
            if (offset == nikon_readout_observed[i] && s->observed_known[i]) {
                *value = s->observed[i];
                return MEMTX_OK;
            }
        }
    }
    if (nikon_readout_configuration(DEVICE(s), bank->region, offset, &word)) {
        *value = word;
        return MEMTX_OK;
    }
    return nikon_readout_error(bank, offset, size, false, 0);
}

static void nikon_readout_begin(NikonReadoutControl *s, const uint32_t *record,
                                uint8_t fields)
{
    s->requests++;
    /*
     * A busy provider rejects another pulse without superseding its live
     * descriptor. Staging remains available for a subsequent guest command.
     */
    if (s->provider_active) {
        qemu_set_irq(s->request, 1);
        qemu_set_irq(s->request, 0);
        return;
    }
    memcpy(s->submitted, record, sizeof(s->submitted));
    s->submitted_fields = fields;
    s->generation++;
    if (!s->generation) {
        s->generation++;
    }
    s->request_valid = true;
    s->ready_generation = s->submitted[0x3c0 / 4] == 1 ? s->generation : 0;
    s->completion_generation = 0;
    qemu_set_irq(s->request, 1);
    qemu_set_irq(s->request, 0);
}

static void nikon_readout_commit_selected(void *opaque, int n, int level)
{
    NikonReadoutControl *s = opaque;

    s->commit_selected[n] = !!level;
}

static void nikon_readout_commit(void *opaque, int n, int level)
{
    NikonReadoutControl *s = opaque;

    if (level && s->commit_selected[n]) {
        memcpy(s->committed, s->configuration[NIKON_READOUT_PARAMETERS],
               sizeof(s->committed));
        s->committed_valid = true;
        s->committed_fields = s->fields_written;
    }
}

static void nikon_readout_run(void *opaque, int n, int level)
{
    NikonReadoutControl *s = opaque;

    if (s->running == !!level) {
        return;
    }
    s->running = !!level;
    if (level) {
        if (s->committed_valid) {
            nikon_readout_begin(s, s->committed, s->committed_fields);
        }
    } else {
        s->request_valid = false;
        s->provider_active = false;
        s->ready_generation = 0;
        s->completion_generation = 0;
        s->committed_valid = false;
        qemu_set_irq(s->cancel, 1);
        qemu_set_irq(s->cancel, 0);
    }
}

static MemTxResult nikon_readout_write(void *opaque, hwaddr offset,
                                      uint64_t value, unsigned size,
                                      MemTxAttrs attrs)
{
    NikonReadoutBank *bank = opaque;
    NikonReadoutControl *s = bank->s;
    uint32_t mask;

    if (size != 4 || (offset & 3) ||
        !nikon_readout_config(bank->region, offset, &mask) || (value & ~mask)) {
        return nikon_readout_error(bank, offset, size, true, value);
    }
    s->configuration[bank->region][offset / 4] = value;
    s->known[bank->region][offset / 4] = true;
    if (bank->region == NIKON_READOUT_PARAMETERS) {
        for (unsigned i = 0; i < ARRAY_SIZE(nikon_readout_fields); i++) {
            if (offset == nikon_readout_fields[i]) {
                s->fields_written |= 1u << i;
                break;
            }
        }
    }
    if (bank->region == NIKON_READOUT_PARAMETERS && offset == 0x400 && value) {
        nikon_readout_begin(s, s->configuration[NIKON_READOUT_PARAMETERS],
                           s->fields_written);
    }
    return MEMTX_OK;
}

static const MemoryRegionOps nikon_readout_ops = {
    .read_with_attrs = nikon_readout_read,
    .write_with_attrs = nikon_readout_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 4, .max_access_size = 4,
               .unaligned = false },
};

static void nikon_readout_reset(DeviceState *dev)
{
    NikonReadoutControl *s = NIKON_READOUT_CONTROL(dev);
    unsigned region, word;
    uint32_t mask;

    memset(s->configuration, 0, sizeof(s->configuration));
    memset(s->committed, 0, sizeof(s->committed));
    memset(s->submitted, 0, sizeof(s->submitted));
    memset(s->commit_selected, 0, sizeof(s->commit_selected));
    s->committed_valid = false;
    s->running = false;
    s->request_valid = false;
    s->provider_active = false;
    s->fields_written = 0;
    s->committed_fields = 0;
    s->submitted_fields = 0;
    qemu_set_irq(s->cancel, 1);
    qemu_set_irq(s->cancel, 0);
    memset(s->known, 0, sizeof(s->known));
    memset(s->observed, 0, sizeof(s->observed));
    memset(s->observed_known, 0, sizeof(s->observed_known));
    for (region = 0; region < NIKON_READOUT_REGIONS; region++) {
        for (word = 0; word < READOUT_WORDS; word++) {
            s->known[region][word] =
                nikon_readout_config(region, word * 4, &mask);
        }
    }
    s->requests = 0;
    s->generation = 0;
    s->ready_generation = 0;
    s->completion_generation = 0;
    s->provider_errors = 0;
    s->reported_error = false;
    qemu_set_irq(s->request, 0);
    qemu_set_irq(s->ready, 0);
    qemu_set_irq(s->completion, 0);
}

static void nikon_readout_init(Object *obj)
{
    NikonReadoutControl *s = NIKON_READOUT_CONTROL(obj);
    static const unsigned sizes[] = { 0x384, 0x404 };
    static const char * const names[] = {
        "nikon-readout-buffers", "nikon-readout-parameters",
    };
    unsigned i;

    for (i = 0; i < NIKON_READOUT_REGIONS; i++) {
        s->bank[i].s = s;
        s->bank[i].region = i;
        memory_region_init_io(&s->bank[i].memory, obj, &nikon_readout_ops,
                              &s->bank[i], names[i], sizes[i]);
        sysbus_init_mmio(SYS_BUS_DEVICE(s), &s->bank[i].memory);
    }
    qdev_init_gpio_out_named(DEVICE(s), &s->request, "readout-request", 1);
    qdev_init_gpio_out_named(DEVICE(s), &s->cancel, "readout-cancel", 1);
    qdev_init_gpio_in_named(DEVICE(s), nikon_readout_commit_selected,
                            "commit-selected", 2);
    qdev_init_gpio_in_named(DEVICE(s), nikon_readout_commit, "commit", 2);
    qdev_init_gpio_in_named(DEVICE(s), nikon_readout_run, "run", 1);
    qdev_init_gpio_out_named(DEVICE(s), &s->ready, "readout-ready", 1);
    qdev_init_gpio_out_named(DEVICE(s), &s->completion,
                             "readout-completion", 1);
    qdev_init_gpio_in_named(DEVICE(s), nikon_readout_provider_ready,
                            "provider-ready", 1);
    qdev_init_gpio_in_named(DEVICE(s), nikon_readout_provider_completion,
                            "provider-completion", 1);
    object_property_add_uint64_ptr(obj, "requests", &s->requests,
                                   OBJ_PROP_FLAG_READ);
    object_property_add_uint64_ptr(obj, "request-generation", &s->generation,
                                   OBJ_PROP_FLAG_READ);
    object_property_add_uint64_ptr(obj, "ready-generation",
                                   &s->ready_generation, OBJ_PROP_FLAG_READ);
    object_property_add_uint64_ptr(obj, "completion-generation",
                                   &s->completion_generation,
                                   OBJ_PROP_FLAG_READ);
    object_property_add_uint64_ptr(obj, "provider-errors", &s->provider_errors,
                                   OBJ_PROP_FLAG_READ);
}

static const VMStateDescription nikon_readout_vmstate = {
    .name = "nikon-readout-control",
    .unmigratable = true,
};

static void nikon_readout_class_init(ObjectClass *oc, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->vmsd = &nikon_readout_vmstate;
    device_class_set_legacy_reset(dc, nikon_readout_reset);
}

static const TypeInfo nikon_readout_type = {
    .name = TYPE_NIKON_READOUT_CONTROL,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(NikonReadoutControl),
    .instance_init = nikon_readout_init,
    .class_init = nikon_readout_class_init,
};

/* The original selector table at VA1078a92c is independent of the SDES
 * parameter/status banks. These stores do not identify a pixel transfer.
 */
OBJECT_DECLARE_SIMPLE_TYPE(NikonSdesChannels, NIKON_SDES_CHANNELS)

typedef struct NikonSdesChannel {
    hwaddr configuration, control, command, auxiliary;
} NikonSdesChannel;

static const NikonSdesChannel nikon_sdes_channels[] = {
    { 0x000, 0x01c, 0x020, 0x024 },
    { 0x040, 0x05c, 0x060, 0x064 },
    { 0x080, 0x0ac, 0x0b4, 0x0b8 },
    { 0x0d0, 0x0ec, 0x0f0, 0x0f4 },
    { 0x110, 0x12c, 0x130, 0x134 },
    { 0x150, 0x16c, 0x170, 0x174 },
    { 0x190, 0x1ac, 0x1b0, 0x1b4 },
    { 0x1d0, 0x1ec, 0x1f0, 0x1f4 },
    { 0x210, 0x22c, 0x230, 0x234 },
    { 0x250, 0x26c, 0x270, 0x274 },
    { 0x290, 0x2ac, 0x2b0, 0x2b4 },
};

struct NikonSdesChannels {
    SysBusDevice parent_obj;
    MemoryRegion memory;
    uint32_t values[NIKON_SDES_CHANNEL_SIZE / 4];
    qemu_irq command[NIKON_SDES_CHANNEL_COUNT];
    qemu_irq global_command[2];
    uint64_t requests;
    uint64_t global_requests;
    bool reported_error;
};

static bool nikon_sdes_channel_field(hwaddr offset, unsigned *channel,
                                     uint32_t *mask, bool *command)
{
    unsigned i;

    *mask = UINT32_MAX;
    *command = false;
    for (i = 0; i < ARRAY_SIZE(nikon_sdes_channels); i++) {
        const NikonSdesChannel *c = &nikon_sdes_channels[i];
        unsigned words = i == 2 ? 11 : 7;

        if (offset >= c->configuration &&
            offset < c->configuration + words * 4) {
            if (offset - c->configuration == (i == 2 ? 0x18 : 8)) {
                *mask = 0xffff;
            }
        } else if (offset == c->control) {
            if (i == 2) {
                *mask = 0xff;
            }
        } else if (i == 2 && offset == c->control + 4) {
            /* Selector4 has a byte control followed by the packed word. */
        } else if (offset == c->command) {
            *mask = 1;
            *command = true;
        } else if (offset != c->auxiliary) {
            continue;
        }
        *channel = i;
        return true;
    }
    /* The same literal base also has two global RMW/commit pairs and
     * independent parameter stores. None is a result/status observation.
     */
    *channel = NIKON_SDES_CHANNEL_COUNT;
    switch (offset) {
    case 0x2d0:
    case 0x2e0:
    case 0x2f4:
    case 0x2f8:
        return true;
    case 0x2ec:
        *mask = 0xfff;
        return true;
    case 0x2d4:
    case 0x2e4:
        *channel += offset == 0x2e4;
        *mask = 1;
        *command = true;
        return true;
    default:
        return false;
    }
}

bool nikon_sdes_channels_configuration(DeviceState *dev, unsigned channel,
                                       unsigned offset, uint32_t *value)
{
    NikonSdesChannels *s = NIKON_SDES_CHANNELS(dev);
    unsigned actual;
    uint32_t mask;
    bool command;
    hwaddr address;

    if (channel >= NIKON_SDES_CHANNEL_COUNT || !value || (offset & 3)) {
        return false;
    }
    address = nikon_sdes_channels[channel].configuration + (hwaddr)offset;
    if (!nikon_sdes_channel_field(address, &actual, &mask, &command) ||
        actual != channel) {
        return false;
    }
    *value = s->values[address / 4];
    return true;
}

bool nikon_sdes_channels_global_configuration(DeviceState *dev, unsigned offset,
                                              uint32_t *value)
{
    NikonSdesChannels *s = NIKON_SDES_CHANNELS(dev);
    unsigned channel;
    uint32_t mask;
    bool command;

    if (!value || (offset & 3) ||
        !nikon_sdes_channel_field(offset, &channel, &mask, &command) ||
        channel < NIKON_SDES_CHANNEL_COUNT) {
        return false;
    }
    *value = s->values[offset / 4];
    return true;
}

static MemTxResult nikon_sdes_channels_error(NikonSdesChannels *s,
                                            hwaddr offset, unsigned size,
                                            bool write, uint64_t value)
{
    nikon_access_error(&s->reported_error, s->memory.addr + offset,
                      size, write, value, "sdes-channel-field-not-supported");
    return MEMTX_ERROR;
}

static MemTxResult nikon_sdes_channels_read(void *opaque, hwaddr offset,
                                           uint64_t *value, unsigned size,
                                           MemTxAttrs attrs)
{
    NikonSdesChannels *s = opaque;
    unsigned channel;
    uint32_t mask;
    bool command;

    *value = 0;
    if (size != 4 || (offset & 3) ||
        !nikon_sdes_channel_field(offset, &channel, &mask, &command)) {
        return nikon_sdes_channels_error(s, offset, size, false, 0);
    }
    *value = s->values[offset / 4];
    return MEMTX_OK;
}

static MemTxResult nikon_sdes_channels_write(void *opaque, hwaddr offset,
                                            uint64_t value, unsigned size,
                                            MemTxAttrs attrs)
{
    NikonSdesChannels *s = opaque;
    unsigned channel;
    uint32_t mask;
    bool command;

    if (size != 4 || (offset & 3) ||
        !nikon_sdes_channel_field(offset, &channel, &mask, &command) ||
        (value & ~mask) || (command && value != 1)) {
        return nikon_sdes_channels_error(s, offset, size, true, value);
    }
    s->values[offset / 4] = value;
    if (command) {
        if (channel < NIKON_SDES_CHANNEL_COUNT) {
            s->requests++;
            qemu_irq_pulse(s->command[channel]);
        } else {
            s->global_requests++;
            qemu_irq_pulse(s->global_command[
                           channel - NIKON_SDES_CHANNEL_COUNT]);
        }
    }
    return MEMTX_OK;
}

static const MemoryRegionOps nikon_sdes_channels_ops = {
    .read_with_attrs = nikon_sdes_channels_read,
    .write_with_attrs = nikon_sdes_channels_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 4, .max_access_size = 4,
               .unaligned = false },
};

static void nikon_sdes_channels_reset(DeviceState *dev)
{
    NikonSdesChannels *s = NIKON_SDES_CHANNELS(dev);
    unsigned i;

    /* Functional cold-zero applies only to fields admitted by the decoder. */
    memset(s->values, 0, sizeof(s->values));
    s->requests = 0;
    s->global_requests = 0;
    s->reported_error = false;
    for (i = 0; i < NIKON_SDES_CHANNEL_COUNT; i++) {
        qemu_set_irq(s->command[i], 0);
    }
    for (i = 0; i < ARRAY_SIZE(s->global_command); i++) {
        qemu_set_irq(s->global_command[i], 0);
    }
}

static void nikon_sdes_channels_init(Object *obj)
{
    NikonSdesChannels *s = NIKON_SDES_CHANNELS(obj);

    memory_region_init_io(&s->memory, obj, &nikon_sdes_channels_ops, s,
                          "nikon-sdes-channels", NIKON_SDES_CHANNEL_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(s), &s->memory);
    qdev_init_gpio_out_named(DEVICE(s), s->command, "command-publish",
                             NIKON_SDES_CHANNEL_COUNT);
    qdev_init_gpio_out_named(DEVICE(s), s->global_command,
                            "global-command-publish",
                            ARRAY_SIZE(s->global_command));
    object_property_add_uint64_ptr(obj, "requests", &s->requests,
                                   OBJ_PROP_FLAG_READ);
    object_property_add_uint64_ptr(obj, "global-requests", &s->global_requests,
                                   OBJ_PROP_FLAG_READ);
}

static const VMStateDescription nikon_sdes_channels_vmstate = {
    .name = TYPE_NIKON_SDES_CHANNELS,
    .unmigratable = true,
};

static void nikon_sdes_channels_class_init(ObjectClass *oc, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->vmsd = &nikon_sdes_channels_vmstate;
    device_class_set_legacy_reset(dc, nikon_sdes_channels_reset);
}

static const TypeInfo nikon_sdes_channels_type = {
    .name = TYPE_NIKON_SDES_CHANNELS,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(NikonSdesChannels),
    .instance_init = nikon_sdes_channels_init,
    .class_init = nikon_sdes_channels_class_init,
};

static void nikon_sensor_register_types(void)
{
    type_register_static(&nikon_sensor_type);
    type_register_static(&nikon_sdes_type);
    type_register_static(&nikon_capture_type);
    type_register_static(&nikon_readout_type);
    type_register_static(&nikon_sdes_channels_type);
}
type_init(nikon_sensor_register_types)
