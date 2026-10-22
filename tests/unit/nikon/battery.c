/*
 * Host arithmetic, bounded search and CLI trust-boundary fixtures.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "../../../tools/nikon/battery.h"

static const char vector0[] = "c1bac8b20c6ffe82fecd05132fbd7c1003";
static const char vector2[] = "9e13f3d8a9e86567a1ec12c6e1f1964105";
static const char vector3[] = "079860c54741744e8109f40615ae05a407";
static const char *tool;

static void parse(const char *hex, uint8_t bytes[17])
{
    g_assert_true(nikon_battery_parse_challenge(hex, bytes));
}

static void assert_fill(const uint8_t *bytes, size_t size, uint8_t fill)
{
    for (size_t i = 0; i < size; i++) {
        g_assert_cmphex(bytes[i], ==, fill);
    }
}

static void test_vectors(void)
{
    uint8_t low[17], high[17];

    /*
     * These eight x-coordinates came from the independent Python polynomial
     * oracle, not from a native-tool recording or a guest seed answer.
     */
    g_assert_true(nikon_battery_selftest());
    for (uint32_t seed = 0; seed < 8; seed++) {
        g_assert_true(nikon_battery_challenge(seed, low));
        g_assert_true(nikon_battery_challenge(seed ^ UINT32_C(0x80000000),
                                            high));
        g_assert_cmpmem(low, sizeof(low), high, sizeof(high));
    }
}

static void test_parse(void)
{
    uint8_t bytes[17], expected[17];
    char modified[sizeof(vector0)];

    parse(vector0, expected);
    g_autofree char *upper = g_ascii_strup(vector0, -1);

    parse(upper, bytes);
    g_assert_cmpmem(bytes, sizeof(bytes), expected, sizeof(expected));
    memset(bytes, 0xa5, sizeof(bytes));
    g_assert_false(nikon_battery_parse_challenge(NULL, bytes));
    g_assert_false(nikon_battery_parse_challenge(vector0, NULL));
    g_assert_false(nikon_battery_parse_challenge("", bytes));
    g_assert_false(nikon_battery_parse_challenge(vector0 + 1, bytes));
    g_assert_false(nikon_battery_parse_challenge(
        "c1bac8b20c6ffe82fecd05132fbd7c100300", bytes));
    for (unsigned i = 0; i < 34; i++) {
        memcpy(modified, vector0, sizeof(modified));
        modified[i] = 'z';
        g_assert_false(nikon_battery_parse_challenge(modified, bytes));
    }
    g_assert_false(nikon_battery_parse_challenge(
        "c1bac8b20c6ffe82fecd05132fbd7c1008", bytes));
    assert_fill(bytes, sizeof(bytes), 0xa5);
}

static void test_search(void)
{
    uint8_t challenge[17];
    NikonBatterySearchResult result = { .representative = UINT32_MAX };

    parse(vector2, challenge);
    g_assert_cmpint(nikon_battery_search(challenge, 0, 4, NULL, NULL, &result),
                    ==, NIKON_BATTERY_FOUND);
    g_assert_cmpuint(result.tried, ==, 3);
    g_assert_cmpuint(result.representative, ==, 2);
    parse(vector3, challenge);
    result.representative = UINT32_MAX;
    g_assert_cmpint(nikon_battery_search(challenge, 0, 3, NULL, NULL, &result),
                    ==, NIKON_BATTERY_NOT_FOUND);
    g_assert_cmpuint(result.tried, ==, 3);
    g_assert_cmpuint(result.representative, ==, UINT32_MAX);
    /* The final permitted representative is included, with no uint32 wrap. */
    parse("aae8d794fc45fa2ed43709c89c72cecc06", challenge);
    g_assert_cmpint(nikon_battery_search(challenge,
                    NIKON_BATTERY_SEED_LIMIT - 1, 1, NULL, NULL, &result),
                    ==, NIKON_BATTERY_FOUND);
    g_assert_cmpuint(result.tried, ==, 1);
    g_assert_cmpuint(result.representative, ==, 0x7fffffff);
}

