/* C1.70 MOVE/LINE/QUAD/CLOSE and relative H/V with affine fill and stroke.
 * Cairo supplies rasterization; no host font or text participates.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "qemu/bswap.h"
#include "hw/nikon/path.h"
#include <cairo.h>
#include <math.h>

static double nikon_path_float(const uint32_t *word)
{
    uint32_t bits = le32_to_cpu(*word);
    float value;

    memcpy(&value, &bits, sizeof(value));
    return value;
}

static bool nikon_path_point(const double m[9], double u, double v,
                             double *x, double *y)
{
    *x = m[0] * u + m[3] * v + m[6];
    *y = m[1] * u + m[4] * v + m[7];
    return isfinite(u) && isfinite(v) && fabs(u) <= 4096 && fabs(v) <= 4096 &&
           isfinite(*x) && isfinite(*y) && fabs(*x) <= 8192 && fabs(*y) <= 8192;
}

bool nikon_path_mask(uint8_t *mask, unsigned width, unsigned height,
                     const uint8_t *segments,
                     unsigned segment_count, const uint32_t *coordinates,
                     unsigned coordinate_count, const uint32_t matrix[9],
                     bool nonzero, const NikonPathStroke *stroke,
                     const char **reason)
{
    double m[9], points[NIKON_PATH_MAX_COORDINATES];
    double x = 0, y = 0, u = 0, v = 0;
    unsigned at = 0;
    bool open = false, success = false;
    cairo_surface_t *surface = NULL;
    cairo_t *cr = NULL;

    if (!((width == NIKON_PATH_WIDTH && height == NIKON_PATH_HEIGHT) ||
          (width == 1024 && height == 688)) ||
        !segment_count || segment_count > NIKON_PATH_MAX_SEGMENTS ||
        !coordinate_count || coordinate_count > NIKON_PATH_MAX_COORDINATES) {
        *reason = "transfer-path-count-not-supported";
        return false;
    }
    for (unsigned i = 0; i < 9; i++) {
        m[i] = nikon_path_float(&matrix[i]);
        if (!isfinite(m[i]) || fabs(m[i]) > 4096) {
            *reason = "transfer-path-transform-not-supported";
            return false;
        }
    }
    if (m[2] != 0 || m[5] != 0 || m[8] != 1 ||
        m[0] * m[4] - m[1] * m[3] == 0) {
        *reason = "transfer-path-transform-not-supported";
        return false;
    }
    if (stroke && (!isfinite(stroke->radius) || stroke->radius < 0 ||
                   stroke->radius > 4096 || !isfinite(stroke->extent) ||
                   stroke->extent < 0 || stroke->extent > 4096 ||
                   stroke->cap > 2 || stroke->join > 2)) {
        *reason = "transfer-path-stroke-not-supported";
        return false;
    }
    for (unsigned i = 0; i < coordinate_count; i++) {
        points[i] = nikon_path_float(&coordinates[i]);
        if (!isfinite(points[i]) || fabs(points[i]) > 4096) {
            *reason = "transfer-path-coordinate-not-supported";
            return false;
        }
    }
    if (mask) {
        surface = cairo_image_surface_create_for_data(mask, CAIRO_FORMAT_A8,
                      width, height, width);
        cr = cairo_create(surface);
        cairo_set_fill_rule(cr, nonzero ? CAIRO_FILL_RULE_WINDING :
                                         CAIRO_FILL_RULE_EVEN_ODD);
        cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
        cairo_set_source_rgba(cr, 0, 0, 0, 1);
        cairo_set_antialias(cr, CAIRO_ANTIALIAS_GRAY);
        /* Functional coverage tolerance, not a physical GPU precision claim. */
        cairo_set_tolerance(cr, 1.0 / 16);
    }
    for (unsigned i = 0; i < segment_count; i++) {
        unsigned operands;
        double end_x = x, end_y = y, control_x = 0, control_y = 0;

        switch (segments[i]) {
        case 0:
            operands = 0;
            break;
        case 2:
        case 4:
            operands = 2;
            break;
        case 10:
            operands = 4;
            break;
        case 7:
        case 9:
            operands = 1;
            break;
        default:
            *reason = "transfer-path-segment-not-supported";
            goto out;
        }
        if (at + operands > coordinate_count ||
            (segments[i] == 2 ? open && !stroke : !open)) {
            *reason = "transfer-path-topology-not-supported";
            goto out;
        }
        if (operands) {
            if (segments[i] == 7) {
                u += points[at];
            } else if (segments[i] == 9) {
                v += points[at];
            } else {
                u = points[at + operands - 2];
                v = points[at + operands - 1];
            }
            if (!nikon_path_point(m, u, v, &end_x, &end_y) ||
                (segments[i] == 10 &&
                 !nikon_path_point(m, points[at], points[at + 1],
                                   &control_x, &control_y))) {
                *reason = "transfer-path-coordinate-not-supported";
                goto out;
            }
        }
        switch (segments[i]) {
        case 0:
            if (cr) {
                cairo_close_path(cr);
            }
            open = false;
            break;
        case 2:
            if (cr) {
                cairo_move_to(cr, end_x, end_y);
            }
            open = true;
            break;
        case 4:
        case 7:
        case 9:
            if (cr) {
                cairo_line_to(cr, end_x, end_y);
            }
            break;
        case 10:
            if (cr) {
                cairo_curve_to(cr,
                    x + (control_x - x) * (2.0 / 3),
                    y + (control_y - y) * (2.0 / 3),
                    end_x + (control_x - end_x) * (2.0 / 3),
                    end_y + (control_y - end_y) * (2.0 / 3), end_x, end_y);
            }
            break;
        }
        if (operands) {
            x = end_x;
            y = end_y;
        }
        at += operands;
    }
    if ((open && !stroke) || at != coordinate_count) {
        *reason = "transfer-path-topology-not-supported";
        goto out;
    }
    if (cr) {
        if (stroke) {
            static const cairo_line_cap_t caps[] = {
                CAIRO_LINE_CAP_BUTT, CAIRO_LINE_CAP_ROUND, CAIRO_LINE_CAP_SQUARE,
            };
            static const cairo_line_join_t joins[] = {
                CAIRO_LINE_JOIN_MITER, CAIRO_LINE_JOIN_ROUND, CAIRO_LINE_JOIN_BEVEL,
            };
            cairo_matrix_t transform;

            /* The path is in device space; the pen still scales in user space. */
            cairo_matrix_init(&transform, m[0], m[1], m[3], m[4], m[6], m[7]);
            cairo_set_matrix(cr, &transform);
            cairo_set_line_width(cr, 2.0 * stroke->radius);
            cairo_set_line_cap(cr, caps[stroke->cap]);
            cairo_set_line_join(cr, joins[stroke->join]);
            cairo_set_miter_limit(cr, stroke->radius ?
                hypot(stroke->radius, stroke->extent) / stroke->radius : 1);
            cairo_stroke(cr);
        } else {
            cairo_fill(cr);
        }
        cairo_surface_flush(surface);
        if (cairo_status(cr) != CAIRO_STATUS_SUCCESS ||
            cairo_surface_status(surface) != CAIRO_STATUS_SUCCESS) {
            *reason = "transfer-path-rasterization-failed";
            goto out;
        }
    }
    success = true;
