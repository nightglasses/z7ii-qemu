# Interpolation record storage

The original C1.70 MAS write at PC`10404398` uses VA`3d5ab400`, translated
by the guest MMU to PA`c33ab400`. It belongs to allocation kind74, not to
the resource57 drawing surfaces. Addresses below are MAS VA unless marked PA.

## Producer and ownership

`10306fb0` initializes the83 pool kinds through pointer table`11982938`.
Entry74 at`11982a60` selects row`11983548`, whose six words describe flags118,
size`f00000`, unit`10000`, count`f0`, PA base`c24cb400` and control701.
The five mode override tables have a NULL entry74. `10298a7a` builds the
pool through `10297d12 -> 102e5e6c`; the latter derives240 units from
size/unit, constructs allocation metadata and retains the source parameters.
The observed pool object at`b1e46d0c` has the same base, size and unit.
Its complete storage range is therefore PA`[c24cb400,c33cb400)`,15MiB.
This declaration establishes this pool, not the larger MMU conversion class.

`102b64b0` owns the current request. Its input's third pointer selects object
`b2364e40`. It sets that object's `+4750` scratch pointer, queries service9
through `10299378`, and calls `1030d96e(1,74,19c80)` at`102b6574`.
The source query at`1040df30` computes a40-byte record grid; the special
6/3 mode uses64-pixel cells and eight subdivisions vertically. The observed
dimensions1374x914 give22x120 records, totaling`19c80` bytes.

The returned handle`b2c9b4e0` points through`+4` to descriptor`b2c8b018`.
That descriptor contains PA`c33ab400`, reserved length`20000`, unit index238
and unit count2. Thus the current two-unit allocation is
PA`[c33ab400,c33cb400)`, while its requested record prefix ends at`c33c5080`.
The request, reservation and complete pool are three different bounds.

`102b658c` stores the translated pointer in object`+4758`. Service9 then
dispatches `1040decc -> 1040e112 -> 104093c4 -> 10408e4e -> 10404354`.
The last helper receives a packet whose eight destination pointers occupy
`+28..+44`. Each destination receives nine words: eight interpolated23-bit
fields and a tag at`+20`; records have40-byte stride, leaving the last word
untouched. The first packet's destinations are spaced`370` bytes,22 records.
Its first write is100hex and tag306hex. These are software records, not a
framebuffer or an emulated hardware completion.

## Conversion and finite model

`103c5a7a` selects row27 at`b2b415a0`: PA class base`bd200000`, type1,
logical base`37400000`, next PA class`c3e00000`. It returns
`37400000 + c33ab400 - bd200000 = 3d5ab400`. The guest L1 descriptor
at`b2b74000 + 4*3d5` is`c3314426`, independently translating the same VA
to PA`c33ab400`. All16 section descriptors covering the pool's logical
range`[3c6cb400,3d5cb400)` select the corresponding physical sections.

The machine backs the entire declared pool with ordinary shared QEMU RAM.
It reuses `z7ii_ram`, whose backing offset matches the physical page offset;
the guest-visible view retains both exact pool boundaries. Initial zero is
an explicit functional-board assumption, not measured physical power-on RAM.
Bytes persist across machine reset. Firmware owns allocation metadata, guest
PTEs, record generation and consumers. No physical mirror at the logical VA,
neighboring storage, ready bit, IRQ, DMA completion or guest return is added.

## Validation and next frontier

The new native fixture fails against the old model at its first pool write
readback. After implementation, all29 `nikon_memory` cases pass in8.87s.
The CPU fixture includes partial-page, interior-page and last-word accesses;
the pool fixture DMA-copies and compares the complete`19c80` request, checks
exact byte boundaries and absent physical VA aliases through HMP memory
errors, then verifies RAM retention after reset. These synthetic checks
establish the storage contract separately from firmware progress.

Unchanged `interpolationmemory-20261024/verified` uses the same cold-neutral
NOR file, held cold F4, RNG0, acceleration/TEMP inputs,9600-baud UART peer and
five-CPU MTTCG composition. All six original loader COPY ranges compare with
the input; the version guard returns0 and property6433 stays empty.
The formerly rejected write now succeeds. The final read-only snapshot has
544 records tagged306 and24 tagged305, with2072 records still zero. The next
failure occurs on another task stack, so completion of the entire record
generation is not established by this bounded run.

The first new rejection is CPU1 PC`1024ffda`, READ16 PA`5d170000`.
`1024feb6` reads a descriptor-selected16-bit grid, expands samples into
32-bit output words and computes threshold counts/mean over a central area.
Its source pointer's producer, ownership, complete bounds and required data
remain the next investigation; zero samples are not supplied by this change.
QMP reports debug/stopped, with neither display head bound to a console.
This is functional-input/model progress, not normal GUI output or complete
boot. Raw source, traces, old-model failure, native batch and input comparison
remain in ignored `work/runs/interpolationmemory-20261024/`.
