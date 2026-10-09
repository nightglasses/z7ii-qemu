# Native GUI integration

The integration uses QEMU 11.1.2 at upstream commit
`4fc49f46dc95d4a27de2509e7fceb2931e91faeb`. The configured development
build is `work/build/mas-diagnostic`; its source is the matching fixed upstream
checkout `work/upstream/qemu-11.1.2`. Maintained Nikon source lives in the parent
repository's `hw/nikon`, `include/hw/nikon` and `tests/*/nikon`; the current
official dependency is the read-only root `qemu/` submodule.
Only the changed native source files were synchronized for incremental builds.

The `106a0ce` build compiled and linked the complete six-class storage profile,
five-component bundle input, GPIO, completion, HDMAC, bridge and packet transfer.
The original complete bundle subsequently entered GUI initialization phases 1
and 2 and the GUI message task, then stopped on an unsupported USIO byte write.
The corresponding original-run record is linked from
[storage](../runtime/storage.md); it does not establish visible GUI output.

The new cases passed across the initial batch and its necessary corrections:

| Cases | Result | Local evidence under `work/validation/gui` |
|---|---|---|
| Memory class boundaries, independence, decode retention and holes: 4 | PASS | `memory.log` |
| GPIO explicit 192-pad input, completion domains and bridge events: 3 | PASS | `devices.log`, before its transfer failure |
| Packet byte copy, cancellation and equal-plane geometry: 3 | PASS | `transfercorrected.log`, 1.49 seconds |
| DMA pause, halt, cancel, reset, reentrant bus writes and Audio: 7 | PASS | `dmacorrected.log`, 2.06 seconds |
| Bundle parsing and invalid input rejection: 2 | PASS | `bundle.log`, before its VR record failure |
| VR record extraction and bounds: 1 | PASS | `recordscorrected.log`, 0.33 seconds |

The original 16 passing cases were not rerun. These 20 cases validate native
functional contracts; they are not measurements of camera hardware or a claim
that the complete camera boots.

The first Make invocation compiled the system and main qtest executables, but
its extra explicit parallel qtest targets selected an upstream implicit Make
rule and failed. The build continued with the upstream Ninja targets. The
initial device test failures had two independent causes: `-S` disabled virtual
timer execution in qtest fixtures, and a signed `ldl_le_p()` value was extended
while comparing VR addresses with `size_t`. Tests now observe actual board GIC
inputs instead of accessing the secure guest CPU interface. Each failed run
remains in the ignored logs; only the affected cases were repeated.

The next integration at `f44817e` also compiled both native ARM and RX system
targets and passed only its six new or previously failing cases:

| Cases | Result | Local evidence under `work/validation/gui` |
|---|---|---|
| USIO transmit, receive, idle, flushing and disconnected nine-bit lanes: 4 | PASS | `usio.log`, 1.44 seconds |
| Source-derived paired transfer planes: 1 | PASS | `planes.log`, 1.00 seconds |
| RX TCG reset, caller stack, EXTB read/write and relocated undefined exception: 1 | PASS | `vrcorrected.log`, 0.91 seconds |

The RX target is the existing upstream TCG implementation with explicit EXTB
support. Its first native fixture had its register-store operands reversed;
the corrected synthetic instructions and paused TCG launch establish the
intended CPU behavior. Original RX execution separately revealed the TLB's
page-offset requirement for unaligned RAM extents. Private backing with an
offset alias preserves each exact mapped extent without adding padding or
changing guest bytes. These results do not establish complete original RX
firmware execution, device peers or visible camera output.
