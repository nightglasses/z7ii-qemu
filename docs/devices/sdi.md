# SDI mode-register reads

The functional SDI model implements the Z7II C1.70 MAS mode-register-read
transaction for selectors 5–8. Its response is a model-invalid zero, not a
physical DRAM identity. Completion uses the existing virtual 10 microsecond
device latency; this is not a measured DRAM latency.

The source-established producers are MAS Thumb VA `0x1020393e` and `0x102039a2`.
The consumers at `0x102037fc` and `0x1020384e` read and acknowledge the eight DDR
status words, signal the native event, and let the requester read the result.
MAS VAs are firmware addresses, not measured hardware addresses. The board
profile maps the two modeled physical bank apertures at `0x20020000` and
`0x20040000`; the following offsets are relative to either aperture.

| Offset | Width | Modeled contract |
| --- | --- | --- |
| `0x311c` | 32 bits | Accept `(selector << 24) | 0x10001`, selectors 5–8 only |
| `0x321c` | 32 bits | MRR interrupt enable, readable/writable 0 or 1, reset 0 |
| `0x331c` | 32 bits | Completion flag, write-one-to-clear |
| `0x3a08` | 32 bits | Zero response after completion, retained after acknowledgement |
| `0x3204`, `0x3208` | 32 bits | Original enable/disable controls, readable/writable 0 or 1; no error producer |

Commands require released top and DDR resets and completed DDR initialization.
An in-flight or unacknowledged command rejects another request. A timer makes
completion pending, independently of interrupt masking. The bank IRQ combines
existing initialization and training completion with enabled MRR completion.
Disabling MRR interrupts preserves its pending completion. Reset cancels the
timer and invalidates the response. Early result reads, unsupported selectors,
widths, command bits and control values remain explicit errors.

The native `nikon_sdimrr_unit` fixture covers all four selectors, delayed
completion, masked and combined IRQs, acknowledgement and retained results,
reset cancellation and rejected boundaries. These tests establish this finite
device contract; they do not establish camera startup or physical RAM data.
