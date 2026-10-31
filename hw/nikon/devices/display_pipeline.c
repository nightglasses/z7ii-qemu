/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Nikon Virtual Camera - Display & Graphics Hardware Abstraction Layer (HAL)
 *
 * Implements the cross-platform display pipeline dispatch and CPU reference backend.
 */

#include "qemu/osdep.h"
#include "qemu/bswap.h"
#include "hw/nikon/display_pipeline.h"
#include "hw/nikon/devices.h"
#include <pixman.h>

#define FRAME_PIXELS (2048ULL * 2048ULL)

static inline uint32_t cpu_pixel_to_argb(const uint8_t *data, unsigned mode,
                                         uint32_t control, uint32_t lanes)
{
    unsigned red, green, blue, alpha = 255;
    uint16_t packed;

    if (!mode) {
        unsigned i, component[4];

        for (i = 0; i < 4; i++) {
            unsigned lane = (lanes >> (i * 8)) & 3;
            component[i] = data[(control & 0x10) ? 3 - lane : lane];
        }
        red = component[0];
        green = component[1];
        blue = component[2];
        if (control & 0x10000000) {
            alpha = component[3];
        }
    } else {
        packed = (control & 0x10) ? lduw_be_p(data) : lduw_le_p(data);
        if (mode == 2) {
            red = ((packed >> 10) & 31) * 255 / 31;
            green = ((packed >> 5) & 31) * 255 / 31;
            blue = (packed & 31) * 255 / 31;
            alpha = (packed & 0x8000) ? 255 : 0;
        } else if (mode == 3) {
            red = ((packed >> 8) & 15) * 17;
            green = ((packed >> 4) & 15) * 17;
            blue = (packed & 15) * 17;
            alpha = ((packed >> 12) & 15) * 17;
        } else {
            red = (packed >> 11) * 255 / 31;
            green = ((packed >> 5) & 63) * 255 / 63;
            blue = (packed & 31) * 255 / 31;
        }
    }
    if (alpha == 255) {
        return 0xff000000U | (red << 16) | (green << 8) | blue;
    }
    if (!alpha) {
        return 0;
    }
    return (alpha << 24) | ((red * alpha / 255) << 16) |
           ((green * alpha / 255) << 8) | (blue * alpha / 255);
}

static inline uint32_t cpu_yuva_to_argb(const uint8_t *pair, unsigned column)
{
    int y = MAX(0, pair[column & 1] - 16);
    int u = pair[2] - 128, v = pair[3] - 128;
    unsigned red = CLAMP((298 * y + 409 * v + 128) >> 8, 0, 255);
    unsigned green = CLAMP((298 * y - 100 * u - 208 * v + 128) >> 8, 0, 255);
    unsigned blue = CLAMP((298 * y + 516 * u + 128) >> 8, 0, 255);

    return 0xff000000U | (red << 16) | (green << 8) | blue;
}

/* -------------------------------------------------------------------------
 * CPU Reference Backend Implementation
 * ------------------------------------------------------------------------- */

static bool cpu_display_init(NikonDisplayPipeline *p)
{
    (void)p;
    return true;
}

static void cpu_display_cleanup(NikonDisplayPipeline *p)
{
    if (p->output) {
        pixman_image_unref(p->output);
        p->output = NULL;
    }
    p->in_frame = false;
}

static bool cpu_display_begin_frame(NikonDisplayPipeline *p,
                                    unsigned width, unsigned height)
{
    if (!width || !height || (uint64_t)width * height > FRAME_PIXELS) {
        return false;
    }
    if (p->output) {
        pixman_image_unref(p->output);
    }
    p->output = pixman_image_create_bits(PIXMAN_a8r8g8b8, width, height, NULL, 0);
    if (!p->output) {
        return false;
    }
    p->width = width;
    p->height = height;
    p->in_frame = true;
    return true;
}

