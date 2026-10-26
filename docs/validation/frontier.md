# Original-firmware startup frontier

## Current: fixed-lens status GUI and dynamic paired-parameter boundary

The [ordered vector model](../devices/transfer/vector.md) executes the complete
mixed SRC clear / SRC_OVER fill-and-stroke family, including relative QUAD and
full or inherited state. All 28 native transfer subtests pass, including 19
mixed-batch variants. The whole batch is validated and rendered privately
before committing; no guest completion, status or display pixels are patched.

With the corrected fixed-lens operating profile, unchanged C1.70 firmware now
binds the original 1024×682 GUI without the former request to extend the lens.
The scene background remains black. In the 60-host-second cold-neutral window,
QMP is running at 15/30 seconds and debug-stopped by 45 seconds; paired work
advances through 0/36/76/76 jobs, writing 202,263,360 bytes. Lens exchanges
complete 843 times with zero rejection. Head0 remains unbound, GUI readiness is
zero and no panic dump is present. This is status-GUI progress, not normal
live-view acceptance or 60 seconds of continued execution.

![Original GUI without the lens-extension warning](/evidence/transfer/gui.png)

The strict stop is CPU1 MAS VA `1036a4a2`, WRITE32 PA `20380010=1`, where the
paired field at bank offset `598` changes from selected `1a6a1a6a` to
`17c717c7`. Its dynamic numerical processing remains the next image boundary;
the model does not silently ignore it or add another static tuple.

The source-named capture family is `800` / `LvMovie`, not `400` / `LvMovieRec`.
A separate read-only hardware-breakpoint diagnostic at MAS VA `102324cc`
captures config bytes0/24/263 as 0/1/0 and argument2 as zero: this secondary
selection explains the movie-live-view family. Property5772 and its cache both
hold1. The external selector's producer remains under investigation; neither
the lens's50 selector nor application main_mode is the photo/movie enum.
The [curated receipt](/evidence/transfer/vector.json) separates native, original
and diagnostic evidence, explicit inputs and remaining limitations.

## Previous: coherent fixed-lens operating and mode-query state

The [whole fixed-lens protocol](../devices/lens/protocol.md) now separates
stationary optical availability, configuration pending/commit, streaming and
mode-request processing. The original getter decoded the former zero at status
byte216 to logical1, making `40000500` unavailable for capture/live-view
admission. The fixed nonretracting optics now publish logical3. This is a
declared device input, not a guest eligibility or display-cache write.

The audit also adds the complete50/51 request/query group: three source-produced
selectors, whole-packet validation, independent2 ms processing and actual
pending status. It retains required normal capability bits, correct optical
coordinates and valid stationary history. Five additional biased coordinates
have unresolved physical semantics and remain explicitly qualified virtual
inputs; they are not blindly set to the iris value.

In the unchanged15-host-second cold-neutral run with F4 held, the original
capture policy at MAS VA `b2951140` is `800`, and its source-named label is
`LvMovie`. It is movie live view, not the `400` movie-recording family. The
optical state cache reaches3 with no lens rejection. Execution reaches a new
744/920-byte mixed graphics program: two SRC rectangle clears followed by
SRC_OVER fill and stroke of one rounded rectangle. CPU0 VA `106ee3c6`, WRITE32
PA `201810e0=1`, rejects this unsupported ordered batch. Both display heads
are unbound and paired work is0 at this earlier graphics stop; no panic is
present. The historical bound status GUI below belongs to the former
unavailable-optics input, not the corrected profile.

With F4 released, a separate60-host-second run stays running in application
mode5, completes22 lens packets without rejection, processes one50 request
and two51 queries, commits selector2 once and stops history through the
original16 command. Configuration remains committed. Head1 binds a status
console and capture family is0. This is a different idle application branch,
not proof of a photo-mode transition. The actual photo/movie selector is
upstream of capture configuration bytes0/24/263 and remains under investigation.

The [operating receipt](/evidence/lens/operating.json) separates15 native
subtests, old-model failures, these original runs and the remaining boundaries.
Neither normal live view nor photo/movie capture is yet accepted.

## Previous: dynamic paired color and a bound status GUI

