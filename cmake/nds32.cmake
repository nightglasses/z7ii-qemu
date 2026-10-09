# NDS32 system hooks over the fixed official GNU provider.
# NDS_UPSTREAM and native_file(path,content) are supplied by the native source graph.
function(nds_replace variable before after)
  string(LENGTH "${${variable}}" size)
  string(LENGTH "${before}" removed)
  string(REPLACE "${before}" "" without "${${variable}}")
  string(LENGTH "${without}" remaining)
  math(EXPR count "${size} - ${remaining}")
  if(NOT count EQUAL removed)
    message(FATAL_ERROR "Fixed upstream NDS32 source does not match its unique system seam")
  endif()
  string(REPLACE "${before}" "${after}" updated "${${variable}}")
  set(${variable} "${updated}" PARENT_SCOPE)
endfunction()

file(READ "${NDS_UPSTREAM}/sim/nds32/Makefile.in" content)
nds_replace(content [==[
SIM_OBJS = \
	$(SIM_NEW_COMMON_OBJS) \
	interp.o \
	nds32-cop0.o \
	nds32-syscall.o \
	sim-resume.o \
]==] [==[
SIM_OBJS = \
	$(SIM_NEW_COMMON_OBJS) \
		interp.o \
		system.o \
	nds32-cop0.o \
	nds32-syscall.o \
	sim-resume.o \
]==])
nds_replace(content [==[## COMMON_POST_CONFIG_FRAG

nds32-syscall.o: nds32-syscall.c targ-vals.h nds32-sim.h nds32-syscall.h
]==] [==[## COMMON_POST_CONFIG_FRAG

nds32-syscall.o: nds32-syscall.c targ-vals.h nds32-sim.h nds32-syscall.h

system.o: system.c system.h nds32-sim.h
dv-boundary.o: boundary.c system.h
	$(COMPILE) $(srcdir)/boundary.c
	$(POSTCOMPILE)
]==])
native_file("sim/nds32/Makefile.in" "${content}")

