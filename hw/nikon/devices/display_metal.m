/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Nikon Virtual Camera - macOS Metal Display & Compute Pipeline
 *
 * Implements GPU-accelerated frame composition, YUV-to-RGB color space
 * conversion, and multi-layer rendering utilizing Apple Silicon's Unified Memory
 * Architecture (UMA) and Metal Compute Shaders.
 */

#include "qemu/osdep.h"
#include "hw/nikon/display_pipeline.h"
#include <pixman.h>

#ifndef __APPLE__

bool nikon_metal_is_supported(void)
{
    return false;
}

const NikonDisplayOps *nikon_display_metal_get_ops(void)
{
    return NULL;
}

#else

#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#include <dlfcn.h>

#define FRAME_PIXELS (2048ULL * 2048ULL)

typedef struct PrimaryParams {
    int32_t matrix0[3][3];
    int32_t matrix1[3][3];
    uint32_t apply_color_matrix;
    uint32_t alpha;
    uint32_t width;
    uint32_t height;
    uint32_t mode;
} PrimaryParams;

static const char *s_metal_shader_source =
"#include <metal_stdlib>\n"
"using namespace metal;\n"
"\n"
"struct PrimaryParams {\n"
"    int32_t matrix0[3][3];\n"
"    int32_t matrix1[3][3];\n"
"    uint32_t apply_color_matrix;\n"
"    uint32_t alpha;\n"
"    uint32_t width;\n"
"    uint32_t height;\n"
"    uint32_t mode;\n"
"};\n"
"\n"
"kernel void primary_yuv_to_argb(\n"
"    device const uint8_t *luma        [[buffer(0)]],\n"
"    device const uint8_t *chroma      [[buffer(1)]],\n"
"    device uint32_t *output           [[buffer(2)]],\n"
"    constant PrimaryParams &params    [[buffer(3)]],\n"
"    uint2 gid                         [[thread_position_in_grid]])\n"
"{\n"
"    uint x = gid.x;\n"
"    uint y = gid.y;\n"
"    if (x >= params.width || y >= params.height) {\n"
"        return;\n"
"    }\n"
"    uint uv_row = (params.mode == 4) ? (y / 2) : y;\n"
"    uint uv_col = (x & ~1u);\n"
"    uint uv_idx = uv_row * params.width + uv_col;\n"
"    int u = int(chroma[uv_idx]) - 128;\n"
"    int v = int(chroma[uv_idx + 1]) - 128;\n"
"\n"
"    int32_t color[3];\n"
"    for (int i = 0; i < 3; i++) {\n"
"        color[i] = params.matrix0[i][1] * u + params.matrix0[i][2] * v;\n"
"    }\n"
"\n"
"    int y_val = int(luma[y * params.width + x]);\n"
"    int32_t rgb[3];\n"
"    for (int j = 0; j < 3; j++) {\n"
"        int32_t val = color[j] + params.matrix0[j][0] * y_val;\n"
"        rgb[j] = clamp((val + 8192) >> 14, 0, 255);\n"
"    }\n"
"\n"
"    int32_t final_rgb[3];\n"
"    for (int i = 0; i < 3; i++) {\n"
"        int32_t val = rgb[i];\n"
"        if (params.apply_color_matrix) {\n"
"            int32_t sum = 0;\n"
"            for (int j = 0; j < 3; j++) {\n"
"                sum += params.matrix1[i][j] * rgb[j];\n"
"            }\n"
"            val = clamp((sum + 512) >> 10, 0, 255);\n"
"        }\n"
"        final_rgb[i] = (params.alpha == 255) ? val : (val * int(params.alpha) / 255);\n"
"    }\n"
"\n"
"    output[y * params.width + x] = (params.alpha << 24) |\n"
"        (uint(final_rgb[0]) << 16) | (uint(final_rgb[1]) << 8) | uint(final_rgb[2]);\n"
"}\n";

typedef id<MTLDevice> (*MTLCreateSystemDefaultDeviceFn)(void);
static MTLCreateSystemDefaultDeviceFn s_create_default_device = NULL;

static bool init_metal_runtime(void)
{
    static bool attempted = false;
    static bool available = false;

    if (attempted) {
        return available;
    }
    attempted = true;

    void *metal_lib = dlopen("/System/Library/Frameworks/Metal.framework/Metal", RTLD_LAZY | RTLD_GLOBAL);
    if (!metal_lib) {
        metal_lib = dlopen("Metal.framework/Metal", RTLD_LAZY | RTLD_GLOBAL);
    }
    if (metal_lib) {
        s_create_default_device = (MTLCreateSystemDefaultDeviceFn)dlsym(metal_lib, "MTLCreateSystemDefaultDevice");
        if (s_create_default_device) {
            @autoreleasepool {
                id<MTLDevice> test_dev = s_create_default_device();
                available = (test_dev != nil);
            }
        }
    }
    return available;
}

