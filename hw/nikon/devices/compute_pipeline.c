/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Nikon Virtual Camera - Image & ISP Compute Hardware Abstraction Layer (HAL)
 *
 * Implements compute pipeline dispatch and CPU reference operations.
 */

#include "qemu/osdep.h"
#include "hw/nikon/compute_pipeline.h"
#include "hw/nikon/gatepixels.h"

/* -------------------------------------------------------------------------
 * CPU Reference Compute Operations
 * ------------------------------------------------------------------------- */

static bool cpu_compute_init(NikonComputePipeline *p)
{
    (void)p;
    return true;
}

static void cpu_compute_cleanup(NikonComputePipeline *p)
{
    (void)p;
}

static bool cpu_compute_warp_nv12(NikonComputePipeline *p,
                                  const NikonGatePixels *params,
                                  uint8_t *output,
                                  size_t output_size)
{
    (void)p;
    return nikon_gate_pixels_nv12(params, output, output_size);
}

static bool cpu_compute_demosaic_bayer(NikonComputePipeline *p,
                                       const NikonComputeBayerDesc *desc)
{
    unsigned x, y;
    (void)p;

    if (!desc || !desc->raw || !desc->rgb || !desc->width || !desc->height ||
        (desc->width & 1) || (desc->height & 1)) {
        return false;
    }

    for (y = 0; y < desc->height; y += 2) {
        for (x = 0; x < desc->width; x += 2) {
            /* Standard RGGB quad:
             * R  Gr
             * Gb B
             */
            uint32_t r  = desc->raw[y * desc->width + x];
            uint32_t gr = desc->raw[y * desc->width + x + 1];
            uint32_t gb = desc->raw[(y + 1) * desc->width + x];
            uint32_t b  = desc->raw[(y + 1) * desc->width + x + 1];

            uint32_t g_avg = (gr + gb) >> 1;

            /* Apply white balance / color response calibration */
            if (desc->red_response) {
                r = (r * desc->red_response) >> 11;
            }
            if (desc->blue_response) {
                b = (b * desc->blue_response) >> 11;
            }

            uint32_t r8 = MIN(r >> 4, 255);
            uint32_t g8 = MIN(g_avg >> 4, 255);
            uint32_t b8 = MIN(b >> 4, 255);
            uint32_t argb = 0xff000000U | (r8 << 16) | (g8 << 8) | b8;

            /* Fill 2x2 output pixel block */
            desc->rgb[y * desc->width + x] = argb;
            desc->rgb[y * desc->width + x + 1] = argb;
            desc->rgb[(y + 1) * desc->width + x] = argb;
            desc->rgb[(y + 1) * desc->width + x + 1] = argb;
        }
    }
    return true;
}

static const NikonComputeOps cpu_compute_ops = {
    .name = "CPU Reference (Scalar / SIMD)",
    .type = NIKON_COMPUTE_BACKEND_CPU,
    .init = cpu_compute_init,
    .cleanup = cpu_compute_cleanup,
    .warp_nv12 = cpu_compute_warp_nv12,
    .demosaic_bayer = cpu_compute_demosaic_bayer,
};

/* -------------------------------------------------------------------------
 * Hardware Backend Registry & Placeholders
 * ------------------------------------------------------------------------- */

static const NikonComputeOps metal_compute_stub_ops = {
    .name = "macOS Metal Compute / Accelerate (Reserved)",
    .type = NIKON_COMPUTE_BACKEND_METAL,
    .init = cpu_compute_init,
    .cleanup = cpu_compute_cleanup,
    .warp_nv12 = cpu_compute_warp_nv12,
    .demosaic_bayer = cpu_compute_demosaic_bayer,
};

static const NikonComputeOps directml_compute_stub_ops = {
    .name = "Windows DirectML / DirectCompute (Reserved)",
    .type = NIKON_COMPUTE_BACKEND_DIRECTML,
    .init = cpu_compute_init,
    .cleanup = cpu_compute_cleanup,
    .warp_nv12 = cpu_compute_warp_nv12,
    .demosaic_bayer = cpu_compute_demosaic_bayer,
};

