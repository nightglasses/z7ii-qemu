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

static void test_compute_default_pipeline(void)
{
    NikonComputePipeline *def = nikon_compute_pipeline_get_default();
    g_assert_nonnull(def);
    g_assert_nonnull(def->ops);
}

static void fill_warp_cell(uint8_t *p, int32_t x, int32_t y, unsigned exponent)
{
    memset(p, 0, 40);
    for (unsigned corner = 0; corner < 4; corner++) {
        uint32_t cx = (x + (corner & 1 ? (1U << exponent) * 256 : 0)) & 0x7fffff;
        uint32_t cy = (y + (corner & 2 ? 8 * 256 : 0)) & 0x7fffff;
        for (unsigned b = 0; b < 4; b++) {
            p[corner * 8 + b] = cx >> (8 * b);
            p[corner * 8 + 4 + b] = cy >> (8 * b);
        }
    }
    uint32_t tag = 0x300 | exponent;
    for (unsigned b = 0; b < 4; b++) {
        p[32 + b] = tag >> (8 * b);
    }
}

static void test_compute_warp_identity(void)
{
    static const uint8_t ramp[24] = {
        0, 10, 20, 30, 40, 50, 60, 70,
        80, 90, 100, 110, 120, 130, 140, 150,
        0, 200, 40, 160, 80, 120, 120, 80,
    };
    uint8_t table[40], out_cpu[24], out_default[24];
    NikonGatePixels p = {
        .input = ramp, .input_size = sizeof(ramp),
        .src_width = 4, .src_height = 4,
        .records = table, .records_size = sizeof(table),
        .columns = 1, .rows = 1, .dst_width = 4, .dst_height = 4,
    };

    fill_warp_cell(table, 0, 0, 3);

    NikonComputePipeline *cpu = nikon_compute_pipeline_create(NIKON_COMPUTE_BACKEND_CPU);
    g_assert_true(nikon_compute_pipeline_warp_nv12(cpu, &p, out_cpu, sizeof(out_cpu)));
    g_assert_cmpmem(out_cpu, sizeof(out_cpu), ramp, sizeof(ramp));
    nikon_compute_pipeline_free(cpu);

    /* Test default pipeline (transparent acceleration) */
    g_assert_true(nikon_compute_pipeline_warp_nv12(NULL, &p, out_default, sizeof(out_default)));
    g_assert_cmpmem(out_default, sizeof(out_default), ramp, sizeof(ramp));
}

#if defined(__APPLE__)
static void test_compute_metal_backend(void)
{
    NikonComputePipeline *metal = nikon_compute_pipeline_create(NIKON_COMPUTE_BACKEND_METAL);
    g_assert_nonnull(metal);
    g_assert_cmpint(metal->ops->type, ==, NIKON_COMPUTE_BACKEND_METAL);

    /* 1. Demosaic test */
    uint16_t raw[16];
    uint32_t rgb_metal[16], rgb_cpu[16];
    NikonComputeBayerDesc desc_metal, desc_cpu;

    for (unsigned y = 0; y < 4; y += 2) {
        for (unsigned x = 0; x < 4; x += 2) {
            raw[y * 4 + x] = 1200;
            raw[y * 4 + x + 1] = 2400;
            raw[(y + 1) * 4 + x] = 2400;
            raw[(y + 1) * 4 + x + 1] = 3600;
        }
    }

    desc_metal.raw = raw;
    desc_metal.rgb = rgb_metal;
    desc_metal.width = 4;
    desc_metal.height = 4;
    desc_metal.red_response = 2048;
    desc_metal.blue_response = 2048;

    desc_cpu = desc_metal;
    desc_cpu.rgb = rgb_cpu;

    NikonComputePipeline *cpu = nikon_compute_pipeline_create(NIKON_COMPUTE_BACKEND_CPU);
    g_assert_true(nikon_compute_pipeline_demosaic_bayer(cpu, &desc_cpu));
    g_assert_true(nikon_compute_pipeline_demosaic_bayer(metal, &desc_metal));

    /* Metal demosaic must match CPU demosaic bit-for-bit */
    g_assert_cmpmem(rgb_metal, sizeof(rgb_metal), rgb_cpu, sizeof(rgb_cpu));

    /* 2. Warp NV12 test */
    static const uint8_t ramp[24] = {
        0, 10, 20, 30, 40, 50, 60, 70,
        80, 90, 100, 110, 120, 130, 140, 150,
        0, 200, 40, 160, 80, 120, 120, 80,
    };
    uint8_t table[40], out_metal[24], out_cpu_warp[24];
    NikonGatePixels p = {
        .input = ramp, .input_size = sizeof(ramp),
        .src_width = 4, .src_height = 4,
        .records = table, .records_size = sizeof(table),
        .columns = 1, .rows = 1, .dst_width = 4, .dst_height = 4,
    };
    fill_warp_cell(table, 0, 0, 3);

    g_assert_true(nikon_compute_pipeline_warp_nv12(cpu, &p, out_cpu_warp, sizeof(out_cpu_warp)));
    g_assert_true(nikon_compute_pipeline_warp_nv12(metal, &p, out_metal, sizeof(out_metal)));

    /* Metal warp must match CPU reference */
    g_assert_cmpmem(out_metal, sizeof(out_metal), out_cpu_warp, sizeof(out_cpu_warp));

    nikon_compute_pipeline_free(cpu);
    nikon_compute_pipeline_free(metal);
}
#endif

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);

    g_test_add_func("/nikon/compute/backend-detection", test_compute_detection_and_names);
    g_test_add_func("/nikon/compute/lifecycle", test_compute_lifecycle);
    g_test_add_func("/nikon/compute/demosaic-bayer", test_compute_demosaic_bayer);
    g_test_add_func("/nikon/compute/warp-bounds", test_compute_warp_bounds);
    g_test_add_func("/nikon/compute/default-pipeline", test_compute_default_pipeline);
    g_test_add_func("/nikon/compute/warp-identity", test_compute_warp_identity);
#if defined(__APPLE__)
    g_test_add_func("/nikon/compute/metal-pipeline", test_compute_metal_backend);
#endif

    return g_test_run();
}
