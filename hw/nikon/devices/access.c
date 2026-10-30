/* Shared Nikon diagnostic access rejection.
 *
 * Every distinct uncovered access is reported.  The previous behaviour kept a
 * single flag per device, so after the first rejection the device went silent
 * and later, different accesses were swallowed.  Analysis needs every
 * (pc, address, width, direction) tuple, so repeat accesses of the same tuple
 * are folded into a count instead of being hidden.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "qemu/error-report.h"
#include "qemu/log.h"
#include "system/runstate.h"
#include "hw/core/cpu.h"
#include "hw/nikon/devices.h"

#define NIKON_ACCESS_SEEN 64

typedef struct NikonAccessKey {
    uint64_t pc;
    hwaddr addr;
    uint32_t size;
    bool write;
    unsigned count;
} NikonAccessKey;

static NikonAccessKey nikon_access_seen[NIKON_ACCESS_SEEN];
static unsigned nikon_access_count;
static bool nikon_access_overflow_reported;

static unsigned nikon_access_note(uint64_t pc, hwaddr addr, unsigned size,
                                  bool write)
{
    for (unsigned i = 0; i < nikon_access_count; i++) {
        NikonAccessKey *key = &nikon_access_seen[i];

        if (key->pc == pc && key->addr == addr && key->size == size &&
            key->write == write) {
            return ++key->count;
        }
    }
    if (nikon_access_count < NIKON_ACCESS_SEEN) {
        NikonAccessKey *key = &nikon_access_seen[nikon_access_count++];

        *key = (NikonAccessKey) { pc, addr, size, write, 1 };
        return 1;
    }
    if (!nikon_access_overflow_reported) {
        nikon_access_overflow_reported = true;
        error_report("nikon: further distinct uncovered accesses are not "
                     "recorded after %d tuples", NIKON_ACCESS_SEEN);
        qemu_log_mask(LOG_GUEST_ERROR,
                      "nikon: uncovered-access table full after %d tuples\n",
                      NIKON_ACCESS_SEEN);
    }
    return 1;
}

void nikon_access_error(bool *reported, hwaddr addr, unsigned size,
                       bool write, uint64_t value, const char *reason)
{
    CPUState *cpu = current_cpu;
    g_autofree char *message = NULL;
    uint64_t pc;
    unsigned seen;

    /* Debugger/monitor probes must not become guest execution evidence. */
    if (!cpu) {
        return;
    }
    pc = (uint64_t)CPU_GET_CLASS(cpu)->get_pc(cpu);
    seen = nikon_access_note(pc, addr, size, write);
    if (reported) {
        *reported = true;
    }
    /* Same tuple again: it is the same uncovered access, already reported. */
    if (seen > 1) {
        return;
    }
    message = g_strdup_printf("nikon: uncovered access "
                              "cpu=%d pc=0x%08" PRIx64 " address=0x%08"
                              HWADDR_PRIx " direction=%s width=%u "
                              "value=0x%" PRIx64 " transaction=error reason=%s",
                              cpu->cpu_index, pc, addr,
                              write ? "write" : "read", size, value, reason);
    error_report("%s", message);
    qemu_log_mask(LOG_GUEST_ERROR, "%s\n", message);
    qemu_system_vmstop_request(RUN_STATE_DEBUG);
}

bool nikon_wait_stall(bool *reported, hwaddr addr, unsigned size,
                      uint64_t value, unsigned limit, const char *reason)
{
    static hwaddr last_addr;
    static uint64_t last_value;
    static unsigned polls;
    CPUState *cpu = current_cpu;
    g_autofree char *message = NULL;
    uint64_t pc;

    if (!cpu) {
        return false;
    }
    if (addr == last_addr && value == last_value) {
        polls++;
    } else {
        last_addr = addr;
        last_value = value;
        polls = 1;
    }
    if (polls <= limit) {
        return false;
    }
    if (reported) {
        if (*reported) {
            return false;
        }
        *reported = true;
    }
    polls = 0;
    pc = (uint64_t)CPU_GET_CLASS(cpu)->get_pc(cpu);
    message = g_strdup_printf("nikon: wait stalled "
                              "cpu=%d pc=0x%08" PRIx64 " address=0x%08"
                              HWADDR_PRIx " width=%u value=0x%" PRIx64
                              " polls=%u reason=%s",
                              cpu->cpu_index, pc, addr, size, value,
                              limit, reason);
    error_report("%s", message);
    qemu_log_mask(LOG_GUEST_ERROR, "%s\n", message);
    qemu_system_vmstop_request(RUN_STATE_DEBUG);
    return true;
}