static const NikonComputeOps vulkan_compute_stub_ops = {
    .name = "Vulkan Compute (Reserved)",
    .type = NIKON_COMPUTE_BACKEND_VULKAN,
    .init = cpu_compute_init,
    .cleanup = cpu_compute_cleanup,
    .warp_nv12 = cpu_compute_warp_nv12,
    .demosaic_bayer = cpu_compute_demosaic_bayer,
};

const char *nikon_compute_backend_name(NikonComputeBackendType type)
{
    switch (type) {
    case NIKON_COMPUTE_BACKEND_CPU:
        return "cpu";
    case NIKON_COMPUTE_BACKEND_METAL:
        return "metal";
    case NIKON_COMPUTE_BACKEND_DIRECTML:
        return "directml";
    case NIKON_COMPUTE_BACKEND_VULKAN:
        return "vulkan";
    default:
        return "unknown";
    }
}

NikonComputeBackendType nikon_compute_detect_optimal_backend(void)
{
    const char *env = getenv("NIKON_COMPUTE_BACKEND");

    if (env) {
        if (!g_ascii_strcasecmp(env, "metal")) {
            return NIKON_COMPUTE_BACKEND_METAL;
        }
        if (!g_ascii_strcasecmp(env, "directml") || !g_ascii_strcasecmp(env, "dml")) {
            return NIKON_COMPUTE_BACKEND_DIRECTML;
        }
        if (!g_ascii_strcasecmp(env, "vulkan") || !g_ascii_strcasecmp(env, "vk")) {
            return NIKON_COMPUTE_BACKEND_VULKAN;
        }
        if (!g_ascii_strcasecmp(env, "cpu")) {
            return NIKON_COMPUTE_BACKEND_CPU;
        }
    }

#if defined(__APPLE__)
    return NIKON_COMPUTE_BACKEND_METAL;
#elif defined(_WIN32)
    return NIKON_COMPUTE_BACKEND_DIRECTML;
#else
    return NIKON_COMPUTE_BACKEND_CPU;
#endif
}

NikonComputePipeline *nikon_compute_pipeline_create(NikonComputeBackendType type)
{
    NikonComputePipeline *p = g_new0(NikonComputePipeline, 1);

    switch (type) {
    case NIKON_COMPUTE_BACKEND_METAL:
        p->ops = &metal_compute_stub_ops;
        break;
    case NIKON_COMPUTE_BACKEND_DIRECTML:
        p->ops = &directml_compute_stub_ops;
        break;
    case NIKON_COMPUTE_BACKEND_VULKAN:
        p->ops = &vulkan_compute_stub_ops;
        break;
    case NIKON_COMPUTE_BACKEND_CPU:
    default:
        p->ops = &cpu_compute_ops;
        break;
    }

    if (p->ops->init && !p->ops->init(p)) {
        if (p->ops->type != NIKON_COMPUTE_BACKEND_CPU) {
            p->ops = &cpu_compute_ops;
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

void nikon_compute_pipeline_free(NikonComputePipeline *p)
{
    if (!p) {
        return;
    }
    if (p->ops && p->ops->cleanup) {
        p->ops->cleanup(p);
    }
    g_free(p);
}

bool nikon_compute_pipeline_warp_nv12(NikonComputePipeline *p,
                                      const NikonGatePixels *params,
                                      uint8_t *output,
                                      size_t output_size)
{
    if (!p || !p->ops || !p->ops->warp_nv12) {
        return false;
    }
    return p->ops->warp_nv12(p, params, output, output_size);
}

bool nikon_compute_pipeline_demosaic_bayer(NikonComputePipeline *p,
                                          const NikonComputeBayerDesc *desc)
{
    if (!p || !p->ops || !p->ops->demosaic_bayer) {
        return false;
    }
    return p->ops->demosaic_bayer(p, desc);
}
