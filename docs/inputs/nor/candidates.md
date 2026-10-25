# ID34 structural candidates

The read-only scan found **296 structural windows**, not 296 ID34
records. No candidate is authenticated or approved as an external input. The
screen deliberately retains overlapping windows, degenerate one-cell layouts
and repeated coordinates; these weak matches require loader/xref evidence.

## Method and retained evidence

The stdlib-only scanner and full results are ignored artifacts:
`run id34candidates/scan.py` and `result.json`. Reproduce from
the repository root with `rtk python3 run id34candidates/scan.py`.
The JSON records exact absolute input paths, lengths, parameters, every
dimension match, its first coordinate exclusion reason, and each survivor's
active rectangles. Inputs are the original payloads identified by
[the source inventory](id34.md), not the partial seed or zero/one hypotheses.

Each candidate begins on a **four-byte payload-file boundary** and must leave
a complete `0xc00`-byte envelope inside the file. Little-endian unsigned words
at `+1c,+20,+24,+28` must respectively be in `1..30,1..20,1..8,1..8`.
For every active aggregation cell `(x,y)`, the scanner reads index `8*y+x`
from the four `0x100`-byte arrays at `+2c,+12c,+22c,+32c` and requires
`x0 <= x1 < raw_width` and `y0 <= y1 < raw_height`.
Inactive array cells and `+42c` weights are unconstrained. These are screening
assumptions derived from Z7II consumers; they are not established Z7 C3.80
schema compatibility, which remains uncertain because its registry span differs.

## Results

| Original payload | Bytes | Aligned starts | Dimension matches | Coordinate survivors | Survivors with any nonzero active coordinate |
|---|---:|---:|---:|---:|---:|
| Z7II C1.70 MAS | 38,239,096 | 9,559,007 | 10,548 | 86 | 28 |
| Z7II C1.70 SLA | 8,834,368 | 2,207,825 | 2,797 | 23 | 16 |
| Z6II C1.70 MAS | 38,750,136 | 9,686,767 | 10,564 | 89 | 33 |
| Z6II C1.70 SLA | 8,832,192 | 2,207,281 | 2,796 | 27 | 22 |
| Z7 C3.80 MAS | 33,052,088 | 8,262,255 | 9,857 | 71 | 23 |

The supplied Z7 package has no separately identified SLA component, so no Z7
SLA was scanned. Of 36,562 dimension matches, 36,266 fail an active rectangle's
ordering or coordinate bounds. The result preserves that first failing cell
and its four values rather than silently discarding it. There are no survivors
whose raw dimensions are exactly 30 by 20, but those maxima are not required
values and their absence excludes nothing further.

These nonzero-coordinate survivors with comparatively large active grids are
compact starting points for the parent's xref investigation. Offsets are
**payload file offsets**, not container offsets; the complete survivor list is
in `result.json` and these examples are not a ranking of authenticity.

| Payload | Example offsets | Raw width × height | Aggregation width × height |
|---|---|---|---|
| Z7II MAS | `19e5c38`, `19e6284` | 4 × 6 | 7 × 8 |
| Z6II MAS | `19f6578`, `19f6bc4` | 4 × 6 | 7 × 8 |
| Z7 MAS | `1545020`, `154566c` | 3 × 5 | 6 × 7 |
| Z7II SLA | `208230` | 3 × 4 | 1 × 3 |
| Z6II SLA | `207e94` | 3 × 4 | 3 × 4 |

The parent's follow-up byte/xref review rejects adopting the two Z7II MAS
`19e5c38/19e6284` examples as ID34. Their first three header words are
`0,0,111e/111f`, the apparent dimensions are the enumeration-like sequence
`4,6,7,8`, and a following `111f/1120` item appears near `+654`, with an
apparent `64c` repeat stride. The first three apparent coordinate arrays and
both coefficient arrays are nearly zero, while the fourth starts with `0..6`.
The parent reports no direct IDA xref at MAS VA `11a05c38/11a06284`.
This combination suggests repeated metadata rather than an ID34 object; it is
a concrete adverse disposition, not proof of the metadata's actual type.
The structural scan still retains these windows in its original result.

## Coordinate and coefficient distribution

