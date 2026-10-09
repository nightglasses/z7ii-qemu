/* GNU NDS32 native system execution fixtures; no Nikon bytes.
   SPDX-License-Identifier: GPL-3.0-or-later */
#include "config.h"
#include <assert.h>
#include <string.h>
#include "gdb/callback.h"
#include "system.h"
#include "opcode/nds32.h"

static SIM_DESC
open_system (host_callback *callback)
{
  char *argv[] = { "nds", "--nds32-system", "--endian=little",
                   "--memory-region=0,0x10000",
                   "--memory-region=0x20000,0x4000", NULL };
  SIM_DESC sd;
  *callback = default_callback;
  callback->init (callback);
  sd = sim_open (SIM_OPEN_DEBUG, callback, NULL, argv);
  assert (sd);
  return sd;
}

static void
instruction (SIM_DESC sd, uint32_t address, uint32_t code)
{
  unsigned char bytes[] = { code >> 24, code >> 16, code >> 8, code };
  assert (sim_write (sd, address, bytes, sizeof bytes) == sizeof bytes);
}

static void
close_system (SIM_DESC sd, host_callback *callback)
{
  sim_close (sd, 0);
  callback->shutdown (callback);
}

static void
return_levels (void)
{
  host_callback callback;
  SIM_DESC sd = open_system (&callback);
  sim_cpu *cpu = STATE_CPU (sd, 0);
  instruction (sd, 0x100, N32_TYPE0 (MISC, N32_MISC_IRET));
  CPU_PC_SET (cpu, 0x100);
  CCPU_SR[SRIDX_PSW].u = 8 | 4;
  CCPU_SR[SRIDX_IPSW].u = 8 | 2 | 1;
  CCPU_SR[SRIDX_IPC].u = 0x200;
  CCPU_SR[SRIDX_P_IPSW].u = 8 | 1;
  CCPU_SR[SRIDX_P_IPC].u = 0x300;
  CCPU_SR[SRIDX_ITYPE].u = 0x321;
  CCPU_SR[SRIDX_P_ITYPE].u = 0x123;
  CCPU_SR[SRIDX_EVA].u = 0x70010;
  CCPU_SR[SRIDX_P_EVA].u = 0x20020;
  CCPU_GPR[26].u = 11;
  CCPU_GPR[27].u = 22;
  CCPU_SR[SRIDX_P_P0].u = 33;
  CCPU_SR[SRIDX_P_P1].u = 44;
  assert (nds32_system_run (sd, 1) == SIM_RC_OK);
  assert (CPU_PC_GET (cpu) == 0x200);
  assert (CCPU_SR[SRIDX_PSW].u == (8 | 2 | 1));
  assert (CCPU_SR[SRIDX_IPSW].u == (8 | 1));
  assert (CCPU_SR[SRIDX_IPC].u == 0x300);
  assert (CCPU_SR[SRIDX_ITYPE].u == 0x123);
  assert (CCPU_SR[SRIDX_EVA].u == 0x20020);
  assert (CCPU_GPR[26].u == 33 && CCPU_GPR[27].u == 44);
  CPU_PC_SET (cpu, 0x100);
  assert (nds32_system_run (sd, 1) == SIM_RC_OK);
  assert (CPU_PC_GET (cpu) == 0x300);
  assert (CCPU_SR[SRIDX_PSW].u == (8 | 1));
  CPU_PC_SET (cpu, 0x100);
  CCPU_SR[SRIDX_PSW].u = 8 | 6;
  CCPU_SR[SRIDX_OIPC].u = 0x400;
  assert (nds32_system_run (sd, 1) == SIM_RC_OK);
  assert (CPU_PC_GET (cpu) == 0x400);
  assert (CCPU_SR[SRIDX_PSW].u == (8 | 4));
  close_system (sd, &callback);
}

static void
interrupt_priority (void)
{
  host_callback callback;
  SIM_DESC sd = open_system (&callback);
  sim_cpu *cpu = STATE_CPU (sd, 0);
  uint32_t original = 8 | 1 | (4U << 16);
  unsigned index = SRIDX (1, 11, 0);
  instruction (sd, 0x100, N32_JI (J, 0));
  instruction (sd, 9 * 4, N32_JI (J, 0));
  instruction (sd, 10 * 4, N32_JI (J, 0));
  CPU_PC_SET (cpu, 0x100);
  CCPU_SR[SRIDX_PSW].u = original;
  nds32_system_write_sr (cpu, SRIDX_INT_MASK, 3);
  nds32_system_write_sr (cpu, SRIDX (1, 1, 2), 0);
  nds32_system_write_sr (cpu, index, 3);
  assert (nds32_system_irq (sd, 0, 1) == SIM_RC_OK);
  assert (nds32_system_irq (sd, 1, 1) == SIM_RC_OK);
  assert (nds32_system_run (sd, 1) == SIM_RC_OK);
  assert (CPU_PC_GET (cpu) == 10 * 4);
  assert (CCPU_SR[SRIDX_IPC].u == 0x100);
  assert (CCPU_SR[SRIDX_IPSW].u == original);
  assert (CCPU_SR[SRIDX_ITYPE].u == (10U << 5));
  assert (((CCPU_SR[SRIDX_PSW].u >> 16) & 7) == 0);
  assert (nds32_system_read_sr (cpu, SRIDX_INT_PEND) == 3);
  nds32_system_write_sr (cpu, SRIDX_INT_PEND, 3);
  assert (nds32_system_read_sr (cpu, SRIDX_INT_PEND) == 3);
  assert (nds32_system_irq (sd, 0, 0) == SIM_RC_OK);
  assert (nds32_system_irq (sd, 1, 0) == SIM_RC_OK);
  nds32_system_write_sr (cpu, SRIDX (1, 9, 4), 1);
  assert (nds32_system_irq (sd, 0, 1) == SIM_RC_OK);
  assert (nds32_system_irq (sd, 0, 0) == SIM_RC_OK);
  assert (nds32_system_read_sr (cpu, SRIDX_INT_PEND) == 1);
  nds32_system_write_sr (cpu, SRIDX_INT_PEND, 1);
  assert (nds32_system_read_sr (cpu, SRIDX_INT_PEND) == 0);
  close_system (sd, &callback);
}

