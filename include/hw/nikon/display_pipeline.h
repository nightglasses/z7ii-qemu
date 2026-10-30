/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Nikon Virtual Camera - Display & Graphics Hardware Abstraction Layer (HAL)
 *
 * Provides a unified abstraction for camera frame composition, color conversion,
 * layer compositing, and presentation, with pluggable backends:
 * - CPU Reference: Pixman-based portable software engine (fallback on all platforms)
 * - Metal: macOS Apple Silicon GPU compute and zero-copy rendering
 * - Direct3D 12: Windows GPU compute and direct surface presentation
 * - Vulkan: Cross-platform GPU compute pipeline
 */
#ifndef HW_NIKON_DISPLAY_PIPELINE_H
#define HW_NIKON_DISPLAY_PIPELINE_H

#include "qemu/osdep.h"
#include "ui/console.h"
#include <pixman.h>

typedef enum NikonDisplayBackendType {
    NIKON_DISPLAY_BACKEND_CPU = 0,
    NIKON_DISPLAY_BACKEND_METAL,
    NIKON_DISPLAY_BACKEND_D3D12,
    NIKON_DISPLAY_BACKEND_VULKAN,
    NIKON_DISPLAY_BACKEND_COUNT,
} NikonDisplayBackendType;

typedef struct NikonDisplayPipeline NikonDisplayPipeline;

typedef struct NikonDisplayPrimaryDesc {
    const uint8_t *luma;
    const uint8_t *chroma;
    unsigned width;
    unsigned height;
    unsigned pitch_y;
    unsigned pitch_uv;
    unsigned mode;
    unsigned alpha;
    int32_t matrix[2][3][3];
    bool apply_color_matrix;
    /* Scaling ratios */
    unsigned a, b, e, f;
    unsigned scaled_w, scaled_h;
} NikonDisplayPrimaryDesc;

typedef struct NikonDisplayLayerDesc {
    const uint8_t *primary;
    const uint8_t *secondary; /* Alpha plane for modes >= 5 */
    unsigned width;
    unsigned height;
    unsigned stride;
    unsigned alpha_stride;
    unsigned mode;
    uint32_t control;
    uint32_t lanes;
    int x, y;
    bool yuva;
} NikonDisplayLayerDesc;

typedef struct NikonDisplayRectDesc {
    unsigned x, y, w, h;
    uint32_t color_rgb; /* 0x00RRGGBB */
} NikonDisplayRectDesc;

typedef struct NikonDisplayOps {
    const char *name;
    NikonDisplayBackendType type;

    /* Initialize backend resources */
    bool (*init)(NikonDisplayPipeline *p);
    /* Release backend resources */
    void (*cleanup)(NikonDisplayPipeline *p);

    /* Prepare output frame canvas of size width x height */
    bool (*begin_frame)(NikonDisplayPipeline *p, unsigned width, unsigned height);

    /* Render primary Y/UV camera image onto output */
    bool (*composite_primary)(NikonDisplayPipeline *p,
                              pixman_image_t *output,
                              const NikonDisplayPrimaryDesc *desc,
                              int dest_x, int dest_y,
                              unsigned out_width, unsigned out_height);

    /* Composite an overlay layer (menus, icons, texts) */
    bool (*composite_layer)(NikonDisplayPipeline *p,
                            pixman_image_t *canvas,
                            const NikonDisplayLayerDesc *desc,
                            unsigned out_width, unsigned out_height);

    /* Draw graphic rectangles */
    bool (*fill_rectangles)(NikonDisplayPipeline *p,
                            pixman_image_t *output,
                            const NikonDisplayRectDesc *rects,
                            unsigned count,
                            unsigned width, unsigned height);

    /* Blit / Present output to QEMU DisplaySurface */
    void (*present)(NikonDisplayPipeline *p,
                    DisplaySurface *surface,
                    pixman_image_t *output,
                    unsigned width, unsigned height);

    /* Complete frame transaction */
    void (*end_frame)(NikonDisplayPipeline *p);
} NikonDisplayOps;

struct NikonDisplayPipeline {
    const NikonDisplayOps *ops;
    void *backend_state;
    pixman_image_t *output;
    unsigned width;
    unsigned height;
    bool in_frame;
};

/* Factory & Lifecycle */
NikonDisplayPipeline *nikon_display_pipeline_create(NikonDisplayBackendType type);
void nikon_display_pipeline_free(NikonDisplayPipeline *p);

/* Backend probing */
NikonDisplayBackendType nikon_display_detect_optimal_backend(void);
const char *nikon_display_backend_name(NikonDisplayBackendType type);
bool nikon_metal_is_supported(void);
const NikonDisplayOps *nikon_display_metal_get_ops(void);

/* High-level execution helpers */
bool nikon_display_pipeline_begin_frame(NikonDisplayPipeline *p,
                                        unsigned width, unsigned height);
bool nikon_display_pipeline_composite_primary(NikonDisplayPipeline *p,
                                              const NikonDisplayPrimaryDesc *desc,
                                              int dest_x, int dest_y,
                                              unsigned out_width, unsigned out_height);
bool nikon_display_pipeline_composite_layer(NikonDisplayPipeline *p,
                                            pixman_image_t *canvas,
                                            const NikonDisplayLayerDesc *desc,
                                            unsigned out_width, unsigned out_height);
bool nikon_display_pipeline_fill_rectangles(NikonDisplayPipeline *p,
                                            const NikonDisplayRectDesc *rects,
                                            unsigned count);
void nikon_display_pipeline_present(NikonDisplayPipeline *p, DisplaySurface *surface);
void nikon_display_pipeline_end_frame(NikonDisplayPipeline *p);

#endif /* HW_NIKON_DISPLAY_PIPELINE_H */
