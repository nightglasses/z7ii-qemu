/* Native bridge protocol tests. SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef NIKON_QTEST_BRIDGE_H
#define NIKON_QTEST_BRIDGE_H

#include "../libqtest.h"

void nikon_test_bridge(QTestState *q);
void nikon_bridge_add_tests(void);

#endif
