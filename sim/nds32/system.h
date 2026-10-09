/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef NDS32_SYSTEM_H
#define NDS32_SYSTEM_H

#include "nds32-sim.h"
#include "gdb/nds32system.h"

SIM_RC nds32_system_options (SIM_DESC);
void nds32_system_reset (sim_cpu *);
void nds32_system_poll (sim_cpu *);
void nds32_system_standby (sim_cpu *, unsigned);
int nds32_system_waiting (sim_cpu *);
void nds32_system_exception (sim_cpu *, unsigned, unsigned, uint32_t, int,
                             unsigned);
void nds32_system_iret (sim_cpu *);
int nds32_system_big_endian (sim_cpu *);
uint32_t nds32_system_read_sr (sim_cpu *, unsigned);
void nds32_system_write_sr (sim_cpu *, unsigned, uint32_t);
unsigned nds32_system_read (sim_cpu *, uint32_t, void *, unsigned, int);
unsigned nds32_system_write (sim_cpu *, uint32_t, const void *, unsigned);

#endif
