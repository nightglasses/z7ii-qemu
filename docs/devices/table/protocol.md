# Requested table samples

The device owns `20204000..2020442b`, with board IRQ connected to GIC121. The ec048f saved native source closes configuration `103b020a`, request `103b046a`, ISR `103b0020` and request/wait consumer `103b5330`. A configuration table and three results do not establish physical pixels, Bayer, ROI or a sensor identity.

`103b046a` sets44 bit16, then writes `14=1`. The model raises named `sample-request` GPIO for one outstanding transaction after this sequence. A disconnected request remains outstanding with no ready flag, data, IRQ or guest event. Providers explicitly publish all96 `data-bit` inputs after the request and raise `external-event[0]`. Fresh complete publication atomically snapshots194/198/19c, lowers the request and latches2c bit8. Partial, stale and unsolicited publications cannot complete it.

ISR `103b0020` copies raw2c into its guest object and ACKs before reading all three words. It then RMWs00/04 bit0 and clears44 bits16..18. Those controls and ACK preserve the sample. Raw17/25 originate only from explicitly driven external events1/2. Pending8/17/25 follows the W1C functional convention and drives GIC121 directly;20 is an ordinary parameter. Callbacks and RTOS handling remain original guest code.

Software wrappers are resolved: `103854cc` adds14c to the object and clears the requested flag mask via `103610d2`→`101043a4`; `103854d4/dc` wait via `103610da/10361116`→`10104a14` with mode1. The former waits indefinitely, the latter preserves the caller's timeout. The consumer waits for software bit3 after requesting. This binds control/IRQ/software wait without inventing an image processor.

| Aligned32-bit fields | Software-generated values |
|---|---|
| 00,04,08 | bit0 RMW, preserve other bits |
| 40 /44 /48 | changed masks7003f /70003 /3 |
| 20,400,420 | fullword |
| 4c,50 /54 /58 /d4,190 | masks7fff /0fffffff /3fff /1fffffff |
| 184,1a0 /188 /18c /404,424 | bool /low4 /low8 /low16 |
| 5c..94 andd8..110 | separate15-word low14 arrays |
| 98..d0 and114..14c | separate15-word low4 arrays |
| 150..180 |13-word low4 array |
| 200..3fc |128-word table,mask3fff3fff |

Optional table pointers cause original firmware writes, never host default tables. The first array starts5c and ends94;98 starts its distinct neighbor. Configuration1a0 is independent of result194/198/19c. Original `103b04cc/04d6` read408/428 into software words; the saved source contains no producer/processing rule for them. These reads remain explicit errors until the endpoint is established, rather than invented counters or guessed DMA through400/420.

The request GPIO, handshake, cold state/readback and W1C are disclosed functional conventions. No physical source or transform is asserted. Reset clears pending, request, validity and publication tracking, preserves real input levels and creates no edge. The device is unmigratable.

Native regression `tests/qtest/nikon/sampling.c` covers activation, partial96-bit publication, atomic delivery, realGIC121, original ACK/control order, retained snapshots and freshness on rearm. Execution evidence belongs to the sole native builder's fixed batch receipt and is not claimed before the run.
