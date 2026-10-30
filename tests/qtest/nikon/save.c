/* Cold QOM receipt for the source-bounded save handoff. */
#include "qemu/osdep.h"
#include "qemu/bswap.h"
#include "libqtest.h"
#include "qobject/qdict.h"

static uint64_t property(QTestState *q, const char *name)
{
    QDict *reply = qtest_qmp(q,
        "{'execute':'qom-get','arguments':{'path':'/machine/peripheral/save',"
        "'property':%s}}", name);
    uint64_t value;

    g_assert(qdict_haskey(reply, "return"));
    value = qdict_get_uint(reply, "return");
    qobject_unref(reply);
    return value;
}

static uint64_t machine_property(QTestState *q, const char *name)
{
    QDict *reply = qtest_qmp(q,
        "{'execute':'qom-get','arguments':{'path':'/machine/save',"
        "'property':%s}}", name);
    uint64_t value;

    g_assert(qdict_haskey(reply, "return"));
    value = qdict_get_uint(reply, "return");
    qobject_unref(reply);
    return value;
}

static void test_cold_handoff(void)
{
    QTestState *q = qtest_init("-M none -nodefaults -display none "
                               "-device nikon-save,id=save");

    g_assert_cmpuint(property(q, "phase"), ==, 0);
    g_assert_cmpuint(property(q, "requests"), ==, 0);
    g_assert_cmpuint(property(q, "commits"), ==, 0);
    g_assert_cmpuint(property(q, "failures"), ==, 0);
    g_assert_cmpuint(property(q, "source"), ==, 0);
    g_assert_cmpuint(property(q, "length"), ==, 0);
    g_assert_cmpuint(property(q, "format"), ==, 0);
    g_assert_cmpuint(property(q, "error"), ==, 0);
    qtest_quit(q);
}

static void test_machine_save(void)
{
    uint32_t idle = cpu_to_le32(0xeafffffe);
    g_autofree char *image = NULL;
    int fd = g_file_open_tmp("nikonsavebiosXXXXXX", &image, NULL);
    g_autofree char *bios = NULL;
    QTestState *q;

    g_assert_cmpint(fd, >=, 0);
    close(fd);
    g_assert_true(g_file_set_contents(image, (char *)&idle,
                                      sizeof(idle), NULL));

    bios = g_shell_quote(image);
    q = qtest_initf("-M z7ii-mas-diagnostic -smp 1 -bios %s "
                    "-nodefaults -display none", bios);

    g_assert_cmpuint(machine_property(q, "phase"), ==, 0);
    g_assert_cmpuint(machine_property(q, "requests"), ==, 0);
    g_assert_cmpuint(machine_property(q, "commits"), ==, 0);
    g_assert_cmpuint(machine_property(q, "failures"), ==, 0);
    g_assert_cmpuint(machine_property(q, "source"), ==, 0);
    g_assert_cmpuint(machine_property(q, "length"), ==, 0);
    g_assert_cmpuint(machine_property(q, "format"), ==, 0);
    g_assert_cmpuint(machine_property(q, "error"), ==, 0);

    qtest_quit(q);
    unlink(image);
}

