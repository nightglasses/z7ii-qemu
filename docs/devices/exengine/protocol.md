# EX channel engine window

The original EX media record transfer reaches one register window at
`0x50060800`. `sub_8023e04` loads that constant into a stack descriptor and
submits it through `sub_8044c40` and `sub_8044cd2`, which rewrite a control
word at offset `0` (clear bits 2 and 3, later set bit 2) and a second word at
offset `4` (clear bits 5 and 6). `sub_8044d16` and `sub_8044d20` index a
32-bit table at offset `0x50` with a limit of twenty entries and log `14005`
or `14006` beyond it.

Before this window existed the guest took a precise data bus error
(`CFSR = 0x8200`, `BFAR = 0x50060800`) on every boot, the fault handler
reached `sub_804cd74` and the AIRCR writer at `0x8049088`, and the whole EX
domain restarted about once per second.

## Model

`hw/nikon/devices/exengine.c` (`nikon-ex-engine-functional`) maps one 4 KiB
window and models exactly the shape the software uses:

| Offset | Width | Behaviour |
|---|---|---|
| `0x000` | 32 | control word, readable and writable |
| `0x004` | 32 | second status word, readable and writable |
| `0x050 + 4n`, `n < 20` | 32 | channel entries, readable and writable |
| anything else | any | explicit access error |

Reset clears the words and the channel entries. The device has no interrupt,
no completion source and no fabricated ready value: a status read returns what
the guest wrote. That is deliberate — the original software contract for this
block is not recovered, so only the accesses the image actually performs are
made legal, and every other access stays an explicit stop.

`/nikon/ex/engine-window` covers the control/status read-back, the twenty
channel entries and the reset.

## Open

The block's identity, its completion semantics and the meaning of the cleared
bits are unknown. The window exists so the guest no longer faults before its
own contract can be observed; the next layer it reveals is a usage fault in the
RTOS interrupt dispatcher (see [EX runtime](../../runtime/ex.md)).
