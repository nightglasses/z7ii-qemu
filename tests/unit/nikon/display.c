/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Unit tests for Nikon Display & Graphics Hardware Abstraction Layer (HAL)
 */

#include "qemu/osdep.h"
#include "hw/nikon/display_pipeline.h"
#include "ui/surface.h"

int (*qemu_main)(void);

static void test_backend_detection_and_names(void)
{
    g_assert_cmpstr(nikon_display_backend_name(NIKON_DISPLAY_BACKEND_CPU), ==, "cpu");
    g_assert_cmpstr(nikon_display_backend_name(NIKON_DISPLAY_BACKEND_METAL), ==, "metal");
    g_assert_cmpstr(nikon_display_backend_name(NIKON_DISPLAY_BACKEND_D3D12), ==, "d3d12");
    g_assert_cmpstr(nikon_display_backend_name(NIKON_DISPLAY_BACKEND_VULKAN), ==, "vulkan");
    g_assert_cmpstr(nikon_display_backend_name(NIKON_DISPLAY_BACKEND_COUNT), ==, "unknown");

    /* Optimal backend matches host capabilities */
    NikonDisplayBackendType optimal = nikon_display_detect_optimal_backend();
#if defined(__APPLE__)
    if (nikon_metal_is_supported()) {
        g_assert_cmpint(optimal, ==, NIKON_DISPLAY_BACKEND_METAL);
    } else {
        g_assert_cmpint(optimal, ==, NIKON_DISPLAY_BACKEND_CPU);
    }
#else
    g_assert_cmpint(optimal, ==, NIKON_DISPLAY_BACKEND_CPU);
#endif
}

static void test_pipeline_lifecycle(void)
{
    NikonDisplayPipeline *p = nikon_display_pipeline_create(NIKON_DISPLAY_BACKEND_CPU);
    g_assert_nonnull(p);
    g_assert_nonnull(p->ops);
    g_assert_cmpint(p->ops->type, ==, NIKON_DISPLAY_BACKEND_CPU);

    /* Free safely */
    nikon_display_pipeline_free(p);

    /* Freeing NULL must not crash */
    nikon_display_pipeline_free(NULL);

    /* Creating fallback backend */
    p = nikon_display_pipeline_create((NikonDisplayBackendType)999);
    g_assert_nonnull(p);
    g_assert_cmpint(p->ops->type, ==, NIKON_DISPLAY_BACKEND_CPU);
    nikon_display_pipeline_free(p);
}

static void test_frame_bounds_and_lifecycle(void)
{
    NikonDisplayPipeline *p = nikon_display_pipeline_create(NIKON_DISPLAY_BACKEND_CPU);
    g_assert_nonnull(p);

    /* Valid frame dimensions */
    g_assert_true(nikon_display_pipeline_begin_frame(p, 1024, 682));
    g_assert_cmpuint(p->width, ==, 1024);
    g_assert_cmpuint(p->height, ==, 682);
    g_assert_true(p->in_frame);

    /* End frame cleanly resets in_frame state */
    nikon_display_pipeline_end_frame(p);
    g_assert_false(p->in_frame);

    /* Negative / boundary tests: zero dimensions must fail */
    g_assert_false(nikon_display_pipeline_begin_frame(p, 0, 682));
    g_assert_false(nikon_display_pipeline_begin_frame(p, 1024, 0));

    /* Negative: exceeds MAX_FRAME_PIXELS (2048 x 2048) */
    g_assert_false(nikon_display_pipeline_begin_frame(p, 4096, 4096));

    nikon_display_pipeline_free(p);
}

static void test_layer_composite(void)
{
    NikonDisplayPipeline *p = nikon_display_pipeline_create(NIKON_DISPLAY_BACKEND_CPU);
    pixman_image_t *canvas;
    uint32_t *canvas_data;
    uint8_t layer_data[16 * 16 * 4];
    NikonDisplayLayerDesc desc;

    g_assert_nonnull(p);
    canvas = pixman_image_create_bits(PIXMAN_a8r8g8b8, 32, 32, NULL, 0);
    g_assert_nonnull(canvas);

    /* Fill layer data with test opaque red ARGB (R=0xff, G=0, B=0, A=0xff) */
    for (unsigned i = 0; i < 16 * 16; i++) {
        layer_data[i * 4 + 0] = 0xff; /* R */
        layer_data[i * 4 + 1] = 0x00; /* G */
        layer_data[i * 4 + 2] = 0x00; /* B */
        layer_data[i * 4 + 3] = 0xff; /* A */
    }

    memset(&desc, 0, sizeof(desc));
    desc.primary = layer_data;
    desc.width = 16;
    desc.height = 16;
    desc.stride = 16 * 4;
    desc.mode = 0; /* 32-bit ARGB */
    desc.control = 0x10000000;
    desc.lanes = 0x03020100;
    desc.x = 4;
    desc.y = 4;

    g_assert_true(nikon_display_pipeline_composite_layer(p, canvas, &desc, 32, 32));

    canvas_data = pixman_image_get_data(canvas);
    /* Pixel at (4, 4) should now be red */
    g_assert_cmphex(canvas_data[4 * 32 + 4], ==, 0xffff0000u);
    /* Pixel at (0, 0) should remain 0 */
    g_assert_cmphex(canvas_data[0], ==, 0x00000000u);

    /* Negative: invalid bounds */
    desc.x = 40;
    g_assert_false(nikon_display_pipeline_composite_layer(p, canvas, &desc, 32, 32));

    pixman_image_unref(canvas);
    nikon_display_pipeline_free(p);
}

