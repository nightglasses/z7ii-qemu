# Explicit cold-neutral resource80 input

`nikon-module` generates the single named `cold-neutral-resource80` functional
profile. This is an explicit synthetic external NOR file for the original
loader, not a recovered factory module bank, calibration image, guest RAM
patch or guarantee of a complete boot. It targets only the currently inspected
cold mode11/subtype3 path. Other modes and consumers remain unvalidated.

```sh
work/build/qemu/tools/nikon/nikon-module cold-neutral-resource80 work/descriptor32-cold-neutral-resource80.bin
```

Supply that file explicitly as
`-machine z7ii-mas-diagnostic,descriptor32-input=work/descriptor32-cold-neutral-resource80.bin`
alongside the other declared inputs. The existing [descriptor32 input](descriptor.md)
installs it once into the shared NOR span at MAS PA
`[0x13e80000,0x14000000)`. The original firmware still selects, validates,
copies and consumes it. No guest pointers, readiness, events, IRQs or loaded
RAM are supplied by the tool. Normal NOR changes persist over reset without
replaying the file.

## Complete declaration

The builder returns `GBytes` containing exactly `0x180000` bytes. Every byte
is explicitly zero except the following declarations; the file offsets below
are hexadecimal:

| File offset | Declaration | Interpretation and evidence boundary |
| --- | --- | --- |
| `be014`, `be015` | Bytes `15`, `00` | Selected version bytes; no factory-version identity claim |
| `be2b0 + 2` | Little-endian u16 `4096` | Mode11/subtype3 row's nonzero divisor parameter |
| `be2b0 + 6` | Little-endian u16 `35190` | Fallback base count, used when the original helper returns zero |
| `be2b0 + b` | u8 `1` | Source timing metadata; physical units are not recovered |
| `be2b0 + c` | Little-endian u16 `72` | Input to the observed overhead quantization formula |
| `17fffc` | ASCII `ENDM` | Outer input footer; does not validate the zero-valued records |

The source field layouts and readsets below are established by the original
consumers. Choosing these numeric parameters and zero values for the remaining
body is a **finite functional hypothesis**. Zero is not an implicit claim of
identity calibration, a disabled physical block or valid factory metadata.

## Selected zero-valued readsets

- Header bytes `20` and `23` are zero; the inspected optional branches are not
  selected.
- Record17 at `4e40` is 64 zero bytes. Its inspected byte/halfword fields are
  declared neutral parameters. Halfword `+32` has nine low software selection
  bits, all clear; no physical disable semantics are asserted.
- Capture entries41 and42 at `64c0` and `6540` are each 128 zero bytes. Their
  first fourteen u16 fields are expanded to u32 and shifted13 through0 into
  the control write at PA `20068500`. Zero therefore clears those recovered
  control fields. This does not mean the entire capture block is disabled;
  other driver state and preserve semantics remain guest-owned.
- The timer records at `2c00` and `3230` are each zero through at least
  byte140, covering the inspected 13-byte blocks starting at decimal32,80
  and128. They are separate from the 128-byte capture entries and remain
  opaque neutral metadata. Actual initial/update calls select different
  records; they must not be treated as one shared record.
- Script0 variant1 has values at `bf080` and first length u16 at `bf180`;
  script1 variant0 uses `bf280` / `bf2b0`; script2 variant8 uses `c04b0` /
  `c13b0`. Each first length is zero. Consumer `1030621e` accepts length0 as
  a no-op returning0 without a serial transmission. This proves the finite
  script behavior, not a successful physical configuration transaction.
- Group3 independently reads byte `c04b7 & e0`; those bits are explicitly
  zero as well. A zero script length does not skip that separate read.
- The additional observed script at `cda20` uses length table `cdae0`.
  Its first length is also explicitly zero and takes the same no-op path.

The independent pointer at MAS VA`b18a77a8` is not changed. In the original
run it is`50300000`, the existing resource11 storage base, and its selected
208-byte block at`5054bf20` is all zero. The firmware takes the non-NULL
data branch, not the optional NULL clearing branch. This profile neither
supplies a factory fallback nor rewrites that separate input dependency.

The source receipts remain in
`work/runs/sensorprofile-20261024/` and
`work/runs/capturebanks-20261024/`. Original code and data are not embedded
in the tool or committed fixtures.

## Timing arithmetic and limits

