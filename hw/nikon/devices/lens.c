/*
 * Finite, explicitly virtual normal-family fixed lens.
 * Contract and assumptions: docs/devices/lens/protocol.md.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu/bswap.h"
#include "qemu/log.h"
#include "qemu/timer.h"
#include "hw/core/irq.h"
#include "hw/core/qdev-properties.h"
#include "hw/ssi/ssi.h"
#include "hw/nikon/devices.h"
#include "hw/nikon/peer.h"
#include "migration/vmstate.h"

#define LENS_MAX_PACKET 7426
#define LENS_PHASE_NS 1000000
#define LENS_CONFIG_NS 2000000
#define LENS_MODE_NS 2000000
#define LENS_STREAM_NS 1000000
#define LENS_WORD_NS 4000
OBJECT_DECLARE_SIMPLE_TYPE(NikonLensState, NIKON_LENS)

typedef enum LensPhase {
    LENS_HEADER,
    LENS_PAYLOAD,
    LENS_ERROR,
} LensPhase;

struct NikonLensState {
    SSIPeripheral parent_obj;
    DeviceState *gpio, *stream;
    uint32_t notify_pin;
    qemu_irq notify;
    QEMUTimer *phase_timer, *operation_timer, *stream_timer, *mode_timer;
    uint8_t header[8], input[LENS_MAX_PACKET], output[LENS_MAX_PACKET];
    uint8_t configuration[12], history[42];
    unsigned offset, size;
    LensPhase phase;
    uint32_t sync_anchor;
    uint8_t probe;
    unsigned history_offset;
    uint8_t history_sequence;
    int64_t history_start, stream_deadline;
    uint64_t completed, rejected, configuration_generation;
    uint64_t stream_frames;
    uint64_t aperture_noops;
    uint64_t mode_generation;
    uint8_t mode, requested_mode;
    uint64_t header_counts[256], packet_counts[256];
    bool configured, pending, stream_enabled, notify_level, reported;
    bool mode_pending;
};

static void lens_notify(NikonLensState *s, bool level)
{
    s->notify_level = level;
    qemu_set_irq(s->notify, level);
    if (s->gpio) {
        qemu_set_irq(qdev_get_gpio_in_named(s->gpio, "external-level",
                                           s->notify_pin), level);
    }
}

static bool lens_checksum(const uint8_t *bytes, unsigned size)
{
    unsigned sum = 0;

    for (unsigned i = 0; i < size; i++) {
        sum += bytes[i];
    }
    return !(sum & 0xff);
}

static void lens_finish_packet(uint8_t *bytes, unsigned size)
{
    unsigned sum = 0;

    bytes[0] = 0x55;
    for (unsigned i = 0; i < size - 1; i++) {
        sum += bytes[i];
    }
    bytes[size - 1] = -sum;
}

static void lens_error(NikonLensState *s, const char *reason)
{
    s->phase = LENS_ERROR;
    s->rejected++;
    timer_del(s->phase_timer);
    /* A rejected packet has no successful phase-completion edge. */
    lens_notify(s, true);
    if (!s->reported) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "nikon-lens: %s command=%02x:%02x mode=%02x size=%u\n",
                      reason, s->header[3], s->header[4],
                      s->header[2], s->size);
        s->reported = true;
    }
}

static unsigned lens_packet_size(uint8_t command, uint8_t subcommand)
{
    if (command == 0x24) {
        return subcommand == 0 ? 7426 : subcommand == 0x10 ? 3468 : 0;
    }
    if (command == 0x20) {
        return subcommand == 0 ? 258 : subcommand == 1 ? 24 : 0;
    }
    if (command == 0xa0) {
        return subcommand < 8 ? 24 : 0;
    }
    if (command == 0xa3) {
        return subcommand < 2 ? 4 : 0;
    }
    if (subcommand) {
        return 0;
    }
    switch (command) {
    case 0x10: return 28;
    case 0x11: return 74;
    case 0x12: return 66;
    case 0x13: return 14;
    case 0x15: return 6;
    case 0x16: return 4;
    case 0x1f: return 4;
    case 0x21: return 6;
    case 0x27: return 6;
    case 0x50:
    case 0x51: return 4;
    case 0xa1: return 10;
    default: return 0;
    }
}