static bool cpu_display_composite_primary(NikonDisplayPipeline *p,
                                          pixman_image_t *output,
                                          const NikonDisplayPrimaryDesc *desc,
                                          int dest_x, int dest_y,
                                          unsigned out_width, unsigned out_height)
{
    pixman_image_t *image;
    pixman_transform_t transform;
    uint32_t *pixels;
    unsigned stride, shared_rows, row, col, i, j;

    if (!desc || !desc->luma || !desc->chroma || !desc->width || !desc->height) {
        return false;
    }
    image = pixman_image_create_bits(PIXMAN_a8r8g8b8, desc->width, desc->height,
                                     NULL, 0);
    if (!image) {
        return false;
    }
    pixels = pixman_image_get_data(image);
    stride = pixman_image_get_stride(image) / 4;
    shared_rows = desc->mode == 4 ? 2 : 1;

    for (row = 0; row < desc->height; row += shared_rows) {
        unsigned uv_row = desc->mode == 4 ? row / 2 : row;
        const uint8_t *uv_ptr = desc->chroma + (size_t)uv_row * desc->width;

        for (col = 0; col < desc->width; col += 2) {
            const uint8_t *uv = uv_ptr + col;
            int32_t color[3];

            for (i = 0; i < 3; i++) {
                color[i] = desc->matrix[0][i][1] * (uv[0] - 128) +
                           desc->matrix[0][i][2] * (uv[1] - 128);
            }
            for (unsigned dy = 0; dy < shared_rows; dy++) {
                unsigned y = row + dy;
                const uint8_t *luma_row = desc->luma + (size_t)y * desc->width;
                uint32_t *pix_row = pixels + y * stride;

                for (unsigned dx = 0; dx < 2; dx++) {
                    unsigned x = col + dx;
                    int32_t rgb[3], final[3];

                    for (j = 0; j < 3; j++) {
                        int32_t value = color[j] + desc->matrix[0][j][0] * luma_row[x];
                        rgb[j] = CLAMP((value + 8192) >> 14, 0, 255);
                    }
                    for (i = 0; i < 3; i++) {
                        int32_t value = rgb[i];

                        if (desc->apply_color_matrix) {
                            int32_t sum = 0;
                            for (j = 0; j < 3; j++) {
                                sum += desc->matrix[1][i][j] * rgb[j];
                            }
                            value = CLAMP((sum + 512) >> 10, 0, 255);
                        }
                        final[i] = desc->alpha == 255 ? value : value * desc->alpha / 255;
                    }
                    pix_row[x] = (desc->alpha << 24) |
                        (final[0] << 16) | (final[1] << 8) | final[2];
                }
            }
        }
    }

    pixman_transform_init_scale(&transform,
                                ((int64_t)desc->b << 16) / desc->a,
                                ((int64_t)desc->f << 16) / desc->e);
    if (!pixman_image_set_transform(image, &transform) ||
        !pixman_image_set_filter(image, PIXMAN_FILTER_NEAREST, NULL, 0)) {
        pixman_image_unref(image);
        return false;
    }

    if (dest_x < (int)out_width && dest_y < (int)out_height) {
        pixman_image_composite32(PIXMAN_OP_OVER, image, NULL, output ? output : p->output,
                                 0, 0, 0, 0, dest_x, dest_y,
                                 MIN(desc->scaled_w, out_width - (unsigned)dest_x),
                                 MIN(desc->scaled_h, out_height - (unsigned)dest_y));
    }
    pixman_image_unref(image);
    return true;
}

static bool cpu_display_composite_layer(NikonDisplayPipeline *p,
                                        pixman_image_t *canvas,
                                        const NikonDisplayLayerDesc *desc,
                                        unsigned out_width, unsigned out_height)
{
    pixman_image_t *image;
    uint32_t *pixels;
    unsigned pitch, row_index, column;
    unsigned bytes;

    (void)p;
    if (!desc || !desc->primary || !desc->width || !desc->height ||
        desc->x >= (int)out_width || desc->y >= (int)out_height) {
        return false;
    }
    bytes = desc->mode ? 2 : 4;
    image = pixman_image_create_bits(PIXMAN_a8r8g8b8, desc->width, desc->height,
                                     NULL, 0);
    if (!image) {
        return false;
    }
    pixels = pixman_image_get_data(image);
    pitch = pixman_image_get_stride(image) / 4;

    for (row_index = 0; row_index < desc->height; row_index++) {
        const uint8_t *row = desc->primary + (size_t)row_index * desc->stride;
        const uint8_t *alpha = desc->mode >= 5 ?
            (desc->secondary + (size_t)row_index * desc->alpha_stride) : NULL;
        uint32_t *pix_row = pixels + (size_t)row_index * pitch;

        for (column = 0; column < desc->width; column++) {
            unsigned opacity = 255;
            uint32_t pixel;

            if (desc->mode >= 5 && alpha) {
                opacity = desc->mode == 5 ?
                    ((alpha[column / 2] >> ((column & 1) * 4)) & 15) * 17 :
                    alpha[column];
                if (!opacity) {
                    pix_row[column] = 0;
                    continue;
                }
            }
            pixel = desc->yuva ?
                cpu_yuva_to_argb(row + (column & ~1U) * 2, column) :
                cpu_pixel_to_argb(row + column * bytes, desc->mode,
                                  desc->control, desc->lanes);
            if (opacity != 255) {
                pixel = (opacity << 24) |
                    ((((pixel >> 16) & 255) * opacity / 255) << 16) |
                    ((((pixel >> 8) & 255) * opacity / 255) << 8) |
                    ((pixel & 255) * opacity / 255);
            }
            pix_row[column] = pixel;
        }
    }
    pixman_image_composite32(PIXMAN_OP_OVER, image, NULL, canvas,
                             0, 0, 0, 0, desc->x, desc->y,
                             desc->width, desc->height);
    pixman_image_unref(image);
    return true;
}

