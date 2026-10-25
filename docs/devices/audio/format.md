# Audio format and block configuration

Later continuation: the [HDMAC reload model](../hdmac/reload.md) crosses the
descriptor rejection recorded below, arms both input channels and reaches
input enable plus the initializer return sequence. The subsequent
[observation profile](monitor.md) crosses the separate54/58 getter and next
rejects graphics submission PA201810e0=1. The receipts in this
document retain their earlier scope; actual input samples remain unverified.
Later graphics and RAM progress is tracked in the
[current startup frontier](../../validation/frontier.md).

The normal GUI continuation reaches microphone initialization, not an output
sample transfer. The old profile rejected its legal AUMD fields. This model
closes the complete source-shaped format, port, divider and block-programming
family while keeping data production and physical PCM interpretation separate.
Code addresses below are C1.70 MAS VAs; register offsets are relative to either
channel block at board PA `2c080100/2c080200`.

## Source facts

`102de9bc` builds a ten-byte input record and passes it to `1036da82`:
`00 0a 01 00 00 00 01 00 00 01`. The setter updates AUCR08, AUMD0c and
AUDP18 by RMW. The input AUMD becomes `0d820000`: stage16 selection at24,
input halfword swap at17, stereo at27, and fixed original bits23/26.
The old mask `07800000` rejects both17 and27. The original setter does not
poll status or request an IRQ at this store.

The corresponding output setter `1036da06` uses the other format/swap/port
fields. `1036db04` disables DIVE, updates the full5/3/2-bit clock fields and
reenables it. Its interface does not impose the old odd-MCK-code whitelist.
The related `1036dd7c/ddd6` methods program the block controls,16-bit block
lengths and guest callback pointers. `1036e444/e3ec` then set IRQ/DMA masks;
`1036dd3a/dd58` start/stop input through AUCR12, while `1036dd04/dd1c`
start/stop output through13. These methods keep the original guest state,
callback registration and DMA programming in guest execution.

| Register | Source-selected fields | Finite behavior |
|---|---|---|
| AUCR08 | input format19:16/reverse23, output format30:24/reverse31, existing selection/enable/mask fields | Full4-bit/7-bit labels replace the old code whitelist; FIFO-derived8/9 remain read-only observations |
| AUMD0c | swap19:16, fixed/retained23/26, stages24/25, stereo27 | Mask `0f8f0000`; output stage selection changes actual FIFO capacity; other format fields label the opaque endpoint |
| AUCC14 | MCK28:24, CK20:18, LR17:16, divider enable30 and clock-output31 | Mask `df1f0000`, including every5/3/2-bit field encoding |
| AUDP18 | mix0, LR polarity8, input/output format16/17, output source25:24, input source29:28 | Mask `33030101`; retained wire-format metadata, no host PCM rendering |
| AUDMA2CTL40 | input0/output8 | Mask `101`; explicit block configuration, no autonomous completion |
| AUDMISAMPLE44 / AUDMOSAMPLE48 | low16 | Independent programmed lengths, mask `ffff`; no inferred terminal-counter IRQ |

