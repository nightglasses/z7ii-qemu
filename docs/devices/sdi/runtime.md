# SLA DDR runtime status and IRQ family

Source5 (`43d1909`) contains the complete source methods `1005C07C`,
`1005C0CE`, `1005C11E`, `1005C15C`, `1005C1BE`, and `1005C222`. Their literal
resources at `1005C488..1005C4A8` establish two independent SDI banks. This
extension retains the earlier four initialization/training families.

## Whole status reader and routing

The register blocks already mapped at `20020000` and `20040000` each have an
eight-word DDR status group at bank+3300 through bank+331C. Callbacks read
every word, save its value, and write that same value back. IRQ125 registers
Thumb callback `1005C0CF` for bank0; IRQ126 registers `1005C07D` for bank1.
The initializer `1005C11E` initializes its software lock/event object and
registers the two callbacks. `1005C15C` initializes those IRQs; neither method
programs an SDI IRQ mask or supplies completion data.

| Status word | Bank0 software event | Bank1 software event | Bound model producer |
| --- | --- | --- | --- |
| 0 | none in this callback | none in this callback | DDR initialization request+3100 |
| 1 | 4 | 8 | unbound |
| 2 | 10 hex | 20 hex | unbound |
| 3..5 | none in this callback | none in this callback | unbound |
| 6 | none in this callback | none in this callback | five-mode training request+3118 |
| 7 | 1 | 2 | unbound mode-register response request+311C |

The functional status model exposes the entire finite eight-word group.
Known init/training words retain their existing pending/completion state;
unbound event words are inactive. Readback acknowledgement zero is legal and
has no effect. Known event bit0 has W1C acknowledgement; unbound nonzero
acknowledgements and reserved bits fail. IRQ output0 is the OR of actual
pending init/training flags, an explicit functional interrupt law. Reads and
IRQ registration produce no events, and the host never writes guest event
flags or invokes guest callbacks.

Both events retain the existing 10 microsecond virtual request latency and
phase prerequisites. Clearing one flag does not clear another or deassert a
still-pending bank IRQ. DDR/PHY/global reset cancels affected pending work,
clears its flags and recomputes IRQ. The two bank instances remain independent.

The SLA machine must connect bank0 output0 to its private GIC input `125-32`
and bank1 output0 to `126-32`. MMIO and guest buffers remain in that private
address space. This contract supplies no DMA interface, clock peer or DRAM
manufacturer identity. MAS routing must be independently source-qualified.

## Unsupplied response data stays unsupported

`1005C1BE` submits to bank1+311C, and `1005C222` to bank0+311C. They form
`00010001 | (selector << 24)`, wait for their real software event, and read
bank+3A08. The consumer extracts result bits7:0 and23:16 into two bytes.
This proves the request/result geometry, not the identity or contents of the
DRAM response. No actual response data/profile has been supplied. The model
therefore rejects +311C requests and +3A08 reads; it does not schedule an
invented response, set status word7, or return a constant success. A future
response producer needs independently bound data and a complete causal
request/response contract before activation.

## Separate encoded clock selectors

The `1006716C` family registers software clock controls. Its literal
`1006755C=20020008` is compared as an encoded selector; it is never used there
as an SDI MMIO pointer. `100671B6` maintains reference bits in software object
`C1006330+8/+C`. `10067280` read/modifies actual clock register `04281028`
bit16; `10067530` read/modifies `04281034` using the supplied mask. Related
`10067578` selects `04281058/5C` from the encoded clock field and sets/clears
its bit. These belong to the clock owner, not this DDR controller.

New native focused cases `/nikon/sdi/runtime-bank0` and `runtime-bank1` cover
the whole eight-word callback access pattern, virtual-time IRQ production,
distinct pending flags, zero/readback acknowledgement, reset cancellation,
and rejection leaving the unsupplied response inactive. Build and execution
are delegated to the sole builder; this document claims no new passing run.
