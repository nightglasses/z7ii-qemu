/* Native Nikon qtest device lookup and board IRQ observation.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef NIKON_QTEST_FIXTURE_H
#define NIKON_QTEST_FIXTURE_H

#include "qobject/qdict.h"
#include "qobject/qlist.h"
#include "../libqtest.h"

static inline char *nikon_device_path(QTestState *q, const char *type)
{
    QDict *response = qtest_qmp(q, "{'execute':'qom-list',"
                              "'arguments':{'path':'/machine/unattached'}}");
    const QListEntry *entry;
    char *path = NULL;
    g_autofree char *child_type = g_strdup_printf("child<%s>", type);

    QLIST_FOREACH_ENTRY(qdict_get_qlist(response, "return"), entry) {
        const QDict *child = qobject_to(QDict, qlist_entry_obj(entry));

        if (!strcmp(qdict_get_str(child, "type"), child_type)) {
            g_assert_null(path);
            path = g_strdup_printf("/machine/unattached/%s",
                                   qdict_get_str(child, "name"));
        }
    }
    qobject_unref(response);
    g_assert_nonnull(path);
    return path;
}

static inline void nikon_intercept_gic(QTestState *q)
{
    g_autofree char *path = nikon_device_path(q, "arm_gic");

    /* qtest transactions are nonsecure. Observe the board's actual SPI
     * input instead of pretending to initialize secure guest GIC state.
     */
    qtest_irq_intercept_in(q, path);
}

#endif
