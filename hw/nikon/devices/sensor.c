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
#include "system/address-spaces.h"
#include "system/dma.h"
#include "system/reset.h"
#include "hw/nikon/sensor.h"

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

static void nikon_sensor_register_types(void)
{
    type_register_static(&nikon_sensor_type);
}
type_init(nikon_sensor_register_types)
