/*
 * Functional LCD scanout observer for the MAS composition buffer.
 *
 * A real panel scans its composition buffer continuously; the unchanged
 * firmware in this model never writes a latch register, so the observer samples
 * the guest-owned buffer on a virtual-clock cadence and publishes it to a QEMU
 * console.  It reads guest RAM only: no register, IRQ, completion or status is
 * added to the guest contract, and the guest cannot observe the observer.
 *
 * Geometry and pixel format are the transfer device's documented RGBA program
 * (1024x688 RGBA8888 with six inactive rows at the top), not a recovered panel
 * specification.  A frame is converted and published only when the sampled
 * content changes, so an idle guest costs one strided read per period.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "qemu/module.h"
#include "qemu/timer.h"
#include "qapi/error.h"
#include "hw/core/qdev-properties.h"
#include "hw/core/sysbus.h"
#include "hw/nikon/devices.h"
#include "hw/nikon/panel.h"
#include "migration/vmstate.h"
#include "system/address-spaces.h"
#include "ui/console.h"
#include "ui/qemu-pixman.h" /* IWYU pragma: keep */

#define PANEL_SAMPLE_STRIDE 1024u

static const GraphicHwOps nikon_panel_graphics;

OBJECT_DECLARE_SIMPLE_TYPE(NikonPanelState, NIKON_PANEL)

struct NikonPanelState {
    SysBusDevice parent_obj;
    uint64_t base;
    uint32_t width;
    uint32_t height;
    uint32_t skip_rows;
    uint64_t interval_ns;
    QEMUTimer *timer;
    QemuConsole *console;
    uint32_t *frame;
    uint32_t *previous;
    uint64_t sample_hash;
    bool have_frame;
};

void nikon_panel_convert_rgba8888(uint32_t *dst, const uint8_t *src,
                                  size_t pixels)
{
    size_t i;

    for (i = 0; i < pixels; i++) {
        const uint8_t *p = src + i * 4;

        /* Console surfaces are a8r8g8b8: 0xAARRGGBB in host byte order. */
        dst[i] = ((uint32_t)p[3] << 24) | ((uint32_t)p[0] << 16) |
                 ((uint32_t)p[1] << 8) | (uint32_t)p[2];
    }
}

static uint64_t nikon_panel_sample(const uint8_t *data, size_t bytes)
{
    uint64_t hash = 0xcbf29ce484222325ULL;
    size_t offset;

    for (offset = 0; offset + 4 <= bytes; offset += 4 * PANEL_SAMPLE_STRIDE) {
        uint32_t word;

        memcpy(&word, data + offset, sizeof(word));
        hash = (hash ^ word) * 0x100000001b3ULL;
    }
    return hash;
}

static void nikon_panel_publish(NikonPanelState *s, unsigned width,
                                unsigned height)
{
    DisplaySurface *surface;
    pixman_image_t *image;

    if (!s->console) {
        s->console = qemu_graphic_console_create(DEVICE(s), 0,
                                                  &nikon_panel_graphics, s);
        if (!s->console) {
            return;
        }
    }
    qemu_console_resize(s->console, width, height);
    surface = qemu_console_surface(s->console);
    if (!surface || !surface->image) {
        return;
    }
    image = pixman_image_create_bits(PIXMAN_a8r8g8b8, width, height,
                                     s->frame, width * 4);
    if (!image) {
        return;
    }
    pixman_image_composite32(PIXMAN_OP_SRC, image, NULL, surface->image,
                             0, 0, 0, 0, 0, 0, width, height);
    pixman_image_unref(image);
    qemu_console_update_full(s->console);
}

