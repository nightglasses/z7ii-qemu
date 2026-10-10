/* Source-bounded save handoff state contract. SPDX-License-Identifier: GPL-2.0-or-later */
#include "qemu/osdep.h"
#include "qapi/error.h"
#include "hw/nikon/save.h"

static DeviceState *save_device(void)
{
    DeviceState *dev = qdev_new(TYPE_NIKON_SAVE);

    g_assert_true(qdev_realize(dev, NULL, &error_abort));
    return dev;
}

static void test_save_lifecycle(void)
{
    DeviceState *dev = save_device();
    NikonSaveRequest request = {
        .token = 7,
        .source = 0xb1810000,
        .length = 0x1000,
        .format = NIKON_SAVE_FORMAT_UNKNOWN,
    };
    NikonSaveStatus status;
    Error *error = NULL;

    g_assert_true(nikon_save_request(dev, &request, &error));
    g_assert_null(error);
    g_assert_true(nikon_save_status(dev, &status, &error));
    g_assert_cmpuint(status.phase, ==, NIKON_SAVE_REQUESTED);
    g_assert_cmpuint(status.requests, ==, 1);
    g_assert_cmpuint(status.commits, ==, 0);
    g_assert_cmphex(status.source, ==, request.source);
    g_assert_cmpuint(status.length, ==, request.length);

    g_assert_false(nikon_save_commit(dev, 8, &error));
    g_assert_nonnull(error);
    error_free(error);
    error = NULL;
    g_assert_true(nikon_save_commit(dev, request.token, &error));
    g_assert_null(error);
    g_assert_true(nikon_save_status(dev, &status, &error));
    g_assert_cmpuint(status.phase, ==, NIKON_SAVE_COMMITTED);
    g_assert_cmpuint(status.commits, ==, 1);

    object_unref(OBJECT(dev));
}

static void test_save_failure_and_cancel(void)
{
    DeviceState *dev = save_device();
    NikonSaveRequest request = {
        .token = 11,
        .source = 0xb1800000,
        .length = 0x200,
        .format = NIKON_SAVE_FORMAT_RAW,
    };
    NikonSaveStatus status;
    Error *error = NULL;

    g_assert_true(nikon_save_request(dev, &request, &error));
    g_assert_true(nikon_save_fail(dev, request.token, 0x42, &error));
    g_assert_null(error);
    g_assert_true(nikon_save_status(dev, &status, &error));
    g_assert_cmpuint(status.phase, ==, NIKON_SAVE_FAILED);
    g_assert_cmpuint(status.failures, ==, 1);
    g_assert_cmphex(status.error, ==, 0x42);

    request.token++;
    g_assert_true(nikon_save_request(dev, &request, &error));
    g_assert_true(nikon_save_cancel(dev, request.token, &error));
    g_assert_cmpuint(nikon_save_status(dev, &status, &error), ==, true);
    g_assert_cmpuint(status.phase, ==, NIKON_SAVE_CANCELLED);
    g_assert_cmpuint(status.failures, ==, 1);

    object_unref(OBJECT(dev));
}

static void test_save_rejects_invalid(void)
{
    DeviceState *dev = save_device();
    NikonSaveRequest request = { .token = 1, .source = 1, .length = 1,
                                 .format = 99 };
    Error *error = NULL;

    g_assert_false(nikon_save_request(dev, &request, &error));
    g_assert_nonnull(error);
    error_free(error);
    error = NULL;
    request.format = NIKON_SAVE_FORMAT_JPEG;
    g_assert_true(nikon_save_request(dev, &request, &error));
    g_assert_null(error);
    g_assert_false(nikon_save_request(dev, &request, &error));
    g_assert_nonnull(error);
    error_free(error);
    object_unref(OBJECT(dev));
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/nikon/save/lifecycle", test_save_lifecycle);
    g_test_add_func("/nikon/save/failure-cancel", test_save_failure_and_cancel);
    g_test_add_func("/nikon/save/reject-invalid", test_save_rejects_invalid);
    return g_test_run();
}