static bool cpu_display_fill_rectangles(NikonDisplayPipeline *p,
                                        pixman_image_t *output,
                                        const NikonDisplayRectDesc *rects,
                                        unsigned count,
                                        unsigned width, unsigned height)
{
    unsigned i;
    pixman_image_t *target = output ? output : p->output;

    if (!target || !rects || !count) {
        return false;
    }
    for (i = 0; i < count; i++) {
        const NikonDisplayRectDesc *r = &rects[i];
        pixman_color_t rgb;
        pixman_rectangle16_t rect;

        if (!r->w || !r->h || r->x >= width || r->y >= height) {
            continue;
        }
        rgb = (pixman_color_t) {
            .red = (r->color_rgb & 255) * 257,
            .green = ((r->color_rgb >> 8) & 255) * 257,
            .blue = ((r->color_rgb >> 16) & 255) * 257,
            .alpha = 65535,
        };
        rect = (pixman_rectangle16_t) {
            (int16_t)r->x, (int16_t)r->y,
            (uint16_t)MIN(r->w, width - r->x),
            (uint16_t)MIN(r->h, height - r->y),
        };
        pixman_image_fill_rectangles(PIXMAN_OP_OVER, target, &rgb, 1, &rect);
    }
    return true;
}

static void cpu_display_present(NikonDisplayPipeline *p,
                                DisplaySurface *surface,
                                pixman_image_t *output,
                                unsigned width, unsigned height)
{
    pixman_image_t *src = output ? output : p->output;

    if (src && surface && surface->image) {
        pixman_image_composite32(PIXMAN_OP_SRC, src, NULL,
                                 surface->image, 0, 0, 0, 0, 0, 0,
                                 width, height);
    }
}

static void cpu_display_end_frame(NikonDisplayPipeline *p)
{
    if (p->output) {
        pixman_image_unref(p->output);
        p->output = NULL;
    }
    p->in_frame = false;
}

static const NikonDisplayOps cpu_display_ops = {
    .name = "CPU Reference (Pixman)",
    .type = NIKON_DISPLAY_BACKEND_CPU,
    .init = cpu_display_init,
    .cleanup = cpu_display_cleanup,
    .begin_frame = cpu_display_begin_frame,
    .composite_primary = cpu_display_composite_primary,
    .composite_layer = cpu_display_composite_layer,
    .fill_rectangles = cpu_display_fill_rectangles,
    .present = cpu_display_present,
    .end_frame = cpu_display_end_frame,
};

/* -------------------------------------------------------------------------
 * Hardware Backend Registry & Placeholders
 * ------------------------------------------------------------------------- */

static const NikonDisplayOps d3d12_display_stub_ops = {
    .name = "Windows Direct3D 12 Pipeline (Reserved)",
    .type = NIKON_DISPLAY_BACKEND_D3D12,
    .init = cpu_display_init,
    .cleanup = cpu_display_cleanup,
    .begin_frame = cpu_display_begin_frame,
    .composite_primary = cpu_display_composite_primary,
    .composite_layer = cpu_display_composite_layer,
    .fill_rectangles = cpu_display_fill_rectangles,
    .present = cpu_display_present,
    .end_frame = cpu_display_end_frame,
};

static const NikonDisplayOps vulkan_display_stub_ops = {
    .name = "Vulkan Pipeline (Reserved)",
    .type = NIKON_DISPLAY_BACKEND_VULKAN,
    .init = cpu_display_init,
    .cleanup = cpu_display_cleanup,
    .begin_frame = cpu_display_begin_frame,
    .composite_primary = cpu_display_composite_primary,
    .composite_layer = cpu_display_composite_layer,
    .fill_rectangles = cpu_display_fill_rectangles,
    .present = cpu_display_present,
    .end_frame = cpu_display_end_frame,
};

const char *nikon_display_backend_name(NikonDisplayBackendType type)
{
    switch (type) {
    case NIKON_DISPLAY_BACKEND_CPU:
        return "cpu";
    case NIKON_DISPLAY_BACKEND_METAL:
        return "metal";
    case NIKON_DISPLAY_BACKEND_D3D12:
        return "d3d12";
    case NIKON_DISPLAY_BACKEND_VULKAN:
        return "vulkan";
    default:
        return "unknown";
    }
}

