/* NDS32 architectural interruption extension to the mature GNU decoder.
   SPDX-License-Identifier: GPL-3.0-or-later */
#include "config.h"
#include "system.h"
#include "sim-core.h"
#include "sim-events.h"

#define PSW_INTL_MASK 6U
#define PSW_POM_MASK 24U
#define PSW_TRANSLATION_MASK 192U
#define PSW_CPL_MASK (7U << 16)
#define SOFTWARE_PENDING (1U << 16)
#define SR_INT_CTRL SRIDX (1, 1, 2)
#define SR_INT_PRI SRIDX (1, 11, 0)
#define SR_INT_TRIGGER SRIDX (1, 9, 4)
#define IVB_CAPABILITIES ((1U << 11) | (3U << 1))

static uint32_t
input_mask (sim_cpu *cpu)
{
  /* The opt-in functional profile has sixteen IVIC v1 input pins.  This is
     chosen to execute TPJ's 25 vectors and 0xffff pending/mask initialization,
     not a claim about Nikon's silicon identity or reset ROM. */
  return 0xffffU;
}

static SIM_RC
system_option (SIM_DESC sd, sim_cpu *cpu, int option, char *arg, int command)
{
#if WITH_HW
  STATE_CPU (sd, 0)->system_mode = 1;
  return SIM_RC_OK;
#else
  sim_io_eprintf (sd, "NDS32 system mode requires --enable-sim-hardware.\n");
  return SIM_RC_FAIL;
#endif
}

SIM_RC
nds32_system_options (SIM_DESC sd)
{
  static const OPTION options[] = {
    { { "nds32-system", no_argument, NULL, OPTION_START },
      '\0', NULL, "Finite physical memory and architectural IVIC interruptions",
      system_option },
    { { NULL, no_argument, NULL, 0 }, '\0', NULL, NULL, NULL }
  };
  return sim_add_option_table (sd, NULL, options);
}

void
nds32_system_reset (sim_cpu *cpu)
{
  cpu->system_levels = 0;
  cpu->system_edges = 0;
  cpu->system_irq_entry = 0;
  cpu->system_sleep = 0;
  cpu->system_consent = 0;
  cpu->system_wakeup = 0;
  if (!cpu->system_mode)
    return;
  /* Explicit functional reset: privileged physical execution, IVIC v1 with
     sixteen pins, fixed priority, little-endian exception data.  Actual TPJ
     instructions, not the host, establish SP, enable IRQs and copy data. */
  CCPU_SR[SRIDX_PSW].u = 1U << 3;
  CCPU_SR[SRIDX_IVB].u = IVB_CAPABILITIES;
  CCPU_SR[SR_INT_CTRL].u = 1;
  CCPU_SR[SRIDX_INT_MASK].u = 0;
  CCPU_SR[SRIDX_INT_PEND].u = 0;
  CCPU_SR[SR_INT_TRIGGER].u = 0;
}

