/* Native router tests. SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef NIKON_QTEST_ROUTER_H
#define NIKON_QTEST_ROUTER_H

#include "../libqtest.h"

void nikon_router_test(QTestState *q, const char *path, uint64_t base);
void nikon_router_requests_test(QTestState *q, const char *path, uint64_t base);
void nikon_router_timing_test(QTestState *q, const char *path, uint64_t base);

#endif
