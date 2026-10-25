# EX GPIO input profile

This device translates official QEMU keyboard events into external electrical
levels on the seventeen GPIO descriptors selected by the original Z7II C1.70
EX initializer. It does not implement a second keyboard protocol in guest
memory, invoke a GUI handler, send an invented peer packet or assert an IRQ.
The existing EX GPIO/EXTI and original guest code perform sampling, debouncing,
interrupt handling and subsequent message production.

## Original source and address origin

The preserved EX export is TOOL producer
`b865bbc379dc590a49b4ad8f0cac4c17b57ae52a`, at
`run ex-patched-producer-baseline/export-b865bbc/`.
Its 1,269-function inventory is a saved export, not complete EX code discovery.
EX payload O starts at component byte zero, and ROM VA is `08000000 + O`.
The payload is 524,288 bytes. No new Ghidra or native decompiler was run.

Original initializer `VA0803de88` obtains seventeen registry descriptors using
the ID bytes at ROM `O4ef8f..4efa0` and stores them at RAM
`VA200184c8 + 4*i`. The original common reader `VA08043acc..08043ad6`
reads a descriptor's GPIO IDR at `base+10`, applies its pin mask, and returns
zero or one. The initializer `VA080211d0` installs the original u16 lookup
table at `VA0804d9b0` through `080402e4 → 080403ac`; its seventeen selected
entries equal their registry IDs. The registry reader `08021188` and writer
`080211b4` use RAM `2000ee10 + 4*ID`.

The original callback table at `VA0804d950` leads to
`0803fde0 → 0803fe34`. The latter registers 114 descriptor pointers, IDs
`00..71`, and finishes with a tail branch at `080402b8` to `080211b4`.
These 465 original Thumb instructions were decoded in a single bounded,
source-anchored Capstone 5.0.6 pass; they are a raw-byte candidate, not new
official native coverage. Descriptor contents come from the fixed compressed
initialization record `O38dc`: ROM input `O51ba8..528e1` expands to 7,696 bytes
at `VA2000d000`. The existing TOOL `ex_runtime.initial_data` implementation
at `7ced9c1077a2ad9c9a3a6d27989223607a2bfb31` was reused without alteration.
Each selected descriptor's actual GPIO base and single-bit mask agree with
`port=ID>>4`, `pin=ID&15`. The ignored `run input/source.json` preserves all
seventeen bindings, the complete registration candidate and eleven selected
saved native function bodies and references. Old metadata digests are not
used as a new acceptance gate.

| Host key | Original registry ID | EX pad |
|---|---|---|
| F1 | 0a | A10 |
| F2 | 11 | B1 |
| F3 | 1f | B15 |
| F4 | 25 | C5 |
| F5 | 1b | B11 |
| F6 | 47 | E7 |
| F7 | 4d | E13 |
| F8 | 5b | F11 |
| F9 | 5e | F14 |
| F10 | 61 | G1 |
| F11 | 62 | G2 |
| F12 | 6d | G13 |
| 1 | 6e | G14 |
| 2 | 6f | G15 |
| 3 | 1d | B13 |
| 4 | 53 | F3 |
| 5 | 1c | B12 |

## Transport and functional assumptions

`nikon-input` is a non-MMIO device using the fixed upstream QEMU
`ui/input.h` event interface. Machine code connects its named `pad` outputs
before realize to the EX provider's mature GPIO input lines, using
`nikon_input_gpio_ids[]` from `include/hw/nikon/input.h`. It must not connect
these EX pads to the separate MAS GPIO/EXIU routes merely because both use
GPIO. The device registers only keyboard events; mouse, relative, absolute
and multitouch events have no implementation or fabricated success path.

Host key assignments above and `down=0 / released=1` electrical levels are a
named functional switch profile. They are not physical button-name,
electrical polarity or camera wiring measurements. Cold realization publishes
released levels. Repeated down/up events do not change a held level. A board
reset retains held host keys and re-publishes their levels in standard
Resettable exit, after every receiver's hold phase. It does not make a held
key a fresh pulse. Unrealize unregisters the official input handler and
releases the external levels. The device is explicitly unmigratable.

Fixed official QEMU `4fc49f46dc95d4a27de2509e7fceb2931e91faeb`
`hw/gpio/stm32l4x5_gpio.c` resets IDR and external connection state in
`stm32l4x5_gpio_reset_hold`. Publishing from the frontend's legacy hold phase
could therefore be erased by a later receiver hold. Standard Resettable exit
removes that board-reset ordering dependency; realization uses a separate
level-publishing helper and does not invoke a reset callback directly.
The frontend is realized without a bus. A QOM child alone is not a reset-tree
connection: realize registers it with official `qemu_register_resettable`,
and unrealize unregisters it before retiring the input handler. This makes its
exit phase reachable through global reset and prevents a stale reset object
after destruction.
Reset of only an EX GPIO through RCC does not reset this frontend. Preserving
or reapplying physical external drive for that controller-only reset belongs
to the EX provider, independently of this board-reset correction.

The original EX event path `0803e51c → 0803e12c` passes event value `0203008c`
and the source-built normalized record at RAM `20018678` to an installed
callback. Its callback installation, physical peer transport, receiver and
GUI consumer remain open. This device sends none of that record itself.
The MENU name is not bound to any row above. Physical dial names and touch
packet coordinates are also unbound; no wheel delta
or touch event is translated into GPIO transitions or memory writes.
The later C5 startup join below establishes one original input-to-GUI-worker
transition; other input consumers and a visible GUI first frame remain open.