The unit fixture independently evaluates the inspected integer expressions
from the generated row; the tool itself generates bytes and implements no
firmware algorithm. With `x = 4096`, `103066be` gives
`(72000*x + 71999)/72000 = 4096`. The inspected non-fallback path obtains
`(144144000/4096) & ~1 = 35190`, equal to the declared fallback. Overhead72
gives `10*((1000*72/72 + 5)/10) = 1000`. These fixed inputs keep the
intermediate32-bit arithmetic in range and the divisor positive.

Selector3 calls `102ffc5a` and uses period load `4096-1 = 4095` and
count load `35190-1 = 35189`. Selector4 calls the distinct conditional path
`102ffb14`. For the example guest duration7383 and correction0,
`q = ceil(72*7383/4096) = 130`; its count load would be35319, with derived
values `q+2 = 132`, `q+3 = 133` and metadata `1+q = 131`. Duration, flags
and correction belong to guest state. This arithmetic example does not claim
that q is always130 or that the selectors have a fixed temporal order.
The original call sites must establish their actual order and values.

## Native tool validation

The tool and `nikon_module_unit` use the existing native Meson graph and
`qemuutil`/GLib dependencies. After the repository's normal configure/build,
select the fixture with `make check MTESTARGS=nikon_module_unit`.

The fixture checks full output size and footer, version bytes, the selected
zero readsets and capture control fields, the four observed no-op script lists,
group3 bits, fixed timing declarations and independent source arithmetic.
CLI checks exercise generation with a spaced path, unknown profile,
missing/extra arguments, empty output and output I/O failure. There is no
generic decoder, arbitrary profile validator, hash gate or parser introduced
only to create negative tests: the builder takes no external parameters.

These are synthetic input/tool checks. Original mode11 startup behavior,
later consumers, GUI readiness and complete boot require the parent's actual
unchanged-firmware execution receipts. No such progress is asserted by the
builder or these unit fixtures alone.

## Original-loader and execution receipt

The parent native `nikon_module_unit` batch passes all4 cases. The `neutral`
run supplies its generated file with the preceding other declared inputs and
unchanged firmware. All six original COPY ranges compare byte-for-byte with
the input. Original `10305dac` returns0 at caller`102d7254`, with packed
value150hex; no ID68 is produced and property6433 is empty at the checkpoint.

Original calls select timer records`3230` and`2c00`: selector4 computes
`2ed0 + 90*6 = 3230`, while selector3 computes`28a0 + 90*6 = 2c00`
(offsets hexadecimal). In this run selector4 executes first, with q130 and
count35319, then selector3 overwrites them with q0/count35189. The final
read-only snapshot alone would not establish that both paths ran.

The original bank1 writer continues through PA`2006be00`. The next uncovered
transaction is CPU0 PC`10208ea4`, WRITE32 PA`2006b800=8`; QMP reports
debug/stopped, with no console yet. This validates the declared cold-path
input through that frontier, not normal-menu output, later mode coverage,
physical Sensor calibration or complete boot. Raw receipts, COPY comparison,
guard result and timer traces remain in `work/runs/capturebanks-20261024/`.

The later [capture-group model](../../devices/capturegroup/protocol.md) crosses that
configuration write. With the same generated input, the unchanged
`capturegroup-20261024/green` run again compares all six COPY ranges and
records guard0/property6433 empty. Its first uncovered access is CPU0
PC`10208e14`, READ32 PA`2006bb70`, in the old-result snapshot preceding
unit20 commit/run. The input profile does not declare those hardware results.
QMP remains debug/stopped before a console exists; the scope above is unchanged.

In `captureresults-20261024/green`, the same input additionally crosses the
initial result read under the separate device's explicit cold-empty assumption.
All eight result words become unknown at group commit/effective run; the file
does not supply them. Original code reaches mask10 commits and unit20 run,
then first rejects CPU0 PC`102090bc`, WRITE32 PA`20069514=6`. Six COPY ranges
still compare and the guard/property check is unchanged. There is no console
or qualified post-submit result at this checkpoint.

The later [paired-parameter model](../../devices/captureparams/protocol.md)
crosses that write without altering the input. In `captureparams-20261024/green`,
the six COPY ranges and guard/property results remain unchanged, the128-word
table compares with the original ROM source, and unit200 commit/run executes.
The first uncovered access is now CPU1 PC`10404398`, WRITE32 PA`c33ab400=100`,
in a software interpolation-record buffer whose storage ownership remains
unrecovered. There is still no console or qualified post-submit result.
