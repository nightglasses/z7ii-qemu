# Source-qualified GPIO descriptor and packed configuration family

The original SLA whole-source provider retains complete functions at source VA
`10066c1c`, `10066c42`, `10066d1c`, and `10066d50`, and the complete 232-record
GPIO registry. The saved provider is
`zero-domain-source/sla.S`; bounded presentations are
`sla-domain/run sla/gpio-exiu-family.txt` and `gpio-copy-records.txt`.
These are original source-qualified functions and data, not evidence of a
completed runtime call or physical GPIO activity.
Provider source4 belongs to fixed source producer `1076d278`; its authoritative
`sla-rows.jsonl` directly records source VA `10066d1c` at SLA ROM O `46d1c`
(`8a88`, Thumb) and VA `10066d50` at O `46d50` (`10b5`, Thumb).

Object E7's guard at `10066d50` calls `10066d1c` with the literal register
addresses `04282330` and `04282430` from source VA `10066f84/88`.
Each call emits two full 32-bit stores. Configuration halfword+4 becomes
control bits31..16; nonzero words+8/+c/+10/+14/+18 set bits0/1/2/3/8.
Configuration word+0 becomes the following data word. The shared machine GPIO
region therefore contains independent pairs at relative offsets `330/334`
and `430/434`. Control accepts mask `ffff010f`, data accepts all 32 bits,
and both replace the complete stored word. This is not PDR's upper16 write
mask protocol. No direct caller or literal function pointer for this E7
function was found in this saved source batch; runtime invocation remains
unobserved. Original E7 registry record at source O `841150` contains only
ID E7 and otherwise zero fields, so it supplies no register reset values.

The registry separately binds objects BB..C6 to PDR `04282030`, using only
bits0,1,4,5,8..15; C7..D6 bind `04282034` bits0..15, and D7..E6 bind
`04282038` bits0..15. Those records have zero DDR, function and pull pointers.
The complete setter `10066c1c` still performs a 32-bit masked PDR store when
descriptor+8 is nonzero: high16 selects changed low16 latch bits. The complete
getter `10066c42` reads that address and extracts descriptor byte+4's bit.
The model preserves these three finite PDR states and rejects masks outside
`ff33/ffff/ffff` respectively. It does not add their nonexistent `130/230`
control-bank counterparts or reinterpret them as ordinary port13.

Cold reset zero and stored-word readback for the new registers are explicit
functional profile assumptions. Their physical electrical function, external
provider and silicon reset state are unbound. The new words do not drive
pads, publish peer wires, assert IRQs or change the existing twelve-port
external-level behavior. `nikon_gpio_output` continues to report only the
original 192 pins whose actual DDR and EPCR select GPIO output, with PDR
supplying the driven level. The MMIO extent grows to `438` solely to include
the last finite data word; all remaining holes and unsupported widths fail.

Two new native cases, `/nikon/gpio/descriptors` and `/nikon/gpio/mux`, cover
all source descriptor bits, masked preservation, both independent control/data
pairs and all 32 combinations of the five packed booleans. They also cover
reset, unsupported masks and widths preserving state, and the absence of pad
edges. They are synthetic functional tests; only the sole builder executes
them, and no new test-pass or original-runtime receipt is claimed here.
