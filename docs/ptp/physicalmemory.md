# Physical memory and NVRAM capture bounds

The archived Nikon Z 7II USB captures report firmware `V1.60`; the F003
selector 4 string is `0160b1`. They supply comparison bytes and transport
observations, not proof of compatibility with the emulator's C1.70 image,
initial CPU state or a complete factory calibration input. The USB identity is
`0x04b0:0x044b`. Addresses and file offsets below are hexadecimal; stated
byte counts are decimal unless formatted as hexadecimal offsets.

The [capture manifest](/evidence/physical/ram_dumps/manifest.json) preserves
historical filenames, request selectors, returned lengths and recorded hashes.
Names such as `resource_80_master` are collector annotations, not identities
encoded in the returned payloads. The original collector did not retain or
validate response codes in this manifest. Matching a requested length alone
does not establish a complete resource or successful semantic decoding.

## Memory request and address domain

The collector sends service subcommand `F032` through operation `94fd`, with
an address and requested byte count. It requests `04000000` for 1,048,576 bytes
and `b188b000` for 8,192 bytes. Both files have those lengths. The first
manifest entry records 0.060 host seconds, or 16.75 MiB/s for that transfer;
this is one recorded measurement, not a sustained throughput guarantee.

Source notes identify MAS VA `119e405c`, validator `104b010a` and executor
`104b020c`. The described executor forwards the request address to a USB
streamer. That does not establish physical-address access, an MMU translation
or equivalence between V1.60 addresses and the target C1.70 MAS VAs. Addresses
below therefore identify service-request addresses and file-relative offsets;
the manifest's historical `base_address` field does not prove a board PA.
The partial validator analysis also does not prove that every DDR address
outside the listed exclusions can be read.

## Boot-range bytes

[`dram_boot_04000000.bin`](/evidence/physical/ram_dumps/dram_boot_04000000.bin)
contains ARM instruction patterns and a stack literal pool. At file offset 0,
the PC-relative load reads the little-endian word at offset `bc`, which is
`00000020`, not `040000c4`. Executing it with an instruction address of
`04000000` would load PC=`00000020`. A link/execution mapping or alias is needed
before assigning an effective boot target. Offset `c4` begins stack literals,
not the stack-initialization instructions.

The preceding mode-selection and SP-load instructions associate these
little-endian literals with processor modes:

| Literal file offset | Mode | SP literal |
| --- | --- | --- |
| `c4` | UND | `04008080` |
| `c8` | ABT | `04008100` |
| `cc` | FIQ | `04008380` |
| `d0` | IRQ | `04008300` |
| `d4` | SYS | `04008400` |
| `d8` | SVC | `04008600` |

These correct the earlier reset-target and stack-label interpretation. Saved
code bytes are not a live CPU register snapshot and do not establish a
Cortex-A9 identity, reset entry, core count, MMU state or valid QEMU loader
placement. Mapping this one-megabyte live-memory capture as boot firmware has
not been validated. The existing [boot input contracts](../startup/bootshim.md)
and unchanged-firmware execution remain separate evidence.

## Runtime-range bytes

[`dram_mas_globals_b188b000.bin`](/evidence/physical/ram_dumps/dram_mas_globals_b188b000.bin)
contains the following values at the requested address plus the corresponding
file offset. Words are decoded little-endian.

| Request address | File offset | Observed value | Previous proposed binding |
| --- | --- | --- | --- |
| `b188b768` | `768` | byte `00` | PTPCMD task-state byte |
| `b188bc6c` | `c6c` | word `00000001` | PTP session identifier |
| `b188bc7c` | `c7c` | word `000001ff` | Service predicate pointer |
| `b188bc80` | `c80` | word `00000003` | Service-mode flag |

The earlier table reported `01`, `00000001`, `104798d9` and `00000001`.
Only the session-value comparison agrees. The V1.60-to-C1.70 field binding and
address translation need independent validation before assigning these bytes
those source meanings. This capture does not guarantee binary parity for
C1.70 runtime assertions or prove an unlocked state at a particular cell.

## Requested NVRAM extents

`F029` requests offset 0, a byte count and selectors 0..6. The collector assigns
resource numbers in filenames; the archived response metadata does not verify
that mapping or the complete block extent. All file lengths match the listed
requests. Sizes below are bytes.