void
nds32_system_exception (sim_cpu *cpu, unsigned vector, unsigned type,
                        uint32_t eva, int instruction, unsigned software)
{
  uint32_t psw, level, entry, ivb;
  if (!cpu->system_mode)
    return;
  psw = CCPU_SR[SRIDX_PSW].u;
  level = (psw & PSW_INTL_MASK) >> 1;
  ivb = CCPU_SR[SRIDX_IVB].u;
  if (level == 3)
    sim_engine_halt (CPU_STATE (cpu), cpu, NULL, CPU_PC_GET (cpu),
                     sim_stopped, SIM_SIGSEGV);
  if (level == 2)
    {
      CCPU_SR[SRIDX_OIPC] = CCPU_USR[USR0_PC];
      CCPU_SR[SRIDX_PSW].u = (psw & ~PSW_INTL_MASK) | 6;
    }
  else
    {
      if (level == 1)
        {
          CCPU_SR[SRIDX_P_IPSW] = CCPU_SR[SRIDX_IPSW];
          CCPU_SR[SRIDX_P_IPC] = CCPU_SR[SRIDX_IPC];
          CCPU_SR[SRIDX_P_ITYPE] = CCPU_SR[SRIDX_ITYPE];
          CCPU_SR[SRIDX_P_EVA] = CCPU_SR[SRIDX_EVA];
          CCPU_SR[SRIDX_P_P0] = CCPU_GPR[26];
          CCPU_SR[SRIDX_P_P1] = CCPU_GPR[27];
        }
      CCPU_SR[SRIDX_IPSW].u = psw;
      CCPU_SR[SRIDX_IPC] = CCPU_USR[USR0_PC];
      CCPU_SR[SRIDX_ITYPE].u = (type & 15) | (instruction ? 16 : 0)
                              | (vector << 5) | ((software & 0x7fff) << 16);
      CCPU_SR[SRIDX_EVA].u = eva;
      psw &= ~(1U | PSW_INTL_MASK | PSW_POM_MASK |
               PSW_TRANSLATION_MASK | (1U << PSW_BE) | (1U << 11) |
               (1U << 12) | (1U << 13));
      psw |= ((level + 1) << 1) | (1U << 3);
      if (CCPU_SR[SRIDX (0, 3, 0)].u & (1U << 26))
        psw |= 1U << PSW_BE;
      if (CCPU_SR[SRIDX (0, 3, 0)].u & (1U << 31))
        psw |= 1U << 12;
      if (vector == EXP_DEBUG)
        psw |= 1U << 10;
      if (vector == EXP_MACHINE_ERROR)
        psw |= 1U << (instruction ? 8 : 9);
      if (cpu->system_irq_entry)
        psw = (psw & ~PSW_CPL_MASK) | (cpu->system_irq_priority << 16);
      CCPU_SR[SRIDX_PSW].u = psw;
    }
  cpu->system_irq_entry = 0;
  if (level == 2)
    {
      if (vector == EXP_MACHINE_ERROR)
        CCPU_SR[SRIDX_PSW].u |= 1U << (instruction ? 8 : 9);
      if (vector == EXP_DEBUG &&
          (CCPU_SR[SRIDX (3, 5, 0)].u & 0xffff) < 0x30)
        CCPU_SR[SRIDX_PSW].u |= 1U << 10;
    }
  entry = (ivb & 0xffff0000U) + (vector << (2 + 2 * ((ivb >> 14) & 3)));
  CPU_PC_SET (cpu, entry);
  /* Restart is the GNU engine's exception unwind, avoiding completion of
     the faulting load/store or decoder destination register write. */
  sim_engine_restart (CPU_STATE (cpu), cpu, cpu, entry);
}

void
nds32_system_iret (sim_cpu *cpu)
{
  uint32_t level = (CCPU_SR[SRIDX_PSW].u & PSW_INTL_MASK) >> 1;
  uint32_t destination;
  if (!cpu->system_mode)
    return;
  if (!(CCPU_SR[SRIDX_PSW].u & PSW_POM_MASK))
    nds32_system_exception (cpu, EXP_GENERAL, 1, CPU_PC_GET (cpu), 1, 0);
  if (level == 3)
    {
      destination = CCPU_SR[SRIDX_OIPC].u;
      CCPU_SR[SRIDX_PSW].u = (CCPU_SR[SRIDX_PSW].u & ~PSW_INTL_MASK) | 4;
    }
  else
    {
      destination = CCPU_SR[SRIDX_IPC].u;
      CCPU_SR[SRIDX_PSW] = CCPU_SR[SRIDX_IPSW];
      if (level == 2)
        {
          CCPU_SR[SRIDX_IPSW] = CCPU_SR[SRIDX_P_IPSW];
          CCPU_SR[SRIDX_IPC] = CCPU_SR[SRIDX_P_IPC];
          CCPU_SR[SRIDX_ITYPE] = CCPU_SR[SRIDX_P_ITYPE];
          CCPU_SR[SRIDX_EVA] = CCPU_SR[SRIDX_P_EVA];
          CCPU_GPR[26] = CCPU_SR[SRIDX_P_P0];
          CCPU_GPR[27] = CCPU_SR[SRIDX_P_P1];
        }
    }
  cpu->iflags |= NIF_BRANCH;
  cpu->baddr = destination;
}

int
nds32_system_big_endian (sim_cpu *cpu)
{
  if (cpu->system_mode && (CCPU_SR[SRIDX_PSW].u & PSW_INTL_MASK) == 6)
    return !!(CCPU_SR[SRIDX (0, 3, 0)].u & (1U << 26));
  return CCPU_SR_TEST (PSW, PSW_BE);
}

uint32_t
nds32_system_read_sr (sim_cpu *cpu, unsigned index)
{
  if (cpu->system_mode && index == SRIDX_INT_PEND)
    {
      uint32_t trigger = CCPU_SR[SR_INT_TRIGGER].u;
      return ((cpu->system_levels & ~trigger) | cpu->system_edges)
             | (CCPU_SR[index].u & SOFTWARE_PENDING);
    }
  return CCPU_SR[index].u;
}

