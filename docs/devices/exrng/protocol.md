# EX hardware random number generator

The original media record path builds a descriptor from the constant
`0x50060800` (`sub_802e36`) and drives it through `sub_804c40`,
`sub_804ccd2` and `sub_804c6c`: control bits 2 and 3 are cleared, status bits
5 and 6 are cleared, and later control bit 2 is set again. Those offsets and
bits match the STM32F4 RNG register layout, and the block is modelled that way.

Before the window existed the guest took a precise data bus error
(`CFSR = 0x8200`, `BFAR = 0x50060800`) on every boot, the fault handler reached
`sub_804cd74` and wrote AIRCR `SYSRESETREQ` at `0x8049088`, and the whole EX
domain restarted about once per second.

## Model

`hw/nikon/devices/exrng.c` (`nikon-ex-rng-functional`) maps 1 KiB at
`0x50060800`:

| Offset | Width | Behaviour |
|---|---|---|
| `0x00` | 32 | control: bit2 enable, bit3 interrupt enable, other bits read back as written |
| `0x04` | 32 | status: bit0 data ready while enabled, bits 5/6 clear by writing zero |
| `0x08` | 32 | 32 random bits; the read clears data ready |
| anything else | any | explicit access error |

Randomness comes from the host generator, so no fixed sequence and no invented
value is used. The block raises no interrupt and reports no error conditions of
its own. A read of the data word while the block is disabled returns zero and
leaves data-ready clear, which is the documented register behaviour rather than
a fabricated payload.

`/nikon/ex/rng` covers the enable/ready/data/clear sequence and reset. The
original bundle no longer faults on this window.

## Open

The block's vendor identity is inferred from the register layout, not recovered
from documentation, and the interrupt path is not modelled because the image
enables the block without its interrupt.