static void lens_response(NikonLensState *s)
{
    uint8_t *out = s->output;

    /* Explicit virtual inputs; unnamed zero coordinates are documented. */
    memset(out, 0, s->size);
    switch (s->header[3]) {
    case 0x10:
        stw_le_p(out + 1, 0x7ffe); /* Explicit virtual identity. */
        stl_le_p(out + 3, 1);
        out[12] = 3;
        break;
    case 0x12:
        /* Both focal endpoints are 50 mm at F2.8: Av=3, raw=384+192*3. */
        stw_le_p(out + 1, 960);
        stw_le_p(out + 3, 960);
        stw_le_p(out + 7, 50);
        stw_le_p(out + 11, 50);
        out[25] = 1;
        break;
    case 0x15:
        out[1] = s->pending ? 1 : 0;
        out[2] = s->configured ? 0 : 1;
        break;
    case 0x16:
        out[1] = 15;
        break;
    case 0x1f:
        out[2] = 1; /* Ordinary family, supported negotiation endpoint. */
        break;
    case 0x20:
        if (!s->header[4]) {
            static const unsigned aperture[] = { 1, 9, 15, 162, 164 };

            for (unsigned i = 0; i < ARRAY_SIZE(aperture); i++) {
                stw_le_p(out + aperture[i], 960);
            }
            /* All relative aperture ranges remain zero for the fixed iris. */
            out[169] = s->configured ? 0x40 : 0x20;
            out[203] = s->pending ? 8 : 0;
            /* Fixed optics remain usable; configuration busy is separate. */
            out[216] = 3;
        } else {
            stw_le_p(out + 1, 960);
            stw_le_p(out + 3, 960);
            stw_le_p(out + 7, 50);
        }
        break;
    case 0x27:
        stl_le_p(out + 1, s->sync_anchor);
        break;
    case 0x51:
        out[1] = s->mode_pending ? 0x80 : 0;
        break;
    case 0x24:
        /*
         * Zero higher-order radial coefficients retain r'=r. The other
         * zero sentinel selects the original Q15-unity response path.
         */
        if (!s->header[4]) {
            out[7385] = 1; /* Explicit 27-float representation. */
        }
        break;
    default:
        break;
    }
    lens_finish_packet(out, s->size);
}

static void lens_operation_done(void *opaque)
{
    NikonLensState *s = opaque;

    s->pending = false;
    s->configured = true;
    s->configuration_generation++;
}

static void lens_mode_done(void *opaque)
{
    NikonLensState *s = opaque;

    s->mode = s->requested_mode;
    s->mode_pending = false;
    s->mode_generation++;
}

static void lens_stream_word(void *opaque)
{
    NikonLensState *s = opaque;
    int64_t now = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);

    if (!s->stream_enabled) {
        return;
    }
    if (!s->history_offset) {
        memset(s->history, 0, sizeof(s->history));
        s->history[1] = s->history_sequence;
        /* Stationary virtual motor coordinate and status. */
        lens_finish_packet(s->history, sizeof(s->history));
        s->history_start = s->stream_deadline;
    }
    if (!nikon_spi_slave_clock(s->stream,
                               lduw_le_p(s->history + s->history_offset),
                               16, NULL)) {
        s->stream_deadline = now + LENS_STREAM_NS;
    } else {
        s->history_offset += 2;
        if (s->history_offset == sizeof(s->history)) {
            s->history_offset = 0;
            s->history_sequence++;
            s->stream_frames++;
            s->stream_deadline = MAX(now + LENS_WORD_NS,
                                     s->history_start + LENS_STREAM_NS);
        } else {
            s->stream_deadline += LENS_WORD_NS;
        }
    }
    timer_mod(s->stream_timer, s->stream_deadline);
}

static bool lens_process(NikonLensState *s)
{
    const uint8_t *in = s->input;

    if (in[0] != 0x55 || !lens_checksum(in, s->size)) {
        lens_error(s, "payload-checksum-not-supported");
        return false;
    }
    switch (s->header[3]) {
    case 0x1f:
        if ((in[1] != 1 && in[1] != 2) || in[2]) {
            lens_error(s, "probe-not-supported");
            return false;
        }
        s->probe = in[1];
        break;
    case 0x13:
        if ((in[1] & ~0x1f) || (in[4] & ~0x3f) || s->pending) {
            lens_error(s, "configuration-not-supported");
            return false;
        }
        memcpy(s->configuration, in + 1, sizeof(s->configuration));
        s->pending = true;
        timer_mod(s->operation_timer,
                  qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + LENS_CONFIG_NS);
        break;
    case 0x16:
        if (in[1] > 1 || (in[1] && !s->stream)) {
            lens_error(s, "stream-control-not-supported");
            return false;
        }
        if (s->stream_enabled == !!in[1]) {
            break;
        }
        s->stream_enabled = in[1];
        timer_del(s->stream_timer);
        if (s->stream_enabled) {
            s->stream_deadline = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) +
                (s->history_offset ? LENS_WORD_NS : LENS_STREAM_NS);
            timer_mod(s->stream_timer, s->stream_deadline);
        }
        break;
    case 0x21:
        s->sync_anchor = ldl_le_p(in + 1);
        break;
    case 0x50:
        /* 103130aa emits selectors0/1/2, independently of configuration. */
        if (in[1] > 2 || in[2] || s->mode_pending) {
            lens_error(s, "mode-request-not-supported");
            return false;
        }
        s->requested_mode = in[1];
        s->mode_pending = true;
        timer_mod(s->mode_timer,
                  qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + LENS_MODE_NS);
        break;
    case 0xa0:
    case 0xa1:
    case 0xa3:
        if (!s->configured || s->pending) {
            lens_error(s, "aperture-state-not-supported");
            return false;
        }
        for (unsigned i = 1; i < s->size - 1; i++) {
            if (in[i]) {
                lens_error(s, "aperture-travel-or-parameters-not-supported");
                return false;
            }
        }
        /* The fixed iris has no travel, so no motor work is pending. */
        s->aperture_noops++;
        break;
    default:
        break;
    }
    return true;
}

