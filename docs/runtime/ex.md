# Private EX composition

A complete decoded bundle attaches the original MAS, SLA and EX components.
MAS keeps at most two Cortex-A7 CPUs. SLA owns two additional Cortex-A7 CPUs,
its memory and its GIC. EX owns one Cortex-M4 CPU, its private ARMv7M memory,
SRAM, DMA and NVIC. EX memory is never mapped into the MAS system address space.
The complete immutable EX GBytes slice is supplied before realization; the
device retains its own reference. Raw `-bios` MAS input has neither private peer.

Official multi-thread TCG needs capacity for all five contexts while the MAS
domain still has two CPUs:

```sh
qemu-system-arm -machine z7ii-mas-diagnostic,bundle=<decoded-container> \
    -smp cpus=2,maxcpus=5 -accel tcg,thread=multi -nodefaults
```

The machine guards the MAS array bounds separately from execution capacity.
Adding a private EX CPU does not create a third MAS CPU or change either GIC's
domain CPU count. The existing source-qualified SLA bridge and memory links
remain independent from EX. VR and TPJ are retained bundle components; their
execution is not implied by this composition.

The source EX TxA task uses MAS SSI0 to clock EX SPI1's slave RX endpoint. RxA
uses SSI1 to clock EX SPI4's slave TX endpoint. Both source MAS resource records
contain the no-DMA sentinel `158`; these channels remain FIFO/IRQ driven. The
existing DMA connections for SSI2 through SSI4 are unchanged. The mature SSI
chip-select index identifies the endpoint, while original MCU software NSS
controls its SPI configuration; it is not a frame acknowledgement.

Original constructor `1020b980` supplies equal 16-byte configurations for
SSI0 and SSI1: master, 16-bit words, LSB first, CPOL0/CPHA1 and a 20 MHz
requested rate. The divider is computed from the guest's selected source
clock; machine code does not replace that source rate with a fixed value.
The RX/TX requests contain `80` hexadecimal beats, giving 256 bytes per
frame. Original IRQ records bind SSI0 RX/TX to 192/193 and SSI1 to 194/195.
The original EX slave configuration uses eight-bit words. The native endpoint
therefore receives an explicit 16-bit, LSB-first master clock contract and
performs two real EX byte clocks per MAS word, packing their actual replies.
Machine code performs no frame conversion or synthesized completion.

Four software-complementary physical pairs are explicit functional-board
assumptions, because no board netlist was recovered:

| Original EX producer/consumer | MAS producer/consumer | Role |
|---|---|---|
| GPIO B14 output | GPIO5 bit9 input | TX readiness |
| GPIO D12 output | GPIO6 bit15 input | Frame validation |
| GPIO E3 output | GPIO5 bit10 input | RX request |
| GPIO G6 input | GPIO6 bit12 output | RX acknowledgement |

Only real guest GPIO drives traverse these pairs. No power flag, ready bit,
reply frame or IRQ is supplied by machine code. The independent EX model owns
its C8 I2C-expander interrupt and source-backed GPIO pull configuration.

The source 17-pad input frontend is realized after EX and connected before its
own realization. Its explicit host-key profile publishes physical pad levels;
the guest performs scanning and debouncing. Resettable exit and the EX retained
input wrapper preserve actual supplied levels across global, MCU and RCC resets.
This does not assign a camera menu meaning to a host key or synthesize a touch
contact. Real original menu, peripheral and SPI-arm execution still requires a
combined-runtime witness after this composition has been compiled and tested.

The separate 24-pad EX I2C expander at address `0x22`, QOM path
`/machine/ex/io`, accepts an explicit machine input property `ex-io-levels`.
For example, `ex-io-levels=0:0;9:1;23:0` drives pad indices 0 and 23 low and
pad 9 high. Entries are decimal `pin:level` pairs separated by semicolons;
pins must be unique in `0..23`, and levels must be `0` or `1`. An empty or
omitted property drives no expander pads. Every omitted pin remains physically
disconnected and follows the expander's existing pull/floating contract.
Quote the machine argument in a shell when it contains semicolons.

Nonempty input requires a complete bundle and must be configured before machine
initialization. After EX realization, the machine applies only the specified
levels through the expander's existing named `pad` GPIO inputs. Its existing
external-drive state retains them through reset; the machine does not replay
configuration on reset or override later external input changes. This option
does not name camera buttons, infer polarity or measured board wiring, connect
the seventeen-key frontend to the expander, or supply guest events or replies.
Any selected pad values are explicit external-input assumptions and must be
recorded independently from original-firmware execution progress.

Focused synthetic coverage lives in qtests
`/arm/nikon/combined/ex-io-levels` and
`/arm/nikon/combined/ex-io-levels-reject`, plus the native
`nikon_exlevels_unit` fixture. The unit reads the private expander over its real
I2C bus, checks driven low/high versus disconnected pads under programmed
pull-downs, global and physical reset, and subsequent input release. These
fixtures require no proprietary firmware and do not establish original startup.

The independent SLA clone requires an external 128 KiB input before the package
ROM. The default keeps that range unavailable. An explicit machine option
`sla-boot-prefix=erased` supplies a counterfactual erased-FF input; an explicit
file supplies exactly 128 KiB of read-only bytes through the shared SLA parser.
Neither option copies code into the clone destination or changes guest page
tables. The erased profile is not recovered Nikon instructions and cannot
establish original execution if a CPU enters its bytes. The option requires a
bundle because raw MAS input has no private SLA domain. A combined run must
record the selected input profile separately from the source-qualified EX
endpoint and from any actual original GUI output.

The configured EX battery path also polls a separate board input, EX GPIOD.13.
The unchanged firmware leaves the pin as a GPIO input and reads its IDR bit at
`0x40020c10`; with the input undriven the configured path repeatedly returns to
its wait state before ADC/transport initialization. The machine therefore
accepts the explicit boolean `ex-gpiod13=on` input, which holds that named GPIO
input high and replays the level across EX GPIO mode/reset activity. Omission
leaves the input undriven. This is a board-input assumption, not a fabricated
guest status or power report; it only removes the observed GPIOD.13 gate.

MAS has a separate `mas-boot-prefix` option with the same strict, erased or
exact-file choices. The two options invoke the existing shared input parser
independently and map distinct immutable ROMD storage in their own address
spaces. A file supplied for one domain is not reused for the other domain.
The original MAS selector-2 transfer reads `10000000..10100000` into `baa00000`:
only its first 128 KiB is external; the following `e0000` bytes come from the
original MAS package. This option maps only that missing input range, keeps
guest writes under the existing MEMC rejection contract, and never preloads
the destination or page tables. MAS input is also valid with raw `-bios`; an
SLA input still requires the complete bundle.