file(READ "${NDS_UPSTREAM}/sim/nds32/configure.ac" content)
nds_replace(content [==[
SIM_AC_OPTION_ENDIAN
SIM_AC_OPTION_ALIGNMENT(NONSTRICT_ALIGNMENT)
SIM_AC_OPTION_WARNINGS

AC_CHECK_FUNCS([ \
]==] [==[
SIM_AC_OPTION_ENDIAN
SIM_AC_OPTION_ALIGNMENT(NONSTRICT_ALIGNMENT)
SIM_AC_OPTION_HARDWARE(yes, [core pal glue], [boundary])
SIM_AC_OPTION_WARNINGS

AC_CHECK_FUNCS([ \
]==])
native_file("sim/nds32/configure.ac" "${content}")

file(READ "${NDS_UPSTREAM}/sim/nds32/interp.c" content)
nds_replace(content [==[#include "sim-fpu.h"
#include "sim-trace.h"
#include "sim-options.h"

#include "opcode/nds32.h"
#include "nds32-sim.h"
]==] [==[#include "sim-fpu.h"
#include "sim-trace.h"
#include "sim-options.h"
#include "system.h"

#include "opcode/nds32.h"
#include "nds32-sim.h"
]==])
nds_replace(content [==[  uint32_t cia = CCPU_USR[USR0_PC].u;
  int i;

  if (msg)
    {
      va_list va;
]==] [==[  uint32_t cia = CCPU_USR[USR0_PC].u;
  int i;

  if (cpu->system_mode)
    nds32_system_exception (cpu, e, sig == SIM_SIGILL ? 1 : 4,
                            cia, 1, 0);

  if (msg)
    {
      va_list va;
]==])
nds_replace(content [==[  SIM_ASSERT (size <= sizeof (uint64_t));

  if (aligned_p && (addr & (size - 1)) != 0)
    nds32_raise_exception (cpu, EXP_GENERAL, SIM_SIGSEGV,
			   "Alignment check exception. "
			   "Read of address 0x%08x in size of %d.\n",
			   addr, size);

  r = sim_read (sd, addr, (unsigned char *) &val, size);
  order = CCPU_SR_TEST (PSW, PSW_BE) ? BIG_ENDIAN : LITTLE_ENDIAN;
  val = extract_unsigned_integer ((unsigned char *) &val, size, order);

]==] [==[  SIM_ASSERT (size <= sizeof (uint64_t));

  if (aligned_p && (addr & (size - 1)) != 0)

    {
      nds32_system_exception (cpu, EXP_GENERAL, 0, addr, 0, 0);
      nds32_raise_exception (cpu, EXP_GENERAL, SIM_SIGSEGV,
			   "Alignment check exception. "
			   "Read of address 0x%08x in size of %d.\n",
			   addr, size);
    }

  r = nds32_system_read (cpu, addr, (unsigned char *) &val, size, 0);
  order = CCPU_SR_TEST (PSW, PSW_BE) ? BIG_ENDIAN : LITTLE_ENDIAN;
  val = extract_unsigned_integer ((unsigned char *) &val, size, order);

]==])
nds_replace(content [==[  SIM_ASSERT (size <= sizeof (uint64_t));

  if (aligned_p && (addr & (size - 1)) != 0)
    nds32_raise_exception (cpu, EXP_GENERAL, SIM_SIGSEGV,
			   "Alignment check exception. "
			   "Write of address 0x%08x in size of %d.\n",
			   addr, size);

  order = CCPU_SR_TEST (PSW, PSW_BE) ? BIG_ENDIAN : LITTLE_ENDIAN;
  store_unsigned_integer ((unsigned char *) &val, size, order, val);
  r = sim_write (sd, addr, (unsigned char *) &val, size);

  if (r == size)
    return;
]==] [==[  SIM_ASSERT (size <= sizeof (uint64_t));

  if (aligned_p && (addr & (size - 1)) != 0)
    {
      nds32_system_exception (cpu, EXP_GENERAL, 0, addr, 0, 0);
      nds32_raise_exception (cpu, EXP_GENERAL, SIM_SIGSEGV,
			   "Alignment check exception. "
			   "Write of address 0x%08x in size of %d.\n",
			   addr, size);
    }

  order = CCPU_SR_TEST (PSW, PSW_BE) ? BIG_ENDIAN : LITTLE_ENDIAN;
  store_unsigned_integer ((unsigned char *) &val, size, order, val);
  r = nds32_system_write (cpu, addr, (unsigned char *) &val, size);

  if (r == size)
    return;
]==])
nds_replace(content [==[	      len = strnlen (buf, 4);
	      size = (len == 4) ? 4 : len + 1;	/* Include zero byte.  */
	    }
	  ret = sim_write (sd, base, (unsigned char *) buf, size);
	  if (ret != size)
	    {
	      nds32_raise_exception (cpu, EXP_GENERAL, SIM_SIGSEGV,
]==] [==[	      len = strnlen (buf, 4);
	      size = (len == 4) ? 4 : len + 1;	/* Include zero byte.  */
	    }
	  ret = nds32_system_write (cpu, base, (unsigned char *) buf, size);
	  if (ret != size)
	    {
	      nds32_raise_exception (cpu, EXP_GENERAL, SIM_SIGSEGV,
]==])
nds_replace(content [==[	{
	  /* LMW */

	  ret = sim_read (sd, base, (unsigned char *) buf, 4);
	  if (ret != 4)
	    {
	      nds32_raise_exception (cpu, EXP_GENERAL, SIM_SIGSEGV,
]==] [==[	{
	  /* LMW */

	  ret = nds32_system_read (cpu, base, (unsigned char *) buf, 4, 0);
	  if (ret != 4)
	    {
	      nds32_raise_exception (cpu, EXP_GENERAL, SIM_SIGSEGV,
]==])
nds_replace(content [==[		  len = strnlen (buf, 4);
		  size = (len == 4) ? 4 : len + 1;	/* Include zero byte.  */
		}
	      ret = sim_write (sd, base, (unsigned char *) buf, size);
	      if (ret != size)
		{
		  nds32_raise_exception (cpu, EXP_GENERAL, SIM_SIGSEGV,
]==] [==[		  len = strnlen (buf, 4);
		  size = (len == 4) ? 4 : len + 1;	/* Include zero byte.  */
		}
	      ret = nds32_system_write (cpu, base, (unsigned char *) buf, size);
	      if (ret != size)
		{
		  nds32_raise_exception (cpu, EXP_GENERAL, SIM_SIGSEGV,
]==])
nds_replace(content [==[	    {
	      /* LMW */

	      ret = sim_read (sd, base, (unsigned char *) buf, 4);
	      if (ret != 4)
		{
		  nds32_raise_exception (cpu, EXP_GENERAL, SIM_SIGSEGV,
]==] [==[	    {
	      /* LMW */

	      ret = nds32_system_read (cpu, base, (unsigned char *) buf, 4, 0);
	      if (ret != 4)
		{
		  nds32_raise_exception (cpu, EXP_GENERAL, SIM_SIGSEGV,
]==])
nds_replace(content [==[    case 0xc:			/* msync */
      break;
    case 0x5:			/* trap */
    case 0xa:			/* break */
      nds32_raise_exception (cpu, EXP_DEBUG, SIM_SIGTRAP, NULL);
      return;
    case 0x2:			/* mfsr */
      CCPU_GPR[rt] = CCPU_SR[__GF (insn, 10, 10)];
      break;
    case 0x3:			/* mtsr */
      {
]==] [==[    case 0xc:			/* msync */
      break;
    case 0x5:			/* trap */
      nds32_system_exception (cpu, EXP_GENERAL, 2, cia, 1,
                              __GF (insn, 5, 15));
      nds32_raise_exception (cpu, EXP_DEBUG, SIM_SIGTRAP, NULL);
      return;
    case 0xa:			/* break */
      nds32_system_exception (cpu, EXP_DEBUG, 0, cia, 1,
                              __GF (insn, 5, 15));
      nds32_raise_exception (cpu, EXP_DEBUG, SIM_SIGTRAP, NULL);
      return;
    case 0x2:			/* mfsr */
      if (cpu->system_mode && !(CCPU_SR[SRIDX_PSW].u & 24U))
	nds32_system_exception (cpu, EXP_GENERAL, 1, cia, 1, 0);
      CCPU_GPR[rt].u = nds32_system_read_sr (cpu, __GF (insn, 10, 10));
      break;
    case 0x3:			/* mtsr */
      {
]==])
nds_replace(content [==[	switch (__GF (insn, 5, 5))
	  {
	  case 0:		/* mtsr */
	    CCPU_SR[sridx] = CCPU_GPR[rt];
	    switch (sridx)
	      {
	      case SRIDX_PFM_CTL:
]==] [==[	switch (__GF (insn, 5, 5))
	  {
	  case 0:		/* mtsr */
	    if (cpu->system_mode && !(CCPU_SR[SRIDX_PSW].u & 24U))
	      nds32_system_exception (cpu, EXP_GENERAL, 1, cia, 1, 0);
	    nds32_system_write_sr (cpu, sridx, CCPU_GPR[rt].u);
	    switch (sridx)
	      {
	      case SRIDX_PFM_CTL:
]==])
nds_replace(content [==[	  case 2:		/* setgie */
	    if (sridx != 0x80)
]==] [==[	  case 2:		/* setgie */
	    if (cpu->system_mode && !(CCPU_SR[SRIDX_PSW].u & 24U))
	      nds32_system_exception (cpu, EXP_GENERAL, 1, cia, 1, 0);
	    if (sridx != 0x80)
]==])
nds_replace(content [==[      }
      break;
    case 0xb:			/* syscall */
      nds32_syscall (cpu, __GF (insn, 5, 15), cia);
      break;
    case 0x4:			/* iret */
      nds32_bad_op (cpu, cia, insn, "iret (MISC)");
      break;
    case 0x6:			/* teqz */
      nds32_bad_op (cpu, cia, insn, "teqz (MISC)");
      break;
    case 0x7:			/* tnez */
      nds32_bad_op (cpu, cia, insn, "tnez (MISC)");
      break;
    case 0xe:			/* tlbop */
]==] [==[      }
      break;
    case 0xb:			/* syscall */
      nds32_system_exception (cpu, EXP_SYSCALL, 0, cia, 1,
                              __GF (insn, 5, 15));
      nds32_syscall (cpu, __GF (insn, 5, 15), cia);
      break;
    case 0x4:			/* iret */
      if (cpu->system_mode)
	{
	  nds32_system_iret (cpu);
	  break;
	}
      nds32_bad_op (cpu, cia, insn, "iret (MISC)");
      break;
    case 0x6:			/* teqz */
      if (cpu->system_mode)
	{
	  if (CCPU_GPR[rt].u == 0)
	    nds32_system_exception (cpu, EXP_GENERAL, 2, cia, 1,
	                            __GF (insn, 5, 15));
	  break;
	}
      nds32_bad_op (cpu, cia, insn, "teqz (MISC)");
      break;
    case 0x7:			/* tnez */
      if (cpu->system_mode)
	{
	  if (CCPU_GPR[rt].u != 0)
	    nds32_system_exception (cpu, EXP_GENERAL, 2, cia, 1,
	                            __GF (insn, 5, 15));
	  break;
	}
      nds32_bad_op (cpu, cia, insn, "tnez (MISC)");
      break;
    case 0xe:			/* tlbop */
]==])
nds_replace(content [==[    case 0x35:			/* break16, ex9.it */
      if (imm9u < 32)		/* break16 */
	{
	  nds32_raise_exception (cpu, EXP_DEBUG, SIM_SIGTRAP, NULL);
	  return;
	}

      /* ex9.it */
      sim_read (sd, (CCPU_USR[USR0_ITB].u & ~3U) + (imm9u << 2),
		(unsigned char *) &insn, 4);
      insn = extract_unsigned_integer ((unsigned char *) &insn, 4, BIG_ENDIAN);
      nds32_decode16_ex9 (cpu, insn, cia);
      return;
]==] [==[    case 0x35:			/* break16, ex9.it */
      if (imm9u < 32)		/* break16 */
	{
	  nds32_system_exception (cpu, EXP_DEBUG, 0, cia, 1, imm9u);
	  nds32_raise_exception (cpu, EXP_DEBUG, SIM_SIGTRAP, NULL);
	  return;
	}

      /* ex9.it */
      nds32_system_read (cpu, (CCPU_USR[USR0_ITB].u & ~3U) + (imm9u << 2),
			 (unsigned char *) &insn, 4, 1);
      insn = extract_unsigned_integer ((unsigned char *) &insn, 4, BIG_ENDIAN);
      nds32_decode16_ex9 (cpu, insn, cia);
      return;
]==])
nds_replace(content [==[	      nds32_set_nia (cpu, CCPU_GPR[ra5].u);
	      return;
	    case 2:		/* ex9.it imm5 */
	      sim_read (sd, (CCPU_USR[USR0_ITB].u & ~3U) + (imm5u << 2),
			(unsigned char *) &insn, 4);
	      insn = extract_unsigned_integer ((unsigned char *) &insn, 4,
					       BIG_ENDIAN);
	      nds32_decode16_ex9 (cpu, insn, cia);
]==] [==[	      nds32_set_nia (cpu, CCPU_GPR[ra5].u);
	      return;
	    case 2:		/* ex9.it imm5 */
	      nds32_system_read (cpu, (CCPU_USR[USR0_ITB].u & ~3U) + (imm5u << 2),
				 (unsigned char *) &insn, 4, 1);
	      insn = extract_unsigned_integer ((unsigned char *) &insn, 4,
					       BIG_ENDIAN);
	      nds32_decode16_ex9 (cpu, insn, cia);
]==])
nds_replace(content [==[  while (1)
    {
      uint32_t insn;

      recent_cia[recent_cia_idx] = cia;
      recent_cia_idx = (recent_cia_idx + 1) & RECENT_CIA_MASK;
]==] [==[  while (1)
    {
      uint32_t insn;
      unsigned char instruction[4];

      if (nds32_system_waiting (cpu))
        {
          if (sim_events_tick (sd))
            sim_events_process (sd);
          continue;
        }
      nds32_system_poll (cpu);
      if (cia & 1)
	nds32_system_exception (cpu, EXP_GENERAL, 0, cia, 1, 0);

      recent_cia[recent_cia_idx] = cia;
      recent_cia_idx = (recent_cia_idx + 1) & RECENT_CIA_MASK;
]==])
nds_replace(content [==[      nds32_pfm_event (cpu, PFM_CYCLE);
      nds32_pfm_event (cpu, PFM_INST);

      r = sim_read (sd, cia, (unsigned char *) &insn, 4);
      insn = extract_unsigned_integer ((unsigned char *) &insn, 4,
				       BIG_ENDIAN);

      if (r != 4)
	nds32_dump_registers (sd);
      SIM_ASSERT (r == 4);

      if (TRACE_LINENUM_P (cpu))
	{
]==] [==[      nds32_pfm_event (cpu, PFM_CYCLE);
      nds32_pfm_event (cpu, PFM_INST);

      r = nds32_system_read (cpu, cia, instruction, 2, 1);
      if (r == 2 && !(instruction[0] & 0x80))
	r += nds32_system_read (cpu, cia + 2, instruction + 2, 2, 1);
      else
	instruction[2] = instruction[3] = 0;
      insn = extract_unsigned_integer (instruction, 4, BIG_ENDIAN);

      if (r != ((instruction[0] & 0x80) ? 2 : 4))
	nds32_dump_registers (sd);
      SIM_ASSERT (r == ((instruction[0] & 0x80) ? 2 : 4));

      if (TRACE_LINENUM_P (cpu))
	{
]==])
nds_replace(content [==[      if (cpu->iflags & NIF_BRANCH)
	{
	  if (cpu->baddr & 1)
	    nds32_raise_exception (cpu, EXP_GENERAL, SIM_SIGSEGV,
				   "Alignment check exception. "
				   "Unaligned instruction address 0x%08x\n",
				   cia);
	  cia = cpu->baddr;
	}

]==] [==[      if (cpu->iflags & NIF_BRANCH)
	{
	  if (cpu->baddr & 1)
	    {
	      CPU_PC_SET (cpu, cpu->baddr);
	      nds32_system_exception (cpu, EXP_GENERAL, 0, cia, 1, 0);
	      nds32_raise_exception (cpu, EXP_GENERAL, SIM_SIGSEGV,
				   "Alignment check exception. "
				   "Unaligned instruction address 0x%08x\n",
				   cia);
	    }
	  cia = cpu->baddr;
	}

]==])
nds_replace(content [==[  CCPU_FPCFG_PUT (FREG, 3);
  /* Floating-Point Control Status Register.  */
  CCPU_FPCSR.u = 0;
}

static void
]==] [==[  CCPU_FPCFG_PUT (FREG, 3);
  /* Floating-Point Control Status Register.  */
  CCPU_FPCSR.u = 0;
  nds32_system_reset (cpu);
}

static void
]==])
nds_replace(content [==[      return 0;
    }

  /* The parser will print an error message for us, so we silently return.  */
  if (sim_parse_args (sd, argv) != SIM_RC_OK)
    {
]==] [==[      return 0;
    }

  if (nds32_system_options (sd) != SIM_RC_OK)
    {
      nds32_free_state (sd);
      return 0;
    }

  /* The parser will print an error message for us, so we silently return.  */
  if (sim_parse_args (sd, argv) != SIM_RC_OK)
    {
]==])
nds_replace(content [==[    }

  /* Allocate 64MB memory if none is set up by user.  */
  if (STATE_MEMOPT (sd) == NULL)
    sim_do_command (sd, "memory region 0,0x4000000");	/* 64 MB */

  /* CPU specific initialization.  */
]==] [==[    }

  /* Allocate 64MB memory if none is set up by user.  */
  if (STATE_MEMOPT (sd) == NULL && !STATE_CPU (sd, 0)->system_mode)
    sim_do_command (sd, "memory region 0,0x4000000");	/* 64 MB */

  /* CPU specific initialization.  */
]==])
nds_replace(content [==[      STATE_CALLBACK (sd)->target_endian = BFD_ENDIAN_LITTLE;
    }

  STATE_CALLBACK (sd)->syscall_map = cb_nds32_libgloss_syscall_map;
  nds32_init_libgloss (sd, prog_bfd, argv, env);

  return SIM_RC_OK;
}
]==] [==[      STATE_CALLBACK (sd)->target_endian = BFD_ENDIAN_LITTLE;
    }

  if (!cpu->system_mode)
    {
      STATE_CALLBACK (sd)->syscall_map = cb_nds32_libgloss_syscall_map;
      nds32_init_libgloss (sd, prog_bfd, argv, env);
    }

  return SIM_RC_OK;
}
]==])
nds_replace(content [==[  order = CCPU_SR_TEST (PSW, PSW_BE) ? BIG_ENDIAN : LITTLE_ENDIAN;
  val = extract_unsigned_integer ((unsigned char *) &val, size, order);
]==] [==[  order = nds32_system_big_endian (cpu) ? BIG_ENDIAN : LITTLE_ENDIAN;
  val = extract_unsigned_integer ((unsigned char *) &val, size, order);
]==])
nds_replace(content [==[  order = CCPU_SR_TEST (PSW, PSW_BE) ? BIG_ENDIAN : LITTLE_ENDIAN;
  store_unsigned_integer ((unsigned char *) &val, size, order, val);
]==] [==[  order = nds32_system_big_endian (cpu) ? BIG_ENDIAN : LITTLE_ENDIAN;
  store_unsigned_integer ((unsigned char *) &val, size, order, val);
]==])
nds_replace(content [==[  int order = CCPU_SR_TEST (PSW, PSW_BE) ? BIG_ENDIAN : LITTLE_ENDIAN;
  char enb4map[2][4] = { {3, 2, 1, 0}, {0, 1, 2, 3} };
]==] [==[  int order = nds32_system_big_endian (cpu) ? BIG_ENDIAN : LITTLE_ENDIAN;
  char enb4map[2][4] = { {3, 2, 1, 0}, {0, 1, 2, 3} };
]==])
nds_replace(content [==[    case 0x0:			/* standby */
    case 0x1:			/* cctl */
]==] [==[    case 0x0:			/* standby */
      nds32_system_standby (cpu, __GF (insn, 5, 2));
      break;
    case 0x1:			/* cctl */
]==])
nds_replace(content [==[  if (wac && (base & 0x3))
    {
      nds32_raise_exception (cpu, EXP_GENERAL, SIM_SIGSEGV,
]==] [==[  if (wac && (base & 0x3))
    {
      nds32_system_exception (cpu, EXP_GENERAL, 0, base, 0, 0);
      nds32_raise_exception (cpu, EXP_GENERAL, SIM_SIGSEGV,
]==])
native_file("sim/nds32/interp.c" "${content}")

file(READ "${NDS_UPSTREAM}/sim/nds32/sim-main.h" content)
nds_replace(content [==[  /* If NIF_BRANCH, this is the destination address.  */
  SIM_ADDR	baddr;

  sim_cpu_base base;
};

]==] [==[  /* If NIF_BRANCH, this is the destination address.  */
  SIM_ADDR	baddr;

  /* Native system mode keeps external pin state separate from guest SRs. */
  int system_mode;
  int system_sleep;
  unsigned system_standby;
  int system_consent;
  int system_wakeup;
  uint32_t system_levels;
  uint32_t system_edges;
  unsigned system_irq_priority;
  int system_irq_entry;
  void (*system_software_output) (void *, int);
  void *system_software_data;

  sim_cpu_base base;
};

]==])
native_file("sim/nds32/sim-main.h" "${content}")