`distribution.py` reads the existing survivors without introducing another
validity filter; `distribution.json` retains per-candidate counts, active
coefficient value frequencies and the five leaders by active and full-table
nonzero counts. Coordinates below count all four words of every active
rectangle, including overlapping candidate windows; they do not count unique
physical records.

| Payload | Zero coordinate words | Nonzero coordinate words | Nonzero values observed |
|---|---:|---:|---|
| Z7II MAS | 2,016 | 152 | 1,2,3,4,5,9 |
| Z7II SLA | 488 | 116 | 1,2,3 |
| Z6II MAS | 1,854 | 258 | 1,2,3,4,5,9 |
| Z6II SLA | 328 | 224 | 1,2 |
| Z7 MAS | 1,486 | 126 | 1,2,3,4,6,7,8,9 |

Examples with the most nonzero **active** coefficients follow. A pair `a/b`
means nonzero active entries in the `+42c` and `+52c` arrays; each full table
has 64 entries. Nonzero counts are investigative descriptions, not evidence
of valid weighting semantics or preferred original data.

| Payload and offset | Dimensions raw; aggregation | Active nonzero coefficients | Full-table nonzero coefficients | Observation |
|---|---|---|---|---|
| Z7II MAS `244fecc` / SLA `857ad0` | 2×3; 4×5 | 9/0 | 13/0 | All active coordinates zero; coefficient values include `fc,fd6,11c` and small integers. |
| Z7II MAS `199a990,199acf0` / SLA `2082ac,20860c` | 4×3; 4×1 | 4/4 | 64/64 | All active coefficient values are 1; every active coordinate word is nonzero. |
| Z6II MAS `19ab2e0` / SLA `207e94` | 3×4; 3×4 | 12/12 | 64/64 | First active table all 1; second has ten 1s and two 3s. All active coordinate words nonzero. |
| Z6II MAS `19ab920` / SLA `2084d4` | 2×3; 2×3 | 6/6 | 64/64 | Both active coefficient tables all 1; all active coordinate words nonzero. |
| Z7 MAS `1f24894` | 2×3; 4×5 | 9/0 | 13/0 | Same active coefficient histogram as Z7II `244fecc`; all active coordinates zero. |
| Z7 MAS `150ab00,150ad40` | 4×3; 4×1 | 4/4 | 64/64 | Both active coefficient tables all 1; all active coordinate words nonzero. |

Full-table nonzero counts reach 64/64 at many small active-grid candidates,
including Z7II MAS `1960774,19607f8,1960850` and Z6II MAS
`19710e8,197116c,19711c4`, each with only one active coefficient per table.
Therefore sorting by the entire table alone can favor unrelated dense data.
The mirrored MAS/SLA patterns above are observations to trace, not proof of a
shared resource record. No candidate has acquired authenticated ID34 status.

Many passing headers are small integer sequences such as `2,3,4,5`, occur at
overlapping starts, or have all-zero active coordinates. Consequently geometry
alone is a weak discriminator. No marker, record boundary, ID34 registry
reference, loader selection, scalar semantics, safe downstream arithmetic,
factory provenance or full-body compatibility has been established for any
survivor. In particular, the 83 startup scalar assignments and unconditional
`+774` parameter value were not validated by this scan.

The parent's subsequent containing-object review identifies the dense all-one
Z7II windows `199a990/199acf0` inside the table addressed by
`102cec74(a,b) = 119ba598 + 288*a + 32*b` (MAS VA). Its caller `1035d68a`
multiplies the first four words of an eight-word tuple by the corresponding
last four words. The matching native getter in Z6II is at MAS VA `102cff0e`
and returns base `119cae8c`, containing its `19ab2e0/19ab920` file-offset
examples. These windows span multiple tuples and are not established ID34
boundaries. The parent retains byte review, pointer searches and getter
comparison in `run id34producer/`; see the
[consumer follow-up](../../runtime/id34.md#re-read-persistence-and-settings-are-distinct-paths).

This result is limited to the five named byte ranges, positive dimensions,
little-endian uint32 coordinate schema, four-byte alignment and complete
`0xc00` envelope. Packed, differently aligned, compressed or alternative-schema
objects can evade it. No firmware was downloaded or modified; no substitute
record, QEMU/GDB execution, IDA change or dependency installation was performed.