typedef struct Cancel {
    unsigned calls;
    unsigned after;
} Cancel;

static bool cancel_after(void *opaque)
{
    Cancel *cancel = opaque;

    return cancel->calls++ >= cancel->after;
}

static void test_cancel(void)
{
    uint8_t challenge[17], reply[29];
    NikonBatterySearchResult result = { .representative = UINT32_MAX };
    Cancel cancel = { .after = 0 };

    parse(vector3, challenge);
    g_assert_cmpint(nikon_battery_search(challenge, 0, 4, cancel_after,
                                        &cancel, &result),
                    ==, NIKON_BATTERY_CANCELLED);
    g_assert_cmpuint(result.tried, ==, 0);
    cancel = (Cancel) { .after = 2 };
    g_assert_cmpint(nikon_battery_search(challenge, 0, 4, cancel_after,
                                        &cancel, &result),
                    ==, NIKON_BATTERY_CANCELLED);
    g_assert_cmpuint(result.tried, ==, 2);
    g_assert_cmpuint(result.representative, ==, UINT32_MAX);
    /* A candidate hit does not escape cancellation at verification commit. */
    cancel = (Cancel) { .after = 1 };
    g_assert_cmpint(nikon_battery_search(challenge, 3, 1, cancel_after,
                                        &cancel, &result),
                    ==, NIKON_BATTERY_CANCELLED);
    g_assert_cmpuint(result.tried, ==, 1);
    g_assert_cmpuint(result.representative, ==, UINT32_MAX);
    memset(reply, 0xa5, sizeof(reply));
    for (unsigned after = 0; after < 3; after++) {
        cancel = (Cancel) { .after = after };
        g_assert_cmpint(nikon_battery_first_auth(challenge, 3, cancel_after,
                                                &cancel, reply),
                        ==, NIKON_BATTERY_CANCELLED);
        assert_fill(reply, sizeof(reply), 0xa5);
    }
    /* Cancellation has no persistent global state in the core. */
    g_assert_cmpint(nikon_battery_search(challenge, 3, 1, NULL, NULL, &result),
                    ==, NIKON_BATTERY_FOUND);
}

static void test_boundaries(void)
{
    uint8_t challenge[17];
    NikonBatterySearchResult result = { .representative = UINT32_MAX };
    static const struct {
        uint64_t start, count;
    } invalid[] = {
        { 0, 0 }, { NIKON_BATTERY_SEED_LIMIT, 1 },
        { NIKON_BATTERY_SEED_LIMIT - 1, 2 }, { 1, UINT64_MAX },
        { UINT64_MAX, UINT64_MAX },
    };

    parse(vector0, challenge);
    for (unsigned i = 0; i < G_N_ELEMENTS(invalid); i++) {
        g_assert_cmpint(nikon_battery_search(challenge, invalid[i].start,
                                            invalid[i].count, NULL, NULL,
                                            &result),
                        ==, NIKON_BATTERY_INVALID);
        g_assert_cmpuint(result.tried, ==, 0);
        g_assert_cmpuint(result.representative, ==, UINT32_MAX);
    }
    g_assert_cmpint(nikon_battery_search(NULL, 0, 1, NULL, NULL, &result),
                    ==, NIKON_BATTERY_INVALID);
    g_assert_cmpint(nikon_battery_search(challenge, 0, 1, NULL, NULL, NULL),
                    ==, NIKON_BATTERY_INVALID);
    challenge[16] |= 8;
    g_assert_cmpint(nikon_battery_search(challenge, 0, 1, NULL, NULL, &result),
                    ==, NIKON_BATTERY_INVALID);
}

