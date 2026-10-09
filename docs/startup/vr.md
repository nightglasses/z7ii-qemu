# VR RX peripheral protocol

The `nikonvr` CPU profile uses existing RX TCG and EXTB. Its chip part number
has not been proved. Register names and bit positions are checked against the
[official Renesas RX Driver Package](https://github.com/renesas/rx-driver-package/blob/master/source/r_bsp/r_bsp_vx.xx/r_bsp/mcu/rx64m/register_access/ccrx/iodefine.h),
retrieved on 2026-10-09. That reference is a register protocol reference; it is
not evidence that Nikon used an RX64M. SYSTEM and RTC are separate devices.

The observed RSPI1 address `d0140`, three-bit ADADC and sampling registers at
`890e0` are additionally checked against the
[official RX65N register header](https://github.com/renesas/rx-driver-package/blob/master/source/r_bsp/r_bsp_vx.xx/r_bsp/mcu/rx65n/register_access/ccrx/iodefine.h),
retrieved on 2026-10-09. This matches peripheral capabilities and still does
not identify the Nikon part. MTU3a extended clock selection is described in
tables 24.7–24.9 of the [Renesas RX65N/RX651 hardware manual](https://www.renesas.com/en/document/mah/rx65n-group-rx651-group-users-manual-hardware).

The original bootstrap at `fff001f8` initializes PORT/MPC and at `fff005bd`
initializes CMT, MTU, SCI1/5, eight DMA channels, DTC and ADC. The final ICU
routine at `fff009a4` programs priorities, masks and selectable interrupt
sources. The model maps these finite register groups individually and rejects
unknown offsets, widths and unsupported phases through `MEMTX_ERROR`.

PORT stores direction, output, peripheral selection, open drain, pull and drive
configuration. PIDR is produced by GPIO output or a real external pad, with a
named low input assumption when nothing drives the pad. PCR can pull an
unconnected pad high. External producers use the `pad` input GPIO list; pad
levels are exposed on the separate `pad-out` output list, as required by QEMU's
named GPIO contract. MPC PWPR protects pin function writes. P47 configured by
the guest as IRQ15 supplies the original SCI5 select interrupt; P17 supplies
the application's IRQ7 input. Both follow
the real select line, the selected edge/level mode, IR, IER and IPR. ICUb stores
256 priorities and group masks. Upstream ICUa cannot represent this layout:
its IPR storage has only 142 entries and four DMA selectors. CMT reuses the
existing upstream Renesas timer implementation and drives vectors 28/29.

The machine supplies an explicitly named **functional 60 MHz peripheral
clock**. This is not a measured Nikon hardware frequency. CMT, MTU/TPU internal
clocks and master SCI/RSPI transfer timing use this input. SCI5 uses external
SSI clock frames. SCI10's synchronous master clocks use SMR, BRR and CKE;
its transmit timer cannot create RDRF when its SSI bus has no attached peer.
Writing configuration or enabling DMA cannot create a received SCI byte.

The original MAS SPI4 traffic is consumed by VR **SCI5**, not by RSPI0. Direct
observations in the application are SCI5 at `8a0a0`, SMR `80`, external-clock
SCR CKE `2`, receiver DMA0 from `8a0a5`, sender DMA1 to `8a0a3`, and DMRSR0/1
values 84/85. `nikon_vr_peripherals_attach_spi()` attaches a normal upstream
SSI slave to the MAS SPI bus. Its chip-select input drives P47. Each actual
clock frame returns a byte previously written by the guest to TDR and offers
the incoming byte to RDR. RDRF/overrun, TX empty/end, group requests and DMA
activation result from those transfers. The model does not parse opcode 18,
262-byte records or manufacture byte 2 acknowledgements. The guest owns
consumption, record validation and reply generation.

The normal DMA mode implements original `DMTMD=2001/2101`, source/destination
increment or decrement/fixed modes, each actual address-space read/write,
remaining count, disable-on-completion, DTIF/ESIF and interrupt enables. An
empty enabled TDR produces its genuine initial request; subsequent requests
follow external SCI clocks or completed RSPI master frames. Failed memory
transactions preserve the remaining count and produce an error instead of a
successful beat.

DTC activation requires DTCST, the matching DTCER and IER, and an actual
peripheral request. It reads the vector pointer and full-address descriptor
from guest RAM and performs each actual address-space transaction. Normal
and repeat modes implement byte/word/longword size, address updates, repeat
area reload, chain activation, DISEL and WBDIS. WBDIS preserves the guest
descriptor and DTCER rather than silently completing it. All peripheral
requests use a QEMU bottom half after their register callback returns, so
DMA/DTC never reenters the MMIO region which raised the request. Repeated
timer activations retain their count; ordinary ready requests coalesce while
pending. Nested requests from MMIO writes are processed after the current
DMA/DTC transaction returns.
Unsupported descriptor phases or failed bus transactions report errors.

The complete original extracted body contains the DTC vector table at body
offset `c7c00`, copied by the guest to `39c00`. Vectors 108, 109 and 212 point
to guest descriptors `160fc`, `1611c` and `1613c`. The first two implement
sixteen longword repeat transfers between RSPI2 SPDR and guest buffers,
followed by a chained byte write to SPCR; the last performs a byte SPCR write
with WBDIS. These values are evidence for the implemented protocol and are
not embedded firmware rules in the device. The bit semantics are checked
against the [Renesas DTC migration guide](https://www.renesas.com/en/document/apn/rx65n-h8sx1668-dtc-migration-guide-h8sx1668-rx65n-rev100),
R01AN6775EJ0100 revision 1.00, 2023-03-27.

RSPI0/1/2 have independent upstream SSI buses and transfer timers, sequence
commands, TX/RX holding registers, status and DMA/DTC events. SPDCR determines
the one-to-four-frame buffer size. The original RSPI2 SPDCR `23` waits for
four longword frames, clocks every frame and exposes the completed receive
buffer to the guest's DTC transfers. With no peripheral
attached, upstream SSI returns zero: this is an explicit functional MISO idle
wire, not a sensor value or a VR acknowledgement. Other unconnected serial
inputs do not receive unsolicited bytes.

MTU1/2/3/4/6/7 and TPU0–5 own running counters, compare registers, status flags,
counter clear and overflow events. TSTRA/TSTRB/TPUA.TSTR start and stop their
respective channels. Selectable A/B events use the guest's actual ICU selector
values and write protection; selector 9 is MTU1B. MTU TCR2=1 selects clock/2.
External clock modes advance only on the named `timer-clock` GPIO input, in
channel order MTU1,2,3,4,6,7,TPU0,1,2,3,4,5. Original TPU5 TPSC=7 consequently
does not advance while its external clock wire is disconnected.

The three observed ADC channels accept real 12-bit `analog` GPIO input values;
negative input disconnects a channel. No conversion completes until every
selected channel is connected. ADST starts a conversion, ADADS selects value
addition, and original ADADC=5 samples sixteen times. Result registers are
read-only and unavailable until produced. ADIE produces selected B event 64;
continuous scan repeats actual sampling. Each sampling step uses an explicitly
named **functional 1 microsecond settling interval**, not a silicon timing
measurement. No analog endpoint is attached by this component profile.

The native fixture `tests/qtest/nikon/peripherals.c` checks MPC locking,
GPIO/mux readback, actual IRQ15 edges and acknowledgement, mature CMT start/
stop, RSPI-clock-driven transmit/receive DMA counts and failed bus reads,
MTU compare/stop events, selector protection, external TPU clock edges and ADC
input-dependent sixteen-sample completion.
Additional cases check a repeat DTC chain and WBDIS writeback suppression,
SCI10 transmit DMA with an absent receive peer, and RSPI2 four-frame buffering.
It contains a synthetic component container and no Nikon executable code.
It is separate from the acceptance run using the complete original decoded
container. Passing a synthetic register fixture does not establish an
original guest-produced MAS/VR reply.

The complete application was scanned for peripheral base loads and their
effective accesses, together with the bootstrap. The resulting consumer
groups are implemented as separate regions:

| Consumer | Register window | Source evidence |
| --- | --- | --- |
| PORT/MPC | `8c000–8c1ff` | Bootstrap `fff001f8`; SCI1 P17 and SCI5 P47 mux/input consumers |
| ICUb | `87000–87a01` | Bootstrap `fff009a4`; IRQ7/15, groups BL0/AL0, DMA selectors and selectable A/B vectors |
| DMA0–7 | `82000–82204` | SCI5 RX/TX 84/85, SCI1 RX 60, SCI10 RX/TX 104/105 and RSPI requests |
| DTC | `82400–8240f` | Application `00016a69`; original RAM descriptors for vectors 108/109/212 |
| SCI1 / SCI5 | `8a020–8a02d` / `8a0a0–8a0ad` | Application `0002f34b` / original MAS transport |
| SCI10 | `d0040–d005f` | Application `00017373`; synchronous master, DMA6/7, FIFO reset command |
| CMT | `88000–8800f` | Bootstrap CMT initialization; upstream timer vectors 28/29 |
| TPU0–5 | `88100–8816f` | Application `0002f0ba` TPU5 external clock and effective TCNT `88166` |
| MTU1/2/3/4/6/7 | `c1200–c12ff`, `c1380–c139f`, `c1400–c141f`, `c1a00–c1aff` | Bootstrap counter setup; application compare consumers and MTU4 selected vector 212 |
| ADC channels 0–2 | `89000–890e2` | Bootstrap sixteen-sample addition; application result consumers |
| RSPI0 / 1 / 2 | `d0100–d0120`, `d0140–d0160`, `d0300–d0320` | Application SPI initialization and RSPI2 DTC sensor transport |

Each window admits only named registers with their actual access width; a
window is not a claim that every offset is implemented. SYSTEM/RTC consumers
are owned by the separate Renesas devices, including keyed software reset at
`800c2`. Application loads of `80000` in the arithmetic helper near `00020818`
are numeric constants, not register accesses.

## Remote SCI5 endpoint

`nikon-vr-peer` is a thin native frontend with `peer` and `peripherals` QOM
links. The common `nikon-peer` CharBackend carries CLOCK, SELECT and GPIO
signals; this adapter contains no firmware packet parser. SELECT line 0 is
the physical P47/IRQ15 level. GPIO line ids are RX `port * 8 + bit`; machine
wiring must map those ids to the corresponding physical peer pads. Only PDR
outputs with PMR selecting GPIO are published. Output levels rejected by
transport backpressure remain unpublished and retry on transport readiness;
the next publication uses the current physical level.

The public `nikon_vr_peripherals_clock()` accepts one eight-bit token and
retains it while the guest is unarmed. The existing QEMU bottom half waits for
actual active select, synchronous SCI5 CKE=2, TE/RE, an enabled RX DMA request
84, the source's non-inverted LSB-first SCMR format, a clear receive holding
register and a byte actually present in TDR.
Register writes and DMA completion wake that bottom half. It clocks the
existing SCI frame exactly once, drains the actual RX/TX DMA requests, and
only then invokes the completion callback with the captured guest TDR byte.
CS edges, empty TDR and unarmed DMA cannot create replies. Transport reply
backpressure retains the captured byte and retries sending, without another
SCI clock or DMA beat.

The adapter accepts source-required eight- and sixteen-bit wire beats. MAS
constructor `103BC13C` supplies configuration byte `+1=0`; native `103706C4`
passes it to `10243170`, which clears CTRL bit 2 (`MSB1ST`). MAS uses
little-endian halfword DMA, so a sixteen-bit beat clocks the low byte first.
VR bootstrap `fff00755` writes SMR `80`, and `fff00758` writes SCMR `72`.
SCMR SDIR=0 means LSB-first transmission/reception in the
[Renesas SCI migration guide](https://www.renesas.com/en/document/apn/h8h8s-rx-mcus-h8s-rx-migration-guide-serial-communications-interface-rev100),
R01AN4046EJ0200 revision 2.00, page 15. Both ends therefore agree without
bit reversal. A sixteen-bit request performs two independently armed SCI5
frames, low then high, and replies with `low | (high << 8)`. Cancellation
between them retains the first byte's actual RX/TX DMA effects and cancels
the remaining clock; no first byte is replayed.

The extracted original application programs a 276-byte hardware window:
`000176b0` writes DMA1 DMCRA at `82048`; the high-select ISR restores DMA1
count 276 at `00017dce` and DMA0 count 276 at `00017e05`. The low-select ISR
at `00017d6a–00017da7` supplies the guest TX buffer and enables SCR `f2`.
These counts can cover the MAS 268-byte transfer, but do not prove an original
completed packet. In particular, a final sixteen-bit beat still needs two
actual TDR bytes; an empty second frame waits instead of receiving padding.

Chip select is independently guest-owned: MAS GPIO resource `ae` is port 11
pin 1, set low at `103BC296` and high at `103BC2BA` around `10370A14`.
It must drive SELECT line 0 through machine wiring; SPI enable is not a
substitute for that physical wire.

`nikon_vr_peripherals_cancel()` removes the matching pending callback. Effects
of a frame already clocked are retained. Adapter reset, incoming CANCEL,
disconnect and detach cancel the shared incoming token without sending a
reply. The normal SPI select input starts inactive high, as in the local SSI
slave; disconnect does not fabricate a new select edge.
The busless frontend explicitly registers its Resettable object with QEMU's
system reset hierarchy and unregisters it on unrealize. Merely defining a
device reset hook does not attach a busless device to that hierarchy.

The immutable dependency is the actual native8 PASS snapshot
`f8078bf944ef0867736223365a2ece21ba111735`; the endpoint uses shared peer API
`9398300` on `3439c35`/`964a354`. A ninth native case uses the standard qtest
socket helper and the unchanged transport envelope. It checks GPIO levels,
real select IRQ, unarmed and inactive-select waits, cancel/reset/disconnect,
guest-written TDR bytes `a5`, `5a`, `77`, and received bytes reaching RAM via
DMA. It also verifies sixteen-bit low/high ordering and cancellation while
the second byte waits for real TDR refill. Qtest manually programs the
synthetic guest registers and does not
execute Nikon's ISR. This new case awaits sole-builder validation.

The complete original image remains strictly stopped at bootstrap
`fff00167`, reading the four-byte external function-pointer slot `fffc8008`.
Bootstrap performs `JSR` through that value at `fff00169` before entering
application `00018583`. This endpoint supplies neither that data slot nor
target instructions, and its synthetic fixture is not an original VR ACK
receipt.
