# QEMU to GUI Handoff

## Status

The long-running Z7II C1.70 functional-v1 goal is paused at the user's request.
No QEMU, GDB, or provider process remains running. The QEMU working tree is
clean at `5c3b4df8` (`qemu: map source-backed MAS inner load views`). The
official `qemu/` submodule is unchanged.

The current objective is still the complete natural path from power-on through
the original firmware's GUI first frame. A green focused test or a synthetic
frame is not completion.

## Reproduction input

- Bundle: `/Users/auturora/Documents/Codex/nightglass/work/z7ii-1.70/decoded-container.bin`
- Machine: `z7ii-mas-diagnostic`
- Boot shim: `functional-v1`
- Useful binary: `work/build/qemu/qemu-system-arm`
- Typical run:

```sh
work/build/qemu/qemu-system-arm \
  -M z7ii-mas-diagnostic,bundle=/Users/auturora/Documents/Codex/nightglass/work/z7ii-1.70/decoded-container.bin,boot-shim=functional-v1 \
  -smp cpus=2,maxcpus=5 -accel tcg,thread=multi \
  -display none -serial none -monitor none
```

Use multi-thread TCG for the mainline run. Single-thread TCG changes task
ordering and can produce a false early `destination=0` observation.

## Established startup facts

These facts are supported by original-payload static listing plus read-only
GDB observations:

1. CPU reset, CP15/MMU setup, SMP release, task creation, scheduler restore,
   the kernel prelude, PKGB entry, and CMD88 all execute naturally.
2. `0x103a34c0` is the CMD88 resource initializer. It writes the runtime object
   at `0xb1808c98`:

   - `+0x04 = 0xb0d00000`
   - `+0x08 = 0xaf200000`
   - `+0x0c = 0xb0900000`

3. `0x103a3604 -> 0x103a361e -> 0x103a3b66` is a separate selector-zero COPY
   path. It has no static call edge to `0x103a34c0`; do not reorder the tasks or
   prefill the object.
4. EV88 is a guest-owned event path. `0x10388c18` can set handle
   `0xb18094f0` with mask 2 through the IRQ102 event-gate path. Do not inject
   EV88, IRQ102, ready state, or a peer reply.
5. The old `0x102dcc46 -> 0x20060034` DataAbort receipt predates the current
   router implementation. Current `hw/nikon/devices/router.c` accepts offset
   `0x34` as a 32-bit configuration field, and `z7ii.c` maps the router at
   `0x20060000`.

## COPY receipts

The machine-readable receipt is `work/provider-forward-all.json`. It was
captured with a read-only hardware breakpoint at `0x103a3b66` and no guest
writes. The multi-thread run produced these important requests:

- `0x107a4f34 -> 0xb0d00000`, length `0x3b1c0`
- `0x107e00f4 -> 0xb0905000`, length `0x34238`
- `0x1081432c -> 0xb093a000`, length `0x33300`
- `0x1087a92c -> 0xb096e000`, length `0x2cbc8`
- `0x122acf40 -> 0x50200000`, length `0x27800`
- `0x122d81a0 -> 0x50000000`, length `0x57e30`
- `0x1232ffd0 -> 0x50080000`, length `0x69e70`
- `0x122d4750 -> 0x50000000`, length `0x3a50`

The multi-thread run proves B0D and B090 transfers cross naturally before the
`0x50200000` request. The current QEMU model deliberately maps B0D and B090
as named regions, but has no owner for `0x50200000`; the COPY preflight rejects
that destination. `0x50000000` and `0x50080000` also lack a proven provider.

The single-thread run can show `0x107a4f34 -> 0x00000000` because CMD88
initialization and selector-zero loading are independent tasks. Treat that as
a scheduling diagnostic, not as permission to alias PA zero or prefill B0D.

## GUI boundary

The static GUI chain is:

```text
phase/config
  -> GUI child registry
  -> event/action dispatch
  -> transfer record
  -> display/scanout
```

The transfer consumer reads `child+0x18` as a callback and `child+0x1c` as
its argument. The constructor chain rooted at `0x10715172` and literal
`0xb2033968` only clears `+0x18`; it does not write `+0x1c`. The only proven
registration seams are the exact-child calls at `0x10636368` and
`0x10636328`. Generic stores to the same offsets belong to unrelated geometry
or state objects and must not be reused.

QEMU `transfer.c` owns the real transfer registers, DMA extent checks, and
completion IRQs. The display-bank model owns its guest-configured bank data and
pixman output. No callback backend or framebuffer shortcut is justified until
the exact child identity and writer are recovered.

## Next work

1. Close the `0x50200000`/`0x50000000` provider contract by combining raw
   Listing, address translation, descriptor consumers, and current QEMU
   MemoryRegion behavior. Either implement an evidence-backed QEMU provider or
   prove a package-external backend boundary with a typed, replaceable interface.
2. Re-run the multi-thread functional trace after any provider change and
   capture COPY completion and the first post-provider guest PC. Do not use
   synthetic RAM, aliases, IRQs, or completion writes as a shortcut.
3. Only after the provider boundary is crossed, re-run the exact-child GUI
   watcher and recover the `+0x18/+0x1c` callback ABI, then connect the original
   transfer/display path.
4. Keep all debugger files and raw logs under ignored `work/`; delete large
   logs after extracting the small machine-readable receipt.

## Prohibited shortcuts

- Do not map `0x50200000` or `0x50000000` merely because the guest requests it.
- Do not connect SLA `peer-ready` to the MAS bridge `peer-ready` without a
  matching original wiring proof; they are currently separate inputs.
- Do not inject event flags, IRQs, DMA completion, callback pointers, or pixels.
- Do not treat the GUI qtest acceptance scaffold as a real first-frame receipt.
- Do not modify `vendor/qemu`, add source-recovery work, or leave stale QEMU/GDB
  processes behind.
