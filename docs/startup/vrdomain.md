# VR execution boundary and mainline relevance

Status: defer further VR boot probes while investigating the camera's first GUI
frame. Keep the standalone RX profile, peripheral contracts, external-input seam
and tests. The observed MAS SPI4 exchange has not demonstrated a causal gate on
display startup. This is a priority decision for the measured boot, not a claim
that VR is unnecessary for every camera mode.

## Current source and validation

`vr_map_ram()` in `hw/nikon/machines/vr.c` maps the merged COPY/ZERO
destinations declared by the guest plus the separately named caller stack
`[0x1000,0x2000)`. It does not add the probe-only ranges `[0,0x0fe0)` and
`[0x2000,0x2100)`. Their removal retires that experiment; it does not establish
that those addresses are absent on the physical MCU. COPY/ZERO tables describe
initialisation, not total installed RAM.

The temporary `li()` change in ignored
`work/config/source/target/rx/translate.c` is also absent from the regenerated
source graph. `cmake/rx.cmake` retains the existing explicit EXTB capability;
the official `qemu/` submodule is unchanged.

On 2026-10-21, `make prepare` followed by
`make check MTESTARGS='nikon_vr nikon_peripherals' -j8` rebuilt the RX machine
and translator and passed both executables: 2 machine subtests and 20 peripheral
subtests, zero failures. Receipt:
`run validation/vrtriage/focused.txt`. These are synthetic native contracts,
not original-firmware boot or reply receipts.

## Unchanged VR execution

The bundle carries 3200 contiguous 256-byte body records at RX domain VA
`[0xfff00000,0xfffc8000)`, preceded by metadata. `nikonvr` starts this loaded
component at `0xfff00010` with caller SP `0x2000`; it does not supply a cold
BootROM, a resident implementation or a fixed exception table.

The observed bootstrap order is:

| RX domain VA | Operation |
|---|---|
| `0xfff00035` | call `0xfff001f8`, PORT/MPC initialisation |
| `0xfff00039` | call `0xfff005bd`, timers/SCI/DMA/DTC/ADC initialisation |
| `0xfff0003d` | COPY table walk |
| `0xfff00117` | ZERO table walk |
| `0xfff00167` | read the four-byte pointer at `0xfffc8008` |
| `0xfff00169` | call through that pointer |

The first uncovered transaction is the read at `0xfff00167`. The slot is eight
bytes beyond the uploaded body's end, not a truncated record or an SPI reply.
SCI initialisation runs before this stop; servicing SCI5 interrupts and producing
an original guest reply have not been demonstrated.

`nikon-vr-input` and `nikon-vr-span` can supply exact caller-owned spans in
`[0xfffc8000,0x100000000)`, with explicit origin and qualification. Their holes
return `MEMTX_ERROR`; they generate no resident slot, signatures, table or
instructions. See [vr.md](vr.md) for the input and peripheral contracts.

## Two-process transport observation

The ARM and RX targets are separate QEMU executables. The tested wiring used
`nikon-spi-peer` on MAS SPI4, a `nikon-peer` socket, and `nikon-vr-peer` linked
to `/machine/vr-peripherals` in the RX process. The camera's GPIO pin 177 supplies
SELECT; the VR adapter applies it to P47 and clocks actual configured SCI5 state.
Only a guest-written TDR byte can become a reply. The adapter supplies no boot
state or acknowledgement.

The processes connected, but the RX guest stopped at the resident slot. In the
60 s camera window the monitored display, GUI and EX progress words did not
change. This establishes transport connectivity and the VR execution boundary,
not an operational original-firmware endpoint. The processes also have independent
virtual clocks; no cross-process pause or ordering contract is claimed.

## Retired hypothesis probes

These runs supplied invented data in addition to unchanged body bytes. Their
results describe those assumptions only. They are not the current machine
contract and do not prove original startup.

