# Descriptor32 and resource80

Descriptor32 supplies raw external module data through the shared NOR slot
`[13e80000,14000000)`. Its original loader performs segmented COPY and fill;
the inspected path has no decompression, decryption or variable-length decoder.
The complete semantic resource80 body has not been recovered. A successful
outer marker check alone does not establish usable Sensor configuration.

A separate [explicit cold-neutral profile](virtual.md) now defines the
inspected mode11/subtype3 readsets as finite synthetic input. Its original
loader comparison, guard return0 and ID68-free checkpoint qualify that bounded
path, without claiming recovered factory semantics for the complete body.

All firmware code/data addresses below are C1.70 MAS VAs. The explicit NOR
property uses the existing physical mapping; resource80's observed RAM base
is MAS VA `b7d00000`. Source projections and parent execution receipts are in
`run moduleinput/`.

## Slot validation

The11-word descriptor32 at ROM VA `123f8ad8` has primary `13e80000`, no
alternative, physical span `180000`, max length `16e000`, and word4=1.
The last value does not select a compressed resource format. Resource80's
directory row at VA `123f93c4` names descriptor32; descriptor31/resource79
shares the same physical bytes with a different directory contract.

`103a4b0e` calls `103c048a(80,118d0850,8,phase)`. The loader passes
descriptor words5 onward as validation flags. Word8=1 enables the4-byte
ENDM comparison in `102a874e`; type0 through `102a7d56` selects
`primary + physical span - 4`, hence `13fffffc`, not `13fedffc`.
Word7=0 skips the32-word checksum branch in `102a80a8`; word9=0 skips
sequence reading in `102a807e`. Other descriptors' end-8 sequence rules
must not be imported into this contract.