static void test_reply(void)
{
    /* Public wire-only recovery accepted by original EX; not a guest oracle. */
    static const uint8_t accepted[] = {
        0x02, 0x06, 0x00, 0x9a, 0x07, 0x03, 0x10, 0x15,
        0xec, 0x52, 0x1b, 0x01, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x3b,
    };
    uint8_t challenge[17], reply[29];
    uint8_t checksum = 0;

    parse(vector0, challenge);
    memset(reply, 0xa5, sizeof(reply));
    g_assert_cmpint(nikon_battery_first_auth(challenge, 1, NULL, NULL, reply),
                    ==, NIKON_BATTERY_VERIFY_FAILED);
    g_assert_cmpint(nikon_battery_first_auth(challenge, UINT32_MAX,
                                            NULL, NULL, reply),
                    ==, NIKON_BATTERY_INVALID);
    /* A mismatch confined to the seventeenth wire byte must be rejected. */
    challenge[16] ^= 1;
    g_assert_cmpint(nikon_battery_first_auth(challenge, 0, NULL, NULL, reply),
                    ==, NIKON_BATTERY_VERIFY_FAILED);
    assert_fill(reply, sizeof(reply), 0xa5);
    parse(vector0, challenge);
    g_assert_cmpint(nikon_battery_first_auth(challenge, 0, NULL, NULL, reply),
                    ==, NIKON_BATTERY_FOUND);
    g_assert_cmphex(reply[0], ==, 2);
    g_assert_cmphex(reply[1], ==, 6);
    g_assert_cmphex(reply[2], ==, 0);
    g_assert_cmphex(reply[11], ==, 1);
    assert_fill(reply + 12, 16, 0);
    for (unsigned i = 0; i < sizeof(reply); i++) {
        checksum ^= reply[i];
    }
    g_assert_cmphex(checksum, ==, 0);
    parse("0e2267559ba4ee60b78850de270cf63104", challenge);
    g_assert_cmpint(nikon_battery_first_auth(challenge, 2112874933U,
                                            NULL, NULL, reply),
                    ==, NIKON_BATTERY_FOUND);
    g_assert_cmpmem(reply, sizeof(reply), accepted, sizeof(accepted));
}

static void run_cli(const char *const *arguments, int expected_exit,
                    const char *expected_stdout)
{
    g_autoptr(GPtrArray) argv = g_ptr_array_new_with_free_func(g_free);
    g_autofree char *output = NULL;
    g_autofree char *errors = NULL;
    g_autoptr(GError) error = NULL;
    int status;

    g_ptr_array_add(argv, g_strdup(tool));
    for (unsigned i = 0; arguments[i]; i++) {
        g_ptr_array_add(argv, g_strdup(arguments[i]));
    }
    g_ptr_array_add(argv, NULL);
    g_assert_true(g_spawn_sync(NULL, (char **)argv->pdata, NULL, 0, NULL, NULL,
                               &output, &errors, &status, &error));
    g_assert_no_error(error);
    if (expected_exit == 0) {
        g_assert_true(g_spawn_check_exit_status(status, &error));
        g_assert_no_error(error);
        g_assert_cmpstr(errors, ==, "");
    } else {
        g_assert_false(g_spawn_check_exit_status(status, &error));
        g_assert_error(error, G_SPAWN_EXIT_ERROR, expected_exit);
        if (expected_exit != 1) {
            g_assert_cmpstr(errors, !=, "");
        }
    }
    if (expected_stdout) {
        g_assert_cmpstr(output, ==, expected_stdout);
    } else if (expected_exit == 0) {
        g_assert_nonnull(strstr(output, "\"status\":\"found\""));
        g_assert_nonnull(strstr(output, "\"threads\":1"));
        g_assert_nonnull(strstr(output, "\"tried\":1"));
        g_assert_nonnull(strstr(output, "\"representative\":2"));
    } else {
        g_assert_nonnull(strstr(output, "\"status\":\"not-found\""));
        g_assert_nonnull(strstr(output, "\"tried\":3"));
        g_assert_nonnull(strstr(output, "\"representative\":null"));
        g_assert_cmpstr(errors, ==, "");
    }
}

