# Camera image input profile

This profile carries one explicit still image into the modelled CMOS transport.
It is an **external input**, not recovered sensor data, calibration, or a claim
about the physical sensor.

## Input

| Item | Value |
| --- | --- |
| Machine property | `cmos-image=<absolute path>` |
| Format | binary PGM (`P5`) or PPM (`P6`), 8-bit, `maxval 255` |
| Size | any nonempty input geometry; the guest's descriptor supplies transport geometry |
| Identity | `NIKON_CMOS_FORMAT_FUNCTIONAL_C170` (`0x43313730`, the ASCII tag `C170`) |

The file is read at the first transport preparation and retained until the
`image` property is reassigned, including reassignment to the same path.
Changes to the host file alone do not change the retained scene. Device reset
clears transport state and preserves this configured external input. The device
refuses malformed or truncated binary8-bit PGM/PPM, and refuses source changes
during exposure/readout. An image path binds the source without requiring
`fixture-provider`; an invalid image never falls back to random fixture bytes.
The complete-bundle machine chooses the explicit image when supplied and uses
its existing deterministic fixture source only when no image was selected.

The parser consumes one raster separator, treating CRLF as one separator.
Whitespace and `#` inside the raster are pixel bytes. Header comments are
supported before tokens; a comment in place of the final separator rejects.

## What the model does with it

Tracking in the tree:

```text
z7ii.c        `cmos-image` property, cmos = TYPE_NIKON_CMOS,
              readout-control at 0x20069800 / 0x2006b000
readoutpipeline.c  readout-request -> nikon_readout_request() +
              nikon_readout_c170_mapping() -> nikon_cmos_submit()
cmos.c        samples the image into the guest's destination buffer
```

The guest supplies the descriptor and destination. The finite C1.70 mapping
derives geometry from stride/span and supplies explicit two-byte encoding and
timing assumptions. It samples the input through normal DMA and publishes
completion only after the transfer; it does not preset guest software state.

**Functional scene/encoding choices:** for each output sample, first map through
the guest's crop/scale into configured sensor coordinates `(sx,sy)`. Sample the
scene at `(sx * image_width / sensor_width, sy * image_height / sensor_height)`
with integer division. Thus an image need not have the dimensions inferred
from the readout stride. The mapping retains the same crop coordinate system
when a guest selects a different mode; it does not clamp most pixels to an
image edge.

The `C170` profile requires two bytes per sample. PGM intensity `v` becomes
LE16 `257*v`. PPM becomes `v=(77*R+150*G+29*B+128)>>8`, then the same LE16
encoding. These are explicit grayscale-model choices, not Nikon demosaic,
color science, Bayer order or recovered sensor packing. Generic diagnostic
format words keep their independent opaque byte-channel behavior.

## Measured behaviour (this model, unchanged firmware)

With a 640x480 gradient PPM configured and a 25 s run, the readout pipeline
counters read:

```text
requests=0  accepted=0  completed=0  rejected=0  stale=0
```

That historical run proves only that the then-current marker-only model did
not submit. It does not prove absence of guest descriptor configuration.
The later cold-neutral [producer audit](../../runtime/readoutstate.md#current-cold-neutral-producer-and-completion-join)
observes33 descriptor builds and a unit2 commit/run. The current model connects
that initial run and later markers to the same transport, and only actual
DMA DONE produces the finite mode-selected capture cause. An image alone is
still not a trigger. The finite profile supplies two-byte geometry, format
and timing assumptions; physical Bayer/packing semantics remain unresolved.

## Generating a test image

```sh
python3 - <<'EOF'
w, h = 640, 480
header = f"P6\n{w} {h}\n255\n".encode()
rows = bytearray()
for y in range(h):
    for x in range(w):
        rows += bytes((x * 255 // (w - 1), y * 255 // (h - 1), 128))
open("testimage.ppm", "wb").write(header + rows)
EOF
```

Then start the machine with:

```sh
qemu-system-arm -machine z7ii-mas-diagnostic,bundle=<container>,
cmos-image=/absolute/path/testimage.ppm ...
```

## Verification

The `image-source` native case fails on baseline `9403f26b`: a configured
image with fixture-provider disabled reports provider-bound false. The final
scoped batch passes21 CMOS and33 Sensor subtests. New cases cover source
binding/removal, active replacement rejection, retained file contents and
explicit reload failure, header/raster separation, CRLF, malformed/truncated
and missing files without fixture fallback, and invalid C170 sample width.

The integrated `pipeline/image-gray` case samples a2x2 RGB scene into128x2
LE16 samples through an owned readout request. It compares every sample with
literal red/green/blue/white levels77/149/29/255, then runs the actual statistics
coordinator. All four scalar results, a guest DMA output and stage8 completion
are checked. This validates the finite image-to-DMA-to-statistics contract,
not the unresolved front-end unit1/2 or group processing stages, physical
sensor fidelity, normal GUI output or complete boot.
