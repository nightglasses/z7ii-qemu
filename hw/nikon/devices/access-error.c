/* Shared Nikon diagnostic access rejection.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "qemu/error-report.h"
#include "qemu/log.h"
#include "system/runstate.h"
#include "target/arm/cpu.h"
#include "hw/nikon/devices.h"

void nikon_access_error(bool *reported, hwaddr addr, unsigned size,
                       bool write, uint64_t value, const char *reason)
{
    CPUState *cpu = current_cpu;
    g_autofree char *message = NULL;

    /* Debugger/monitor probes must not become guest execution evidence. */
    if (!cpu || *reported) {
        return;
    }
    *reported = true;
    message = g_strdup_printf("nikon: uncovered access "
                              "cpu=%d pc=0x%08" PRIx64 " address=0x%08"
                              HWADDR_PRIx " direction=%s width=%u "
                              "value=0x%" PRIx64 " transaction=error reason=%s",
                              cpu->cpu_index,
                              (uint64_t)CPU_GET_CLASS(cpu)->get_pc(cpu),
                              addr, write ? "write" : "read", size, value,
                              reason);
    error_report("%s", message);
    qemu_log_mask(LOG_GUEST_ERROR, "%s\n", message);
    qemu_system_vmstop_request(RUN_STATE_DEBUG);
}

