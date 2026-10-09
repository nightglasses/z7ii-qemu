/* Source-backed TPJ peripheral endpoint for the native GNU simulator.
   SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef NDS32_TPJ_H
#define NDS32_TPJ_H

#include <stdint.h>
#include "gdb/remote-sim.h"
#include "hw/nikon/wire.h"

struct tpj;
typedef int (*tpj_emit) (void *, unsigned, unsigned, uint64_t, unsigned,
                        uint32_t, uint32_t, uint64_t);

/* Transport opaque and the endpoint's registered memory live through sim_close.
   Quiesce before sim_close; free the endpoint with tpj_close after sim_close.
   The physical CLOCK/header binding is deliberately not guessed by this API. */
struct tpj *tpj_open (SIM_DESC, tpj_emit, void *);
void tpj_close (struct tpj *);
void tpj_quiesce (struct tpj *);
int tpj_snapshot (struct tpj *, const char *);
int tpj_flash (struct tpj *, const char *);
int tpj_update (struct tpj *, const char *);
int tpj_save (struct tpj *, const char *);
int tpj_transform (struct tpj *, const char *);
/* Finite source-typed stream: LE32kind, LE32ticks. NORMAL adds LE32processing
   kind (0 NONE/1 BUFFER/2 SCAN), LE32processing ticks, then220words:896B.
   CAPTURE is request-matched384halfwords; REFERENCE is a baseline frame.
   No default sample, automatic repetition or contact packet is supplied. */
int tpj_samples (struct tpj *, const char *);
int tpj_boundary (struct tpj *);
/* One shutdown receipt of observed register/transport activity; no guest edits. */
void tpj_report (struct tpj *);
int tpj_clock (struct tpj *, uint64_t, uint32_t, unsigned, uint64_t);
int tpj_i2c (struct tpj *, uint64_t, unsigned, uint32_t, uint64_t);
void tpj_cancel (struct tpj *, uint64_t);
int tpj_signal (struct tpj *, unsigned, uint32_t, int, uint64_t);
void tpj_flush (struct tpj *);

#endif