The [paired pixel operator](../devices/pairs/pixels.md#four-lane-color-correction)
now executes the complete four-lane gain/offset/pedestal/bound packet at
`708..720`. Original producers establish Q9 gain arithmetic, negative black
offsets, interpolated pedestal values and the shared bound. Offset order,
direct RAW12 units and lane-to-RGB mapping remain explicit monochrome functional
hypotheses. Mode controls and the rest of `700..778` retain strict validation.
No guest completion, ready flag or framebuffer is patched.

The unchanged cold-neutral run uses the same fixed virtual lens, healthy UART
battery peer, held F4, five-CPU MTTCG and selected blank 8 GiB card. The complete
virtual tuple has zero slots for the newly interpreted packet. At 15/30/45/60
host seconds, paired completion advances through 0/48/76/76 jobs. These write
202,263,360 actual bytes, with zero cancellations. QMP runs through the 30-second
sample, then debug-stops by45 seconds; this is not 60 seconds of stability.

Head1 now binds the original 1024×682 status GUI. Its message says “Before
taking photos, rotate the zoom ring to extend the lens,” exposing a remaining
virtual-lens state mismatch. Head0 stays unbound and GUI readiness remains zero.
Actual paired main outputs are 1372×770 NV12 at PA `dec7fc00/e0702600`, stride
1376. The diagnostic RGB preview below reads those DMA bytes: four grayscale
input levels produce tinted quadrants under the finite color model. It is an
intermediate image, not the background of the bound GUI or calibrated imagery.

![Original status GUI](/evidence/paired/gui.png)

![Intermediate paired DMA pixels](/evidence/paired/pixels.png)

The next strict stop is CPU1 VA PC `1036a4a2`, WRITE32 PA `20380010=1`:
bank offset `598` changes from selected `1a6a1a6a` to `17c717c7`. That group
still requires producer and numerical interpretation; the model does not ignore
it or replace the tuple to hide the change. The lens completes826 packets,
rejects zero and supplies25,023 complete history records; configuration commits
once. The card receives one512-byte read with no failed I/O; no panic dump is
present. Existing EX external-line warnings remain.

The [affine receipt](/evidence/paired/affine.json) separates 33 native subtests,
red/green history evidence, this bounded original execution and preview decoding.
The next work is the dynamic `598` family and the normal producer of the lens
extension state. Normal live view and a 3D application bridge remain unaccepted.

## Previous: virtual lens initialization and dynamic paired parameters

The explicit [virtual lens](../devices/lens/protocol.md) completes ordinary
identification, optical/configuration/calibration exchange, zero-travel aperture
control and receiver-bound history through the original SPI/GPIO/DMA path.
Its fixed 50 mm F2.8 iris, neutral calibration and timing are declared functional
inputs. The [SPI clocks](../devices/spi/protocol.md#source-selected-input-clocks)
follow the recovered MAS selectors; [HDMAC demands](../devices/hdmac/protocol.md#peripheral-demand-and-delayed-dispatch)
service real FIFO opportunities without accumulating host callback delay.

In the unchanged 60-host-second cold-neutral run with selected 8 GiB card,
healthy UART battery peer, held F4 and five-CPU MTTCG, lens configuration commits
once. It completes 535 packets, rejects zero, and clocks 14,389 complete history
records into the actual receiver. Two A0 and one A3 zero-travel aperture
requests complete. The complete paired calibration input matches the
guest-produced fixed-lens tuple, preserving the existing exact-comparison
boundary; this is a different declared input from the earlier absent endpoint.

Eight paired jobs write 21,290,880 bytes before CPU1 VA PC `1036a4a2`, WRITE32
PA `20380010=1`, rejects the dynamic field at bank offset `70c`: input
`020002f8`, guest `020002f9`. It belongs to the complete config[13] parameter
span `700..778`, whose control words are `101/0`. Its arithmetic is not
implemented, so another static tuple or an ignored-field admission is
insufficient. The next numerical evidence is the record+24 producer, control
semantics and an independently checked finite pixel operator.

QMP is running at 15 seconds and debug-stopped by 30; the 60-second observation
does not establish 60 seconds of continued execution. Both display heads are
unbound and GUI readiness remains zero at this stop. The card receives one
512-byte read with no failed I/O; no panic dump is present. Existing EX line
warnings remain. The [lens receipt](/evidence/lens/session.json) separates 48
native subtests, failed development trials, intermediate profiles and this
original execution. A 3D application bridge, variable optics, normal live view
and physical-camera fidelity remain outside the accepted result.

## Previous: selected media access and continuing paired pixels

The [paired operator](../devices/pairs/pixels.md) now handles the complete
cycle-selected noise packet with mathematically validated primitive feedback,
and both source-selected RAW/main/grid address groups. Work histories remain
shared as the inspected producer requires. Seed, numerical scales, selected
group interpretation and event cadence remain explicit functional assumptions;
no firmware, callback or requested completion is patched.

The [GPIO audit](../devices/gpio/input.md#whole-source-family-audit) covers all
232 descriptors and 31 direct read sites. It preserves dynamic EX/CComp/SLA
providers and supplies source-backed absence for SD P5.14. The
[storage contract](../devices/storage/protocol.md) adds explicit `slot0-drive`,
separates insertion from training, separates actual level status from event
capture, and joins all twelve PCI wrappers to a finite root transaction-error
latch. It preserves native NVMe/block operations and actual bus errors.

In the latest unchanged 60-host-second cold-neutral run, an explicitly selected
8 GiB blank raw card receives one original 512-byte read, zero writes/flushes
and zero failed I/O. The linked host reaches PHY state `11`, insertion `404`
and root errors `0`; no fatal exception dump is present. Paired jobs advance
through 0/52/100/108 at 15/30/45/60 seconds, writing 287,426,880 bytes and adding
114,095,520 main pixels. Paired rejection/cancellation remain zero. QMP stays
running with no strict uncovered/unsupported transaction; existing EX external
line warnings are not relabeled as warning-free execution.

Head1 binds a 1024×682 status GUI with an absent/non-CPU-lens notice; the former
SD warning is gone. Head0 remains unbound and GUI readiness is zero. All graph
ports are inactive at the snapshot, so the next image evidence is the original
graph submission/GUI-admission branch, not an assumed missing completion.
The empty-slot profile separately holds at a four-read pause after 12 jobs
despite fresh RAW; continuation-kick liveness is not established for every
input profile. A prior 120-second window reaches 132 jobs, but predates the
signal correction and is retained with that qualification.

The [noise receipt](/evidence/imagerequest/noise.json) records 54 CMOS and 10
numerical tests. The [storage receipt](/evidence/storage/presence.json) records
10 storage, 23 SLA and six GPIO tests, old-model failures and the original
execution profiles. These focused results establish neither original format/
file-save nor stable normal live view or physical-camera accuracy. Sector image
storage is primary; a host folder is an offline import/export boundary.

## Previous: real RAW processing and the correction-record boundary

The [paired pixel operator](../devices/pairs/pixels.md) connects completed RAW
DMA to selected-phase main, work and grid outputs, then publishes results
through the existing IRQ/W1C/firmware callback route. Its explicit monochrome
profile executes defect/spatial/temporal filtering, guest A/C/B matrices,
three-channel gains and two published gain curves. Numerical formats, filters,
rounding and retained-ring continuation are bounded functional choices.
Opaque calibration parameters still require an explicit matching input tuple.

The capture producer now accepts independent, nonexpanding integer RAW geometry.
Original source supplies unit2 output 1380×1556 followed by RAW 1380×778; the old
model's equality check rejected this normal mode. No descriptor is supplied
until the actual packed RAW rows have been written. A new source epoch cannot
reuse old history, and each processed phase requires a fresh completed RAW
generation. Unchanged retained rings can continue on an original `10` kick;
partial four-address updates cannot.

With unchanged firmware, cold-neutral inputs and the declared paired profile,
four actual phases now write 10,645,440 payload bytes. The cumulative main-pixel
result is 4,225,760. Main Y, half-resolution work and packed grid RAM contain
computed data; this is distinct from the earlier admission-only milestone.
The [pixel receipt](/evidence/imagerequest/pixels.json) separates native fixtures,
intermediate running observations and the final original execution.

The next boundary is the complete 40-byte correction record submitted through
MAS VA `102e563a` and `1036c862`, at bank-relative offsets `df4..e10`.
Its three raw32-bit operands change when the original software continues past
the first four phases. Their arithmetic has not been recovered. The model
rejects this changed opaque tuple instead of adding another constant whitelist
or returning completion without executing it. Normal live view and continuous
startup remain unverified. In the final nominal 60-host-second window, this
rejection stops QMP in `debug`: CPU1 VA PC `1036a4a2`, WRITE32 PA
`20380010=1`, with offending retained offset `df8=f8040000`. Both heads are
unbound and both image-information records remain zero at that earlier stop;
the historical status-GUI output below is not reproduced by this stricter
continuation. No firmware byte, guest callback or completion is patched.

## Previous: running with GUI status output; paired pixels still pending

The [complete kind57 arena](../runtime/storage.md#kind57-gui-arena) replaces
four allocation fragments with the exact source-declared
PA `[bec5f200,c24cb400)` owner. It includes both missing large partitions and
the owned `100`-byte tail, ending at the independent kind74 pool. Original ROM
and all six runtime pointer cells agree. No firmware allocation metadata,
pixels, callback or completion is supplied by this RAM correction.

The [arena receipt](/evidence/imagerequest/guipool.json) records a focused old-model
red,36 memory/4 fill subtests passing and the scoped GUI record7 regression.
In the unchanged60-host-second region/cold-neutral continuation, QMP stays
running with zero uncovered accesses. The previously failing clear at PA
`bff44340`, length `7e0`, performs real DMA with result0/status1. Head1 displays
1024×682 status widgets, the thin border and a memory-card error overlay;
head0 is unbound. Both1124-byte information records are equal and populated
(173 nonzero bytes each), while GUI readiness remains zero.

The remaining boundary is actual image processing, rather than another
observed unsupported transaction: imports/captures remain18 packets/649 words
and9 parameter captures. At the snapshot all graph nodes are inactive, paired
bank0 awaits input with no raw pending and bank1 is quiescent. The
[finite operator design](../devices/pairs/requests.md#numerical-interpretation-and-physical-limits)
joins completed RAW DMA generation, one shared ring phase, real matrix
coefficients and main/reference/grid consumers. Their actual arithmetic,
output layout and result publication must be implemented together.

This window completes128 main acquisitions and1714 statistics jobs, with no
rejection, one main cancellation and no statistics cancellation. It establishes
continued modeled execution and GUI status output under declared inputs;
normal live view, long-duration stability and physical-camera fidelity remain
unverified. No in-window GDB pause or forced event occurs. Sequential QMP and
read-only GDB observations are not atomic snapshots or physical rates.

## Previous: GUI storage after source-produced row fill

The [two-engine byte-row contract](../devices/fill/protocol.md#source-produced-byte-rows)
corrects pitch/active-width register names and replaces target/size whitelists
with the complete source-produced zero operation. Four selected-domain fill
subtests and four scoped startup/GUI cases pass; the
[row receipt](/evidence/imagerequest/fill.json) qualifies unsuccessful broader
and repeated-filter test invocations separately.

Unchanged60-host-second region/cold-neutral execution now performs the request
at PA `bff44340`, reporting a real bus error rather than successful completion.
It later reaches CPU1 VA PC `1051e322`, WRITE8 PA `bff43ac2=80`,
`uncovered-address`. The next boundary is the complete owner of these GUI
addresses, not another zero-fill tuple. Imports/captures remain18/649/9; this
window has128 main acquisitions and517 statistics jobs, no rejection, one main
cancellation and no statistics cancellation. Head1 screendump succeeds and the
two information records are populated; GUI readiness stays zero. Normal live
view and actual paired processing remain unverified.

## Previous: ordinary row fill after initialized path graphics

The [initialized indirect-path family](../devices/transfer/prefix.md#initialized-indirect-path-batches)
recognizes complete global state before the existing1..256-record path parser.
It removes a LUT/converter dispatch collision without a new size whitelist.
The [graphics receipt](/evidence/imagerequest/graphics.json) records an old-model
red and all26 transfer subtests passing, including independent output pixels,
context/cancellation and a1736-byte converter-length collision.

Unchanged60-host-second region/cold-neutral execution crosses the664/1032-byte
rejection and reaches CPU1 VA PC `102dbba2`, WRITE32 PA `20070010=1`,
`fill-request-profile-not-supported`. The new request is an ordinary zero fill:
48 bytes per row,42 rows, PA `[bff44340,bff44b20)`. Source must establish the
whole row operation rather than add another framebuffer address.

This window has18 imports/649 words and nine parameter captures. At the snapshot
no graph node is active and paired bank0 awaits input. Main acquisitions reach129
and statistics539, with no rejection/cancellation. Head1 screendump succeeds, head0 is
unbound; the two image-information records are populated and equal, while GUI
readiness remains zero. This is graphics continuation under declared virtual
inputs, not a completed live-view or paired-operator result.

## Previous: graphics initialization after paired admission

The [stream-request contract](../devices/pairs/requests.md) admits both paired
banks and all42 configured graph ports without generating completion. It backs
the complete active kind25/26/31 pools and corrects type0 parameter capture:
publication installs an actual thirteen-word snapshot, while later complete
quiescent reloads may replace it. The old permanent busy-on-publication
hypothesis is falsified by normal configuration-only reconfiguration.
Active-use queries/publication remain errors until real ownership is modeled.

The [request receipt](/evidence/imagerequest/requests.json) records four focused
old-model regressions and35 memory/23 SLA subtests passing. With unchanged
firmware and the same60-host-second region/cold-neutral profile, imports remain
18 packets/649 words, but parameter captures reach9 with valid mask `13e`.
Paired bank0 is awaiting input, bank1 quiescent; all graph nodes are inactive at
the snapshot.
This crosses paired start and the slot3/4/5 reloads without inventing pixels,
raw events or guest callbacks.

The first uncovered transaction becomes CPU0 VA PC `106ee3c6`, WRITE32 PA
`201810e0=1`, reporting `transfer-stream-truncated`. Separate diagnostics capture
complete664/1032-byte streams containing four indirect filled paths with a
global initialization prefix. The error comes from family dispatch/validation,
not proof of missing command bytes. Its finite graphics grammar is the next
boundary; actual paired input processing remains independently unfinished.

The request checkpoint completes128 main acquisitions and507 statistics jobs,
with no rejections, one main cancellation and no statistics cancellation.
GUI readiness remains zero; both1124-byte image-information records are equal
and populated (173 nonzero bytes, leading word1). Head1 binds a1024×682 console with
status widgets and a memory-card error overlay; head0 is unbound. Later
diagnostic-only runs do not reproduce that binding, so stable first-enable
timing remains open. There is no in-window GDB pause. Sequential QMP/read-only GDB observations are not
atomic snapshots or physical rates. Normal GUI startup and frame retirement
remain unverified.

## Previous: paired-processing start at PA `20380010`

The [paired-table contract](../devices/pairs/tables.md) now retains the whole
source-selected family:22 tables of64 bytes and14 of68 bytes,590 words per
bank in `[1400,1d38)`. Both banks use the existing configuration/reset rules,
with no added DMA, result, ready flag or event. Unknown widths, neighboring
holes and unmodeled start/result semantics remain explicit errors.

The [table receipt](/evidence/imagerequest/tables.json) records native red→green
and23 passing SLA subtests. In the unchanged60-host-second region/cold-neutral
continuation, bank0 has499 nonzero words in the full table extent and bank1
stays zero. Graph imports remain18 packets/649 words; slots1/2/3/4/5/8 stay busy.

The first uncovered transaction becomes CPU1 PC `10369fb4`, WRITE32 PA
`20380010=1`. Original helper `10369faa` starts this paired operation with
bank+`10=1` then+`08=1`, separately from configuration. The next capability is
the actual input/operation/output and completion/cancellation contract, not
another table value. The graph's independently known reverse-start sequence
and terminal mask `c2` remain later, unexecuted requirements.

The run completes128 main acquisitions and463 statistics jobs, with zero
rejections/statistics cancellations and one main cancellation. Property6433,
GUI readiness/information remain zero; neither head has a console. No in-window
GDB pause occurs; sequential QMP/read-only GDB reads are not atomic or physical
rates. Normal GUI and a completed processing-slot cycle remain unverified.

## Previous: paired-bank table publication at PA `20381400`

The [complete type-family contract](../devices/gate/family.md) establishes19
graph types and76 resources:15 processing types across42 command ports. It
adds the remaining type13/14/16/18 streams and type18 workspace provisioning
as a group. Type14's24-byte single-block form is a strict functional hypothesis,
not a recovered original template. No actual pixel operator, completion or
slot retirement follows from the expanded configuration coverage.

The [family receipt](/evidence/imagerequest/family.json) records28 passing
native decoder/SLA subtests and the unchanged60-host-second region/cold-neutral
continuation. Original CPU workspace initialization sets the finite coverage
flag `148=100`; type18 imports84 bytes. Totals become18 packets/649 words,
and original code submits output slots3/4/5. Slots1/2/3/4/5/8 remain busy.
Types13/14/16 remain unconfigured in this original window.

The first uncovered transaction becomes CPU1 PC `1036ca8a`, WRITE32 PA
`20381400=00f600fa`, a paired-bank table publication before processing start.
The next boundary is that whole source-selected table family. Independently,
reverse-start is known to begin with type18/resource34 at `20100920=1`; its
input comes from type10/resource49. The graph's terminal wait mask is `c2`
for resources49/54/55. Data, result and ownership conditions remain open.

The run completes128 main acquisitions and453 statistics jobs, with no
rejections/statistics cancellations and one main cancellation. Property6433,
GUI readiness/information remain zero; neither head has a console. No GDB
pause occurs inside the window; later sequential reads are not atomic or
physical rates. Normal GUI and a completed processing-slot cycle remain unverified.

## Previous: type18/resource34 initialization at PA `201002c0`

The [linked type8/type9 streams](../devices/gate/linked.md) add all eight
source-selected ports, their128/136-byte layouts and strict reset/RAM-DMA/atomic
import. Type9's source ancestor8 is distinct from its type5 input stream4;
its downstream routes and borrowed kind74 interpolation reference are located.
Pixel/table equations and complete input/output DMA association remain open.
No identity transform, automatic completion or slot release is inferred.

The [linked receipt](/evidence/imagerequest/linked.json) records27 passing
native decoder/SLA subtests, including all37 ports and64-bit configured-mask
reset isolation. In the unchanged60-host-second region/cold-neutral run,
imports rise to17 packets/628 words. Type9/resource45, type8 resources41..43
and subsequent supported nodes cross configuration. Slots1/2/8 remain busy;
no actual processing activation is reached.

The first uncovered transaction becomes CPU1 PC `103bddd2`, WRITE32 PA
`201002c0=1`, in callback `103bdd8c` for type18/resource34, backend `20100920`.
It clears SYS41..44, tests `20100148.bit8` and conditionally writes this
control before CPU initialization of the4096-byte workspace and common
reset/import. The next boundary is that complete initialization/command
contract, with graph processing and its terminal result still pending.

The run completes128 main acquisitions and529 statistics jobs, with zero
rejections/statistics cancellations and two main cancellations. Error
property6433, GUI readiness and both information records remain zero; neither
head has a console. No debugger pause occurs inside the window; sequential
QMP/read-only GDB observations are not atomic or physical rates. Normal GUI
publication and processing-slot reuse remain unverified.

## Previous: downstream type9 command reset at PA `20100914`

The [type5/type6 stream contract](../devices/gate/fanout.md) separates the
356-byte register prefix from the396-byte software allocation's CPU-consumed
trailer. Both source-selected ports use the shared strict reset/RAM-DMA/atomic
import lifecycle. Their original upstream/resource routing and trailer stores
are located; physical pixel/table equations and the complete input/output DMA
association remain unresolved. No identity transform, black frame, successful
processor activation, completion event or automatic slot release is inferred.

The [stream receipt](/evidence/imagerequest/fanout.json) records native red→green
with26 passing decoder/SLA subtests. In the same unchanged60-host-second
region/cold-neutral run, original type5/resource35 imports89 words, increasing
the total to9 packets/358 words. Its pointer is retained, reset deasserted,
and slots1/2/8 remain busy. Type6 is covered by the shared source/native contract
but remains unconfigured in this original window.

The active eighteen-node graph begins `0/8 ->5/35 ->9/45`. The first uncovered
transaction becomes CPU1 PC `103bdedc`, WRITE32 PA `20100914=1`, the downstream
type9/resource45 reset at backend `20100900`. Original execution has not yet
finished configuring this graph or reached processing activation. The next
boundary is that unit's complete stream/data contract.

The run completes128 main acquisitions and542 statistics jobs, with zero
rejections/statistics cancellations and one main cancellation. Error
property6433 and both GUI information records stay zero; neither head has a
console. No debugger pause occurs inside the window; QMP then read-only GDB
observations are sequential, not atomic or physical rates. Normal GUI
publication and processing-slot reuse remain unverified.

## Previous: type5 processing command reset at PA `20100e74`

The [paired-bank packet contract](../devices/pairs/parameters.md) now retains
the complete eight-field group containing PA `203800a8` in both banks. MAS and
SLA setters agree on the entire field mapping. The added byte2 member follows
the existing full-word configuration convention; adjacent holes and unmodeled
requests/results remain errors. There is no configuration-triggered processing,
event, busy retirement or inferred pixel arithmetic.

All23 SLA subtests pass after a regression that failed the old `a8` read.
The [packet receipt](/evidence/imagerequest/parameters.json) records the same
unchanged60-host-second region/cold-neutral profile: bank0 retains all eight
values, bank1 stays zero and both raw pending words remain zero. Original code
now additionally submits slot8; slots1/2/8 remain busy. A separate bounded CPU
trace identifies the original configuration-only `42190802` worker branch;
this observation is distinct from the normal run's counts.

The first uncovered transaction is CPU1 PC `103bdedc`, WRITE32 board PA
`20100e74=1`. Original resource tables identify processing type5/resource35 at
backend `20100e60`; callback `103bddea ->103bded0` requests its reset before
payload import. This is a processing resource ID, distinct from NOR resource35
used by the input profile. The next boundary is that whole unit's command and
data contract, not a reason to release already submitted type0 slots.

The normal run records128 main acquisitions,434 statistics jobs, no rejections
and no statistics cancellations; main acquisition has two cancellations. The
previous graph still has eight imported packets/269 words. Error property6433
and both GUI information records remain zero. QMP is debug/stopped; neither
display head has a console. There are no debugger pauses inside the window.
Normal GUI publication and completed processing-slot reuse remain unverified.

## Previous: paired-bank parameter packet at PA `203800a8`

The [processing command contract](../devices/gate/commands.md) now imports
the current graph's seven logical-register layouts across27 source-selected
ports. It includes isolated reset/import, complete RAM-only DMA validation,
type10 auxiliary words, eight paired endpoints and six CPU-populated LUT banks.
The separate workspace initialization bit is an explicit functional hypothesis;
it does not report processing completion. Actual processor activation remains
an error, with no invented input dependency or automatic terminal event.

The [command receipt](/evidence/imagerequest/commands.json) records25 passing
native subtests and a fresh unchanged60-host-second region/cold-neutral run.
Original code imports eight packets totaling269 words, populates its input
buffer map and submits type0 slots1/2. Both slots remain busy. Original terminal
callbacks and software release paths are located, but the real pixel operation,
result DMA and hardware slot-retirement condition are still unmodeled.

The first uncovered transaction is CPU1 PC `1036a2ee`, WRITE32 board PA
`203800a8=4`, before processing activation. Original setter `1036a2ac` writes
an eight-field parameter group selected by the object's bank byte; `a8`
receives payload byte2. The shared paired-event device presently rejects that
field. The next boundary is this complete parameter group and its consumer,
not a justification to complete the already imported graph.

The run completes128 main acquisitions and436 statistics jobs, with zero
rejections and zero statistics cancellations. Error property6433 is empty,
GUI readiness/information remain zero and neither display head has a console.
QMP is debug/stopped. No debugger pauses occur in the observation window;
sequential QMP and read-only GDB observations are not atomic. Separate failed
observer/CPU-trace diagnostics remain qualified in the receipt. Normal GUI
startup and a completed processing-slot reuse cycle remain unverified.

## Previous: graph command-buffer reset at PA `20100814`

The [type0 parameter-slot contract](../devices/gate/slots.md) now covers all
sixteen source-declared slots: complete thirteen-word packets, selector2c,
independent busy ownership and reset cancellation. A submitted slot remains
busy; elapsed time, ordinary stop ACK and W1C cannot turn parameter admission
into processing completion. Physical busy duration/encoding remains a model
hypothesis. No data DMA, result, event, IRQ or guest callback is supplied for
the unmodeled downstream processor.

The [slot receipt](/evidence/imagerequest/slots.json) records native query
red→green and23 passing SLA subtests. In the unchanged60-host-second
region/cold-neutral continuation, the original allocator crosses PA `20102044`
and original code submits slot1. Its complete packet is
`{20121,15500,0,0,80,80,80,0,22003c,0,12000,24000,0}`; slot1 is busy and the
other fifteen slots are idle. The eleven-node graph has a separate downstream
result path; type0's resource descriptors have zero completion masks.

The first uncovered transaction is CPU1 PC `103bdedc`, WRITE32 board PA
`20100814=1`. Graph node1 is type4/resource37, with a decimal72-byte command
payload at VA `b2bdb3a8`. Callback `103bdc72 ->103bded0` pulses backend+14
before supplying its payload pointer at+10. Both input-enable nibbles and both
retained input maps are still zero; parameter submission alone cannot justify
data completion or slot retirement. The next work is this command-buffer
consumer and its real processing/result/terminal-event join, rather than a
timer that clears busy or fabricates a terminal event.

The final observation records128 main acquisitions and448 statistics jobs,
with zero statistics rejection/cancellation and an empty error property6433.
QMP is debug/stopped, neither head has a console, GUI readiness/information
remain zero, and one root is queued with pending `40001`. No debugger pauses
occur inside the window. A separate failed read-only observer included a
reserved input-group word; its E14 is retained as an observer failure, not a
guest regression. The final snapshot uses sparse supported words. Normal GUI
startup and a completed processing-slot reuse cycle remain unverified.

## Previous: processing input availability at PA `20102044`

The [processing scratch arenas](../runtime/storage.md#processing-scratch-arenas)
back both source-declared allocator capacities: sixteen `80000`-byte blocks
at PA `b0100000` and four `4b000`-byte blocks at `af8d4000`. Allocation,
locking, busy arrays, returned descriptors and record construction remain
original guest execution. No wider address class, guest object or processing
completion is supplied by the host.

The [scratch receipt](/evidence/imagerequest/arenas.json) records native
red→green and72 passing memory/sensor cases. In the unchanged60-host-second
region/cold-neutral continuation, the original allocator owns type1 descriptor
`{b0100000,280000,0,5,1}`: five blocks are busy and the other eleven are free.
Type2's busy array is zero; its scope is source/native validation, not observed
runtime use. The run records128 main acquisitions and543 statistics jobs,
with no statistics rejection/cancellation and an empty error list.

The first uncovered transaction is CPU1 PC `103bd1da`, READ32 board PA
`20102044`. Getter `103bd1d6` reads `20102040 +4*index`, here index1.
Consumer `1038917c` consults this value for processing input type0 before
claiming the resource: zero permits that candidate, nonzero tries other
resources or enters the original wait. At that checkpoint the event-gate had
no producer/qualification contract for this query bank. Its full input-slot
mapping and busy lifecycle must be joined to actual submissions and releases;
returning unconditional idle would bypass that ownership check.

QMP is debug/stopped, neither head has a console, GUI readiness/information
remain zero, and one root request is queued with pending `40001`. The next
work was the complete processing input-slot lifecycle, separately from scratch
backing. Normal GUI startup and physical-camera fidelity remain unverified.

## Previous: processing scratch at PA `b0100000`

The [kind41 workspace](../runtime/storage.md#kind41-processing-workspace)
joins ROM pool metadata, the runtime allocation object and original PTEs.
`1026fbb0` owns a complete `400000`-byte unit at board PA `62221800` and
constructs128 records beginning at offset `300000`. The old write at
`625219bc` is a decimal24-byte clear inside the first record; its sampled
scratch register was not the entry length. The board now backs exactly
`[62221800,62621800)` without changing any declared pool boundary, page table
or firmware byte. Cold zero remains a functional board assumption.

The [workspace receipt](/evidence/imagerequest/workspace.json) records native
red→green,34 memory and37 sensor cases, including every page, actual TCG
CPU/bus sharing, cross-page HDMAC and negative bounds. With the same explicit
region/cold-neutral profile, unchanged firmware crosses the record clear.
The60-host-second run records128 main acquisitions and413 statistics jobs,
with no statistics rejection/cancellation and an empty error list.

Its next first uncovered transaction is CPU1 PC `10361044`, WRITE32 PA
`b0100000=0`, inside another original clear. Saved return `10271ae9` joins
consumer `10271abe`, which clears a decimal200-byte processing record.
The existing arena object at VA `b1808c98` stores this base at field `10`;
the separate scratch allocator and its complete ranges must be admitted on
their own source contract. QMP is debug/stopped, neither head has a console,
and GUI readiness/information remain zero. No normal live-view or physical
camera claim follows from this storage correction.

## Previous: original clear at PA `625219bc`

The [published-result latch](../devices/capturegroup/protocol.md#published-result-lifecycle)
separates actual automatic-TG results from later generator configuration and
run/stop changes. Only a valid automatic boundary can replace the eight-word
packet; commands preserve a prior publication without restoring cold zeros or
echoing a requested cursor. Retention across lifecycle changes is an explicit
functional hypothesis, not a physical reset specification.

The [latch receipt](/evidence/imagerequest/latch.json) records a native red on
the preceding expiration rule, then37 sensor and33 memory cases passing.
The unchanged-firmware continuation with the same explicit region/cold-neutral
inputs crosses the `2006bb70` getter. Its60-host-second run records128 main
acquisitions,428 statistics jobs and no statistics rejection/cancellation.
The error list remains empty. Counts are sequential observations, not
throughput comparisons with the preceding run.

The first uncovered transaction is CPU1 PC `10361058`, WRITE32 board PA
`625219bc=0`, inside the original zero-fill helper. The later read-only
snapshot reports DFAR VA `372219bc`, with `r6=37221800`. This identifies an
address translation and an unbacked write, but not yet the allocation owner,
declared extent or clear length. The helper has already transformed its
length register; the sampled `r1=80000000` must not be treated as its original
requested length. Recover the upstream allocation and complete clear request
before backing any additional storage. Existing RAM boundaries and original
firmware bytes remain unchanged.

QMP is debug/stopped, both heads lack a console, GUI readiness/information
remain zero and one root request is queued with pending `40001`. Passing the
getter does not establish the physical bank/cursor mapping, completed normal
GUI startup or stable live view.

## Previous: capture-group result lifecycle after sequence stop

The [global sequence model](../devices/sensor/protocol.md#global-sequence-start-and-stop)
executes all eleven channel start/stop masks and preserves each pending stop's
own virtual deadline. Only completed active channel8/0 stops publish router
IRQ87 causes18/10; the original ISR and callback set Sensor events10/20.
Mask consumption,1µs latency and the channel/cause association remain explicit
functional hypotheses. No pixel work, guest flag or callback is supplied.

The [sequence receipt](/evidence/imagerequest/sequence.json) records36 sensor
and33 memory cases, including a red→green staggered-stop regression. With
unchanged firmware and the same explicit region/cold-neutral inputs, the
60-host-second final run reaches129 main acquisitions and420 statistics jobs.
The Sensor error list is empty, both command masks have been consumed, and
the sequence model reports running `503`, stopping0. This crosses the earlier
ID63 branch; it does not establish normal GUI startup.

The first uncovered transaction is original MAS PC `10208e14`, READ32 board
PA `2006bb70`. Capture-group's result getter rejects an unqualified package;
its current model invalidates qualification on selected commit and run/stop.
The next investigation must establish the last actual result producer and
the getter's lifecycle across sequence restart, distinguishing retained
published results from unpublished or cold state. Do not return cold zeros
or a guessed bank/identity. QMP is debug/stopped, both display heads lack a
console, GUI readiness/information remain zero, and one root request remains
queued with pending `40001`.

## Previous: Sensor switch completion after first-read consumption

The [explicit resource35 profile](../inputs/nor/descriptor.md#resource35-first-read-region-input)
supplies complete bounded virtual region tables through descriptor17's normal
NOR loader. It preserves the rest of that opaque input, including the footer,
and keeps resource80 cold-neutral. Source field848 supplies64 first-read
samples; the preceding255 was the low byte of an erased FF parameter.
Bounds, temporal limits and virtual coefficients are distinguished from
recovered factory data. No guest flag, callback or frame is injected.

The [region receipt](/evidence/imagerequest/regions.json) records5 native
module-tool cases and33 memory cases, with all original `c00` loader bytes
matching the explicit file. A60-host-second unchanged-firmware run completes
128 main acquisitions and419 statistics jobs, with no statistics rejection
or cancellation and no uncovered access. QMP remains running, and head1
binds an actual1024x682 console containing status widgets and an error overlay.
GUI readiness/information storage remain zero; normal live view is incomplete.

The error list contains only ID63. An independently instrumented read-only
breakpoint observes `1026c1d4(100)` with LR `1026c839`, from the normal
`1026c66e ->102d986e(0)` call. The mapper turns status bit100 into Sensor
state9; `10394d06` maps it through table `118f9338[9]` to ID63. This separates
the new error from the solved resource35 negative-coordinate read.

`102d986e(0)` clears Sensor event10 at object VA `b18a764c`, submits command
101 through `102ff522 ->102976c2` to PA `200632e0`, and commits it with
`200632e4=1`. It then waits for event10 with timeout150 in original API units;
a missing completion contributes
status100. A second conditional breakpoint at `102d9906` observes the wait
returning0 with accumulated status0, isolating this missing completion from
the earlier readiness wait and command submission. These debugger probes
are separate from the uninterrupted60-second run. The SDES channel device
at that revision retained/published these global commands without their
completion work. The bounded sequence model above now supplies that hardware
state transition through the normal router route; the waiter flag is never
set by the host. The alternate107/event20 branch belongs to the same cohort.

## Previous: erased region parameters after first-read processing

The [bounded output-storage profile](../devices/statistics/protocol.md#completion-sequence-and-bounded-output-storage)
separates the configured completion sequence from each source-declared output
storage period. Four/six-slot outputs reuse real DMA storage while endpoint1
retains its large pool and full count. This is an explicit functional hypothesis;
physical counter encoding, autonomous cycling and phase remain unverified.
No RAM extent, firmware byte, queue, callback or completion source is extended.

The [storage receipt](/evidence/imagerequest/storage.json) records native
red→green,99 passing memory/CMOS/sensor cases and an expanded phase-reset
fixture. In the60-host-second unchanged cold-neutral continuation, sequential
queries report290 auxiliary completions,510 main acquisitions and799 statistics
completions, with no statistics rejection/cancellation. The two255-frame
first-read sequences finish, capture run/enable become0, unit16 resets and
the queued root is consumed. This reaches the original post-first-read
consumer, beyond the preceding input and RGB DMA boundaries.

That consumer `10218f84 ->10218d24` reads LE16 samples using region bounds
selected by `10218bca` from resource35 (decimal). The read-only snapshot
finds its pointer `b7f58400`; inspected bounds and count fields containFF.
Bounds at offsets24/124/224/324 hex are signed -1, as are the region counts.
With input pitch32 samples and base `5df45400`, the first coordinate(-1,-1)
computes `base +2*(-32-1) =5df453be`. Original PC `10218d7e` faults there;
QMP is debug/stopped. This is an erased-parameter consumer boundary, not
evidence for expanding the kind32 pool before its source-declared beginning.

The next work is resource35's parent input, original load path and a bounded
self-consistent virtual region configuration. GUI readiness and the information
record remain zero; both heads still lack a display console. Normal GUI
output and physical-camera fidelity remain unverified.

## Previous: RGB address progression during the first read

The [large-count grid pool](../runtime/capturedata.md#large-count-grid-storage-and-the-remaining-address-contract)
now backs kind32's complete source-declared PA range `5df45400..5f8d23ff`.
It uses the existing offset-aware RAM helper, independently of kind18. Original
pool initialization, all five absent overrides and both kind32 consumers establish
the allocation; cold-zero RAM remains an explicit functional assumption.
No new result, event, guest flag or firmware change accompanies this storage.

The [pool receipt](/evidence/imagerequest/pools.json) records native DMA
red→green and98 passing memory/CMOS/sensor cases. A60-host-second unchanged
cold-neutral run removes the previous endpoint1 RAM fault: sequential QMP
queries report380 auxiliary completions,1327 main acquisitions and392 statistics
completions, with1313 statistics rejections. Main acquisition has no rejection
or cancellation. The new rejection is endpoint4's calculated PA `5d4be000`,
span10, outside kind19's six declared slots. A separate30-second read-only
snapshot confirms the source setters' endpoint pointer tables and records.
Earlier cursor values can already enter other mapped pools; this is the first
RAM rejection, not the first ownership violation or evidence of correct RGB DMA.

`1023b86c ->1030af00` explicitly allocates6 RGB slots. Independently,
`1023a6c6 ->1023a63c` programs the first-read count255 into endpoints1/2/3/4.
The existing functional statistics hypothesis advances each DMA address once
per completed frame; it reaches `5d200000 +13*36000` at endpoint4. The
relationship between this completion count and RGB address progression must
be recovered before extending that model. Mapping the adjacent kind24 pool,
wrapping at6 or suppressing RGB output would each assume an unproved behavior.
The model retains the explicit rejection and publishes no completion for it.

CaptureService remains4, the first read is pending, information storage stays
zero, GUI readiness is0 and both display heads lack a console. The next work
is the address-control and completion-count contract, not another arbitrary
RAM aperture. First-read completion and normal GUI output remain unverified.

## Previous: kind32 output backing after main acquisition

The [independent main-input profile](../devices/sensor/protocol.md#independent-main-acquisition)
now acquires fresh scene-dependent gray frames after auxiliary readout stops.
It snapshots main/geometry commits, uses the existing TG and CMOS engine,
retains separate channel ownership and publishes input status only after real
sampling completes. Normal mode1 masks per-frame notifications; masked status
does not block its endpoint ring. Completely zero output extents disable those
outputs while preserving their retained routes; incomplete dimensions and an
all-disabled job still reject. Pixel reduction and those finite output/timing
laws remain explicit functional hypotheses.

Native red→green covers absent main input, masked-frame stalls, cross-channel
cancellation and partial outputs. The applicable97-case batch and final32-case
CMOS extension pass. In a60-host-second unchanged-firmware cold-neutral run,
1325 independent main frames complete with no main rejection/cancellation.
The final45-second profile completes985 main frames after290 auxiliary DMAs;
main acquisition therefore continues across the normal run1d transition.

That revision's next rejected work is statistics endpoint1's PA `5df45400`, span300,
resolving to `z7ii.uncovered`. The original `1030ad70` constructor selects
kind32 rather than ordinary kind18 when requested count exceeds6; its unit
stride is12000 and total size is stride times count. The runtime kind32 base
at MAS VA `b26b4bd0` matches this destination. The complete kind32 allocation,
backing and address conversion must be established together; a single-address
mapping or enclosing DDR aperture is not added. All required active output
DMAs must succeed before the model can publish statistics/ring completion.

The [acquisition receipt](/evidence/imagerequest/acquisition.json) separates
source facts, finite hypotheses, native verdicts and original execution.
CaptureService remains4 with the first read pending, information storage is
zero, GUI readiness is0 and both display heads lack a console. Group40 remains
excluded from this request. No first-read completion, FECLCTRK admission or
normal GUI output is claimed.

## Previous output and first-read boundaries

The [finite grayscale stream](../devices/capturework/protocol.md)
now performs unit1 identity forwarding, unit2 geometry sampling and paired
LUT/packing into the four source-allocated kind16 RAM slots. Arithmetic and
shared input association are explicit functional hypotheses; original bytes,
guest state and memory allocation boundaries are unchanged. Native tests check
actual nonzero output, padding, ring/bank/lifecycle and rejection boundaries.

In the final40-second cold-neutral explicit-scene run,388 stream jobs write737300880
payload bytes. Original stage setters100000/200000/4000000 execute and the
unit1-only fence succeeds. Aggregate05b00000 still fails: of its five requested
stages, group1000000 alone has no observed setter. All36 observed OnVd mask5
resumes succeed, but caches1/2, the information notification and both display
consoles remain absent. The [stream receipt](/evidence/imagerequest/stream.json)
separates source facts, model assumptions and original observations.

The [payload audit](../devices/capturegroup/protocol.md#payload-consumers-and-wire-records)
recovers three wire formats (40/24/32 bytes), their conditional consumers and
packed 12/23/29-bit fields; their generating arithmetic remains unresolved.
The existing group model only advances addresses. Two new 25-host-second
breakpoint windows still bypass FECLCTRK before payload consumption: all
28/27 selected roots retain mode0, metadata byte10132=0 and latchdc8=0.
The retirement comparison is not reached. A group completion is therefore
not established as the producer of the currently missing admission condition.
Another 25-second `firstread` probe observes OnVd's normal state4 submission:
`10233cf6` sets `dd8=1` and queues a root, but neither its `041a0003` branch
nor FE worker8 runs. Its requested/active mask is `0xb`, excluding group and
paired work; final reset is0 and run/enable are `0x1d`. The immediate boundary
is this first-read processing and completion lifecycle, with unit2
requesting 8288 by58 input and2072 by58 output rather than the continuous-stream
profile. A separate 25-second control without in-window GDB attachment reaches
the same pending request, with367 input DMAs and366 statistics jobs and no
reported rejection/cancellation. This is an
instrumented execution boundary, not normal GUI output.

The source now connects mode1's normal first-read completion to raw3.bit7,
owner80 and `1023c57a`: switch endpoint1's address set, then stop and emit
`041a0003` after its next completion. The
[finite ring model](../devices/statistics/protocol.md#endpoint-ring-completion-and-first-read-mode)
publishes that cause only at the committed endpoint1 ring tail after all
output DMA succeeds; address selection and ring-end timing are explicit
functional hypotheses. Native red→green and93 applicable cases pass. Original
55-second tracing confirms owner80 consumption, but no first-read notification
or FECLCTRK. A corrected120-second control without live GDB still stops at380
input DMAs/379 statistics jobs and no console.

That audit identified the missing independent main-image acquisition.
First-read run1d intentionally clears auxiliary readout bit1 while retaining
main-input bit0. The preceding provider only acquired CMOS frames for auxiliary
requests, whose stop also invalidated the shared input generation. It could not
supply the first read's255 new inputs after that normal mode switch. The
independent profile above closes that input lifetime. Group40
is excluded from this request and cannot repair that source lifetime. Group
generating arithmetic and paired numeric getters remain unresolved; no guest
flag, frame or completion is fabricated to replace the missing acquisition.

## Earlier frontier observations

The [image request audit](../runtime/imagerequest.md) separates the normal
GUI/display producer from AF6's state9 request. Original drawing and valid
layer programming run; operation10 now unpauses successfully with ID68 absent.
Display active/requested-enable masks and bank port/layer enables remain0.
Phase18's final enable and the image-information04380001 publication have
not executed in their focused cold-neutral windows.

Both root parities are created. Earlier step102669aa cancels the eligible
phase at10266a8c because computed cache1 is unpublished; its cache2 check is
short-circuited. Opposite-phase jobs survive to1026a312 and fail `{2,0}`
cadence admission. The missing cache producer is the original FECLCTRK /
10273268 ->10272d62 calculation, not an established missing external blob.
FE's bootstrap ends with synchronization flagb1887dc8 still0; normal
mode0/metadata+10132=0 bypass the subsequent calculation submission.

The [phase/producer evidence](/evidence/imagerequest/phase.json) joins the
upstream041a0002 request to queued10326676, which transitions service2 to3.
Its20-second focused window has a full60-record queue and lost enqueues, but
the later [coordination audit](/evidence/imagerequest/coordination.json) observes
`10323198(10,2)` and naturally reaches service4 with queue occupancy0 in both
the baseline and the changed model. Backlog is not a permanent transition block.

In the historical baseline, OnVd `10325ff6 ->103250f2 ->10233570` fails FE event mask5 at
`102339d8` and processing-unit mask05b00000 at `102389c2`. The latter is an
**old-cycle fence before new configuration/commit**, not a new-commit ACK.
The callbacks and seven stage causes are source-established in the
[runtime audit](../runtime/imagerequest.md#onvd-waits-and-processing-stage-ownership).
The kernel confirms an AND wait for FE masks1/4 with requested-bit clearing.
The historical bounded FE scan found no mask4 producer; the event snapshot has pattern3b,
with1 present and4 absent. OnVd immediately overwrites the wait return, so
failure alone does not establish a hardware-ready gate. The notification audit
below now resolves the mask4 software route and models qualified input sync;
the independently guarded calculation entry remains open. Literal100/200 timeout
arguments retain unverified extended-ABI units.

The [per-stage audit](../runtime/imagerequest.md#work-and-result-ownership-across-the-fence-family)
separates front-end packets, group cursor metadata, paired parameter results
and actual statistics work. The accepted
[statistics completion](../devices/statistics/protocol.md#processing-stage-completion)
now publishes its own raw3.bit5/IRQ96 only after the owned ten-output processing
and numerical results succeed. The added fixture fails on baseline raw3=0
after actual DMA;18 CMOS plus33 Sensor cases pass. No other stage or FE bit is
inferred from it.

In the30-host-second cold-neutral `end` run,735 auxiliary DMAs and734 statistics
jobs complete, with734 original event800000 setters from102394c9. FE mask5
and the aggregate05b00000 still fail in50 matched waits each. A30-second
guest-error-only run completes725/724. Both naturally end in service4/queue0,
with caches1/2 unpublished, display masks0 and no console; reported rejection,
statistics cancellation and peer rejection remain0. Front-end work, group
buffer processing and PA2006953c-family result production remain open.
The [fence receipt](/evidence/imagerequest/fence.json) separates source, native
and unchanged-firmware observations; accepted configuration does not justify
all requested completions.

The subsequent [front-end audit](../runtime/imagerequest.md#front-end-input-and-buffer-boundaries)
locates unit1's optional parameter pointers and unit2's ROM-policy/coefficient
sources, without identifying their actual pixel input/output contract.
Kind23 construction does not populate its payload; the pool's bytes are zero
in both final physical reads, with its handle already cleared in the second.
The paired result getters do not execute
in these focused windows. Owner mask1's041a000f input notification is also
absent within the owner-entry filter. The new30-second runs complete813/812
and742/741 auxiliary/statistics operations without reported rejection, but
end in different lane/run states and still have no console. No additional
completion model is accepted from configuration or these negative observations.

The [explicit image-input continuation](../runtime/imagerequest.md#explicit-scene-input-through-actual-dma-and-statistics)
repairs source binding, raster parsing, scene sampling and C170 grayscale
encoding. Under the same cold-neutral descriptor32, a60-second original run
with fixture-provider false completes814 auxiliary DMAs and813 statistics jobs.
Four buffer samples and all four scalars match the supplied2x2 scene exactly.
The54-case native batch separately verifies the finite data path. Cache1/2
remain unpublished and there is still no console: usable auxiliary image data
alone does not close the front-end input or calculation-production boundary.

The [notification/submission audit](../runtime/imagerequest.md#notification-routing-and-actual-submission)
closes `041a000f -> FE wait mask4` through its shared instruction tail. This
supersedes the earlier bounded search that found no mask4 producer. The separate
`041a0001 -> FE worker10000` does not dispatch the FE8 completed-queue consumer.
Unit1 initialization, unit2 policy
submission and incremental commit `9e` execute. Eight selected roots still take
the mode0/metadata0/synchronization0 branch before calculation submission.
The implemented shared virtual acquisition hypothesis now publishes input sync
only after actual owned CMOS DMA, snapshot and completion, with continuously
qualified capture run bit0. A55-case native batch covers token validation,
revocation, cancellation/reset and mask/W1C behavior. In the changed-model
40-second explicit-scene run,375 auxiliary DMAs and374 statistics jobs complete.
Owner1 and the original FE mask4 setter execute, and all41 observed OnVd mask5
resume points return1. The unit1-only fence and39 aggregate processing fences
still return0. Cache1/2 and GUI output remain absent; input synchronization
does not substitute for unit1/2, group or paired-result work. The physical
association remains an explicit hypothesis.

The normal information producer remains registered without an observed
publication. An additional first-enable candidate is now source-joined:
`1035a2d8 ->04380001 ->104be876 ->record92 ->1053f42a`.
A change to packet offset1016decimal (`3f8` hex) and the `105b0a7e` policy
guard lead to `104a94fe(5)` at `1053f7c0`; nonzero additionally posts record133
but is not required for operation5. The record has not
been successfully consumed in the focused windows. This conditional route
does not require a target switch or property6433 change; it is not established
as the unique first-enable producer. AF6/state9 is a separate guarded request.

The accepted finite model now reserves an auxiliary request immediately and
admits actual CMOS DMA only at the next fresh automatic TG input edge. This
functional association removes independent completion/resubmission feedback;
earlier/busy edges are discarded, forced levels do not acquire input, and the
original33333334ns virtual period is unchanged. Same-edge statistics uses the
previous completed input. No software event/cache/state or display enable is
injected. Native red→green evidence and the applicable51-case batch cover this
ownership/input boundary separately from original-firmware progress.

With unchanged firmware, cold-neutral descriptor32, RNG0, healthy paced battery,
held F4 and5 MTTCG CPUs, a20-host-second focused window completes519 readouts
and518 statistics jobs; the comparable baseline completes1999/523. The changed
90-second CPU/nochain run completes739/738 and a30-second guest-error-only run
completes818/817. Both end in service4 with empty queues, unpublished caches1/2,
zero display masks and no console; no readout/statistics rejection, statistics
cancellation or peer rejection is reported. Different trace filters affect
throughput, so these are coordination observations, not rates or a complete
stability claim. The next boundary is genuine FE/remaining-processing-stage completion
and computed-input publication before the GUI information notification.
The earlier [admission receipt](/evidence/imagerequest/admission.json) and
six-range COPY comparisons retain their original scope. CPU0 at10100008 alone
is not a fault receipt: the original extended API uses the normal SVC vector.

## Finite processing-data completion

Published records for this checkpoint:
[original execution summary](/evidence/capturecompletion/finalreview-proof.json),
[native CMOS/Sensor results](/evidence/capturecompletion/native-final.txt),
and [commit-revision regression results](/evidence/capturecompletion/native-revision-green.txt).
These attachments are shipped with the site; the [archive](evidence.md)
describes their scope and omissions.

The complete-bundle [statistics profile](../devices/statistics/protocol.md)
joins unit16's selected commit8 snapshot and automatic TG with immutable
successful readout input, ten endpoint DMAs, running-bank twelve-word results,
four scalars, then raw0.bit6 through existing IRQ94/W1C. Geometry, ring
configuration, consumer packing and software retirement are established.
Grayscale arithmetic, auxiliary-to-processing association and1ms latency
are explicit functional hypotheses, not recovered physical ISP fidelity.
No guest software state, callback, PC or UI pixels are injected.

The scoped old implementation fails the new native fixture because the
coordinator is absent. The final applicable native evidence is32 memory,
16 CMOS and33 Sensor cases (81 total). It covers all ten outputs, exact
arithmetic including signed24/u29/u17 saturation, immutable input/config,
bank selection, ring advance, unread-frame replacement after ACK, cancellation,
reset and invalid ROM/overflow/layout/extent/overlap boundaries.

Unchanged `capturecompletion/finalreview` uses the same cold-neutral
descriptor32, RNG0, healthy paced UART battery, held cold F4 and5 CPUs under
MTTCG. All six original loader COPY ranges compare, guard returns0 with
version150, and property6433 is empty. After20 host seconds QMP is running;
sequential reads report1570/1569 accepted/completed auxiliary requests and
519 completed processing jobs, with no processing rejection/cancellation,
uncovered access or peer rejection. The focused trace observes owner bit2,
the twelve-word consumer and pending40000 retirement. All ten configured
output slot0 prefixes contain nonzero DMA-produced bytes. Queue lengths are
0/0/0/0. The preceding `final` checkpoint completed527 processing jobs and
had0/1/1/1 queues with a new root pending40001. These observations demonstrate
sustained processing and retirement, not a one-job/one-root correspondence
or physical frame cadence. Packages may be discarded when queues are empty.

There is still no display console. No new unsupported hardware transaction
is observed, so a sampled CPU PC is not identified as a startup stop.
The next boundary is the original software image/display request: sampled
root[8]/root[16] were0 in `final`, and both runs have service
`b1868af1=2`, `b1868af6=0` and `b1868b0c=0`.
`103f27ba -> 10328ca6` requires state9/property1053=1 before enqueuing
`10323a7e`, which sets AF6=1. Root[8] separately requires B0C=2. Neither
AF6 request function appears in the focused trace. These conditions explain
why those conditional routes are unselected. The later image request audit
above identifies separate normal information and display activation producers;
the full startup prerequisite relationship remains unresolved. No mode is forced.


## Related analysis

| Question | Article |
| --- | --- |
| How do cold-neutral input and processing completion interact? | [Capture service](capture.md) |
| What does a rendered error frame establish? | [Graphics and timing qualification](graphics.md) |
| How do battery and held-input profiles differ? | [External-input profiles](inputprofiles.md) |
| Who publishes battery reports and host commands? | [EX reports](exreports.md) |
| Which display requests and kernel waits were observed? | [Display publication](publication.md) |

The current boundary above takes precedence over earlier observations. Native
fixtures, unchanged-firmware observations and assisted experiments remain separate.