static void test_rectangles_and_present(void)
{
    NikonDisplayPipeline *p = nikon_display_pipeline_create(NIKON_DISPLAY_BACKEND_CPU);
    DisplaySurface *surface;
    uint32_t *surf_pixels;
    NikonDisplayRectDesc rects[2];

    g_assert_nonnull(p);
    g_assert_true(nikon_display_pipeline_begin_frame(p, 32, 32));

    rects[0] = (NikonDisplayRectDesc){
        .x = 2, .y = 2, .w = 8, .h = 8,
        .color_rgb = 0x0000ff00, /* Green */
    };
    rects[1] = (NikonDisplayRectDesc){
        .x = 100, .y = 100, .w = 8, .h = 8, /* Out of bounds clipping */
        .color_rgb = 0x000000ff,
    };

    g_assert_true(nikon_display_pipeline_fill_rectangles(p, rects, 2));

    surface = qemu_create_displaysurface(32, 32);
    g_assert_nonnull(surface);

    nikon_display_pipeline_present(p, surface);

    surf_pixels = surface_data(surface);
    /* Check that pixel (2, 2) is green with alpha 0xff */
    g_assert_cmphex(surf_pixels[2 * 32 + 2], ==, 0xff00ff00u);

    qemu_free_displaysurface(surface);
    nikon_display_pipeline_end_frame(p);
    nikon_display_pipeline_free(p);
}

static void test_metal_pipeline(void)
{
#if defined(__APPLE__)
    if (!nikon_metal_is_supported()) {
        g_test_skip("Metal framework unavailable in test environment");
        return;
    }
    NikonDisplayPipeline *p = nikon_display_pipeline_create(NIKON_DISPLAY_BACKEND_METAL);
    g_assert_nonnull(p);
    g_assert_cmpint(p->ops->type, ==, NIKON_DISPLAY_BACKEND_METAL);

    /* Test begin frame */
    g_assert_true(nikon_display_pipeline_begin_frame(p, 64, 64));

    /* Test primary conversion on Metal compute shader */
    uint8_t luma[64 * 64];
    uint8_t chroma[64 * 64];
    memset(luma, 128, sizeof(luma));
    memset(chroma, 128, sizeof(chroma));

    NikonDisplayPrimaryDesc desc;
    memset(&desc, 0, sizeof(desc));
    desc.luma = luma;
    desc.chroma = chroma;
    desc.width = 64;
    desc.height = 64;
    desc.pitch_y = 64;
    desc.pitch_uv = 64;
    desc.mode = 0;
    desc.alpha = 255;
    desc.a = 1; desc.b = 1; desc.e = 1; desc.f = 1;
    desc.scaled_w = 64; desc.scaled_h = 64;

    /* Identity color matrix */
    desc.matrix[0][0][0] = 16384; /* Y -> R */
    desc.matrix[0][1][0] = 16384; /* Y -> G */
    desc.matrix[0][2][0] = 16384; /* Y -> B */

    g_assert_true(nikon_display_pipeline_composite_primary(p, &desc, 0, 0, 64, 64));

    /* Present and verify output surface */
    DisplaySurface *surface = qemu_create_displaysurface(64, 64);
    g_assert_nonnull(surface);
    nikon_display_pipeline_present(p, surface);

    uint32_t *surf_pixels = surface_data(surface);
    /* Luma=128, Chroma=128 should produce non-zero RGB output with alpha=0xff */
    g_assert_cmphex(surf_pixels[0] & 0xff000000u, ==, 0xff000000u);

    qemu_free_displaysurface(surface);
    nikon_display_pipeline_end_frame(p);
    nikon_display_pipeline_free(p);
#endif
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);

    g_test_add_func("/nikon/display/backend-detection", test_backend_detection_and_names);
    g_test_add_func("/nikon/display/pipeline-lifecycle", test_pipeline_lifecycle);
    g_test_add_func("/nikon/display/frame-bounds", test_frame_bounds_and_lifecycle);
    g_test_add_func("/nikon/display/layer-composite", test_layer_composite);
    g_test_add_func("/nikon/display/rectangles-and-present", test_rectangles_and_present);
    g_test_add_func("/nikon/display/metal-pipeline", test_metal_pipeline);

    return g_test_run();
}
