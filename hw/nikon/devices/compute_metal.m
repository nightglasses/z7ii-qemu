/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Nikon Virtual Camera - macOS Metal Image & ISP Compute Pipeline
 *
 * Implements GPU-accelerated Bayer CFA demosaicing and 4-corner affine grid
 * NV12 frame warping using Apple Silicon Metal Compute Shaders and UMA.
 */

#include "qemu/osdep.h"
#include "qemu/bswap.h"
#include "hw/nikon/compute_pipeline.h"
#include "hw/nikon/gatepixels.h"

#ifndef __APPLE__

const NikonComputeOps *nikon_compute_metal_get_ops(void)
{
    return NULL;
}

#else

#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#include <dlfcn.h>

typedef struct BayerParams {
    uint32_t width;
    uint32_t height;
    uint32_t red_response;
    uint32_t blue_response;
} BayerParams;

typedef struct WarpParams {
    uint32_t src_width;
    uint32_t src_height;
    uint32_t dst_width;
    uint32_t dst_height;
    uint32_t columns;
    uint32_t rows;
} WarpParams;

static const char *s_compute_shader_source =
"#include <metal_stdlib>\n"
"using namespace metal;\n"
"\n"
"struct BayerParams {\n"
"    uint32_t width;\n"
"    uint32_t height;\n"
"    uint32_t red_response;\n"
"    uint32_t blue_response;\n"
"};\n"
"\n"
"struct WarpParams {\n"
"    uint32_t src_width;\n"
"    uint32_t src_height;\n"
"    uint32_t dst_width;\n"
"    uint32_t dst_height;\n"
"    uint32_t columns;\n"
"    uint32_t rows;\n"
"};\n"
"\n"
"kernel void demosaic_bayer_kernel(\n"
"    device const uint16_t *raw        [[buffer(0)]],\n"
"    device uint32_t *output           [[buffer(1)]],\n"
"    constant BayerParams &params      [[buffer(2)]],\n"
"    uint2 gid                         [[thread_position_in_grid]])\n"
"{\n"
"    uint x = gid.x * 2;\n"
"    uint y = gid.y * 2;\n"
"    if (x >= params.width || y >= params.height) {\n"
"        return;\n"
"    }\n"
"\n"
"    uint32_t r  = raw[y * params.width + x];\n"
"    uint32_t gr = raw[y * params.width + x + 1];\n"
"    uint32_t gb = raw[(y + 1) * params.width + x];\n"
"    uint32_t b  = raw[(y + 1) * params.width + x + 1];\n"
"\n"
"    uint32_t g_avg = (gr + gb) >> 1;\n"
"\n"
"    if (params.red_response != 0) {\n"
"        r = (r * params.red_response) >> 11;\n"
"    }\n"
"    if (params.blue_response != 0) {\n"
"        b = (b * params.blue_response) >> 11;\n"
"    }\n"
"\n"
"    uint32_t r8 = min(r >> 4, 255u);\n"
"    uint32_t g8 = min(g_avg >> 4, 255u);\n"
"    uint32_t b8 = min(b >> 4, 255u);\n"
"    uint32_t argb = 0xff000000u | (r8 << 16) | (g8 << 8) | b8;\n"
"\n"
"    output[y * params.width + x] = argb;\n"
"    output[y * params.width + x + 1] = argb;\n"
"    output[(y + 1) * params.width + x] = argb;\n"
"    output[(y + 1) * params.width + x + 1] = argb;\n"
"}\n"
"\n"
"static inline int32_t gate_pixels_round_gpu(int64_t sum, uint shift)\n"
"{\n"
"    uint64_t magnitude = sum < 0 ? 0ULL - (uint64_t)sum : (uint64_t)sum;\n"
"    int32_t rounded = int32_t((magnitude + (1ULL << (shift - 1))) >> shift);\n"
"    return sum < 0 ? -rounded : rounded;\n"
"}\n"
"\n"
"static inline uint8_t gate_pixels_sample_gpu(device const uint8_t *plane,\n"
"                                             uint width, uint height,\n"
"                                             uint stride, uint step,\n"
"                                             uint component,\n"
"                                             int32_t x, int32_t y,\n"
"                                             uint bits)\n"
"{\n"
"    uint unit = 1u << bits;\n"
"    int32_t max_x = int32_t((width - 1) * unit);\n"
"    int32_t max_y = int32_t((height - 1) * unit);\n"
"    x = clamp(x, 0, max_x);\n"
"    y = clamp(y, 0, max_y);\n"
"    uint ix = uint(x) >> bits;\n"
"    uint iy = uint(y) >> bits;\n"
"    uint fx = uint(x) & (unit - 1);\n"
"    uint fy = uint(y) & (unit - 1);\n"
"    uint nx = min(ix + 1, width - 1);\n"
"    uint ny = min(iy + 1, height - 1);\n"
"    uint64_t s00 = uint64_t(plane[iy * stride + ix * step + component]);\n"
"    uint64_t s10 = uint64_t(plane[iy * stride + nx * step + component]);\n"
"    uint64_t s01 = uint64_t(plane[ny * stride + ix * step + component]);\n"
"    uint64_t s11 = uint64_t(plane[ny * stride + nx * step + component]);\n"
"    uint64_t sum = s00 * (unit - fx) * (unit - fy) +\n"
"                   s10 * fx * (unit - fy) +\n"
"                   s01 * (unit - fx) * fy +\n"
"                   s11 * fx * fy;\n"
"    return uint8_t((sum + (1ULL << (2 * bits - 1))) >> (2 * bits));\n"
"}\n"
"\n"
"kernel void warp_nv12_kernel(\n"
"    device const uint8_t *input       [[buffer(0)]],\n"
"    device const uint8_t *records     [[buffer(1)]],\n"
"    device uint8_t *output            [[buffer(2)]],\n"
"    constant WarpParams &params       [[buffer(3)]],\n"
"    uint2 gid                         [[thread_position_in_grid]])\n"
"{\n"
"    uint x = gid.x;\n"
"    uint y = gid.y;\n"
"    if (x >= params.dst_width || y >= params.dst_height) {\n"
"        return;\n"
"    }\n"
"\n"
"    uint tile_col = x / 64;\n"
"    uint tile_row = y / 8;\n"
"    uint tile_idx = tile_row * params.columns + tile_col;\n"
"    device const uint8_t *rec = records + tile_idx * 40;\n"
"    device const uint32_t *rec32 = (device const uint32_t *)rec;\n"
"\n"
"    uint exponent = rec32[8] & 15u;\n"
"    uint tile_w = 1u << exponent;\n"
"\n"
"    int64_t base[2], step[2], base_dy[2], step_dy[2];\n"
"    for (uint axis = 0; axis < 2; axis++) {\n"
"        int32_t c[4];\n"
"        for (uint corner = 0; corner < 4; corner++) {\n"
"            uint32_t word = rec32[corner * 2 + axis];\n"
"            c[corner] = int32_t(word ^ 0x400000) - 0x400000;\n"
"        }\n"
"        base[axis] = (int64_t)tile_w * c[0] * 8;\n"
"        step[axis] = (int64_t)(c[1] - c[0]) * 8;\n"
"        base_dy[axis] = (int64_t)tile_w * (c[2] - c[0]);\n"
"        step_dy[axis] = (c[3] - c[2]) - (c[1] - c[0]);\n"
"    }\n"
"\n"
"    uint row_in_tile = y - tile_row * 8;\n"
"    uint col_in_tile = x - tile_col * 64;\n"
"\n"
"    int64_t cur_base_x = base[0] + (int64_t)row_in_tile * base_dy[0];\n"
"    int64_t cur_step_x = step[0] + (int64_t)row_in_tile * step_dy[0];\n"
"    int64_t nx = cur_base_x + (int64_t)col_in_tile * cur_step_x;\n"
"\n"
"    int64_t cur_base_y = base[1] + (int64_t)row_in_tile * base_dy[1];\n"
"    int64_t cur_step_y = step[1] + (int64_t)row_in_tile * step_dy[1];\n"
"    int64_t ny = cur_base_y + (int64_t)col_in_tile * cur_step_y;\n"
"\n"
"    int32_t sx = gate_pixels_round_gpu(nx, exponent + 3);\n"
"    int32_t sy = gate_pixels_round_gpu(ny, exponent + 3);\n"
"\n"
"    output[y * params.dst_width + x] = gate_pixels_sample_gpu(\n"
"        input, params.src_width, params.src_height, params.src_width, 1, 0, sx, sy, 8);\n"
"\n"
"    if (!((x | y) & 1u)) {\n"
"        uint input_y = params.src_width * params.src_height;\n"
"        uint output_y = params.dst_width * params.dst_height;\n"
"        device const uint8_t *chroma = input + input_y;\n"
"        output[output_y + (y / 2) * params.dst_width + x] =\n"
"            gate_pixels_sample_gpu(chroma, params.src_width / 2, params.src_height / 2, params.src_width, 2, 0, sx, sy, 9);\n"
"        output[output_y + (y / 2) * params.dst_width + x + 1] =\n"
"            gate_pixels_sample_gpu(chroma, params.src_width / 2, params.src_height / 2, params.src_width, 2, 1, sx, sy, 9);\n"
"    }\n"
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