The original erased-input trace records expected bytes `454e444d` versus
`ffffffff`, followed by validation0 and an exit before copy/publication.
The new [explicit input property](descriptor.md#descriptor32-module-bank-input)
installs the full physical slot without preloading resource80 RAM or posting
resource completion.

## Segmented load

`102a87fc/102a8800` supply the8-row manifest at `118d0850`. Each16-byte row
has offset at+0, length at+4, operation byte at+8, phase byte at+9, resource
tag at+10 and COPY selector at+12. Operation0 copies;1 fills00; other
nonzero operations fillFF. This loader selects rows by phase. All numerical
values in the following table are hexadecimal.

| Offset | Length | Operation | Phase |
|---|---|---|---|
| 0 | be000 | COPY | 8 |
| be000 | 6000 | COPY | 8 |
| c4000 | 99c0 | COPY | 4 |
| cd9c0 | 1f60 | COPY | 8 |
| cf920 | 9d950 | COPY | 8 |
| 16d270 | d8c | fill00 | 9 |
| 16dffc | 4 | COPY | 8 |
| 16e000 | 11ffc | fillFF | 9 |

COPY source is NOR primary+offset, destination is resource80 RAM+offset,
and length is unchanged. The six COPY rows use selector1. Non-phase9
submission obtains a channel through `10368b76(2)` and calls `103a3b66`;
the generic phase9 COPY path calls `103a3b14`. Both prepare the same COPY
configuration through `103a3a46`, with length-1, source and destination,
then use `10294c5c` and the original completion wait. Other COPY selectors
use `10360fac` memcpy. No encoded-to-decoded size transformation occurs.
This loader does not check every COPY wrapper result, so RAM comparison is
stronger evidence than marker acceptance or callback alone.

The four bytes at16dffc are an ordinary COPY row, not the slot footer.
Phase9 fill extends RAM up to relative17fffc; the directory span16e000
cannot by itself establish the total reserved RAM extent.

## Parent loader verification

The `structural` run supplies a synthetic full-size slot: zero body,
independent nonzero witnesses at the end of each COPY range, and ENDM at
17fffc. Version bytes atbe014/15 deliberately remain0. It uses unchanged
firmware, the existing other inputs and normal UART peer for5 host seconds;
the debugger reads RAM only after that bounded continuation.

Every observed resource80 load phase3/8/4/5/6 returns validation1. All six
COPY ranges compare byte-for-byte with the supplied file, including their
witnesses, in `structural-comparison.json`; `structural-module80.bin` is
the read-only destination dump. No phase9 request occurs in this trace;
the fill laws above are source evidence, not an executed phase9 receipt.
The expected marker is not copied into the end of resource80 RAM.

The actual version call still returns400hex and property6433 contains ID68.
Thus the input/device/loader path is verified, while normal-menu progress
is unchanged. This fixture is not a normal module, calibration record or
recovered factory input, and its witnesses are not emulator defaults.

## Body recovery and remaining consumer contract

`10305dac` requires the packed version atbe014/15 to have byte0=15hex;
that condition alone says nothing about the rest of the body. `102fe1ea`
selects19 groups of program-data/length tables using a ROM variant map.
For example, groups0/1/2 use data offsetsbf000/bf280/bf3b0, variant strides
128/24/544 decimal, and length tables atbf180/bf2b0/c13b0. `1030621e`
walks zero-terminated u16 lengths and passes each byte segment to `10367fe0`,
which sends at most192 bytes per transport submission. The sender supplies
no universal packet marker, so a presumed81 prefix is not an identification
rule for the entire module.

The separate `coldschema` structural-input trace samples only the entries of
the69 identified resource80-base consumers. It observes script groups0,1,2,3,4
in that order at `102fe1ea`, with parameter object `b18a769c`; it does not
exercise all19 resource table groups. `1030613c` reads header enable byte+35
and conditional u16 values+36/+38/+40. `10304e3c`, also actually called,
consumes the mode/subtype timing rows at `be0c2/be0c6/be0cc + 64*mode +
16*subtype`; some branches divide by the u16 atbe0c2. It also reads header
byte+32 as a wrap bound. These are partial field contracts, not a recovered
common header or a proof that every conditional branch executes. They narrow
the next virtual-profile work to real cold consumers and demonstrate why
version acceptance cannot qualify an otherwise empty timing/program body.

The later `sensorprofile` traces resolve the actual cold selectors:
object `b18a769c` has mode11 and subtype3; `102fef74(11)` returns7.
Group0 selects variant1 (`bf080`), group1 variant0 (`bf280`), and group2
variant8 (`c04b0`); the latter still uses the shared length table `c13b0`.
Group3 consumes `c04b7 & e0`, while groups3/4 construct packets from the
object. These are observed cold consumers, not mode0 defaults.

Independent selector `10300564` installs the64-byte record17 at`4e40`.
`103009a8` subsequently selects128-byte capture entries through a ROM table;
the actual `10300660` inputs are offsets`64c0` (j41) and`6540` (j42).
That consumer reads at most byte125. Its initial a4=1 call installs fields
at entry82/84/86; the later a4=0 call preserves those packet fields. These
capture entries must not be confused with the separate timer-record source
passed to`102ffc5a`, whose three13-byte substructures at+32/+80/+128
require at least141 readable bytes. Both diagnostic traces observe its R0
as`b7d02c00`, closing that separate source at resource80 offset`2c00`.

The actual timing row is`be2b0`. In the `timingdiagnostic` run its+2 halfword
is0; helper`10304b4e` returns499, and the original case7 arithmetic divides
by that zero field and produces0 without a CPU trap. The subsequent zero
counts are encoded minus1 asffff/ffffffff. Thus a virtual profile needs a
nonzero divisor, positive derived counts and verification of the final encoded
values; clearing the version guard cannot establish those constraints.

An independent nonzero-timing hypothesis, `endpointscounts`, supplies
row+2=4096, row+6=35190, row+11=1 and row+12=72. Original object fields
become148=35190,156=4096,160=1000; the separate timer-packet readback is
4095/35189 after minus1 encoding. It reaches the same next parameter bank
as the zero-table diagnostic. This validates a bounded input/arithmetic
relation, not the remaining capture/program/calibration table semantics.

The subsequent named profile also covers the observed selector4 timer record
at`3230` and the extra script/length list at`cda20/cdae0`. Selector3 and4 use
different144-byte record arrays and have no universal temporal order. In
`neutral`, selector4 produces q130/count35319 first, then selector3 replaces
them with q0/count35189. The independent calibration pointer is non-NULL at
`50300000`; its selected208-byte block at`5054bf20` is zero in the existing
declared resource11 storage. The descriptor32 generator does not supply or
rewrite that dependency. This new receipt supersedes the earlier assumption
that only a single timer record or a NULL-clearing path needed consideration.

The explicit `15/00` version-declaration diagnostic passes the guard and
removes ID68 at that checkpoint. With the
[capture packet model](../../devices/sensor/packet.md), unchanged execution
continues to the next uncovered PA`2006a414`. It is debug/stopped before a
display console exists. This experiment isolates the guard's effect; it does
not qualify its otherwise incomplete body as a normal virtual-camera input.

The local inventory covers Z7II and Z6II C1.70 five-component bundles and
the Z7 C3.80 four-component bundle, with component CRC checks and direct
payload comparison. A version/length-table shape search produces false
matches in ordinary property descriptors and callback/integer tables.
No recovered factory body has qualified as a usable input. Unresolved weak matches
remain unknown; this is not proof that every ROM encoding or layout lacks
the data.

The fixed [Z7 assisted launcher](https://github.com/WonderLantern/z7-qemu/blob/97abf0b81cd3211f8374a38987a0beaf4549b90f/tools/interactive.py)
writes two version bytes directly to guest RAM and explicitly records an
incomplete diagnostic Sensor configuration. It supplies no full body to
reuse here. The inspected [Z8 capture preparation](https://github.com/RdWing/z8-qemu/blob/9881d71053dacadbb81d2b352533ccf6a52cd604/tools/capture.py)
similarly marks its selector inputs as experimental and unrecovered record
coefficients; it does not establish a compatible resource80 input.

The cold-neutral profile closes the selected lengths, control fields and
timing arithmetic for the inspected cold path. Its zero-length scripts are
explicit source-supported no-ops; its other zero parameters remain named
functional assumptions. Consumers outside that path, physical metadata and
later modes remain unqualified. New paths require their own source and
runtime checks. A real camera is not established as a necessary next dependency.
