/* Native quantum stop/resume contract, independent of architecture fixtures.
   SPDX-License-Identifier: GPL-3.0-or-later */
#include "config.h"
#include <assert.h>
#include "gdb/callback.h"
#include "system.h"
#include "sim-events.h"
#include "sim-signal.h"
#include "opcode/nds32.h"

static void
instruction (SIM_DESC sd, uint32_t address, uint32_t code)
{
  unsigned char bytes[] = { code >> 24, code >> 16, code >> 8, code };
  assert (sim_write (sd, address, bytes, sizeof bytes) == sizeof bytes);
}

static void
native_fault (SIM_DESC sd, void *opaque)
{
  sim_cpu *cpu = STATE_CPU (sd, 0);
  (void) opaque;
  sim_engine_halt (sd, cpu, NULL, CPU_PC_GET (cpu), sim_stopped, SIM_SIGILL);
}

int
main (void)
{
  char *argv[] = { "budgetcheck", "--nds32-system", "--endian=little",
                   "--memory-region=0,0x10000", NULL };
  host_callback callback = default_callback;
  SIM_DESC sd;
  sim_cpu *cpu;
  enum sim_stop reason;
  int signal;
  callback.init (&callback);
  sd = sim_open (SIM_OPEN_DEBUG, &callback, NULL, argv);
  assert (sd);
  cpu = STATE_CPU (sd, 0);
  instruction (sd, 0x100, N32_TYPE1 (MOVI, 0, 1));
  instruction (sd, 0x104, N32_TYPE1 (MOVI, 0, 2));
  instruction (sd, 0x108, N32_JI (J, 0));
  CPU_PC_SET (cpu, 0x100);
  assert (nds32_system_run (sd, 1) == SIM_RC_OK);
  sim_stop_reason (sd, &reason, &signal);
  assert (reason == sim_stopped && signal == 0);
  assert (CPU_PC_GET (cpu) == 0x104 && CCPU_GPR[0].u == 1);
  assert (nds32_system_run (sd, 1) == SIM_RC_OK);
  sim_stop_reason (sd, &reason, &signal);
  assert (reason == sim_stopped && signal == 0);
  assert (CPU_PC_GET (cpu) == 0x108 && CCPU_GPR[0].u == 2);
  /* A genuine native fault remains nonzero, never accepted as budget expiry. */
  sim_events_schedule (sd, 1, native_fault, NULL);
  assert (nds32_system_run (sd, 4) == SIM_RC_OK);
  sim_stop_reason (sd, &reason, &signal);
  assert (reason == sim_stopped && signal == GDB_SIGNAL_ILL);
  assert (CPU_PC_GET (cpu) == 0x108 && CCPU_GPR[0].u == 2);
  /* The interrupted quantum was descheduled; its stack data cannot fire later. */
  assert (nds32_system_run (sd, 1) == SIM_RC_OK);
  sim_stop_reason (sd, &reason, &signal);
  assert (reason == sim_stopped && signal == 0);
  sim_close (sd, 0);
  callback.shutdown (&callback);
  return 0;
}