static void lens_phase_done(void *opaque)
{
    NikonLensState *s = opaque;

    if (s->phase != LENS_ERROR) {
        lens_notify(s, false);
    }
}

static uint32_t lens_transfer(SSIPeripheral *dev, uint32_t value)
{
    NikonLensState *s = NIKON_LENS(dev);
    uint16_t result = 0;

    if (s->phase == LENS_ERROR) {
        return 0;
    }
    if (timer_pending(s->phase_timer) || value > UINT16_MAX) {
        lens_error(s, "word-or-phase-not-supported");
        return 0;
    }
    lens_notify(s, true);
    if (s->phase == LENS_HEADER) {
        stw_le_p(s->header + s->offset, value);
        s->offset += 2;
        if (s->offset == sizeof(s->header)) {
            unsigned expected = lens_packet_size(s->header[3], s->header[4]);

            s->size = 2 * (unsigned)lduw_le_p(s->header + 5) + 2;
            if (s->header[0] != 0x55 || !lens_checksum(s->header, 8) ||
                !expected || s->size != expected ||
                (s->header[3] == 0x1f ? s->header[2] != 0xff :
                                       s->header[2] > 1)) {
                lens_error(s, "header-or-command-not-supported");
                return 0;
            }
            lens_response(s);
            s->header_counts[s->header[3]]++;
            s->phase = LENS_PAYLOAD;
            s->offset = 0;
            timer_mod(s->phase_timer,
                      qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + LENS_PHASE_NS);
        }
    } else {
        result = lduw_le_p(s->output + s->offset);
        stw_le_p(s->input + s->offset, value);
        s->offset += 2;
        if (s->offset == s->size && lens_process(s)) {
            s->completed++;
            s->packet_counts[s->header[3]]++;
            s->phase = LENS_HEADER;
            s->offset = 0;
            timer_mod(s->phase_timer,
                      qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + LENS_PHASE_NS);
        }
    }
    return result;
}

static void lens_reset(DeviceState *dev)
{
    NikonLensState *s = NIKON_LENS(dev);

    timer_del(s->phase_timer);
    timer_del(s->operation_timer);
    timer_del(s->stream_timer);
    timer_del(s->mode_timer);
    s->phase = LENS_HEADER;
    s->offset = s->size = s->sync_anchor = 0;
    s->probe = 0;
    s->configured = s->pending = s->stream_enabled = s->reported = false;
    s->completed = s->rejected = s->configuration_generation = 0;
    s->stream_frames = s->history_offset = s->history_sequence = 0;
    s->aperture_noops = 0;
    s->mode = s->requested_mode = 0;
    s->mode_pending = false;
    s->mode_generation = 0;
    memset(s->configuration, 0, sizeof(s->configuration));
    memset(s->header_counts, 0, sizeof(s->header_counts));
    memset(s->packet_counts, 0, sizeof(s->packet_counts));
    memset(s->header, 0, sizeof(s->header));
    memset(s->input, 0, sizeof(s->input));
    memset(s->output, 0, sizeof(s->output));
    lens_notify(s, false);
}

static void lens_unrealize(DeviceState *dev)
{
    NikonLensState *s = NIKON_LENS(dev);

    timer_del(s->phase_timer);
    timer_del(s->operation_timer);
    timer_del(s->stream_timer);
    timer_del(s->mode_timer);
    s->stream_enabled = false;
    lens_notify(s, true);
}

