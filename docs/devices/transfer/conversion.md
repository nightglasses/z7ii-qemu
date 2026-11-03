# VG RGBA-to-YUV Color Conversion Contract

The OpenVG/2D graphics engine (VG transfer) in the Nikon Z7II (MAS C1.70)
performs hardware-accelerated color-space conversion from 32-bit RGBA input
to YUV video/display planes. This document records the recovered parameter
encoding, color matrices, lookup tables, numerical range, chroma ordering,
alpha/coverage blending, and the explicit differences from the CPU software
converter at VA `0x102B3B82`.

## 1. Mathematical Formulation & Range

The VG graphics transfer executes limited-range (studio swing) ITU-R BT.601
conversion for display and video scanout, mapping 8-bit RGB `[0, 255]` to:
- Luma $Y \in [16, 235]$ (nominal black at 16, nominal white at 235)
- Chroma $U, V \in [16, 240]$ (neutral gray at 128)

### Per-Pixel RGB to YUV Transformation

For an 8-bit input pixel with color channels $R, G, B \in [0, 255]$:

$$\begin{aligned}
Y &= \operatorname{clamp}\left(\left\lfloor\frac{66 \times R + 129 \times G + 25 \times B + 128}{256}\right\rfloor + 16, 0, 255\right) \\
U &= \operatorname{clamp}\left(\left\lfloor\frac{-38 \times R - 74 \times G + 112 \times B + 128}{256}\right\rfloor + 128, 0, 255\right) \\
V &= \operatorname{clamp}\left(\left\lfloor\frac{112 \times R - 94 \times G - 18 \times B + 128}{256}\right\rfloor + 128, 0, 255\right)
\end{aligned}$$

Division by 256 uses integer right-shift with symmetric rounding bias ($+128$).
The $+16$ and $+128$ pedestals ensure standard limited-range video compliance.

### Inverse YUV to RGB Reconstruction

For reconstructing RGB from limited-range YUV during overlay composition:

$$\begin{aligned}
C &= \max(0, Y - 16) \\
D &= U - 128 \\
E &= V - 128 \\
R &= \operatorname{clamp}\left(\left\lfloor\frac{298 \times C + 409 \times E + 128}{256}\right\rfloor, 0, 255\right) \\
G &= \operatorname{clamp}\left(\left\lfloor\frac{298 \times C - 100 \times D - 208 \times E + 128}{256}\right\rfloor, 0, 255\right) \\
B &= \operatorname{clamp}\left(\left\lfloor\frac{298 \times C + 516 \times D + 128}{256}\right\rfloor, 0, 255\right)
\end{aligned}$$

## 2. Chroma Layout and Downsampling

For each horizontally adjacent pair of pixels $(x, x+1)$:
- Luma bytes $Y_0$ and $Y_1$ are preserved individually at full horizontal resolution.
- Chroma bytes $U$ and $V$ are horizontally downsampled (4:2:2 chroma subsampling):

$$U_{\text{pair}} = \left\lfloor\frac{U_0 + U_1 + 1}{2}\right\rfloor, \quad V_{\text{pair}} = \left\lfloor\frac{V_0 + V_1 + 1}{2}\right\rfloor$$

Chroma downsampling rounds half-up ($+1$ bias). In memory, $U$ strictly
precedes $V$.

### Format 105 (Planar Y + Interleaved UV)
- **Plane 0 (Luma)**: Width 1024 × Height 682 = 698,368 bytes.
  Array of 8-bit luma samples: $Y_0, Y_1, Y_2, \dots$
- **Plane 1 (Chroma)**: Width 1024 × Height 682 = 698,368 bytes.
  Interleaved byte pairs: $U_0, V_0, U_1, V_1, \dots$
- Total footprint: 1,396,736 bytes ($2 \times 698,368$).
- Destination alpha is not stored; downstream pipeline treats 105 as opaque.

### Format 107 (Interleaved Primary + Planar Alpha)
- **Plane 0 (Primary)**: 2 bytes per pixel ($2 \times 1024 \times 682 = 1,396,736$ bytes).
  Packed 4-byte quads representing pixel pairs: $[Y_0, Y_1, U, V]$.
- **Plane 1 (Alpha)**: Width 1024 × Height 682 = 698,368 bytes.
  Per-pixel 8-bit alpha/coverage values.
- Total footprint: 2,095,104 bytes ($1,396,736 + 698,368$).

## 3. Spatial Affine Matrix and 256-Knot LUT

### Affine Coordinate Matrix
In the conversion packets (setup word 13 and draw words 50..58), geometry
coordinates are transformed via an affine 3×3 matrix encoded as IEEE 754
little-endian floats:

$$M = \begin{bmatrix}
1.4222107 & 0 & 0 \\
0 & -1.4208221 & 0 \\
0 & 687.99994 & 1
\end{bmatrix}$$

This maps normalized $720 \times 480$ source coordinates to the target
$1024 \times 682$ display viewport with vertical flipping.

### 256-Knot LUT
Draw packet words 98..353 encode a 256-entry lookup table.
Firmware populates this table with identity values:
$$\text{LUT}[i] = i \quad (i \in [0, 255])$$
The LUT linearizes channel response and preserves 8-bit dynamic range without
tonal distortion.

## 4. Alpha Blending Semantics (SRC vs SRC_OVER)

- **Format 105 (Nominal & Scaled)**:
  Uses `SRC` blend mode. Target destination is treated as opaque ($\alpha = 255$).
  When source alpha is 0, covered pixels render black ($Y=16, U=128, V=128$).
  Fractional coverage blends source RGB against existing destination YUV.

- **Format 107 (Scaled)**:
  Uses `SRC_OVER` blend mode (register `1a0[19:16] = 1`).
  Source alpha and destination alpha blend via Porter-Duff source-over:
  $$A_{\text{out}} = A_{\text{src}} + \left\lfloor\frac{A_{\text{dest}} \times (255 - A_{\text{src}}) + 127}{255}\right\rfloor$$
  Color channels blend weighted by effective coverage and alpha before YUV conversion.

## 5. Comparison: VG BT.601 vs CPU Converter (VA 0x102B3B82)

The C1.70 MAS firmware contains a CPU fallback converter at VA `0x102B3B82`.
The table below highlights the numerical differences:

| Property | VG Graphics Converter | CPU Software Converter (`0x102B3B82`) |
|---|---|---|
| Color Range | Limited-range BT.601 (studio swing) | Full-range RGB (0..255) |
| Luma Pedestal | $+16$ ($Y \in [16, 235]$) | $+0$ ($Y \in [0, 255]$) |
| Chroma Pedestal | $+128$ ($U, V \in [16, 240]$) | $+128$ ($U \in [0, 255], V \in [1, 255]$) |
| Rounding Mode | Nearest ($+128$ bias before $\gg 8$) | Arithmetic floor toward $-\infty$ (`ASR`) |
| Chroma Order | U then V ($U_0, V_0, \dots$) | U then V ($U_0, V_0, \dots$) |
| Alpha Output | Supported (Format 107 explicit plane) | Not supported (destination selector 1 only) |

### Numerical Vector Differences

| Input RGB `(R, G, B)` | VG $Y$ | VG $U$ | VG $V$ | CPU $Y$ | CPU $U$ | CPU $V$ | $\Delta Y$ | $\Delta U$ | $\Delta V$ |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Black `(0, 0, 0)` | 16 | 128 | 128 | 0 | 128 | 128 | +16 | 0 | 0 |
| White `(255, 255, 255)` | 235 | 128 | 128 | 255 | 128 | 128 | -20 | 0 | 0 |
| Pure Red `(255, 0, 0)` | 81 | 90 | 240 | 76 | 85 | 255 | +5 | +5 | -15 |
| Pure Green `(0, 255, 0)` | 145 | 54 | 34 | 149 | 43 | 21 | -4 | +11 | +13 |
| Pure Blue `(0, 0, 255)` | 41 | 240 | 110 | 28 | 255 | 108 | +13 | -15 | +2 |
| Gray 128 `(128, 128, 128)` | 126 | 128 | 128 | 128 | 128 | 128 | -2 | 0 | 0 |
| Low Red `(1, 0, 0)` | 16 | 128 | 128 | 0 | 127 | 128 | +16 | +1 | 0 |

### Observed Firmware Validation Evidence

In the recorded graphics-model execution, when the guest firmware submits a
black clear buffer:
- Output $Y$ plane contains exclusively `16`.
- Output $UV$ plane contains exclusively `128`.
This firmware execution observation definitively proves that the hardware
graphics pipeline uses limited-range BT.601 rather than full-range conversion.

## 6. Functional Assumptions vs Physical GPU Silicon

- **Established Facts**: Limited-range BT.601 coefficients, $+16/+128$ offsets,
  identity 256-LUT, affine f24 matrix encoding, format 105 (planar Y + interleaved UV),
  format 107 (interleaved primary + planar alpha), and observed scanout bytes.
- **Retained Assumptions**: Exact internal ALU bit-depth (e.g. 10-bit or 12-bit
  intermediate registers before 8-bit truncation) in physical ASIC silicon is
  unobservable from software buffers. QEMU models the transformation using 32-bit
  integer fixed-point arithmetic with symmetric rounding.
