# Paired saturation curve

The finite model interprets the complete33-knot unsigned16 Q8 gain curve
published through records2/3 of the paired bank's14×68-byte table family. It
scales actual signed MAIN U/V while preserving Y. Source packing, gain
normalization and software publication are established from C1.70 MAS. A Y axis
with128-sample spacing and placement after B/C/BAC are explicit functional
hypotheses. They do not establish the silicon pixel equation or physical-camera
color fidelity. The other table subfamilies retain their strict explicit-profile
checks; this change does not silently admit new opaque tables.

## Source and complete family

Addresses in this section are C1.70 MAS virtual addresses. Bank offsets are
hexadecimal byte offsets from paired-bank PA `0x20380000` or `0x203c0000`.
Structure offsets and counts are decimal. MMIO accesses are aligned
little-endian32-bit words.

`0x102d362c → 0x102fd61a` binds a separate color-calculation state: state+24
points to the14860-byte publication cache, state+36 to previous input/cache
state, and state+40 to the current128-byte color metadata. `0x102d3674` builds
that metadata; it is not the separate50-byte normalized input or the compact
scalar-packet structure. The complete14-table source paths are:

| Records | Cache offsets | Normal producer | Established operation |
| --- | --- | --- | --- |
| 0/1/4..9 | 576/644/712..1052 | `0x102fc372` | Eight33-u16 base curves plus signed delta curves scaled by two input magnitudes/12 |
| 2/3 | 12008/12076 | `0x102fb1da`, called by `0x102fb3cc` | One33-u16 gain result published into alternating cache slots |
| 10/11 | 1120/1188 | `0x102fc1d4 → 0x102fd928` | Six-coefficient dot product with metadata20..31, divided by256, for every knot |
| 12/13 | 1256/1324 | `0x102fdc50` | Copies two33-u16 ROM curves |

The first and third subfamilies' pixel roles remain unrecovered. The last pair's
values resemble Q10 cosine/sine samples across approximately±32 degrees; that
is a mathematical clue, not a verified hardware use. With special metadata8
selected, `0x102fcffc` instead copies a12×33-u16 bundle for all records except
2/3. The model does not treat source zeros in these other families as proof of a
disabled hardware stage.

For records2/3, `0x102fb1da` recomputes when forced or when metadata8, u16+2,
u16+52, byte36,42,48,54 or u16+50 changes. Its calculation combines the selected
33-u16 base curve with an adjustment proportional to the distance of metadata42
from12, or metadata48 from16 for metadata33 values6/7. It then performs a
u16+50/256 blend where applicable and multiplies by a sensitivity-selected
33-u16 curve with another division by256. These literal256 normalizations and
256-centered adjustments support a Q8 gain interpretation.

Metadata42 is copied from normalized input13 by `0x102d3674`.
`0x102f629c` also derives the exported XMP `Saturation` value from that same
normalized input13. This supports a saturation-related role rather than
independently multiplying R, G, B or luma. It does not prove the interpolation
axis, register selector or exact ASIC equation. The wire retains the fullu16
result without a narrower bit mask, so all0..65535 knot values are finite in the
numerical contract. Intermediate multiplication uses signed64-bit arithmetic.

The producer toggles cached byte645 under metadata54, setting publication flag
14791 or14792. VA `0x102bf33c` maps the flagged cache slots into the root
configuration's pointer array at byte108. Its `a2=1` path can alias the same
source into both pointers. The ordinary asynchronous image setup
`0x10264972 → 0x102c0430 → 0x102c0ae2` supplies that value; the synchronous
`0x102c0034 → 0x102c0a5c` path supplies0. This distinction follows the setup
operation, not an established PASM selector.

VA `0x102e49fe` iterates the14 pointers and skips NULL entries. Setter
`0x1036bfae → 0x1036ca74` copies68 bytes per selected record with ascending
word stores. The whole family is `[1980,1d38)`. Record2 is
`[1a08,1a4c)` and record3 is `[1a4c,1a90)`. Each has16 packed low/high-u16
words followed by knot32 in the low half of its17th word; the upper half must
be zero. Thus `1a08=0x00ae006e` represents knots110 and174, not a single
operation code or a width/height pair.

## Finite pixel equation

`NikonPairedPixelConfig.saturation[33]` is a mandatory complete array. For the
clamped12-bit Y from B, let `i=Y/128` and `f=Y%128`:

