# Uniform MAIN luma gain

The paired-processing model consumes the two 32-node tables at bank-relative
offsets `0x1900` and `0x1940` as one double-buffered, uniform unsigned Q8 gain.
It applies the latest complete publication to actual MAIN Y pixels after the
four post-color passes and before byte quantization. Chroma, WORK, GRID and
pre-color history are unchanged. This is a finite numerical hypothesis for the
ordinary C1.70 table family, not a recovered silicon luma algorithm.

All offsets below are hexadecimal, relative to either paired-processing bank;
counts, gains and arithmetic constants are decimal. MMIO words are aligned
32-bit little-endian values containing two unsigned 16-bit nodes. Each table
occupies 64 bytes; the combined range is `[0x1900,0x1980)`.

## Producer and numerical boundary

The C1.70 MAS producer at VA `0x102FA1D0` selects one of two bulk records using
cached byte `+644` and metadata byte `+54`. It publishes only the selected
record's presence flag. The configuration builder at VA `0x102BF33C` supplies
these as table-array entries 20 and 21; NULL entries are skipped by the original
publication loop at VA `0x102E49FE`. VA `0x1036BF88` sends each present 64-byte
table to its corresponding MMIO range.

In the ordinary branch, VA `0x102FD926`/`0x102FD928` computes each node as a
six-term unsigned 16-bit dot product divided by 256. The ordinary coefficient
record has six values of 256 at every one of the 32 nodes. The resulting nodes
are therefore uniform: the sum of the six metadata values, truncated to 16 bits
when packed by the caller. This establishes the complete unsigned 16-bit wire
domain, including zero, rather than a narrower arbitrary gain whitelist.
The alternate metadata branch copies an identity record; another special
profile can supply nonuniform nodes. Nonuniform publications remain unsupported.

The implementation chooses a MAIN-Y role for this uniform scalar:

```text
scaled = min(4095, floor((Y12 * gain + 128) / 256))
Y8 = min(255, floor((scaled + 8) / 16))
```

The multiply uses unsigned 64-bit arithmetic. A gain of 256 is identity, 128 is
one half, and zero produces genuinely black Y through the same computation.
No omitted table implies gain zero or identity. Selection by the latest complete
CPU publication, the Y coordinate, stage placement, rounding and clipping are
explicit functional choices. The physical table selector and DSP role have not
been recovered. The first 20 tables retain their existing explicit calibration
profile qualification; they are not interpreted as additional gains.

## Publication and ownership

Each table has independent coverage for one ordered 16-word publication. Writing
word zero begins a new round. An out-of-order word cannot borrow earlier
coverage, and all 16 words must be written before a round is eligible. Complete
rounds must contain 32 equal nodes. A complete nonuniform round invalidates new
admission instead of falling back to the previous table.

Any partial round prevents new jobs. With a previously valid gain, admission
waits in AwaitInput without rejecting or consuming real RAW; complete publication
retries that input. A never-valid missing table or a complete nonuniform table
remains an explicit admission error. Staging does not alter an already owned job's
immutable gain: completion still performs the existing memory, alias, gate and
peer preflight, but does not reapply the new table's admission qualification.
A completed uniform publication with a different gain cancels the active job
before output DMA. Same-gain publication, including a complete rewrite of both
tables, preserves it. These MAIN-only changes preserve pre-color history.
A complete eligible publication may retry an existing AwaitInput request using
real unconsumed RAW; it never creates a request, RAW input or completion event.

Reset cancels owned work and clears both publication rounds and the selected
gain's validity. Cold or wholly unpublished tables cannot start a job. The
explicit calibration file must contain zeros in this interpreted range; those
zeros neither preload guest registers nor satisfy publication coverage.

## Validation scope

The pure fixture supplies constant RAW samples across the full gain domain's
important boundaries: 0, 128, 256, 512 and 65535. It checks Y rounding/clipping
and compares chroma, WORK, residual and GRID with an identity-gain reference.
The native fixtures use actual CMOS-to-RAW DMA and paired output DMA, checking
complete MAIN planes and padding, nonsymmetric chroma, an independent step-image
oracle, both publication buffers, missing/reset coverage, partial and reordered
rounds, nonuniform nodes, immutable active snapshots, same-gain rewrites and
changed-gain cancellation. These are synthetic numerical and lifecycle tests;
they do not establish original-firmware startup, physical image fidelity or
correctness of the unrecovered special nonuniform family.
