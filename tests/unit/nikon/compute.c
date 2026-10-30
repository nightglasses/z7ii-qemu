/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Unit tests for Nikon Image & ISP Compute Hardware Abstraction Layer (HAL)
 */

#include "qemu/osdep.h"
#include "hw/nikon/compute_pipeline.h"

int (*qemu_main)(void);

static void test_compute_detection_and_names(void)
{
    g_assert_cmpstr(nikon_compute_backend_name(NIKON_COMPUTE_BACKEND_CPU), ==, "cpu");
    g_assert_cmpstr(nikon_compute_backend_name(NIKON_COMPUTE_BACKEND_METAL), ==, "metal");
    g_assert_cmpstr(nikon_compute_backend_name(NIKON_COMPUTE_BACKEND_DIRECTML), ==, "directml");
    g_assert_cmpstr(nikon_compute_backend_name(NIKON_COMPUTE_BACKEND_VULKAN), ==, "vulkan");
    g_assert_cmpstr(nikon_compute_backend_name(NIKON_COMPUTE_BACKEND_COUNT), ==, "unknown");

    NikonComputeBackendType optimal = nikon_compute_detect_optimal_backend();
#if defined(__APPLE__)
    g_assert_cmpint(optimal, ==, NIKON_COMPUTE_BACKEND_METAL);
#elif defined(_WIN32)
    g_assert_cmpint(optimal, ==, NIKON_COMPUTE_BACKEND_DIRECTML);
#else
    g_assert_cmpint(optimal, ==, NIKON_COMPUTE_BACKEND_CPU);
#endif
}

static void test_compute_lifecycle(void)
{
    NikonComputePipeline *p = nikon_compute_pipeline_create(NIKON_COMPUTE_BACKEND_CPU);
    g_assert_nonnull(p);
    g_assert_nonnull(p->ops);
    g_assert_cmpint(p->ops->type, ==, NIKON_COMPUTE_BACKEND_CPU);

    nikon_compute_pipeline_free(p);
    nikon_compute_pipeline_free(NULL);

    /* Fallback on invalid type */
    p = nikon_compute_pipeline_create((NikonComputeBackendType)999);
    g_assert_nonnull(p);
    g_assert_cmpint(p->ops->type, ==, NIKON_COMPUTE_BACKEND_CPU);
    nikon_compute_pipeline_free(p);
}

static void test_compute_demosaic_bayer(void)
{
    NikonComputePipeline *p = nikon_compute_pipeline_create(NIKON_COMPUTE_BACKEND_CPU);
    uint16_t raw[16];
    uint32_t rgb[16];
    NikonComputeBayerDesc desc;

    g_assert_nonnull(p);

    /* Fill 4x4 RGGB test pattern:
     * R=1000  Gr=2000
     * Gb=2000 B=3000
     */
    for (unsigned y = 0; y < 4; y += 2) {
        for (unsigned x = 0; x < 4; x += 2) {
            raw[y * 4 + x] = 1000;
            raw[y * 4 + x + 1] = 2000;
            raw[(y + 1) * 4 + x] = 2000;
            raw[(y + 1) * 4 + x + 1] = 3000;
        }
    }

    memset(&desc, 0, sizeof(desc));
    desc.raw = raw;
    desc.rgb = rgb;
    desc.width = 4;
    desc.height = 4;
    desc.red_response = 2048;  /* 1.0x */
    desc.blue_response = 2048; /* 1.0x */

    g_assert_true(nikon_compute_pipeline_demosaic_bayer(p, &desc));

    /* Check that pixel (0, 0) has alpha 0xff and non-zero components */
    g_assert_cmphex(rgb[0] & 0xff000000u, ==, 0xff000000u);
    uint32_t r = (rgb[0] >> 16) & 0xff;
    uint32_t g = (rgb[0] >> 8) & 0xff;
    uint32_t b = rgb[0] & 0xff;
    g_assert_cmpuint(r, >, 0);
    g_assert_cmpuint(g, >, 0);
    g_assert_cmpuint(b, >, 0);

    /* Negative tests: odd width/height must fail */
    desc.width = 3;
    g_assert_false(nikon_compute_pipeline_demosaic_bayer(p, &desc));

    /* NULL parameters */
    g_assert_false(nikon_compute_pipeline_demosaic_bayer(NULL, &desc));
    g_assert_false(nikon_compute_pipeline_demosaic_bayer(p, NULL));

    nikon_compute_pipeline_free(p);
}

static void test_compute_warp_bounds(void)
{
    NikonComputePipeline *p = nikon_compute_pipeline_create(NIKON_COMPUTE_BACKEND_CPU);
    uint8_t output[100];

    g_assert_nonnull(p);

    /* NULL params must fail gracefully */
    g_assert_false(nikon_compute_pipeline_warp_nv12(p, NULL, output, sizeof(output)));
    g_assert_false(nikon_compute_pipeline_warp_nv12(NULL, NULL, output, sizeof(output)));

    nikon_compute_pipeline_free(p);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);

    g_test_add_func("/nikon/compute/backend-detection", test_compute_detection_and_names);
    g_test_add_func("/nikon/compute/lifecycle", test_compute_lifecycle);
    g_test_add_func("/nikon/compute/demosaic-bayer", test_compute_demosaic_bayer);
    g_test_add_func("/nikon/compute/warp-bounds", test_compute_warp_bounds);

    return g_test_run();
}