static void nikon_panel_tick(void *opaque)
{
    NikonPanelState *s = opaque;
    MemTxAttrs attrs = MEMTXATTRS_UNSPECIFIED;
    hwaddr size;
    uint8_t *guest;
    uint64_t hash;
    unsigned rows;

    rows = s->height;
    if (rows <= s->skip_rows) {
        goto reschedule;
    }
    rows -= s->skip_rows;
    size = (hwaddr)rows * s->width * 4;
    guest = address_space_map(&address_space_memory,
                              s->base + (hwaddr)s->skip_rows * s->width * 4,
                              &size, false, attrs);
    if (!guest) {
        goto reschedule;
    }
    if (size == (hwaddr)rows * s->width * 4) {
        hash = nikon_panel_sample(guest, size);
        if (hash != s->sample_hash || !s->have_frame) {
            s->sample_hash = hash;
            s->have_frame = true;
            nikon_panel_convert_rgba8888(s->frame, guest, (size_t)rows *
                                         s->width);
            memcpy(s->previous, s->frame, size);
            nikon_panel_publish(s, s->width, rows);
        }
    }
    address_space_unmap(&address_space_memory, guest, size, false, size);
reschedule:
    timer_mod(s->timer, qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) +
                          (int64_t)s->interval_ns);
}

static bool nikon_panel_update(void *opaque)
{
    NikonPanelState *s = opaque;

    if (!s->have_frame || s->height <= s->skip_rows) {
        return true;
    }
    /* The guest owns the pixels; the surface holds the last sampled frame. */
    nikon_panel_publish(s, s->width, s->height - s->skip_rows);
    return true;
}

static const GraphicHwOps nikon_panel_graphics = {
    .gfx_update = nikon_panel_update,
};

static void nikon_panel_realize(DeviceState *dev, Error **errp)
{
    NikonPanelState *s = NIKON_PANEL(dev);
    size_t pixels;

    if (!s->width || !s->height || s->height <= s->skip_rows) {
        error_setg(errp, "panel observer needs a positive visible geometry");
        return;
    }
    if (!s->interval_ns) {
        error_setg(errp, "panel observer needs a non-zero scanout interval");
        return;
    }
    pixels = (size_t)s->width * s->height;
    s->frame = g_new0(uint32_t, pixels);
    s->previous = g_new0(uint32_t, pixels);
    s->timer = timer_new_ns(QEMU_CLOCK_VIRTUAL, nikon_panel_tick, s);
    timer_mod(s->timer, qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) +
                          (int64_t)s->interval_ns);
}

static void nikon_panel_unrealize(DeviceState *dev)
{
    NikonPanelState *s = NIKON_PANEL(dev);

    if (s->timer) {
        timer_del(s->timer);
        timer_free(s->timer);
        s->timer = NULL;
    }
    g_free(s->frame);
    g_free(s->previous);
    s->frame = NULL;
    s->previous = NULL;
}

static const Property nikon_panel_properties[] = {
    DEFINE_PROP_UINT64("base", NikonPanelState, base, 0),
    DEFINE_PROP_UINT32("width", NikonPanelState, width, 1024),
    DEFINE_PROP_UINT32("height", NikonPanelState, height, 688),
    DEFINE_PROP_UINT32("skip-rows", NikonPanelState, skip_rows, 6),
    DEFINE_PROP_UINT64("interval-ns", NikonPanelState, interval_ns,
                       33333333ULL),
};

static const VMStateDescription nikon_panel_vmstate = {
    .name = TYPE_NIKON_PANEL,
    .unmigratable = true,
};

static void nikon_panel_class_init(ObjectClass *oc, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->realize = nikon_panel_realize;
    dc->unrealize = nikon_panel_unrealize;
    dc->vmsd = &nikon_panel_vmstate;
    device_class_set_props(dc, nikon_panel_properties);
}

static const TypeInfo nikon_panel_type = {
    .name = TYPE_NIKON_PANEL,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(NikonPanelState),
    .class_init = nikon_panel_class_init,
};

static void nikon_panel_register_types(void)
{
    type_register_static(&nikon_panel_type);
}

type_init(nikon_panel_register_types)