void
nds32_system_write_sr (sim_cpu *cpu, unsigned index, uint32_t value)
{
  if (!cpu->system_mode)
    {
      CCPU_SR[index].u = value;
      return;
    }
  if (index == SRIDX_INT_PEND)
    {
      /* IVIC v1 hardware pending is W1C, level inputs are owned by the
         external device.  SWI is a separate read/write output. */
      cpu->system_edges &= ~(value & input_mask (cpu));
      CCPU_SR[index].u = value & SOFTWARE_PENDING;
      if (cpu->system_software_output)
        cpu->system_software_output (cpu->system_software_data,
                                    !!(value & SOFTWARE_PENDING));
      return;
    }
  if (index == SRIDX_IVB)
    {
      if (value & (1U << IVB_EVIC))
        nds32_system_exception (cpu, EXP_GENERAL, 7, CPU_PC_GET (cpu), 1, 0);
      CCPU_SR[index].u = (value & 0xffffc000U) | IVB_CAPABILITIES;
      return;
    }
  if (index == SRIDX_PSW || index == SRIDX_IPSW || index == SRIDX_P_IPSW)
    {
      if ((value & PSW_POM_MASK) > (1U << 3))
        nds32_system_exception (cpu, EXP_GENERAL, 7, CPU_PC_GET (cpu), 1, 0);
      /* This profile uses physical local memories.  Translation must never
         be silently mistaken for an identity-mapped MMU. */
      if (value & PSW_TRANSLATION_MASK)
        sim_engine_halt (CPU_STATE (cpu), cpu, NULL, CPU_PC_GET (cpu),
                         sim_stopped, SIM_SIGILL);
    }
  if (index == SRIDX_INT_MASK)
    value &= input_mask (cpu) | SOFTWARE_PENDING | 0xe0000000U;
  if (index == SR_INT_TRIGGER)
    value &= input_mask (cpu);
  CCPU_SR[index].u = value;
}

SIM_RC
nds32_system_irq (SIM_DESC sd, unsigned pin, int level)
{
  sim_cpu *cpu = STATE_CPU (sd, 0);
  uint32_t bit;
  if (!cpu->system_mode || pin >= 16)
    return SIM_RC_FAIL;
  bit = 1U << pin;
  if (level)
    {
      if (!(cpu->system_levels & bit) && (CCPU_SR[SR_INT_TRIGGER].u & bit))
        cpu->system_edges |= bit;
      cpu->system_levels |= bit;
    }
  else
    cpu->system_levels &= ~bit;
  return SIM_RC_OK;
}

SIM_RC
nds32_system_consent (SIM_DESC sd, int level)
{
  sim_cpu *cpu = STATE_CPU (sd, 0);
  if (!cpu->system_mode)
    return SIM_RC_FAIL;
  cpu->system_consent = !!level;
  return SIM_RC_OK;
}

SIM_RC
nds32_system_wakeup (SIM_DESC sd, int level)
{
  sim_cpu *cpu = STATE_CPU (sd, 0);
  if (!cpu->system_mode)
    return SIM_RC_FAIL;
  cpu->system_wakeup = !!level;
  return SIM_RC_OK;
}

void
nds32_system_standby (sim_cpu *cpu, unsigned subtype)
{
  if (!cpu->system_mode)
    return;
  if (subtype > 2)
    nds32_system_exception (cpu, EXP_GENERAL, 1, CPU_PC_GET (cpu), 1, 0);
  /* User-mode no_wake_grant/wake_grant both act as no_wake_grant. */
  if (!(CCPU_SR[SRIDX_PSW].u & PSW_POM_MASK) && subtype == 1)
    subtype = 0;
  cpu->system_sleep = 1;
  cpu->system_standby = subtype;
}

int
nds32_system_waiting (sim_cpu *cpu)
{
  int permission, event;
  if (!cpu->system_mode || !cpu->system_sleep)
    return 0;
  permission = cpu->system_standby == 0 || cpu->system_consent;
  event = cpu->system_wakeup || cpu->system_levels || cpu->system_edges;
  if (permission && (cpu->system_standby == 2 || event))
    {
      cpu->system_sleep = 0;
      return 0;
    }
  return 1;
}

void
nds32_system_software_output (SIM_DESC sd, void (*output) (void *, int),
                              void *data)
{
  sim_cpu *cpu = STATE_CPU (sd, 0);
  cpu->system_software_output = output;
  cpu->system_software_data = data;
  if (output)
    output (data, !!(CCPU_SR[SRIDX_INT_PEND].u & SOFTWARE_PENDING));
}