| Supplied assumption | Observed consequence |
|---|---|
| `0xfffc8008 = 0xfff1d936`, a body address containing `rts` | returns from the absent resident hook; later reads `0xffff60fd` at RX VA `0x1871b` |
| same stub plus an erased high page `[0xffff0000,0x100000000)` | gets past a copied-table device-name comparison; reads unmapped `0x20f8` at `0x1dd27` |
| same inputs plus RAM `[0,0x0fe0)` and `[0x2000,0x2100)` | clears the observed RAM gaps and reaches the RX translator assertion below |
| same inputs plus a temporary translator read-order fix | reaches invalid control flow: the erased-page run attempts a fetch at `0xffffffff` |
| zero-filled high page in the diagnostic setup | reaches `0xffffff7d` and uncovered exception-stack writes near the top of the address space |

The first name comparison uses a copied table at RX VA `0x11510`, with 20-byte
entries. Its first present entry has base `0xffff6000` and name offset `0x100`;
the string comparison against `EMDM` explains the `0xffff60fd` access. It is
not evidence of an uninitialised global.

The RAM additions also had observed consumers: the low initialiser writes
`0..0x9ff` and reads a signature at `0x0af8`; the structure at `0x2000` has a
signature at `+0xf8` and a 256-byte copy. Extending the low range to `0x0fe0`
joined the first ZERO range. This motivated a probe, but did not recover a full
RAM map or qualify the resident-hook assumptions.

The translator assertion was independently located in `li(ctx, 3)`: after a
two-byte opcode at RX VA `0x16e75`, it read immediate byte `0x16e79` before
bytes `0x16e77..0x16e78`. That violates `record_save()`'s contiguous-fetch
invariant. Reading the low two bytes first removed the assertion in the ignored
build tree. That diagnostic change was not added to the maintained CPU overlay.

**Control-flow qualification:** changing the supplied high page changes the
subsequent jump/exception path. This supports an unresolved pointer or vector
dependency, but the recorded runs do not locate the selecting read or establish
a table layout, all call targets, or which bytes are code. The earlier blanket
claim that the page is a recovered function-pointer table was too strong. The
fixed vector interval `[0xffffff80,0x100000000)` lies inside this same supplied
page, not in a separate area beyond it. An all-`rts` table would add another
unqualified interface assumption and is not the next mainline experiment.

Original ignored probe material remains in `run ex-6b-ercd/`, including
`run-vrmock.sh`. Its results depend on the retired RAM and translator changes;
running that script against the current binary does not reproduce the deepest
historical stop.

## Why VR boot is deferred for the GUI investigation

The stronger relevance test is on the MAS side, independently of RX boot. The
[transport investigation](../runtime/transport.md) identifies a fixed
thirteen-transaction SPI4 script and the reply field it actually tests: byte 14,
bit 2. An experimental peer setting that bit increased transaction rate by about
4.7 times. In that same window, 66 transactions and 45 pump iterations left all
four measured points cold:

| Domain VA | Observation point | Executions |
|---|---|---|
| MAS `0x104d7e78` | display provider admission | 0 |
| MAS `0x1053e19a` | GUI dispatcher | 0 |
| MAS `0x10695450` | event-`0x39` poster | 0 |
| EX `0x0803350c` | ladder poster | 0 |

This is a bounded negative result under synthetic replies. It does not validate
all VR commands or exclude a later dependency. It does show that satisfying the
observed ready-bit wait alone does not start the GUI. Further RX boot work would
first require resident/platform semantics absent from the bundle, with no
demonstrated connection to the current display gate.

Keep VR for standalone peripheral/protocol tests, update-storage work and future
guest-produced peer replies. Resume its boot investigation when a MAS branch or
wait is traced to a specific VR result needed by the mainline, or when recovered
resident bytes make the existing stop actionable. Do not add a ready peer or
no-op ROM hooks to the default camera to make it appear complete.

The [subsequent mainline investigation](../runtime/ptp.md) identifies F091 as a
PTPCMD parameter subcommand. Its task waits before command reception, and its
session/F001 service-mode gates are closed. A separate firmware readiness call
at MAS VA `0x1062f120` requires event bit `0x80000000` and pending slot 3; the
observed values are zero. These startup producers, rather than VR boot or an
automatic PTP sender, are the next mainline target.