static void test_cli(void)
{
    const char *challenge[] = { "challenge", "0", NULL };
    const char *found[] = { "search", vector2, "2", "1", "256", NULL };
    const char *not_found[] = { "search", vector3, "0", "3", "2", NULL };
    const char *mismatch[] = { "reply", vector0, "1", NULL };
    const char *badhex[] = { "reply",
        "c1bac8b20c6ffe82fecd05132fbd7c100z", "0", NULL };
    const char *extra[] = { "challenge", "0", "extra", NULL };
    static const char *invalid[] = {
        "", "-1", "+1", " 1", "1 ", "0x10", "1x", "4294967296",
        "18446744073709551616",
    };

    run_cli(challenge, 0, "c1bac8b20c6ffe82fecd05132fbd7c1003\n");
    run_cli(found, 0, NULL);
    run_cli(not_found, 1, NULL);
    run_cli(mismatch, 3, "");
    run_cli(badhex, 2, "");
    run_cli(extra, 2, "");
    for (unsigned i = 0; i < G_N_ELEMENTS(invalid); i++) {
        const char *args[] = { "challenge", invalid[i], NULL };

        run_cli(args, 2, "");
    }
    const char *bad_count[] = {
        "search", vector0, "2147483647", "2", "1", NULL,
    };
    const char *bad_threads[] = { "search", vector0, "0", "1", "257", NULL };

    run_cli(bad_count, 2, "");
    run_cli(bad_threads, 2, "");
}

#ifdef G_OS_UNIX
static void close_stdout(void *opaque)
{
    close(STDOUT_FILENO);
}
#endif

static void test_output_error(void)
{
#ifdef G_OS_UNIX
    const char *commands[][7] = {
        { "selftest", NULL },
        { "challenge", "0", NULL },
        { "reply", vector0, "0", NULL },
        { "search", vector0, "0", "1", "1", NULL },
        { "search", vector3, "0", "3", "1", NULL },
    };

    for (unsigned i = 0; i < G_N_ELEMENTS(commands); i++) {
        g_autoptr(GPtrArray) argv = g_ptr_array_new();
        g_autofree char *output = NULL;
        g_autofree char *errors = NULL;
        g_autoptr(GError) error = NULL;
        int status;

        g_ptr_array_add(argv, (char *)tool);
        for (unsigned j = 0; commands[i][j]; j++) {
            g_ptr_array_add(argv, (char *)commands[i][j]);
        }
        g_ptr_array_add(argv, NULL);
        g_assert_true(g_spawn_sync(NULL, (char **)argv->pdata, NULL, 0,
                                   close_stdout, NULL, &output, &errors,
                                   &status, &error));
        g_assert_no_error(error);
        g_assert_false(g_spawn_check_exit_status(status, &error));
        g_assert_error(error, G_SPAWN_EXIT_ERROR, 3);
        g_assert_cmpstr(output, ==, "");
        g_assert_nonnull(strstr(errors, "cannot write result"));
    }
#else
    g_test_skip("requires a Unix child setup to close stdout before exec");
#endif
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_assert_cmpint(argc, ==, 2);
    tool = argv[1];
    g_test_add_func("/nikon/battery/vectors", test_vectors);
    g_test_add_func("/nikon/battery/parser", test_parse);
    g_test_add_func("/nikon/battery/search", test_search);
    g_test_add_func("/nikon/battery/cancel", test_cancel);
    g_test_add_func("/nikon/battery/boundaries", test_boundaries);
    g_test_add_func("/nikon/battery/reply", test_reply);
    g_test_add_func("/nikon/battery/cli", test_cli);
    g_test_add_func("/nikon/battery/output-error", test_output_error);
    return g_test_run();
}
