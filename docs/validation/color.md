# Full-resolution scene and color validation

Unchanged C1.70 AUTO photo firmware displays a detailed 1200 × 800 P6 photograph
and 20 successive dynamic inputs through normal acquisition, RAW processing,
owned graph outputs and the original display queue. The actual scanout is
1024 × 682. The accepted window lasts 83.13 host seconds, with increasing
processing counts, zero processing errors and post-window panic length zero.
This is bounded functional live-view acceptance under declared virtual inputs;
it is not physical color calibration or complete camera operation.

![Original GUI displaying the normally acquired photograph](/evidence/color/photo.png)

The source is NASA's [Blue Marble composite](https://science.nasa.gov/resource/blue-marble/).
Its 1280 × 800 asset is cropped by 40 pixels on each side to 1200 × 800, without
adding overlays. The widgets above are drawn by the original firmware. The
photo is supplied only to the CMOS provider; no host image replaces a framebuffer,
display surface, output DMA or guest-owned input slot.

## Geometry and color corrections

Two independent input-domain errors explained the earlier bands and purple
neutral patches. A separate diagnostic observed an 8320 × 34 auxiliary
acquisition being enlarged to 1380 × 918 RAW. Repeating CFA tiles vertically
by 27 moved the two green phases' bar boundaries to different half-rows, 54 and
81. That lost detail cannot be recovered by changing a color matrix.

The [independent MAIN acquisition](../devices/readoutpipeline/input.md) now
uses the committed unit2 geometry, 2760 × 918 in this photo profile. The same
provider serializes real auxiliary and MAIN requests, retaining separate owned
snapshots. Capture-work reduces MAIN to actual packed 1380 × 918 RAW. The original
paired crop starts at (2, 2), producing 1376 × 914 MAIN with stride 1408.
Auxiliary statistics continue through their own normal path.

The second error was numerical: encoded RAW values from the original forward
lookup table entered a linear white-balance calculation. The
[frozen RAW codec](../devices/pairs/color.md#virtual-sensor-transport) now
reconstructs the declared linear domain from the exact codes actually written
by the producer. No RGB side buffer supplies replacement pixels. Unknown codes
and incomplete, repeated or decreasing tables reject. The real packed RAW DMA
remains unchanged.

The virtual sensor has fixed Q12 responses R = 2198, Gr = Gb = 4096 and B = 2966,
selected before realization and independent of live white balance. These are
explicit reference-illuminant inputs, not recovered factory calibration.
The lookup inversion, CFA phase, demosaicing, numerical stage order, curve units
and matrix interpretation remain finite hypotheses. Source facts establish
the original producers, published fields, geometry and consumer chain; they do
not establish the physical ISP's equations.

## Dynamic scanout and detail

![Twenty dynamic inputs rendered through the original GUI](/evidence/color/motion.gif)

The test supplies 20 complete 1200 × 800 P6 images through the normal CMOS image
property. Each has eight color bars, a gray ramp, fine gratings and an 80 × 80
neutral square moving 44 source pixels per replacement. After each accepted
replacement, read-only scanout polling waits for that square's expected position.
All intermediate observations are retained in the local execution record. This
does not drain queues, advance ownership or inject completion events.

| Observation | Result |
| --- | --- |
| Visible current square positions | 20 of 20, 20 distinct increasing X coordinates |
| Processing count at each accepted position | Strictly increasing |
| Gray bar RGB mean | (211, 211, 211) in all 20 captures |
| Gray bar maximum channel difference | 0, versus 59 in the earlier shared-input/color-domain run |
| Five gray-ramp means in one representative capture | Approximately 83, 115, 146, 178 and 210; channel differences at most 2.06 |
| Grating periods in the source | 2, 4, 8, 16 and 32 pixels |
| Representative grating peak-to-peak RGB mean | 117.67, 119.33, 179, 194 and 193.67 |
| First-poll-to-visible host latency | 0.75–2.18 seconds, median 1.71 seconds |

The narrow gratings contain CFA/resampling artifacts. Primary bars retain their
dominant color, but several channels clip under the original programmed
matrix/curve profile. A neutral axis and visible detail therefore establish the
corrected functional domains, not photographic color accuracy. The two-pixel
pattern is not an optical resolution claim.

The latency includes host polling and image analysis. An earlier run that waited
only three graph completions sometimes sampled an older displayed frame: it
detected 19 squares and 17 distinct positions in 20 samples. Waiting for actual
scanout retirement resolves that observation error without changing the device
model. The GIF uses 800 ms per frame for inspection; it is not a camera frame-rate
measurement. The [pixel measurements](/evidence/color/pixels.json) retain actual
capture timestamps, counters, patch means and positions.

## Execution and remaining boundaries

The accepted run uses five-CPU MTTCG, cold-neutral resource input, a healthy
deterministic UART battery peer, the virtual 50 mm F2.8 lens, held F4 and a fresh
blank 8 GiB slot0 card. Original firmware publishes the interpreted live
calibration fields. There is no debugger during the live window; the final
read-only debugger snapshot is separate. Paired jobs increase from 24 at the
30-second sample to 148 after all replacements and the restored photo. Graph
jobs increase from 24 to 149 in later sequential reads. These are not atomic
counters, and the final sample is not a 45-second deadline.

The [curated receipt](/evidence/color/capture.json) separates native numerical
and lifecycle fixtures, original execution, the earlier observation error and
the temporary host-only geometry diagnostic. Scoped native checks cover the
36-case pure operator, MAIN/auxiliary ownership, encoded DMA and transfer
boundaries, the complete post packet, cancellation and reset.

One separate first attempt stopped at SLA PC `0x1006acd6`, submitting
`0x20070010 = 1`, with `fill-request-profile-not-supported`. Two subsequent fresh
AUTO runs succeeded. That failure's snapshot selected MAS's private fill device,
so it cannot identify the rejected SLA tuple. The original SLA source also has
a real copy-request family; its presence does not prove that this failure used
it. This remains an explicit startup-stability gap until the SLA private state
is captured at a recurrence.

Normal P/S/A/M mode selection and [exposure controls](../devices/gpio/exposure.md)
remain separate acceptance questions. A fresh M run settles at context 32, but
one queued negative dial0 detent leaves the observed MAS count and shutter
setting unchanged for about 45.6 host seconds. This does not identify which EX
sampling/transport stage missed the input. It is not accepted shutter or aperture
control. Subsequent independent GPIO and breakpoint diagnostics reach the
original EX negative count-store instruction, snapshot and aggregate with the
expected values. The
remaining [input investigation](../devices/gpio/exposure.md#bounded-original-input-diagnosis)
is type2 transport congestion: the selected slot contains an older BatMg status
report and the new input submission returns `ffff`. Later queue indices advance,
so a permanently stopped consumer is not established. The breakpoint run pauses execution
and is excluded from the no-debugger scene acceptance above. Optical exposure
effects, still capture, recording, saving and indefinite stability remain unverified.
