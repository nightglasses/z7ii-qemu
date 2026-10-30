/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Nikon Virtual Camera - Image & ISP Compute Hardware Abstraction Layer (HAL)
 *
 * Provides a unified abstraction for heavy camera image algorithms, affine grid
 * warping, and sensor demosaicing across heterogeneous compute backends:
 * - CPU Reference: Portable SIMD/scalar C implementation
 * - Metal Compute / Accelerate: macOS Apple Silicon GPU & vImage/NEON acceleration
 * - DirectML / DirectCompute: Windows GPU shader acceleration
 * - Vulkan Compute: Cross-platform GPU compute pipeline
 */
#ifndef HW_NIKON_COMPUTE_PIPELINE_H
#define HW_NIKON_COMPUTE_PIPELINE_H

#include "qemu/osdep.h"
#include "hw/nikon/gatepixels.h"

typedef enum NikonComputeBackendType {
    NIKON_COMPUTE_BACKEND_CPU = 0,
    NIKON_COMPUTE_BACKEND_METAL,
    NIKON_COMPUTE_BACKEND_DIRECTML,
    NIKON_COMPUTE_BACKEND_VULKAN,
    NIKON_COMPUTE_BACKEND_COUNT,
} NikonComputeBackendType;

typedef struct NikonComputeBayerDesc {
    const uint16_t *raw;
    uint32_t *rgb;
    unsigned width;
    unsigned height;
    unsigned red_response;
    unsigned blue_response;
} NikonComputeBayerDesc;

typedef struct NikonComputePipeline NikonComputePipeline;

typedef struct NikonComputeOps {
    const char *name;
    NikonComputeBackendType type;

    /* Initialize backend compute resources */
    bool (*init)(NikonComputePipeline *p);
    /* Clean up backend compute resources */
    void (*cleanup)(NikonComputePipeline *p);

    /* Affine grid four-corner NV12 warp (Gatework lens & geometric correction) */
    bool (*warp_nv12)(NikonComputePipeline *p,
                      const NikonGatePixels *params,
                      uint8_t *output,
                      size_t output_size);

    /* Sensor Bayer demosaicing & color response calculation */
    bool (*demosaic_bayer)(NikonComputePipeline *p,
                           const NikonComputeBayerDesc *desc);
} NikonComputeOps;

struct NikonComputePipeline {
    const NikonComputeOps *ops;
    void *backend_state;
};

/* Factory & Lifecycle */
NikonComputePipeline *nikon_compute_pipeline_create(NikonComputeBackendType type);
void nikon_compute_pipeline_free(NikonComputePipeline *p);

/* Backend probing */
NikonComputeBackendType nikon_compute_detect_optimal_backend(void);
const char *nikon_compute_backend_name(NikonComputeBackendType type);

/* High-level execution helpers */
bool nikon_compute_pipeline_warp_nv12(NikonComputePipeline *p,
                                      const NikonGatePixels *params,
                                      uint8_t *output,
                                      size_t output_size);
bool nikon_compute_pipeline_demosaic_bayer(NikonComputePipeline *p,
                                          const NikonComputeBayerDesc *desc);

#endif /* HW_NIKON_COMPUTE_PIPELINE_H */
