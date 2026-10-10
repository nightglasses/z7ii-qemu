# EX SysTick and input-dispatch boundary

This receipt explains the reported stop at `VA 0x080202c8` and records the
smallest source-backed state needed before the EX input dispatcher at
`VA 0x0803e12c` can deliver a record. It uses the unchanged C1.70 EX payload
`00_ex1840_013000.bi.payload.bin` and the existing `nikonex` reset model.

## The stop is not a guest BKPT

The bytes at the reported PC are:

```text
VA 0x080202c8: 2d e9 30 50 20 4c 25 68 05 f1 01 00 20 60
```

Decoded as little-endian Thumb-2, this is a normal SysTick handler:

```asm
080202c8:  e92d 5030       push.w {r4, r5, ip, lr}
080202cc:  4c20            ldr   r4, [pc, #128]
080202ce:  6825            ldr   r5, [r4, #0]
080202d0:  f105 0001       add.w r0, r5, #1
080202d4:  6020            str   r0, [r4, #0]
080202d6:  f02b fa4f       bl    0804b778
080202da:  6025            str   r5, [r4, #0]
080202dc:  e8bd 5030       pop.w {r4, r5, ip, lr}
080202e0:  4770            bx    lr
```

There is no `BKPT` encoding in this instruction window. A `SIGTRAP` reported
while the debugger is stopped at this address therefore identifies a host
debugger breakpoint (or a debugger stop), not a guest breakpoint instruction.
The direct runtime receipt also shows the vector loading `0x080202c9` for
exception 15 and repeated exception entry/return without a fault. The QEMU
machine does not need a trap bypass here.

## Minimum reset state

The EX vector table has the following source-backed entries:

| Item | Value | Evidence |
|---|---:|---|
| initial SP | `0x20000c08` | vector word and `nikon_ex_set_firmware()` validation |
| reset PC | `0x080002b5` | vector word, Thumb bit set |
| SysTick vector | `0x080202c9` | vector slot 15 after the guest scatter copy |
| VTOR | `0x20000000` | reset code writes VTOR after copying `0x1c4` bytes |
| SysTick CTRL | `0x7` | observed unchanged original run |
| SysTick LOAD | `0x0000c34f` (`49999`) | observed unchanged original run |
| SysTick handler | `0x080202c8` | vector target with Thumb bit removed |

The current QEMU EX profile supplies the finite flash/SRAM address envelope,
loads the complete component, and lets the unchanged guest perform its reset
copy and vector-table write. Its SysTick and NVIC devices then perform the
ordinary exception entry/return path. Do not replace this with a host callback
or skip the handler.

## Minimum input-dispatch state

`0x0803e12c` is a null-safe callback boundary, not an action handler. Its
instructions load the state object at `0x20018438`, read `state + 0x1c`, and
branch to that pointer while preserving the caller's `r0`/`r1`:

```asm
0803e12c:  4a02       ldr   r2, [pc, #8]       ; 0x20018438
0803e12e:  69d2       ldr   r2, [r2, #28]       ; event receiver
0803e130:  2a00       cmp   r2, #0
0803e132:  d000       beq   0803e136
0803e134:  4710       bx    r2
0803e136:  4770       bx    lr
```

The minimum guest-owned conditions before this boundary can do useful work
are therefore:

1. EX reset has completed and the vector/SysTick state above is live.
2. The original GPIO descriptor table and sampler have been initialized; the
   sampler must produce a normalized record at `0x20018678`.
3. The real startup registration has populated `state + 0x1c` with a callback.
   A different setter at `0x0803e4e0` writes `state + 0x24`; it is not evidence
   that `state + 0x1c` is populated, so QEMU must not fill either field.
4. One of the original producers supplies an event code (`0x02030081`,
   `0x02030083`, or `0x0203008c`) in `r0` and the normalized record pointer in
   `r1`. The callback receiver and its EX-to-MAS transport remain unobserved.

The electrical QEMU boundary consequently remains `host input -> EX GPIO ->
guest sampler`. It must not inject a record, write `state + 0x1c`, invoke the
callback, or write the MAS event object until a joined original runtime trace
identifies that receiver and transport.

## Reproduction and next observation

Run the unchanged original bundle with `-M nikonex`, `-display none`, and
`-d guest_errors,int`. A normal finite run reaches repeated SysTick entries;
the next useful debugger observation is a breakpoint on the *runtime callback
pointer* loaded from `0x20018438 + 0x1c`, followed by a read-only capture of
`r0`, `r1`, the return address, and any MAS event-object writer. A breakpoint
at `0x080202c8` is only an observation point and must not be interpreted as a
guest failure.