typedef struct MetalComputeBackendState {
    id<MTLDevice> device;
    id<MTLCommandQueue> command_queue;
    id<MTLComputePipelineState> warp_pipeline;
    id<MTLComputePipelineState> demosaic_pipeline;

    id<MTLBuffer> warp_in_buf;
    size_t warp_in_buf_size;
    id<MTLBuffer> warp_rec_buf;
    size_t warp_rec_buf_size;
    id<MTLBuffer> warp_out_buf;
    size_t warp_out_buf_size;

    id<MTLBuffer> demosaic_raw_buf;
    size_t demosaic_raw_buf_size;
    id<MTLBuffer> demosaic_rgb_buf;
    size_t demosaic_rgb_buf_size;
} MetalComputeBackendState;

static bool metal_compute_init(NikonComputePipeline *p)
{
    if (!init_metal_runtime() || !s_create_default_device) {
        return false;
    }

    @autoreleasepool {
        MetalComputeBackendState *state = g_new0(MetalComputeBackendState, 1);
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
        NSString *source = [NSString stringWithUTF8String:s_compute_shader_source];
        id<MTLLibrary> library = [state->device newLibraryWithSource:source
                                                             options:nil
                                                               error:&error];
        if (!library || error) {
            g_free(state);
            return false;
        }

        id<MTLFunction> demosaic_fn = [library newFunctionWithName:@"demosaic_bayer_kernel"];
        id<MTLFunction> warp_fn = [library newFunctionWithName:@"warp_nv12_kernel"];
        if (!demosaic_fn || !warp_fn) {
            g_free(state);
            return false;
        }

        state->demosaic_pipeline = [state->device newComputePipelineStateWithFunction:demosaic_fn
                                                                                error:&error];
        if (!state->demosaic_pipeline || error) {
            g_free(state);
            return false;
        }

        state->warp_pipeline = [state->device newComputePipelineStateWithFunction:warp_fn
                                                                            error:&error];
        if (!state->warp_pipeline || error) {
            g_free(state);
            return false;
        }

        p->backend_state = state;
        return true;
    }
}

