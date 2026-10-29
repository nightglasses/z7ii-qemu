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

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/arm/nikon/save/cold", test_cold_handoff);
    g_test_add_func("/arm/nikon/save/machine", test_machine_save);
    return g_test_run();
}
