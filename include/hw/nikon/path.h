/* Finite affine path mask for the known RGBA720 and conversion contexts.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef HW_NIKON_PATH_H
#define HW_NIKON_PATH_H

#define NIKON_PATH_WIDTH 720U
#define NIKON_PATH_HEIGHT 480U
#define NIKON_PATH_MASK_BYTES (NIKON_PATH_WIDTH * NIKON_PATH_HEIGHT)
/* Functional work bounds, not recovered silicon capacities. */
#define NIKON_PATH_MAX_SEGMENTS 64U
#define NIKON_PATH_MAX_COORDINATES 256U
#define NIKON_PATH_MAX_BATCH 256U

typedef struct NikonPathStroke {
    float radius, extent; /* Object-space half width and tangent miter extent. */
    unsigned cap, join;
} NikonPathStroke;

/* NULL mask validates only. NULL stroke fills; otherwise open contours are valid.
 * Non-NULL mask receives SRC_OVER coverage, with stroke widths in object space.
 */
bool nikon_path_mask(uint8_t *mask, unsigned width, unsigned height,
                     const uint8_t *segments,
                     unsigned segment_count, const uint32_t *coordinates,
                     unsigned coordinate_count, const uint32_t matrix[9],
                     bool nonzero, const NikonPathStroke *stroke,
                     const char **reason);

/* Finite A8 image modulation. NULL overlay validates only the sampler. */
bool nikon_path_image(uint8_t *overlay, const uint8_t *mask,
                      const uint8_t *alpha, unsigned width, unsigned height,
                      const uint32_t sample[9], uint32_t paint,
                      const char **reason);

#endif