static void test_machine_save_handoff(void)
{
    uint32_t idle = cpu_to_le32(0xeafffffe);
    g_autofree char *image = NULL;
    int fd = g_file_open_tmp("nikonsavebiosXXXXXX", &image, NULL);
    g_autofree char *bios = NULL;
    QTestState *q;
    QDict *resp;

    g_assert_cmpint(fd, >=, 0);
    close(fd);
    g_assert_true(g_file_set_contents(image, (char *)&idle,
                                      sizeof(idle), NULL));

    bios = g_shell_quote(image);
    q = qtest_initf("-M z7ii-mas-diagnostic -smp 1 -bios %s "
                    "-nodefaults -display none", bios);

    /* Initial state is idle. */
    g_assert_cmpuint(machine_property(q, "phase"), ==, 0);

    /* 1. Send save request for FX RAW frame in kind 16 capture pool. */
    qtest_qmp_assert_success(q,
        "{'execute':'qom-set','arguments':{'path':'/machine/save',"
        "'property':'request-source','value':1418723328}}"); /* 0x54900000 */
    qtest_qmp_assert_success(q,
        "{'execute':'qom-set','arguments':{'path':'/machine/save',"
        "'property':'request-length','value':91586560}}"); /* 8320x5504x2 */
    qtest_qmp_assert_success(q,
        "{'execute':'qom-set','arguments':{'path':'/machine/save',"
        "'property':'request-format','value':1}}"); /* NIKON_SAVE_FORMAT_RAW */
    qtest_qmp_assert_success(q,
        "{'execute':'qom-set','arguments':{'path':'/machine/save',"
        "'property':'request-token','value':51966}}"); /* 0xcafe */

    /* Verify requested state and latched descriptor. */
    g_assert_cmpuint(machine_property(q, "phase"), ==, 1);
    g_assert_cmpuint(machine_property(q, "requests"), ==, 1);
    g_assert_cmpuint(machine_property(q, "token"), ==, 51966);
    g_assert_cmphex(machine_property(q, "source"), ==, 0x54900000);
    g_assert_cmpuint(machine_property(q, "length"), ==, 91586560);
    g_assert_cmpuint(machine_property(q, "format"), ==, 1);

    /* 2. Concurrent request while requested must be rejected. */
    resp = qtest_qmp(q,
        "{'execute':'qom-set','arguments':{'path':'/machine/save',"
        "'property':'request-token','value':51967}}");
    g_assert_true(qdict_haskey(resp, "error"));
    qobject_unref(resp);

    /* 3. Commit with mismatched token must be rejected. */
    resp = qtest_qmp(q,
        "{'execute':'qom-set','arguments':{'path':'/machine/save',"
        "'property':'commit-token','value':12345}}");
    g_assert_true(qdict_haskey(resp, "error"));
    qobject_unref(resp);

    /* 4. Commit with matching token must succeed and record completion. */
    qtest_qmp_assert_success(q,
        "{'execute':'qom-set','arguments':{'path':'/machine/save',"
        "'property':'commit-token','value':51966}}");
    g_assert_cmpuint(machine_property(q, "phase"), ==, 2);
    g_assert_cmpuint(machine_property(q, "commits"), ==, 1);

    /* 5. Request a second JPEG frame to verify cancellation. */
    qtest_qmp_assert_success(q,
        "{'execute':'qom-set','arguments':{'path':'/machine/save',"
        "'property':'request-source','value':1418723328}}");
    qtest_qmp_assert_success(q,
        "{'execute':'qom-set','arguments':{'path':'/machine/save',"
        "'property':'request-length','value':15728640}}");
    qtest_qmp_assert_success(q,
        "{'execute':'qom-set','arguments':{'path':'/machine/save',"
        "'property':'request-format','value':2}}"); /* NIKON_SAVE_FORMAT_JPEG */
    qtest_qmp_assert_success(q,
        "{'execute':'qom-set','arguments':{'path':'/machine/save',"
        "'property':'request-token','value':51968}}");

    g_assert_cmpuint(machine_property(q, "phase"), ==, 1);
    g_assert_cmpuint(machine_property(q, "requests"), ==, 2);

    /* Cancel with matching token. */
    qtest_qmp_assert_success(q,
        "{'execute':'qom-set','arguments':{'path':'/machine/save',"
        "'property':'cancel-token','value':51968}}");
    g_assert_cmpuint(machine_property(q, "phase"), ==, 4);

    qtest_quit(q);
    unlink(image);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/arm/nikon/save/cold", test_cold_handoff);
    g_test_add_func("/arm/nikon/save/machine", test_machine_save);
    g_test_add_func("/arm/nikon/save/machine/handoff",
                    test_machine_save_handoff);
    return g_test_run();
}
