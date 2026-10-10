# Guest PTE and DMA address contract

This note records the boundary between the guest MMU and the QEMU DMA model
for the Z7II MAS profile. It also records the one newly qualified physical
buffer: the selector-zero download scratch used by the RMU peer path.

## Evidence

- The unchanged C1.70 MAS run captured secure `TTBR0_S=0xb2b74000`.
- The L1 entry at the `0xb0d` index was `0xb0d14422`. The low two bits make
  this an ARM section descriptor, with a section base of `0xb0d00000`.
- The failing master was MAS COPY. CPU secure `DFAR_S` and `DFSR_S` remained
  zero, so this event is a DMA bus fault rather than a CPU data abort.
- The current Z7II physical map declares allocator storage from `0xb1800000`
  onward. The selector-zero loader separately initializes a `0xb0d00000`
  scratch buffer with a `0x400000` bound and passes that range to RMU/IPUD.

The first three bullets are direct runtime observations. The download range is
an independent static source contract, not a consequence of the PTE alone.
The section descriptor proves the guest installed a translation entry; it does
not identify a general physical memory owner, DDR controller decode, bus
security domain, or whether every COPY context consumes the same address
domain as the CPU.

## Existing model

`hw/nikon/devices/copy.c` creates an AddressSpace for the controller and keeps
an effective flat-view interval list through a MemoryListener. A request must
be completely covered before the first DMA transaction. Readable RAM/ROMD is
accepted as a source; writable RAM is required as a destination. An uncovered
range records the first channel/source/destination/residual snapshot and
finishes with status `1`.

The board now exposes only the statically qualified selector-zero range as a
dedicated `z7ii.download` MemoryRegion. It is not attached to the six DDR
classes, is not an alias, and has no host-seeded contents. The guest COPY
request remains responsible for filling it. Other addresses in the former
hole still fail and preserve the first-fault receipt.

## Missing evidence

Further dynamic backing outside this exact download range is not justified
until all of the following are captured from the unchanged guest:

1. The firmware PC/LR that writes the `0xb0d` L1 descriptor, including the
   table's physical address and the relevant domain/security state.
2. The memory-controller or board decode that owns the section's physical
   range, including its size, permissions, and reset/enable condition.
3. The COPY request's complete channel tuple and the address-domain rule that
   maps its destination to that owner. A CPU section descriptor alone is not
   evidence that a DMA master can reach it.
4. One successful read/write or a hardware fault classification that
   distinguishes absent DDR from a disabled/protected endpoint.

The exact download range is the exception because its producer, bound,
consumer and command are all source-backed. It is still not a general
permission to alias the surrounding hole, pre-enable MXIC, or seed contents
from the host. The next trace must collect the four items above before any
other dynamic range is added.

## Acceptance gate

The selector-zero address is closed by the saved producer/consumer contract
and the focused `download-scratch` qtest, which verifies a four-byte COPY,
the exact four-MiB upper bound, and no alias at the next address. The separate
runtime channel-0 destination `0x00000000` remains open: its control/record
producer must still be observed before it can be changed. A GUI milestone
cannot skip this gate: COPY completion is upstream of readout, scanout, and
the first frame.
