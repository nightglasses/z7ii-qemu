# Audio observation registers

Later graphics and RAM progress is tracked in the
[current startup frontier](../../validation/frontier.md). This document
retains its Audio-only receipts and final snapshots.

The finite Audio profile now exposes both paired observation getters used by
the original driver. The normal continuation passes the input getter and next
rejects a graphics stream submission. No microphone data source, input DMA
completion or GUI first frame is supplied by this change.

## Direct source facts

Code addresses are C1.70 MAS VAs. Register offsets below are relative to board
PA2c080100 or2c080200. `1036e3b4` reads full words at54/58; `1036e384`
has the same shape at5c/60. Both return error2 without reading when the
per-channel software mode cell at MAS RAMb1805ba0 is not1. Functions
`1036e344/1036e354` set that RAM cell to1/0 without an MMIO trigger or clear.
The microphone initializer enables it; the stop path disables it.

The normal caller chain is `102e0bdc`→`102df00a`→`1036e3b4`.
The wrapper narrows each full word to16 bits. The consumer takes each high
byte and maps signed amplitude bands to levels0..14 through`102e0af4`.
Thus these reads feed an audio-level consumer, rather than a readiness or
DMA-completion test. The software reads both words directly, without a wait
or acknowledgement transaction in this getter.

The matching [manufacturer-authored Audio header](https://github.com/killsmm/m10v_mono/blob/cebff0686425b739bf63e87c911591ab30b03c1a/ca7_rtos/target/cpu_tkernel/basefw/MILB_API/MILB_Header/include/Peri/_audio_if.h)
ends its named per-channel fields at48; it does not identify54..60. The second
pair's output association follows the matching getter shape and adjacent
input/output ordering. Physical register names, peak-hold behavior, clear on
read, sampling cadence and the full32-bit hardware encoding are not established.

## Explicit functional profile

| Offsets | Functional readback | Producer |
|---|---|---|
| 54/58 | Input observations, initially zero | Disconnected; retain reset value |
| 5c/60 | Output observations, initially zero | Actual virtual FIFO sink consumption |

This is a last-observed-value hypothesis, not a recovered peak detector. For
combined output, the consumed opaque32-bit word contributes low16/high16 to
the two observations. Separate output contributes low16 from each consumed
lane, only when both lanes are present. Each read zero-extends16 bits. These
packing and high-bit choices are functional conventions; the original getter
does not prove them. Format metadata still does not implement physical PCM
conversion or host audio playback.

Queued data, disabled output, underflow and reads preserve the previous
observation. A later quieter word replaces it. Per-channel reset, system reset
and unrealize clear observations and cancel pending consumption. Aligned
32-bit reads of all four offsets on both channels are supported. Writes,
wrong widths, unaligned accesses and adjacent holes remain errors; the MMIO
aperture ends at264. No input FIFO word, occupied stage, DREQ, status, IRQ,
guest software event or callback is produced by a monitor read.

## Native and original validation

The new native read case submits a real HDMAC beat from an Audio monitor to
RAM and checks SS5 plus the overwritten destination. That distinguishes a
successful zero-valued register from qtest's zero readback on a failed bus
transaction. Against the old model it fails with SS3 (08030000 rather than
08050000). The applicable batch passes7 Audio,10 DMA and5 HDMAC cases.
New cases cover all8 registers, repeated/empty reads, actual combined and
separate output consumption, a missing lane, underflow, quieter replacement,
read-only/width/hole guards, channel isolation, reset and unrealize.
Independent read-only review found no implementation defect and required the
physical-semantic qualifications above.

The unchanged original bundle keeps the prior declared TEMP/descriptor/
acceleration inputs, RNG0, cold F4 and healthy normally paced battery peer.
The old run reproduces CPU0`1036e3d8`, READ32 PA2c080154. The new normal
30-host-second runs cross both reads and the original amplitude consumer.
All observation values remain0 with no connected input producer; no input
sample transfer or audio completion is claimed.

The next first uncovered access is CPU1`106ee3c6`, WRITE32 PA201810e0=1,
reason`transfer-stream-truncated`. The original paired-stream submitter is
`106ee04a`. This is a parser frontier, not yet proof that the guest's submitted
bytes are physically truncated. Further stream dispatch/command interpretation
belongs to the graphics model. The final submit is856/1088 bytes at physical
sourcesbd0dff90/bd15ff90 (64-bit unit counts6b/88). Both full snapshots are
saved; their geometry fields include720x480. Draw word50 is0, so the existing
path-family dispatch does not select them, and their sizes match none of the
other dispatch cases. The fallback LUT parser expects224/1688 and emits the
truncation error. This identifies the current rejection mechanism without
claiming the new packet's drawing semantics. Application bytes remain03/02, primary request
state11 and GUI gate/pending3/display-ready remain0. The UART peer rejects no
request. First-frame and physical microphone behavior remain unverified.

Raw source projections, red/native receipts, normal-run argv, traces and final
read-only snapshots are in `run audiolevels-20261023/`. The older
[format](format.md) and [reload](../hdmac/reload.md) records retain their
original checkpoints. Diagnostic stream snapshots must be paired with their
actual submit lengths; earlier partial snapshots are not complete packets.
