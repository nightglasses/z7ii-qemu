# Camera image input profile

This profile carries one explicit still image into the modelled CMOS transport.
It is an **external input**, not recovered sensor data, calibration, or a claim
about the physical sensor.

## Input

| Item | Value |
| --- | --- |
| Machine property | `cmos-image=<absolute path>` |
| Format | binary PGM (`P5`) or PPM (`P6`), 8-bit, `maxval 255` |
| Size | the guest's own readout geometry, not this image's |
| Identity | `NIKON_CMOS_FORMAT_FUNCTIONAL_C170` (`0x43313730`, the ASCII tag `C170`) |

The file is read once when the machine realizes.  The device refuses anything
that is not binary 8-bit PGM/PPM, and refuses a change while a capture is
active.

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

`tests/unit/nikon/transfer.c`-style fixtures do not cover this profile; the
current receipt is the runtime counter read above plus the CMOS unit fixtures
that assert rejection of non-binary, truncated and overdrawn inputs.  Do not read
this profile as evidence of a sensor, of real exposure, or of a natural boot.
