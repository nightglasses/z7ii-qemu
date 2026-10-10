/* Cold QOM receipt for the source-bounded save handoff. */
#include "qemu/osdep.h"
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

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/arm/nikon/save/cold", test_cold_handoff);
    return g_test_run();
}