NikonDisplayBackendType nikon_display_detect_optimal_backend(void)
{
    const char *env = getenv("NIKON_DISPLAY_BACKEND");

    if (env) {
        if (!g_ascii_strcasecmp(env, "metal")) {
            return NIKON_DISPLAY_BACKEND_METAL;
        }
        if (!g_ascii_strcasecmp(env, "d3d12") || !g_ascii_strcasecmp(env, "dx12")) {
            return NIKON_DISPLAY_BACKEND_D3D12;
        }
        if (!g_ascii_strcasecmp(env, "vulkan") || !g_ascii_strcasecmp(env, "vk")) {
            return NIKON_DISPLAY_BACKEND_VULKAN;
        }
        if (!g_ascii_strcasecmp(env, "cpu")) {
            return NIKON_DISPLAY_BACKEND_CPU;
        }
    }

#if defined(__APPLE__)
    if (nikon_metal_is_supported()) {
        return NIKON_DISPLAY_BACKEND_METAL;
    }
    return NIKON_DISPLAY_BACKEND_CPU;
#elif defined(_WIN32)
    return NIKON_DISPLAY_BACKEND_CPU; /* Default fallback until D3D12 device bound */
#else
    return NIKON_DISPLAY_BACKEND_CPU;
#endif
}

NikonDisplayPipeline *nikon_display_pipeline_create(NikonDisplayBackendType type)
{
    NikonDisplayPipeline *p = g_new0(NikonDisplayPipeline, 1);

    switch (type) {
    case NIKON_DISPLAY_BACKEND_METAL:
        if (nikon_metal_is_supported() && nikon_display_metal_get_ops()) {
            p->ops = nikon_display_metal_get_ops();
        } else {
            p->ops = &cpu_display_ops;
        }
        break;
    case NIKON_DISPLAY_BACKEND_D3D12:
        p->ops = &d3d12_display_stub_ops;
        break;
    case NIKON_DISPLAY_BACKEND_VULKAN:
        p->ops = &vulkan_display_stub_ops;
        break;
    case NIKON_DISPLAY_BACKEND_CPU:
    default:
        p->ops = &cpu_display_ops;
        break;
    }

    if (p->ops->init && !p->ops->init(p)) {
        if (p->ops->type != NIKON_DISPLAY_BACKEND_CPU) {
            /* Graceful fallback to CPU reference if GPU initialization fails */
            p->ops = &cpu_display_ops;
            if (p->ops->init && !p->ops->init(p)) {
                g_free(p);
                return NULL;
            }
        } else {
            g_free(p);
            return NULL;
        }
    }
    return p;
}

void nikon_display_pipeline_free(NikonDisplayPipeline *p)
{
    if (!p) {
        return;
    }
    if (p->ops && p->ops->cleanup) {
        p->ops->cleanup(p);
    }
    g_free(p);
}

bool nikon_display_pipeline_begin_frame(NikonDisplayPipeline *p,
                                        unsigned width, unsigned height)
{
    if (!p || !p->ops || !p->ops->begin_frame) {
        return false;
    }
    return p->ops->begin_frame(p, width, height);
}

bool nikon_display_pipeline_composite_primary(NikonDisplayPipeline *p,
                                              const NikonDisplayPrimaryDesc *desc,
                                              int dest_x, int dest_y,
                                              unsigned out_width, unsigned out_height)
{
    if (!p || !p->ops || !p->ops->composite_primary) {
        return false;
    }
    return p->ops->composite_primary(p, p->output, desc, dest_x, dest_y,
                                     out_width, out_height);
}

bool nikon_display_pipeline_composite_layer(NikonDisplayPipeline *p,
                                            pixman_image_t *canvas,
                                            const NikonDisplayLayerDesc *desc,
                                            unsigned out_width, unsigned out_height)
{
    if (!p || !p->ops || !p->ops->composite_layer) {
        return false;
    }
    return p->ops->composite_layer(p, canvas, desc, out_width, out_height);
}

bool nikon_display_pipeline_fill_rectangles(NikonDisplayPipeline *p,
                                            const NikonDisplayRectDesc *rects,
                                            unsigned count)
{
    if (!p || !p->ops || !p->ops->fill_rectangles) {
        return false;
    }
    return p->ops->fill_rectangles(p, p->output, rects, count, p->width, p->height);
}

void nikon_display_pipeline_present(NikonDisplayPipeline *p, DisplaySurface *surface)
{
    if (p && p->ops && p->ops->present) {
        p->ops->present(p, surface, p->output, p->width, p->height);
    }
}

void nikon_display_pipeline_end_frame(NikonDisplayPipeline *p)
{
    if (p && p->ops && p->ops->end_frame) {
        p->ops->end_frame(p);
    }
}