bool nikon_metal_is_supported(void)
{
    return init_metal_runtime();
}

typedef struct MetalBackendState {
    id<MTLDevice> device;
    id<MTLCommandQueue> command_queue;
    id<MTLComputePipelineState> primary_pipeline;
    id<MTLBuffer> output_buffer;
    size_t output_buffer_size;
} MetalBackendState;

static bool metal_display_init(NikonDisplayPipeline *p)
{
    if (!init_metal_runtime() || !s_create_default_device) {
        return false;
    }

    @autoreleasepool {
        MetalBackendState *state = g_new0(MetalBackendState, 1);
        state->device = s_create_default_device();
        if (!state->device) {
            g_free(state);
            return false;
        }

        state->command_queue = [state->device newCommandQueue];
        if (!state->command_queue) {
            g_free(state);
            return false;
        }

        NSError *error = nil;
        NSString *source = [NSString stringWithUTF8String:s_metal_shader_source];
        id<MTLLibrary> library = [state->device newLibraryWithSource:source
                                                             options:nil
                                                               error:&error];
        if (!library || error) {
            g_free(state);
            return false;
        }

        id<MTLFunction> kernel = [library newFunctionWithName:@"primary_yuv_to_argb"];
        if (!kernel) {
            g_free(state);
            return false;
        }

        state->primary_pipeline = [state->device newComputePipelineStateWithFunction:kernel
                                                                               error:&error];
        if (!state->primary_pipeline || error) {
            g_free(state);
            return false;
        }

        p->backend_state = state;
        return true;
    }
}

static void metal_display_cleanup(NikonDisplayPipeline *p)
{
    if (!p) {
        return;
    }
    @autoreleasepool {
        MetalBackendState *state = (MetalBackendState *)p->backend_state;
        if (state) {
            state->output_buffer = nil;
            state->primary_pipeline = nil;
            state->command_queue = nil;
            state->device = nil;
            g_free(state);
            p->backend_state = NULL;
        }
    }
    if (p->output) {
        pixman_image_unref(p->output);
        p->output = NULL;
    }
    p->in_frame = false;
}

static bool metal_display_begin_frame(NikonDisplayPipeline *p,
                                      unsigned width, unsigned height)
{
    if (!width || !height || (uint64_t)width * height > FRAME_PIXELS) {
        return false;
    }

    @autoreleasepool {
        MetalBackendState *state = (MetalBackendState *)p->backend_state;
        size_t required_bytes = (size_t)width * height * 4;

        if (state && (!state->output_buffer || state->output_buffer_size < required_bytes)) {
            state->output_buffer = [state->device newBufferWithLength:required_bytes
                                                              options:MTLResourceStorageModeShared];
            state->output_buffer_size = required_bytes;
        }

        if (p->output) {
            pixman_image_unref(p->output);
        }
        if (state && state->output_buffer) {
            /* Zero-copy host mapping of Metal unified memory buffer */
            p->output = pixman_image_create_bits(PIXMAN_a8r8g8b8, width, height,
                                                 (uint32_t *)[state->output_buffer contents],
                                                 width * 4);
        } else {
            p->output = pixman_image_create_bits(PIXMAN_a8r8g8b8, width, height, NULL, 0);
        }

        if (!p->output) {
            return false;
        }
        p->width = width;
        p->height = height;
        p->in_frame = true;
        return true;
    }
}