```
gain = round((knot[i]*(128-f) + knot[i+1]*f)/128)
chroma = round(signed_mixed_chroma * gain / 256)
output = clamp(chroma + 2048, 0, 4095)
```

Both U and V use that same single gain, after signed C/BAC mixing and before
bias/clipping. Y is unchanged. Interpolation uses nearest rounding; signed
products round nearest with ties away from zero. At Y4095, knots31 and32 are
both consumed safely. The existing later post-color passes, UV2×2 averaging
and8-bit quantization follow. RAW, WORK/grid and pre-color temporal history do
not pass through this curve.

Every knot256 is algebraically neutral through the same equation. Every knot0
explicitly removes chroma. An omitted device publication is not an identity
path. A neutral gray whose B chroma is zero stays neutral at any gain. Colored
fixtures test both signs, interpolation boundaries, clipping, C/BAC ordering and
the complete Y/UV output; these tests are falsifiable consequences of the
functional hypothesis.

## Publication, snapshots and limits

Each slot has a separate ordered17-word publication round. Writing its first
word opens a new round even when its value is unchanged. Subsequent words must
arrive consecutively; a missing, duplicate or out-of-order non-header write
cannot borrow coverage from an older round. A fresh header can restart a
malformed round. Reset clears the words, round state and prior-publication flag.

Admission requires one complete slot with the other wholly unwritten, or two
complete slots whose entire33-knot curves are identical. Zero padding is always
required. No curve, a cold partial curve, malformed order/padding or an explicit
admission with two different complete curves rejects. A partial new round after
a previously usable publication holds an existing AwaitInput request and its
unconsumed real RAW input; it does not report an unsupported profile or create
a result. When an upload begins within that existing request, two complete
but different slots also wait: an alias update publishes the first new slot
before its matching second slot. The explicit `10/08` start or `10` kick ends
this different-pair allowance at the next admission. A busy job still completes
from its immutable snapshot before considering another admission. Malformed
order or padding never obtains this allowance.

Uploads are staging data under
[online numerical uploads](pixels.md#online-numerical-uploads). An active job
keeps its validated immutable curve through partial, ambiguous or changed
complete publications. Completion uses that frozen curve and live layout,
RAM, alias and peer fences; the new curve qualifies only future admission.
These MAIN-only changes preserve pre-color temporal history. Stop/reset remain
cancellation boundaries; reset clears the curve publication state.

Completing a usable publication reconsiders real eligible RAW only for an
already existing AwaitInput request. It neither creates the `10/08` start
sequence nor reuses a consumed generation, releases a busy slot, or fabricates
an event, ready bit or IRQ. Initial configuration and the other bank remain
independent.

The source has software double buffering, but a hardware selector for two
different retained curves is not established. The finite contract therefore
cannot admit that state by choosing the latest slot, averaging the curves
or multiplying them together. In particular, an unaliased warm update can leave
two different retained curves even when only one new source pointer was
published. Supporting it needs the selector's normal producer/consumer, not
another single-value exception.

The explicit calibration input must leave `1a08..1a8c` zero because these are
interpreted fields. Records0/1 and4..13, the independent22×64-byte family and
surrounding apertures retain their existing contracts. Native fixtures supply
a real unity curve through MMIO, not through a calibration preload.

## Scoped validation

Pure fixtures in `tests/unit/nikon/pairedpixels.c` independently compose signed
B/C/BAC,33-knot interpolation, the chroma product and subsequent post passes.
They cover every knot, Y0/127/128/129/2047/4095, positive/negative chroma,
zero/unity/high gains, gray preservation, all CFA encodings and unchanged
pre-color outputs.

Native fixtures in `tests/qtest/nikon/cmos.c` cover actual colored P6
RAW→MAIN transport with a complete Y/UV oracle, both single slots and identical
dual slots, fullu16 gains, initial missing/partial coverage, a missing word in
every new round position, padding, different dual curves, out-of-order writes,
equal/changed complete and partial uploads, frozen completion, reset, history
preservation, alias updates straddling a real RAW arrival and explicit
different-pair rejection. Natural AwaitInput retry does not add an implicit
start or RAW replay. Fixture execution and unchanged-firmware progress belong
to separate integration receipts; this document does not claim either has passed.