static unsigned
peer_read (void *opaque, uint32_t address, void *data, unsigned size)
{
  static const unsigned char reply[] = { 0x78, 0x56, 0x34, 0x12 };
  if (address != 0x70010 || size != 4)
    return 0;
  ++*(unsigned *) opaque;
  memcpy (data, reply, size);
  return size;
}

static void
finite_boundary (void)
{
  static const struct nds32_boundary_ops ops = { peer_read, NULL };
  host_callback callback;
  SIM_DESC sd = open_system (&callback);
  sim_cpu *cpu = STATE_CPU (sd, 0);
  unsigned reads = 0;
  assert (nds32_system_attach (sd, 0x70000, 0x40, &ops, &reads) == SIM_RC_OK);
  instruction (sd, 0x100, N32_TYPE2 (LWI, 0, 1, 0));
  instruction (sd, 7 * 4, N32_JI (J, 0));
  CPU_PC_SET (cpu, 0x100);
  CCPU_GPR[1].u = 0x70010;
  assert (nds32_system_run (sd, 1) == SIM_RC_OK);
  assert (CCPU_GPR[0].u == 0x12345678 && reads == 1);
  CPU_PC_SET (cpu, 0x100);
  CCPU_GPR[0].u = 0xdeadbeef;
  CCPU_GPR[1].u = 0x70014;
  assert (nds32_system_run (sd, 1) == SIM_RC_OK);
  assert (CPU_PC_GET (cpu) == 7 * 4);
  assert (CCPU_SR[SRIDX_IPC].u == 0x100);
  assert (CCPU_SR[SRIDX_EVA].u == 0x70014);
  assert (CCPU_SR[SRIDX_ITYPE].u == ((7U << 5) | 4));
  assert (CCPU_GPR[0].u == 0xdeadbeef && reads == 1);
  close_system (sd, &callback);
}

static void
privileged_access (void)
{
  const uint32_t codes[] = {
    N32_TYPE0 (MISC, N32_MISC_MFSR | (SRIDX_INT_MASK << 10)),
    N32_TYPE0 (MISC, N32_MISC_MTSR | (SRIDX_INT_MASK << 10)),
    N32_TYPE1 (MISC, 1, N32_MISC_MTSR | (2U << 5) | (SRIDX_PSW << 10))
  };
  unsigned i;
  for (i = 0; i < sizeof codes / sizeof codes[0]; ++i)
    {
      host_callback callback;
      SIM_DESC sd = open_system (&callback);
      sim_cpu *cpu = STATE_CPU (sd, 0);
      instruction (sd, 0x100, codes[i]);
      instruction (sd, 7 * 4, N32_JI (J, 0));
      CPU_PC_SET (cpu, 0x100);
      CCPU_SR[SRIDX_PSW].u = 0;
      CCPU_SR[SRIDX_INT_MASK].u = 0x55;
      CCPU_GPR[0].u = 0xdeadbeef;
      assert (nds32_system_run (sd, 1) == SIM_RC_OK);
      assert (CPU_PC_GET (cpu) == 7 * 4);
      assert (CCPU_SR[SRIDX_IPC].u == 0x100);
      assert (CCPU_SR[SRIDX_IPSW].u == 0);
      assert (CCPU_SR[SRIDX_ITYPE].u == ((7U << 5) | 16 | 1));
      assert (CCPU_SR[SRIDX_INT_MASK].u == 0x55);
      assert (CCPU_GPR[0].u == 0xdeadbeef);
      close_system (sd, &callback);
    }
}

static void
power_consent (void)
{
  host_callback callback;
  SIM_DESC sd = open_system (&callback);
  sim_cpu *cpu = STATE_CPU (sd, 0);
  instruction (sd, 0x100, N32_TYPE0 (MISC, 1U << 5));
  instruction (sd, 0x104, N32_TYPE1 (MOVI, 0, 42));
  instruction (sd, 0x108, N32_JI (J, 0));
  CPU_PC_SET (cpu, 0x100);
  assert (nds32_system_run (sd, 4) == SIM_RC_OK);
  assert (cpu->system_sleep && CPU_PC_GET (cpu) == 0x104);
  assert (CCPU_GPR[0].u == 0);
  assert (nds32_system_irq (sd, 0, 1) == SIM_RC_OK);
  assert (nds32_system_run (sd, 4) == SIM_RC_OK);
  assert (cpu->system_sleep && CCPU_GPR[0].u == 0);
  assert (nds32_system_consent (sd, 1) == SIM_RC_OK);
  assert (nds32_system_run (sd, 1) == SIM_RC_OK);
  assert (!cpu->system_sleep && CCPU_GPR[0].u == 42);
  assert (CPU_PC_GET (cpu) == 0x108);
  /* A masked pin wakes the consented core, but does not dispatch an IRQ. */
  assert (CCPU_SR[SRIDX_IPC].u == 0);
  close_system (sd, &callback);
}

int
main (void)
{
  return_levels ();
  interrupt_priority ();
  finite_boundary ();
  privileged_access ();
  power_consent ();
  return 0;
}
