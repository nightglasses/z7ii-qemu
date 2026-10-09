/* Native NDS32 system boundary, GNU simulator lifecycle remains authoritative.
   SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef GDB_NDS32SYSTEM_H
#define GDB_NDS32SYSTEM_H

#include <stdint.h>
#include "gdb/remote-sim.h"

struct hw;
struct nds32_boundary_ops {
  unsigned (*read) (void *, uint32_t, void *, unsigned);
  unsigned (*write) (void *, uint32_t, const void *, unsigned);
};

/* The caller supplies real peer/peripheral behavior.  Zero bytes transferred
   raises the guest bus exception; this API never manufactures a response.
   Callbacks are copied, opaque belongs to the caller until sim_close(). */
SIM_RC nds32_system_attach (SIM_DESC, uint32_t, uint32_t,
                           const struct nds32_boundary_ops *, void *);
SIM_RC nds32_system_irq (SIM_DESC, unsigned, int);
/* Board-level power permission and wake input: never asserted by the CPU. */
SIM_RC nds32_system_consent (SIM_DESC, int);
SIM_RC nds32_system_wakeup (SIM_DESC, int);
/* More than six IVIC inputs route the software output outside the CPU.
   No implicit pin binding is made. */
void nds32_system_software_output (SIM_DESC, void (*) (void *, int), void *);
/* Runs for at most this many mature simulator instruction/event ticks. */
SIM_RC nds32_system_run (SIM_DESC, uint64_t);

#endif