void
nds32_system_poll (sim_cpu *cpu)
{
  uint32_t psw, pending;
  unsigned pin, best = 16, priority = 4;
  int fixed;
  if (!cpu->system_mode)
    return;
  psw = CCPU_SR[SRIDX_PSW].u;
  if (!(psw & 1) || (psw & PSW_INTL_MASK) == 6)
    return;
  pending = nds32_system_read_sr (cpu, SRIDX_INT_PEND)
            & CCPU_SR[SRIDX_INT_MASK].u & input_mask (cpu);
  fixed = !!(CCPU_SR[SR_INT_CTRL].u & 1);
  for (pin = 0; pin < 16; ++pin)
    {
      unsigned p = (CCPU_SR[SR_INT_PRI].u >> (pin * 2)) & 3;
      if (!(pending & (1U << pin)))
        continue;
      if (!fixed && p >= ((psw & PSW_CPL_MASK) >> 16))
        continue;
      if (best == 16 || (!fixed && p < priority))
        {
          best = pin;
          priority = p;
        }
    }
  if (best != 16)
    {
      /* Entry saves the original priority in IPSW.  IRQ delivery does not
         clear its input or fabricate an ACK. */
      /* The common exception entry saves the old CPL in IPSW before setting
         the new priority.  Restart only unwinds the mature decoder. */
      cpu->system_irq_priority = fixed ? 7 : priority;
      cpu->system_irq_entry = 1;
      nds32_system_exception (cpu, EXP_HW0 + best, 0, 0, 0, 0);
    }
}

unsigned
nds32_system_read (sim_cpu *cpu, uint32_t address, void *buffer,
                   unsigned size, int instruction)
{
  uint32_t psw = CCPU_SR[SRIDX_PSW].u;
  unsigned transferred;
  if (!cpu->system_mode)
    return sim_read (CPU_STATE (cpu), address, buffer, size);
  if (cpu->system_mode && (psw & PSW_INTL_MASK) == 6)
    psw &= ~PSW_TRANSLATION_MASK;
  if (cpu->system_mode && (psw & PSW_TRANSLATION_MASK))
    sim_engine_halt (CPU_STATE (cpu), cpu, NULL, CPU_PC_GET (cpu),
                     sim_stopped, SIM_SIGILL);
  /* The GNU CPU callback wrapper aborts the host on short I/O.  Retain the
     mature core's checked transfer result and raise the architectural guest
     fault ourselves, including the real PC/address/direction. */
  transferred = sim_core_read_buffer (CPU_STATE (cpu), NULL,
                                     instruction ? exec_map : read_map,
                                     buffer, address, size);
  if (cpu->system_mode && transferred != size)
    {
      sim_io_eprintf (CPU_STATE (cpu),
                      "NDS32 bus fault pc=%08x address=%08x read width=%u%s\n",
                      CPU_PC_GET (cpu), address, size,
                      instruction ? " instruction" : "");
      nds32_system_exception (cpu, EXP_GENERAL, 4, address, instruction, 0);
    }
  return transferred;
}

unsigned
nds32_system_write (sim_cpu *cpu, uint32_t address, const void *buffer,
                    unsigned size)
{
  unsigned transferred;
  uint32_t psw = CCPU_SR[SRIDX_PSW].u;
  if (!cpu->system_mode)
    return sim_write (CPU_STATE (cpu), address, buffer, size);
  if ((psw & PSW_INTL_MASK) != 6 && (psw & PSW_TRANSLATION_MASK))
    sim_engine_halt (CPU_STATE (cpu), cpu, NULL, CPU_PC_GET (cpu),
                     sim_stopped, SIM_SIGILL);
  transferred = sim_core_write_buffer (CPU_STATE (cpu), NULL,
                                               write_map, buffer, address, size);
  if (cpu->system_mode && transferred != size)
    {
      sim_io_eprintf (CPU_STATE (cpu),
                      "NDS32 bus fault pc=%08x address=%08x write width=%u\n",
                      CPU_PC_GET (cpu), address, size);
      nds32_system_exception (cpu, EXP_GENERAL, 4, address, 0, 0);
    }
  return transferred;
}

static void
quantum_end (SIM_DESC sd, void *data)
{
  sim_cpu *cpu = STATE_CPU (sd, 0);
  *(int *) data = 1;
  sim_engine_halt (sd, cpu, NULL, CPU_PC_GET (cpu), sim_stopped, 0);
}

SIM_RC
nds32_system_run (SIM_DESC sd, uint64_t ticks)
{
  sim_event *event;
  int expired = 0;
  if (!STATE_CPU (sd, 0)->system_mode || ticks == 0 || ticks > 0x7fffffffU)
    return SIM_RC_FAIL;
  event = sim_events_schedule (sd, ticks, quantum_end, &expired);
  sim_resume (sd, 0, 0);
  if (!expired)
    sim_events_deschedule (sd, event);
  return SIM_RC_OK;
}
