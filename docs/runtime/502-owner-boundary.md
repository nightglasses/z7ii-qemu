# `0x50200000` ownership boundary

This note records the current boundary for the first multi-thread MAS COPY
request that reaches physical address `0x50200000`. It is an evidence record,
not a request to map an unverified RAM window.

## Runtime four-tuple

The unchanged Z7II C1.70 MAS run reached the following request before the
address was rejected by the QEMU uncovered-address owner:

| Field | Value |
| --- | --- |
| COPY channel | `0` |
| source | `0x122acf40` |
| destination | `0x50200000` |
| remaining length | `0x27800` |
| submit callsite | `0x10244966` → `0x103a3b66` |
| descriptor | `0x122d4740` |
| descriptor first word | `0x27800` |
| secure TTBR0 | `0xb2b74000` |
| L1 entry address | `0xb2b75408` |
| L1 entry | `0x50214422` |
| translation | `VA 0x50200000 → PA 0x50200000` |

The initial table copy also contains identity section entries from
`0x50000000` through `0x51ffffff`. A page-table entry proves the guest's
translation choice; it does not prove a physical RAM, device or DMA owner.

## QEMU owner audit

The current machine and device graph was checked at source level:

| Candidate | Source path | Result |
| --- | --- | --- |
| Storage classes | `hw/nikon/machines/z7ii.c:z7ii_storage` | Six RAM regions cover `0xb1800000..0xbd200000`; none covers `0x50200000`. |
| Selector-zero arena | `hw/nikon/machines/z7ii.c:z7ii_init` | Dedicated RAM is `0xb0d00000..0xb1100000`; no overlap. |
| Selector-one arena | `hw/nikon/machines/z7ii.c:z7ii_init` | Dedicated RAM is `0xb0900000..0xb0d00000`; no overlap. |
| MXIC endpoints | `hw/nikon/devices/mxic.c:nikon_mxic_attach_ram` | Only the six `z7ii_storage` regions are attached. `initial_regions` are decode transactions and allocate no memory. |
| Bridge peer aperture | `hw/nikon/machines/z7ii.c:peer_window` | Alias is `[0x40000000, 0x50000000)`; the exclusive upper bound excludes `0x50200000`. |
| SysBus and other board regions | `hw/nikon/machines/z7ii.c:z7ii_init` | No MMIO or subregion is installed in `0x50000000..0x51ffffff`. |

Therefore no existing source-backed endpoint can be reused for this request.
The MXIC profile's pair beginning at `0x50000000` is a virtual region-decode
transaction used to enable already-attached finite endpoints. It is not a
claim that the complete 32 MiB identity window is installed RAM.

## Required next evidence

Before changing the QEMU address map, the unchanged firmware run must identify
the physical provider for this request. The next receipt must include:

1. The code that creates or enables the `0x50000000..0x51ffffff` backing,
   including the relevant MXIC or board-decode registers and reset state.
2. The COPY master's address-domain/security attributes for this transaction,
   showing that it reaches the same endpoint as the CPU identity mapping.
3. A successful provider-side read/write or a classified hardware fault that
   distinguishes absent, disabled and protected memory.
4. A consumer or completion event after the copy, so the region is tied to a
   real firmware object rather than inferred from the PTE alone.

Until those facts are captured, adding generic RAM, a bridge alias, an MXIC
enable, a host-seeded buffer or a framebuffer at this address would change the
firmware's behavior without a recovered hardware contract.

## Current decision

The address remains an explicit uncovered boundary. No QEMU model change is
justified by the current evidence.