static void metal_compute_cleanup(NikonComputePipeline *p)
{
    if (!p) {
        return;
    }
    @autoreleasepool {
        MetalComputeBackendState *state = (MetalComputeBackendState *)p->backend_state;
        if (state) {
            state->warp_in_buf = nil;
            state->warp_rec_buf = nil;
            state->warp_out_buf = nil;
            state->demosaic_raw_buf = nil;
            state->demosaic_rgb_buf = nil;
            state->warp_pipeline = nil;
            state->demosaic_pipeline = nil;
            state->command_queue = nil;
            state->device = nil;
            g_free(state);
            p->backend_state = NULL;
        }
    }
}

static bool gate_pixels_overlap(const void *a, size_t an,
                                const void *b, size_t bn)
{
    uintptr_t x = (uintptr_t)a, y = (uintptr_t)b;

    return x > UINTPTR_MAX - an || y > UINTPTR_MAX - bn ||
           (x < y + bn && y < x + an);
}

static bool metal_compute_warp_nv12(NikonComputePipeline *p,
                                    const NikonGatePixels *params,
                                    uint8_t *output,
                                    size_t output_size)
{
    size_t input_y, output_y, input_bytes, output_bytes, grid_bytes;
    unsigned tail_exponent = 3;

    if (!p || !p->backend_state || !params || !params->input || !params->records || !output ||
        !params->src_width || !params->src_height ||
        !params->dst_width || !params->dst_height ||
        ((params->src_width | params->src_height | params->dst_width | params->dst_height) & 1) ||
        params->src_width > 4096 || params->src_height > 4096 ||
        params->dst_width > 4096 || params->dst_height > 4096 ||
        !params->columns || params->columns > 64 || !params->rows || params->rows > 512 ||
        params->columns != DIV_ROUND_UP(params->dst_width, 64) ||
        params->dst_height > params->rows * 8) {
        return false;
    }
    input_y = (size_t)params->src_width * params->src_height;
    output_y = (size_t)params->dst_width * params->dst_height;
    input_bytes = input_y + input_y / 2;
    output_bytes = output_y + output_y / 2;
    grid_bytes = (size_t)params->columns * params->rows * 40;
    while ((1U << tail_exponent) < params->dst_width - (params->columns - 1) * 64) {
        tail_exponent++;
    }
    if (params->input_size != input_bytes || output_size != output_bytes ||
        params->records_size < grid_bytes ||
        gate_pixels_overlap(output, output_bytes, params->input, input_bytes) ||
        gate_pixels_overlap(output, output_bytes, params->records, grid_bytes)) {
        return false;
    }
    for (size_t offset = 0; offset < grid_bytes; offset += 40) {
        unsigned column = (offset / 40) % params->columns;
        unsigned exponent = column + 1 == params->columns ? tail_exponent : 6;

        if (ldl_le_p(params->records + offset + 32) != (0x300 | exponent)) {
            return false;
        }
        for (unsigned i = 0; i < 8; i++) {
            if (ldl_le_p(params->records + offset + 4 * i) & 0xff800000U) {
                return false;
            }
        }
    }

    @autoreleasepool {
        MetalComputeBackendState *state = (MetalComputeBackendState *)p->backend_state;
        id<MTLDevice> dev = state->device;

        /* Ensure input buffer */
        if (!state->warp_in_buf || state->warp_in_buf_size < input_bytes) {
            state->warp_in_buf = [dev newBufferWithLength:input_bytes options:MTLResourceStorageModeShared];
            state->warp_in_buf_size = input_bytes;
        }
        memcpy([state->warp_in_buf contents], params->input, input_bytes);

        /* Ensure records buffer */
        if (!state->warp_rec_buf || state->warp_rec_buf_size < grid_bytes) {
            state->warp_rec_buf = [dev newBufferWithLength:grid_bytes options:MTLResourceStorageModeShared];
            state->warp_rec_buf_size = grid_bytes;
        }
        memcpy([state->warp_rec_buf contents], params->records, grid_bytes);

        /* Ensure output buffer */
        if (!state->warp_out_buf || state->warp_out_buf_size < output_bytes) {
            state->warp_out_buf = [dev newBufferWithLength:output_bytes options:MTLResourceStorageModeShared];
            state->warp_out_buf_size = output_bytes;
        }

        WarpParams wparams = {
            .src_width = params->src_width,
            .src_height = params->src_height,
            .dst_width = params->dst_width,
            .dst_height = params->dst_height,
            .columns = params->columns,
            .rows = params->rows,
        };

        id<MTLCommandBuffer> cmd_buf = [state->command_queue commandBuffer];
        id<MTLComputeCommandEncoder> encoder = [cmd_buf computeCommandEncoder];

        [encoder setComputePipelineState:state->warp_pipeline];
        [encoder setBuffer:state->warp_in_buf offset:0 atIndex:0];
        [encoder setBuffer:state->warp_rec_buf offset:0 atIndex:1];
        [encoder setBuffer:state->warp_out_buf offset:0 atIndex:2];
        [encoder setBytes:&wparams length:sizeof(wparams) atIndex:3];

        MTLSize grid_size = MTLSizeMake(params->dst_width, params->dst_height, 1);
        NSUInteger thread_w = MIN((NSUInteger)16, state->warp_pipeline.maxTotalThreadsPerThreadgroup);
        NSUInteger thread_h = MIN((NSUInteger)16, state->warp_pipeline.maxTotalThreadsPerThreadgroup / thread_w);
        MTLSize threadgroup_size = MTLSizeMake(thread_w, thread_h, 1);

        [encoder dispatchThreads:grid_size threadsPerThreadgroup:threadgroup_size];
        [encoder endEncoding];

        [cmd_buf commit];
        [cmd_buf waitUntilCompleted];

        memcpy(output, [state->warp_out_buf contents], output_bytes);
        return true;
    }
}

