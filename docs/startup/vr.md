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
unconnected pad high. MPC PWPR protects pin function writes. P47 configured by
the guest as IRQ15 supplies the original SCI5 select interrupt. It follows
the real select line, the selected edge/level mode, IR, IER and IPR. ICUb stores
256 priorities and group masks. Upstream ICUa cannot represent this layout:
its IPR storage has only 142 entries and four DMA selectors. CMT reuses the
existing upstream Renesas timer implementation and drives vectors 28/29.

The machine supplies an explicitly named **functional 60 MHz peripheral
clock**. This is not a measured Nikon hardware frequency. CMT, MTU/TPU internal
clocks and master RSPI transfer timing use this input. SCI5 uses external SSI
clock frames.
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
successful beat. DTC configuration does not inject a transfer when it is not
enabled by the original application.

RSPI0/1 have independent upstream SSI buses and transfer timers, sequence
commands, TX/RX holding registers, status and DMA events. With no peripheral
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
It contains a synthetic component container and no Nikon executable code.
It is separate from the acceptance run using the complete original decoded
container. Passing a synthetic register fixture does not establish an
original guest-produced MAS/VR reply.