static void lens_realize(SSIPeripheral *dev, Error **errp)
{
    NikonLensState *s = NIKON_LENS(dev);

    if ((s->gpio && s->notify_pin >= 192) ||
        (!s->gpio && s->notify_pin != UINT32_MAX)) {
        error_setg(errp, "lens notify requires a GPIO link and pin below 192");
    }
}

static char *lens_runtime(Object *obj, Error **errp)
{
    NikonLensState *s = NIKON_LENS(obj);
    GString *text = g_string_new(NULL);

    g_string_append_printf(text,
        "phase=%u offset=%u/%u command=%02x:%02x mode=%02x notify=%u "
        "configured=%u pending=%u stream=%u history-offset=%u probe=%u\n",
        s->phase, s->offset, s->size, s->header[3], s->header[4],
        s->header[2], s->notify_level, s->configured, s->pending,
        s->stream_enabled, s->history_offset, s->probe);
    g_string_append_printf(text,
        "mode-state=%u requested-mode=%u mode-pending=%u mode-generation=%"
        PRIu64 "\n", s->mode, s->requested_mode, s->mode_pending,
        s->mode_generation);
    for (unsigned i = 0; i < ARRAY_SIZE(s->header_counts); i++) {
        if (s->header_counts[i]) {
            g_string_append_printf(text,
                "%02x headers=%" PRIu64 " completed=%" PRIu64 "\n",
                i, s->header_counts[i], s->packet_counts[i]);
        }
    }
    return g_string_free(text, false);
}

static void lens_init(Object *obj)
{
    NikonLensState *s = NIKON_LENS(obj);

    qdev_init_gpio_out_named(DEVICE(obj), &s->notify, "notify", 1);
    s->phase_timer = timer_new_ns(QEMU_CLOCK_VIRTUAL, lens_phase_done, s);
    s->operation_timer = timer_new_ns(QEMU_CLOCK_VIRTUAL,
                                     lens_operation_done, s);
    s->stream_timer = timer_new_ns(QEMU_CLOCK_VIRTUAL, lens_stream_word, s);
    s->mode_timer = timer_new_ns(QEMU_CLOCK_VIRTUAL, lens_mode_done, s);
    object_property_add_uint64_ptr(obj, "completed", &s->completed,
                                   OBJ_PROP_FLAG_READ);
    object_property_add_uint64_ptr(obj, "rejected", &s->rejected,
                                   OBJ_PROP_FLAG_READ);
    object_property_add_uint64_ptr(obj, "configuration-generation",
                                   &s->configuration_generation,
                                   OBJ_PROP_FLAG_READ);
    object_property_add_uint64_ptr(obj, "stream-frames", &s->stream_frames,
                                   OBJ_PROP_FLAG_READ);
    object_property_add_uint64_ptr(obj, "aperture-noops", &s->aperture_noops,
                                   OBJ_PROP_FLAG_READ);
    object_property_add_uint64_ptr(obj, "mode-generation", &s->mode_generation,
                                   OBJ_PROP_FLAG_READ);
    object_property_add_str(obj, "runtime", lens_runtime, NULL);
}

static void lens_finalize(Object *obj)
{
    NikonLensState *s = NIKON_LENS(obj);

    timer_free(s->phase_timer);
    timer_free(s->operation_timer);
    timer_free(s->stream_timer);
    timer_free(s->mode_timer);
}

static const Property lens_properties[] = {
    DEFINE_PROP_LINK("gpio", NikonLensState, gpio, TYPE_NIKON_GPIO,
                     DeviceState *),
    DEFINE_PROP_UINT32("notify", NikonLensState, notify_pin, UINT32_MAX),
    DEFINE_PROP_LINK("stream", NikonLensState, stream, TYPE_NIKON_SPI,
                     DeviceState *),
};

static const VMStateDescription lens_vmstate = {
    .name = "nikon-lens", .unmigratable = true,
};

static void lens_class_init(ObjectClass *oc, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    SSIPeripheralClass *sc = SSI_PERIPHERAL_CLASS(oc);

    dc->vmsd = &lens_vmstate;
    dc->user_creatable = false;
    dc->unrealize = lens_unrealize;
    device_class_set_props(dc, lens_properties);
    device_class_set_legacy_reset(dc, lens_reset);
    sc->realize = lens_realize;
    sc->transfer = lens_transfer;
    sc->cs_polarity = SSI_CS_NONE;
}

static const TypeInfo lens_type = {
    .name = TYPE_NIKON_LENS,
    .parent = TYPE_SSI_PERIPHERAL,
    .instance_size = sizeof(NikonLensState),
    .instance_init = lens_init,
    .instance_finalize = lens_finalize,
    .class_init = lens_class_init,
};

static void lens_register(void)
{
    type_register_static(&lens_type);
}
type_init(lens_register)
