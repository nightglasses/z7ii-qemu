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

    memset(s->configuration, 0, sizeof(s->configuration));
    memset(s->known, 0, sizeof(s->known));
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

static void nikon_sensor_register_types(void)
{
    type_register_static(&nikon_sensor_type);
    type_register_static(&nikon_sdes_type);
}
type_init(nikon_sensor_register_types)