The manufacturer-authored
[Audio I/F header](https://github.com/killsmm/m10v_mono/blob/cebff0686425b739bf63e87c911591ab30b03c1a/ca7_rtos/target/cpu_tkernel/basefw/MILB_API/MILB_Header/include/Peri/_audio_if.h)
independently names the swap, stereo, port, divider and block fields. Its
six-channel layout/base differs from this two-channel composition; original
bits23/26 are not assigned a physical meaning from that header.

## Functional assumptions and execution boundaries

The existing virtual sink consumes opaque32-bit words. Accepting stereo,
swaps, routing and format labels does not assert decoded PCM, pin behavior,
mixing fidelity or audible output. Such metadata cannot create input samples,
occupied FIFO stages, DMA beats, IRQs or guest callbacks. Loopback31 remains
rejected because it would require a new data-producing path.

The independent nominal48MHz clock and divider law remain functional
hypotheses: one output beat takes `(MCK+1)*4*(CK+1)*16*(LR+1)` clock ticks.
QEMU Clock's fixed-point conversion can differ from exact rational time by1ns.
Only enabled output with DIVE and released reset consumes real queued words;
an empty enabled tick sets the existing W0C underflow flag. Capacity changes
and combined/separate lane changes remain rejected while their queues contain
data. Mask, occupancy, backpressure and HDMAC completion laws are unchanged.

Block programming is distinct from dual-channel DMA execution. Input is
disconnected, so even input0 enabled with a nonzero block length supplies no
data, DREQ or completion. Output8 may be programmed while idle, but starting
output or writing a FIFO under that mode remains an explicit error. Switching
into it during active output or with queued words also fails before mutation.
The second request/port selection and block-switch protocol must be bound
before this unsupported output topology can execute. This profile never
silently consumes such a transfer as ordinary single-channel output.

Reset clears block state with the other channel fields. Selected reset and
disable cancel output timing; unrealize resets both channels and cancels both
timers, preventing stale consumption or underflow after removal. Unknown
addresses, bits, widths, unaligned accesses and empty input reads remain errors.

## Native validation

The new `nikon_audio` executable uses the existing native source graph.
`make check MTESTARGS='nikon_audio nikon_dma nikon_hdmac'` passes4+7+4 cases.
The audio format case first fails on the old model's exact `0d820000` store.
It covers all512 AUMD combinations,256 port combinations, all128 output format
labels and16 input labels on both channels. The clock case tests all1024
divider combinations using actual enqueue, pre-deadline occupancy and timed
drain. Block, FIFO preservation, input absence, invalid fields/width/alignment,
unrealize/re-realize, reset and W0C are separately exercised. Qtest bus reads
do not independently expose MEMTX_ERROR; negative cases verify preserved state.

The applicable DMA/HDMAC cases preserve their real-memory transfer, pressure,
clock drain, request gating and reset checks. These synthetic results are
independent of original startup or physical audio validation. Ignored receipts
are in `run audioformat/`, including source projections,
original/native red, `native-final.log` and `native-final-receipt.log`.
The two independent-review guard assertions for entering block-output mode
while enabled/nonempty pass in a focused4-case audio rerun, recorded in
`native-guards-receipt.log`; the unchanged DMA batches were not repeated.

## Original continuation and next boundary

The30-host-second final-device run keeps original bundle bytes unchanged and
uses the existing TEMP3/descriptor18, empty descriptor12, acceleration(0,0,1024),
RNG0, healthy paced UART peer and cold held-F4 inputs. A final read-only GDB
snapshot and bounded TCG trace observe `1036da82` completing its stores and
returning to `102dea1c`, followed by `1036ddd6` and `1036e444`.
Channel0 has AUCR `000a0200`, AUMD `0d820000`, AUST `d2000000`, AUCC
`c3010000`, AUDP0, block-control1 and input length `5dd8` (24024). Both input
and output occupancy words remain0. The UART peer rejects no request.

The next first rejection is CPU0 PC `102dc47a`, WRITE32 PA `2c030034`,
value `2ae00000`, reason `hdmac-access-or-command-not-supported`.
Original `102dc41e` constructs this CB word from the request record's width,
mode, increment and three flag fields. The current HDMAC guard rejects the
`00e00000` field group; it is a new whole descriptor-semantics investigation,
not an unowned audio register. The microphone initializer has not reached its
final input-start call or returned, and no input sample, DMA completion or
audio callback is claimed. Application bytes03/02, primary request state11,
GUI gate/pending3/display-ready0 remain; GUI first frame is unverified.

Receipts include `green-argv.json`, `green.trace`, `green.json`,
`green-process.json` and the source projection for `102dc41e` in the same
ignored directory. This is original execution under declared functional
hypotheses, separate from native fixtures and physical-camera validation.
