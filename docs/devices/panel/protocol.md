# Functional panel observation

`hw/nikon/devices/panel.c` reads the guest-owned composition buffer on a
QEMU virtual-clock timer and publishes its visible pixels through a normal
QEMU graphic console. It adds no guest register, event, IRQ, completion or
status and never writes the buffer. Improving this observer does not satisfy
any guest GUI startup condition.

## Geometry and frame comparison

The machine's existing finite profile is PA `bf309600`, 1024×688 RGBA8888,
with six inactive top rows and a 33,333,333 ns interval. The geometry and format
come from the transfer program; they are a functional observation convention,
not a recovered physical panel timing specification.

Every tick maps the complete visible range read-only. A complete mapping is
converted to host `0xAARRGGBB` words, with alpha copied unchanged, and compared
byte-for-byte with the previous converted frame. The first complete frame and
every subsequent difference publish the whole visible console surface. An
identical frame or a change confined to skipped rows does not publish. No
strided sample or hash stands in for a full visible-frame comparison.

This corrects a source-confirmed observer defect: the former implementation
hashed only one pixel every 4096 bytes. With a 1024-pixel row that observed
only each row's first pixel, so an internal or last-pixel change could leave
the console stale. The former `previous` buffer was written but never read.

A failed or incomplete mapping preserves the previous frame and retries on
the next tick. Unrealize cancels and frees the timer, closes the QEMU console
to detach its callback and device link, and releases both frame buffers.
Realizing the same device again starts without a retained frame. A device
reset creates no pixels or guest-side completion; the next tick continues to
observe the existing guest buffer.

## Validation scope and limits

The native `nikon_panel_unit` fixture retains the two byte-conversion tests
and adds `/nikon/panel/frame-changes`. It initializes the real QOM device,
system RAM, QEMU console/listener and virtual timer. The fixture checks every
visible output pixel, internal and final-pixel changes with each row's first
pixel unchanged, duplicate suppression, skipped rows, incomplete mapping and
recovery, unchanged guest RAM, reset, console callback detachment, timer
cancellation and re-realization. The old implementation's first expected
failure is the missing publication after an internal pixel changes.

The parent-run old-model fixture fails at the internal-pixel publication
assertion (0 updates instead of1). The final native batch passes all3 subtests
in0.51s, with no compiler warning. An intermediate lifecycle fixture used
`sysbus_realize` on an already parented device and failed a QEMU assertion;
the corrected fixture re-realizes through the documented QOM property.
Receipts: `run clearmode-20261023/panel-{red,lifecycle,green}.log`.

The fixture is synthetic observer evidence. It does not establish original
firmware progress, GUI readiness, a complete boot or physical panel behavior.
The sampling cadence is unchanged: guest writes during a scan need not form
an atomic frame, and writes that change and revert between scans need not be
observed. Each idle tick now reads and converts all visible pixels rather
than a sparse subset; no performance measurement is claimed here.
