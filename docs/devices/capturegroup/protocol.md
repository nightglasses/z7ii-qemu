# Capture-group configuration

The finite device retains144 source-identified configuration words at board
PAs`[2006b800,2006bd84)`. Its one SysBus region is exactly584 hex bytes;
all holes and unknown result addresses within that interval remain transaction
errors. This is a configuration contract, not a source of capture readiness,
frames, DMA, IRQs or completion events.

## Original producers and masks

Original MAS source projections, diagnostic execution and native receipts
are scoped separately under`work/runs/capturegroup-20261024/`. Addresses in
the table are board PAs, with the common prefix2006 omitted. Ranges step by4
unless stated otherwise. Masks bound complete values constructed by the
identified software; they are not recovered physical IP permissions.

| Words | Accepted bits | Original MAS writer |
|---|---|---|
| `b800/b804/b81c` | `ff` | `10208e96`, separate byte fields |
| `b808..b818` | `ffff` | `10208e96`, five halfwords |
| Three8-word rows at`b820/b840/b860`: first word | `ff` | `10208cb0` |
| Same rows: remaining seven words | `7f` | `10208cb0`, masked byte stores |
| `b880..b980`,65 words | `ffff` | `10208db6`/wrapper`1020c14a` |
| `b984` | Full32-bit | Original unmasked three-argument packing API |
| `ba00/ba04/ba08/ba0c` | `ffff` | `10208cf4` |
| `ba10` | Full32-bit | `10208f1e`/wrapper`1020c1e2` |
| `ba14..ba28`, six words | `ff` | `10208cf4` |
| `ba2c` | Full32-bit | `10208f12`/wrapper`1020c1d4` |
| `ba30` | Only0/1 | `10208ed0`, known count0/1 packet footprint |
| `ba34..ba50`, eight words | `ffff` | `10208ed0 -> 10208f26` |
| `ba94` | `ffff` | `10208cf4` |
| `ba98/ba9c` | `7fffff` | `10208cf4`, halfword OR byte shifted by15 |
| `baa0` | Full32-bit | `10208cf4`, complete word payload |
| `baa4/baa8/baac` | `ffff` | `10208cf4` |
| `bab0/bab4/bab8` | `ff` | `10208cf4` |
| `bb18/bb24/bb44/bb48/bb4c` | `ff` | Byte setters in`10208dd8..10208e96` |
| `bb3c` | Full32-bit | `10208e76`/wrapper`1020c194` |
| `bb50/bc50/bd50`, `bb60/bc60/bd60` | Full32-bit | `10208e4a`/wrapper`1020c186` |
| `bb80/bc80/bd80` | Full32-bit | `10208dd8` |

The isolated b984 setter has no identified static caller in the scoped
analysis. Its unmasked API establishes only raw configuration retention;
it supplies no evidence for side effects or a result at the neighboring b988.
The latter remains unsupported.

The ba10 API stores its original32-bit argument directly, without truncation
or masking. A current upstream halfword value does not narrow that API's
accepted payload. Likewise ba2c stores `a2 | (a1 << 1)` without masks; the
current caller's boolean values0..3 do not justify a whitelist for3. Bb3c and
all opaque address words also preserve their complete input. No bit in these
words is promoted to a run, enable, ready or completion action.

## Count and retained packet semantics

`10208ed0` computes `(1 << count) - 1` at ba30 and emits parameter records through
the four-halfword helper`10208f26`. The identified successful provider path
`102aa02a` supplies count1 at output+340, whose two four-word writes cover
exactly ba34..ba50. A NULL provider can return without filling the output;
the wrapper may
reuse an older buffer, so that observation does not establish arbitrary
counts, more record locations, or clear-on-disable behavior. The finite
contract therefore accepts only masks0/1, representing count0/1. Other masks remain rejected;
ba54..ba90 remains unknown rather than receiving a speculative wider array.

Count0 produces no subsequent payload stores in the original loop. Writing
ba30=0 consequently preserves all eight retained halfword parameters. More
generally each accepted MMIO store replaces only that word: there is no
implicit batch reset, adjacent-field clearing or auto-population of omitted
fields.

`10239e36` prepares the group through the ordinary configuration wrappers
and obtains three provider slots through`1030b730`, `1030b764` and`1030b7b4`.
The two address sets and extra words at bb/bc/bd are independent retained
payloads. Their physical memory meaning is unresolved, and the model neither
validates them as DMA destinations nor accesses the referenced RAM.

`10239dc6` reads the existing result packet through`1020c17a`, conditionally
updates ba2c and then writes ba10 through`1020c1e2`. Its comparison involving
the returned bb74 byte is not a producer of that byte. The model must not
return a matching byte to force this caller forward.