static bool metal_compute_demosaic_bayer(NikonComputePipeline *p,
                                        const NikonComputeBayerDesc *desc)
{
    if (!p || !p->backend_state || !desc || !desc->raw || !desc->rgb ||
        !desc->width || !desc->height || (desc->width & 1) || (desc->height & 1)) {
        return false;
    }

    size_t raw_bytes = (size_t)desc->width * desc->height * sizeof(uint16_t);
    size_t rgb_bytes = (size_t)desc->width * desc->height * sizeof(uint32_t);

    @autoreleasepool {
        MetalComputeBackendState *state = (MetalComputeBackendState *)p->backend_state;
        id<MTLDevice> dev = state->device;

        /* Ensure raw buffer */
        if (!state->demosaic_raw_buf || state->demosaic_raw_buf_size < raw_bytes) {
            state->demosaic_raw_buf = [dev newBufferWithLength:raw_bytes options:MTLResourceStorageModeShared];
            state->demosaic_raw_buf_size = raw_bytes;
        }
        memcpy([state->demosaic_raw_buf contents], desc->raw, raw_bytes);

        /* Ensure rgb buffer */
        if (!state->demosaic_rgb_buf || state->demosaic_rgb_buf_size < rgb_bytes) {
            state->demosaic_rgb_buf = [dev newBufferWithLength:rgb_bytes options:MTLResourceStorageModeShared];
            state->demosaic_rgb_buf_size = rgb_bytes;
        }

        BayerParams bparams = {
            .width = desc->width,
            .height = desc->height,
            .red_response = desc->red_response,
            .blue_response = desc->blue_response,
        };

        id<MTLCommandBuffer> cmd_buf = [state->command_queue commandBuffer];
        id<MTLComputeCommandEncoder> encoder = [cmd_buf computeCommandEncoder];

        [encoder setComputePipelineState:state->demosaic_pipeline];
        [encoder setBuffer:state->demosaic_raw_buf offset:0 atIndex:0];
        [encoder setBuffer:state->demosaic_rgb_buf offset:0 atIndex:1];
        [encoder setBytes:&bparams length:sizeof(bparams) atIndex:2];

        /* Thread position in grid corresponds to 2x2 blocks */
        MTLSize grid_size = MTLSizeMake(desc->width / 2, desc->height / 2, 1);
        NSUInteger thread_w = MIN((NSUInteger)16, state->demosaic_pipeline.maxTotalThreadsPerThreadgroup);
        NSUInteger thread_h = MIN((NSUInteger)16, state->demosaic_pipeline.maxTotalThreadsPerThreadgroup / thread_w);
        MTLSize threadgroup_size = MTLSizeMake(thread_w, thread_h, 1);

        [encoder dispatchThreads:grid_size threadsPerThreadgroup:threadgroup_size];
        [encoder endEncoding];

        [cmd_buf commit];
        [cmd_buf waitUntilCompleted];

        memcpy(desc->rgb, [state->demosaic_rgb_buf contents], rgb_bytes);
        return true;
    }
}

static const NikonComputeOps metal_compute_ops = {
    .name = "macOS Metal Compute (Apple Silicon GPU)",
    .type = NIKON_COMPUTE_BACKEND_METAL,
    .init = metal_compute_init,
    .cleanup = metal_compute_cleanup,
    .warp_nv12 = metal_compute_warp_nv12,
    .demosaic_bayer = metal_compute_demosaic_bayer,
};

const NikonComputeOps *nikon_compute_metal_get_ops(void)
{
    if (init_metal_runtime()) {
        return &metal_compute_ops;
    }
    return NULL;
}

#endif