out:
    if (cr) {
        cairo_destroy(cr);
        cairo_surface_destroy(surface);
    }
    return success;
}

bool nikon_path_image(uint8_t *overlay, const uint8_t *mask,
                      const uint8_t *alpha, unsigned width, unsigned height,
                      const uint32_t sample[9], uint32_t paint,
                      const char **reason)
{
    double m[9];

    for (unsigned i = 0; i < 9; i++) {
        uint32_t word = le32_to_cpu(sample[i]);

        if (word >> 24) {
            *reason = "transfer-image-sampler-not-supported";
            return false;
        }
        m[i] = !(word & 0x7fffff) ? 0 :
            (word & 0x800000 ? -1 : 1) *
            ldexp(1.0 + (word & 65535) / 65536.0,
                  ((word >> 16) & 127) - 63);
        if (!isfinite(m[i]) || fabs(m[i]) > 4096) {
            *reason = "transfer-image-sampler-not-supported";
            return false;
        }
    }
    if (!width || width > 256 || !height || height > 256 ||
        (paint >> 24) != 255 || m[6] != 0 || m[7] != 0 || m[8] != 1 ||
        m[0] * m[4] - m[1] * m[3] == 0) {
        *reason = "transfer-image-sampler-not-supported";
        return false;
    }
    if (!overlay) {
        return true;
    }
    for (unsigned y = 0; y < NIKON_PATH_HEIGHT; y++) {
        for (unsigned x = 0; x < NIKON_PATH_WIDTH; x++) {
            unsigned p = y * NIKON_PATH_WIDTH + x;
            double u, v;
            unsigned tx, ty, coverage;

            if (!mask[p]) {
                continue;
            }
            u = (m[0] * (x + 0.5) + m[1] * (y + 0.5) + m[2]) * width;
            v = (m[3] * (x + 0.5) + m[4] * (y + 0.5) + m[5]) * height;
            /* Functional nearest/clamp sampling, as in the fixed Z7 source. */
            tx = floor(CLAMP(u, 0, width - 1));
            ty = floor(CLAMP(v, 0, height - 1));
            coverage = (mask[p] * alpha[ty * width + tx] + 127) / 255;
            for (unsigned c = 0; c < 3; c++) {
                overlay[4 * p + c] =
                    (((paint >> (8 * c)) & 255) * coverage + 127) / 255;
            }
            overlay[4 * p + 3] = coverage;
        }
    }
    return true;
}