static bool metal_display_composite_primary(NikonDisplayPipeline *p,
                                            pixman_image_t *output,
                                            const NikonDisplayPrimaryDesc *desc,
                                            int dest_x, int dest_y,
                                            unsigned out_width, unsigned out_height)
{
    if (!desc || !desc->luma || !desc->chroma || !desc->width || !desc->height) {
        return false;
    }

    MetalBackendState *state = (MetalBackendState *)p->backend_state;
    if (!state || !state->device || !state->primary_pipeline || !state->command_queue) {
        return false;
    }

    @autoreleasepool {
        size_t luma_size = (size_t)desc->width * desc->height;
        size_t chroma_size = (desc->mode == 4) ?
            ((size_t)desc->width * (desc->height / 2)) :
            ((size_t)desc->width * desc->height);
        size_t primary_out_size = (size_t)desc->width * desc->height * 4;

        /* Create shared zero-copy buffers for unified memory access */
        id<MTLBuffer> luma_buf = [state->device newBufferWithBytes:desc->luma
                                                            length:luma_size
                                                           options:MTLResourceStorageModeShared];
        id<MTLBuffer> chroma_buf = [state->device newBufferWithBytes:desc->chroma
                                                              length:chroma_size
                                                             options:MTLResourceStorageModeShared];
        id<MTLBuffer> prim_out_buf = [state->device newBufferWithLength:primary_out_size
                                                                options:MTLResourceStorageModeShared];

        PrimaryParams params;
        memcpy(params.matrix0, desc->matrix[0], sizeof(params.matrix0));
        memcpy(params.matrix1, desc->matrix[1], sizeof(params.matrix1));
        params.apply_color_matrix = desc->apply_color_matrix ? 1 : 0;
        params.alpha = desc->alpha;
        params.width = desc->width;
        params.height = desc->height;
        params.mode = desc->mode;

        id<MTLBuffer> params_buf = [state->device newBufferWithBytes:&params
                                                              length:sizeof(params)
                                                             options:MTLResourceStorageModeShared];

        id<MTLCommandBuffer> cmd_buf = [state->command_queue commandBuffer];
        id<MTLComputeCommandEncoder> encoder = [cmd_buf computeCommandEncoder];

        [encoder setComputePipelineState:state->primary_pipeline];
        [encoder setBuffer:luma_buf offset:0 atIndex:0];
        [encoder setBuffer:chroma_buf offset:0 atIndex:1];
        [encoder setBuffer:prim_out_buf offset:0 atIndex:2];
        [encoder setBuffer:params_buf offset:0 atIndex:3];

        MTLSize threads_per_group = MTLSizeMake(16, 16, 1);
        MTLSize threadgroups = MTLSizeMake((desc->width + 15) / 16,
                                           (desc->height + 15) / 16, 1);

        [encoder dispatchThreadgroups:threadgroups
                threadsPerThreadgroup:threads_per_group];
        [encoder endEncoding];

        [cmd_buf commit];
        [cmd_buf waitUntilCompleted];

        /* Wrap converted GPU output into a Pixman surface for scaling and clipping */
        pixman_image_t *prim_image = pixman_image_create_bits(PIXMAN_a8r8g8b8,
                                                              desc->width, desc->height,
                                                              (uint32_t *)[prim_out_buf contents],
                                                              desc->width * 4);
        if (!prim_image) {
            return false;
        }

        pixman_transform_t transform;
        pixman_transform_init_scale(&transform,
                                    ((int64_t)desc->b << 16) / desc->a,
                                    ((int64_t)desc->f << 16) / desc->e);
        if (!pixman_image_set_transform(prim_image, &transform) ||
            !pixman_image_set_filter(prim_image, PIXMAN_FILTER_NEAREST, NULL, 0)) {
            pixman_image_unref(prim_image);
            return false;
        }

        if (dest_x < (int)out_width && dest_y < (int)out_height) {
            pixman_image_composite32(PIXMAN_OP_OVER, prim_image, NULL,
                                     output ? output : p->output,
                                     0, 0, 0, 0, dest_x, dest_y,
                                     MIN(desc->scaled_w, out_width - (unsigned)dest_x),
                                     MIN(desc->scaled_h, out_height - (unsigned)dest_y));
        }
        pixman_image_unref(prim_image);
        return true;
    }
}

static inline uint32_t metal_pixel_to_argb(const uint8_t *data, unsigned mode,
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
        packed = (control & 0x10) ?
            (((uint16_t)data[0] << 8) | data[1]) :
            (((uint16_t)data[1] << 8) | data[0]);
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

static inline uint32_t metal_yuva_to_argb(const uint8_t *pair, unsigned column)
{
    int y = MAX(0, pair[column & 1] - 16);
    int u = pair[2] - 128, v = pair[3] - 128;
    unsigned red = CLAMP((298 * y + 409 * v + 128) >> 8, 0, 255);
    unsigned green = CLAMP((298 * y - 100 * u - 208 * v + 128) >> 8, 0, 255);
    unsigned blue = CLAMP((298 * y + 516 * u + 128) >> 8, 0, 255);

    return 0xff000000U | (red << 16) | (green << 8) | blue;
}

static bool metal_display_composite_layer(NikonDisplayPipeline *p,
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
                metal_yuva_to_argb(row + (column & ~1U) * 2, column) :
                metal_pixel_to_argb(row + column * bytes, desc->mode,
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

static bool metal_display_fill_rectangles(NikonDisplayPipeline *p,
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

static void metal_display_present(NikonDisplayPipeline *p,
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

static void metal_display_end_frame(NikonDisplayPipeline *p)
{
    if (p->output) {
        pixman_image_unref(p->output);
        p->output = NULL;
    }
    p->in_frame = false;
}

static const NikonDisplayOps s_metal_display_ops = {
    .name = "macOS Metal Hardware Pipeline (UMA)",
    .type = NIKON_DISPLAY_BACKEND_METAL,
    .init = metal_display_init,
    .cleanup = metal_display_cleanup,
    .begin_frame = metal_display_begin_frame,
    .composite_primary = metal_display_composite_primary,
    .composite_layer = metal_display_composite_layer,
    .fill_rectangles = metal_display_fill_rectangles,
    .present = metal_display_present,
    .end_frame = metal_display_end_frame,
};

const NikonDisplayOps *nikon_display_metal_get_ops(void)
{
    return &s_metal_display_ops;
}

#endif