### Original C5 startup join

With the unchanged bundle and explicit functional descriptor, QMP `cont` and
F4-down sent consecutively let the existing pad3/C5 level be present before
initial EX sampling. EX sample `2001f334` bit0 becomes1, original MAS record
byte20 becomes2, normalized type1 selects main mode1, and the manager starts
GUI worker3/mode1. BatCk remains2 and no battery peer is attached. The run
then stops at an unsupported shared channel9 timing transaction, before a
verified GUI first frame. This is a functional host binding, not a physical
camera button name. Late F4-down reaches C5 low but does not by itself
retrigger the consumed EX synchronization update. The complete source,
negative late-input window and cold-input receipts are in
[policy.md](../../runtime/policy.md) and `run policy-20261025/`.

## Original dispatch and rotary consumers

The saved function inventory omits original code between adjacent owned
bodies. A bounded raw-byte check found the actual dispatcher `VA0803e098`:
its input bit 1 branch calls the saved `0803e51c` normalizer. Original
literal `VA0803dcd8` contains Thumb pointer `0803e099`; the source instructions
at `0803dca8` pass it to the saved `080214ac` callback installer, which stores
that argument at object `+24`. The same source routine installs callbacks
`0803ddbd` and `0803ddc9` with arguments 8 and 1 through `08020a5a`.
Those arguments do not establish a physical sampling period or frequency.

Original call-derived root `0803dd00` calls two GPIO phase samplers,
`0803e364` and `0803e3bc`. The former reads descriptor slots 7 and 8
(F11/F14); the latter reads slots 5 and 6 (E7/E13). Their common reader
`0803ce34` loads descriptor base `+8` and mask `+c`, then tail-calls the saved
`08043acc` GPIO IDR reader. Both samplers form `first_pin << 1 | second_pin`
and retain the previous state. The F11/F14 sampler decrements on 0 to 2 and
increments on 2 to 0; E7/E13 increments on 0 to 1 and decrements on 1 to 0.
Both preserve signed saturation and accumulate into state
`20018438+299/+29a`. Original `0803df98` copies those counters to the guest
record and passes event `02030081` through `0803e12c`. These are software
rotation consumers, without a physical dial-name or peer-receiver binding.

The ignored `run input/unowned-event-candidates.json` records eight
call/literal-derived raw CFG candidates, their exact original bytes and
remaining computed-flow boundaries. Capstone does not establish an incoming
ITSTATE, official function ownership, complete native coverage or actual
guest execution. The current device exposes the proven GPIO pads and keeps
those four phase pads independently controllable by their host keys. A wheel
waveform still needs an explicit delivery cadence and actual guest sampling
observation; an instantaneous host pulse would not establish a consumed
rotation. Touch transport and the installed event callback's peer path remain
open.

## Focused native validation content

`tests/qtest/nikon/input.c` supplies `nikon_test_input(QTestState *)` for the
sole builder to register on a stopped synthetic board with this frontend.
It uses real QMP `input-send-event`, intercepts named pad outputs and checks
all seventeen down/repeat/up paths, simultaneous keys, an unmapped key, held
levels across system reset and independent release. QEMU requires a running
VM for this official input command; the fixture explicitly uses `cont/stop`.
No original firmware, host memory injection or direct GUI call is involved.
`tests/unit/nikon/input.c` supplies an independent native system-library unit,
using official `qemu_init` with machine none and qtest acceleration. Actual
upstream STM32L4X5 GPIO receivers have connected fixture clocks and input-mode
pads. Mature `sysbus_realize` attaches these receivers to the normal bus reset
tree without duplicate manual registration. A separate standard Resettable
assert/release sequence places receiver hold after frontend hold, exposing
the earlier ordering failure without invoking phase callbacks directly. The
unit sends official QEMU Linux key events in the normal running state,
performs a real global system reset, and reads each selected GPIO's actual IDR
region, including released
pads and held keys after reset. It checks independent release, repeated events
and complete frontend/receiver retirement followed by reset and reconstruction.
It does not intercept frontend outputs: official qtest output interception
replaces their sink and would disconnect the real receiver. It adds no test
machine, global MMIO address or alias to EX's private address space. The
earlier proposed global-address GPIO qtest entry is removed. The output-only
QMP fixture remains separate. The sole builder owns native registration and
compilation; original GUI observation remains pending. Neither
fixture asserts power state, guest event flags or a GUI callback.

The native fixture roots its frontend and GPIO objects in the actual QOM tree
before wiring. Official GPIO links resolve a canonical target path: an IRQ
child of an unrooted GPIO would otherwise become an empty link. Each of the
seventeen connectors is checked against the real receiver before realization.
The earlier empty-link fixture failure was not evidence of input reset or
GPIO hardware behavior. The IDR assertions remain unchanged.

The sole builder compiled and linked the production reset implementation
`33e8ec68e85839cd8744ad11a1409909e73c69a8` and executed native unit source
`792509ca0360215abea55a77dbadadf1ba0bf4e5` in canonical composition
`f252e9ca320eeae013cdf9210de9259f08795131`. The retained receipt
`run inputrootedreceipt.log` records
`/nikon/input/receivers-reset-lifecycle` PASS, exit 0, 0.62 seconds, 1 test and
0 failures. Actual assertions cover all seventeen keys, held global reset,
late receiver hold, independent release and registry retirement/reconstruction.
The earlier output-only fixture was not rerun. This is native lifecycle and
electrical input validation; it does not establish original EX peer transport
or a working MENU/GUI event path.