| Historical filename | Request | Captured bytes | Direct content observation |
| --- | --- | --- | --- |
| `nvram_resource_80_master.bin` | `F029`, selector 0 | 65,536 | Identical to selector 3 capture |
| `nvram_resource_71.bin` | `F029`, selector 1 | 65,536 | Structured nonzero bytes; field meanings unresolved |
| `nvram_resource_70.bin` | `F029`, selector 2 | 32,768 | Structured nonzero bytes; timing-table identity unresolved |
| `nvram_resource_79.bin` | `F029`, selector 3 | 65,536 | Identical to selector 0; mirror semantics unproved |
| `nvram_resource_11.bin` | `F029`, selector 4 | 65,536 | Nonzero bytes; gain/ADC interpretation unproved |
| `nvram_resource_12.bin` | `F029`, selector 5 | 65,536 | Contains erased and zero regions; metering interpretation unproved |
| `nvram_resource_67_afadjust.bin` | `F029`, selector 6 | 65,536 | Starts with ASCII `AFadjustkohatsu` |
| `nvram_block_17_audio.bin` | `F021` | 32,768 | Starts with ASCII `AUDIO`; codec/gain fields unbound |
| `nvram_block_53_serial_otp.bin` | `F025` | 4,096 | Starts with ASCII `8027437`; OTP identity unproved |
| `nvram_block_08.bin` | `F02D` | 4,096 | Mostly zero; security-boundary interpretation unproved |
| `nvram_block_09.bin` | `F03D` | 32,768 | Mostly erased; battery-table interpretation unproved |
| `nvram_block_10.bin` | `F03A` | 32,768 | Entirely `ff`; no thermal table established |

Identical selector 0/3 bytes are a reproducible comparison, not proof of two
independent physical stores or mirror behavior. Signatures support the literal
text observations, not complete subsystem schemas or calibrated algorithms.

The [descriptor32 loader](../inputs/nor/module.md#slot-validation) accepts a
complete `180000`-byte NOR slot and uses segmented resource80 COPY/fill ranges
through `16e000`, with a separate end marker. The selector 0 capture is only
`10000` bytes and cannot be supplied directly through
[`descriptor32-input`](../inputs/nor/descriptor.md#descriptor32-module-bank-input).
It does not replace the declared cold-neutral resource80 profile, prove sensor
geometry or demonstrate that initialization failures will disappear. Padding
it or adding a marker would not recover the missing factory data.

## Version and capture metadata

The [F003 capture](/evidence/physical/ram_dumps/subsystem_versions_f003.json)
records selectors 1/2/3=`Z 7_2`, 4=`0160b1`, 5=`012000`, 6=`003`, 8=`------`
and 9=`010100`. Subsystem names attached by the collector remain annotations;
these strings do not independently identify processor or hardware revisions.

The historical SDRAM capture report records `90c0` returning `2001`, followed
by 706 object handles. It reports handles `09198e10` and `29198e10`, format
codes `3000/3801` and lengths 21,538,310/9,006,794 bytes. The full objects and
causal before/after listing are not included here. The archived
[160×120 thumbnail](/evidence/physical/sdram_captured_thumb.jpg) demonstrates
a decoded image; it does not by itself prove every sensor/ISP stage or that
media were untouched.

## Usable comparisons and unresolved bindings

These files can support byte comparisons, signature checks and investigation
of a matching source record. F025 now has a concrete exception to the unresolved
resource labels: its`ACCELE` record at offset`100`, length`c0`, matches the C1.70
resource57 manifest and consumed fields. The
[native import and explicit unit retarget](../inputs/nor/descriptor.md#captured-accele-import-and-explicit-units)
preserve the original type3 and corrections; both resulting records are verified
through the original loader. The remaining4096-byte capture is not thereby a
complete descriptor18 input or verified OTP resource53. Physical sensor range
and strap remain distinct from the declared virtual unit mapping.

The property and preview observations are qualified in the
[physical PTP baseline](physical.md). Other records still need their resource
selector, complete extent and consumer binding checked through the existing
input contract; the version difference alone is not a reason to discard them.

No supplied capture identifies the target C1.70 C-matrix configuration record,
its adjacent controls or their numerical effect. The boot-range file and PTP
globals are not substitutes for that record. A matched record base, complete
payload and source consumer are needed before interpreting coefficients or
injecting a calibration resource.