The full configuration branch is `1023abaa` with flag40 and a non-NULL
pointer-array slot4. It configures shared unit20 through `1020c01a`, calls
`10239e36`, then calls `10239dc6` with force1. Force bypasses the identity
comparison, but still reads the complete old result packet first. Only after
that call returns does `1020c07e` submit mask10 to the shared command ports
at20068100/104 and call `10208c46 -> 1020aafc` to request run for unit20
at20068008. This last call is run, not reset. The incremental branch through
`1023aed8` calls the same result consumer with force0 before its later shared
commit. Neither path establishes a result producer at bb70..bd7c.

## Unsupported results and state boundaries

All accesses must be aligned little-endian32-bit. Unknown widths, offsets
and upper bits reject before mutation. Known configuration reads return the
last accepted value. Reset and unrealize clear that retained configuration
under an explicit functional cold-zero/readback convention; physical reset
values and universal physical register readability are unestablished.

`10208e0a`/wrapper`1020c17a` consumes eight distinct32-bit registers:
bb70/bb74 and the bb/bc/bd78/7c result words. It truncates the first two to
bytes at output+0/+1, then stores the six full words at+4 through+24.
Bd7c is read twice, including the return value; the source supplies no
read-to-consume semantics. `10239dc6` uses only output byte1 for its identity
comparison against MAS VA`b26b47cb`. The requested identity also reaches bb44,
but a desired/applied identity relationship between bb44 and bb74 remains a
hypothesis, not a recovered producer. Neither zero-valued requested identity
nor force1 establishes zero hardware results. Their providers are unknown,
so reads and writes there remain errors.
The same applies to all other holes, including b988 and ba54..ba90. Zeroed
host storage does not make those addresses readable.

The device has no timer, IRQ, request output, frame input or DMA engine.
The separate shared capture-control unit`0x20` reset/enable/disable and commit
paths retain their existing contracts. Configuring this group does not
trigger that device's stop fence as a substitute for a normal data result.
Readout admission and CMOS observation completion likewise have no recovered
mapping to this result packet. The next evidence needed is the packet's cold
state and update trigger, including whether identity latches at configuration,
shared commit or frame completion; none is inferred from those other events.

## Validation and execution scope

The original diagnostic red uses unchanged firmware and an explicit
functional resource80 input supplied through the original NOR loader. Only
its cold11/subtype3 branch is qualified; it is not factory data, complete
normal startup or a normal GUI receipt. The saved final diagnostic snapshot
is read-only after20 host seconds. Such progress is reported separately from
native synthetic protocol acceptance.

The parent's scoped `native-group-red.log` writes b800=ff in the new fixture
and reads0 from the old uncovered model. The fixture
`/arm/nikon/sensor/capture/group` covers all144 fields across four independent
data patterns, every bounded field's unknown upper bits, high raw32 payloads,
three independent opaque address sets, count0 retention and unsupported masks
(including encoded count2/3) rejection, byte/halfword/unaligned accesses, both aperture boundaries, reset,
and absence of capture run intent, pending status, IRQs, readout requests or
RAM changes after virtual time advances. HMP physical reads assert an actual
MemTxResult error at all209 unknown aligned words before and after reset;
a zero-filled qtest error buffer is not treated as a successful status read.
The parent's `native-group-green.log` records all30 Sensor subtests passing.
This native synthetic receipt is separate from the following execution.

The unchanged20-host-second `green` run uses the same generated cold-neutral
file. All six original loader COPY ranges compare with it; version guard
`10305dac` returns0 at `102d7254`, packed150, and property6433 remains empty.
Request flags2ea enter `1023abaa`, then `10239e36(b1887ef8,0)` configures this
group. Its b800/b804 values8 and three bb/bc/bd80 valuesc00 are retained;
the packet count is1 and requested identity is0. Execution then calls
`10239dc6(b1887ef8,1) -> 1020c17a -> 10208e0a` and first rejects CPU0
PC`10208e14`, READ32 PA`2006bb70`. The original ba30/count writer has not yet
executed, so the model's retained ba30 is still cold0 despite the source
packet's count1. No unit20 entry into `1020aafc` occurs in the scoped trace.
The source order places its configuration commit/run after the blocked
result read, not before it. QMP reports debug/stopped and neither display
head has a console. This crosses the configuration frontier under declared
functional assumptions; it does not establish normal GUI output or complete
boot. Raw source projections, trace, read-only snapshot and proof remain in
`work/runs/capturegroup-20261024/`.
