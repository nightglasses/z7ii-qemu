# Save handoff

`nikon-save` is a host-side handoff contract for the original C1.70 file-save
path. The saved static evidence identifies the local request worker at
`1044b300` (command `0x42091001`), the sector callback at `1047e768`, and the
Block I/O bridge at `104d5a3a`. It also identifies the normal file-object write
path `10567d8e -> 105671ba -> 105bea78`. These are firmware consumer boundaries,
not proof of a captured-frame format or a complete writer.

The device records a request containing a token, guest source address, byte
length, and an optional RAW/JPEG/unknown format label. A later source-backed
writer must explicitly commit, fail, or cancel the same token. The public C API
and read-only QOM counters expose this state for the GUI integration and
runtime receipts.

The handoff deliberately performs no media I/O. A request does not read guest
memory, create a host file, acknowledge a firmware ring, or assert a completion
IRQ. A commit only records that an external writer reported durable completion;
it cannot be called as a substitute for the writer. Failures require a
nonzero source error code, and stale or concurrent tokens are rejected.

The existing `nikon.storage` device remains the source-bound NVMe endpoint. Its
real BlockBackend and native queue completion are separate from this handoff.
The existing VR record store likewise remains the only implemented durable
record writer. Neither path establishes that the original camera has saved a
RAW or JPEG frame. The next integration evidence must join a real frame
producer, size/format descriptor, sector or file writer, and its completion
consumer before this handoff is connected to guest execution.

The unit lifecycle tests cover request, token rejection, commit, failure,
cancellation, and invalid inputs. The qtest cold receipt verifies that the
realized device starts idle with no pending request, output, or error.
