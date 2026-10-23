# Capture run, stop and quiescence

The complete finite `08/0c` family replaces unconstrained08 retention and the
old0c mask restricted to bits2/3. All addresses below are C1.70 MAS VAs unless
marked PA; offsets are relative to capture-control PA `20068000`.

## Original producers and consumers

`1020aafc` writes its R1 unit mask to08; `1020a830` writes it to0c.
The eight start wrapper families `1020c07e/23c/3c0/444/56c/648/728/802`
submit their associated100/104 commands before08. They select the same
unit namespace used by reset00/04 and enable10/14. Direct masks cover
1,2,10,20,200,400,800; mapper `10209796` selects4/8, and the configuration
referenced by `1020c58a/5f6` supplies40/80. The11-bit union is `eff`; bit8
has no recovered start/stop unit. These are unit identities, not the upper
software flags consumed by `1023c796`.

The stop path is `102389dc` → `1023c796` → the selected0c wrapper.
For unit1, `1023cd42` calls `1023858a(1)` then `1020c462` → `1020a830`.
There is also a direct cleanup path through `102301b6`'s continuation,
`10238680` → `1023c50a` → `1023cd42`, observed in the current trace.
`1023858a` conditionally adds upper quiescence bits to the ordinary masks
when `b26b38cc & 2` and `b26b38d0` are both nonzero.

These upper notifications are distinct from the low-level stop causes in
`1020a71a`: the latter select units for `1020a7a8` to restore masks, clear18,
disable14, assert reset00 and call the original optional stop callback.
Original IRQ94..98 handlers `1020a836..1020a9b2` read/ACK pending, process
that low-level stop path, then dispatch the separately masked status snapshot.
Registrar `1023954a` installs the upper callbacks through `1020c034`.
In particular, callback `10239298` tests raw0 bit15 for the combined4/8
group; enabling both bits13/15 in `1023858a` does not prove that each is a
different unit's stop ACK. Callback `102394dc` tests raw7 bit3 and publishes
PREPRO bit20; raw7 bits4/5 alone do not supply that upper notification.

| Unit mask | Low-level stop cause | Upper quiescence cause |
|---|---|---|
| `1` | none recovered | raw0 `200` |
| `2` | raw7 `30` | raw7 `8` |
| `4/8` | none recovered | shared raw0 `8000`, both halves stopped |
| `10` | raw1 `1` | raw0 `80000000` |
| `20` | raw0 `10000000` | raw0 `08000000` |
| `40` | none recovered | raw0 `20000` |
| `80` | none recovered | raw0 `80000` |
| `200` | raw1 `100` | raw1 `80` |
| `400` | raw1 `10` | raw1 `8` |
| `800` | raw1 `1000` | raw1 `800` |

Raw wordN is PA `20068210 + 20*N` hex; its IRQ mask is10 hex bytes earlier.
Both classes use the existing pending, mask, W1C and GIC path. The fixed Z7
implementation independently uses corresponding
[run/stop controls](https://github.com/WonderLantern/z7-qemu/blob/97abf0b81cd3211f8374a38987a0beaf4549b90f/qemu/nikon-z7-lab.c#L1774)
and separates the two
[stop notification classes](https://github.com/WonderLantern/z7-qemu/blob/97abf0b81cd3211f8374a38987a0beaf4549b90f/qemu/nikon-z7-lab.c#L1131).
This supports the interpretation; it is not a physical Z7II specification.

## Finite hypotheses and cancellation

The model interprets08 as OR-ing run intent for enabled, non-reset units;
0c clears selected run intent and schedules idempotent quiescence. Start is
not data completion. Only aligned32-bit masks within `eff` are accepted,
including zero and combinations; complements are not part of this grammar.
Readback through08/0c mirrors effective run intent as a functional convention,
not a recovered original read consumer.

There is no active pixel worker bound to these run bits yet. The explicit
idle-worker hypothesis permits stop confirmation after1us virtual time,
including an already idle unit. Each of the11 units has its own QEMU timer;
duplicate outstanding requests coalesce without moving their deadline.
For4/8, publication waits until both run bits and both queued timers are clear.
Thus t0 stop4 followed by t999ns stop8 cannot notify until t1999ns.

An08 command supersedes the selected unit's outstanding stop. Reset assertion
00 and disable14 cancel only the selected units and clear their run intent.
They preserve latched pending from either this model or an external provider.
A control operation on8 cannot cancel an unrelated outstanding stop4.
An expired4/8 confirmation retains its identity while waiting for the other
half. Cancelling that other half rechecks the shared drain; it does not lose
the already completed confirmation or restart its delay. Publication consumes
both completed identities once, so W1C cannot be followed by a stale replay.
Whole-device reset/unrealize cancels all timers and clears state; finalization
frees them. Future asynchronous data processing must join this stop fence to
its actual drain/cancellation, rather than acknowledging unfinished work.

The delay, idle-stop behavior, enable/reset admission and cancellation rules
are explicit functional hypotheses. The source establishes the unit values,
callback consumers and cause mapping; it does not establish physical latency.
No normal frame/data cause, common raw0 `80` end-of-sequence, DMA, pixel,
guest flag, callback invocation or task/PC change is supplied by this profile.

## Validation and runtime scope

`make check MTESTARGS=nikon_sensor` passes all25 cases. The new runstop case
covers all11 unit bits, combinations and masks/W1C, split4/8, staggered and
duplicate stops, per-unit restart/reset/disable cancellation, invalid masks,
widths/alignment, reset and unrealize. Native reds preserve the old rejection,
the initially missing upper/low-level split, the shared4/8 early-deadline
defect and loss of an expired identity after late cancellation of its peer.
The final model gives each half independent lifetime and timing.

The final-binary30-host-second run of the unchanged original bundle uses
the existing TEMP3/descriptor18, empty
descriptor12, acceleration(0,0,1024), RNG0, healthy paced UART and cold held-F4
inputs. Original `1020aafc(R1=1)` executes, then the0c store returns through
`1023cd50`; a subsequent disable14 for unit1 cancels its queued fence. No
capture ISR or upper quiescence callback is observed in this continuation;
the cause mappings currently have native coverage, not original runtime proof.
The old first MMIO error disappears and the UART peer continues without a
rejection. Application bytes03/02 and primary request state11 remain.

The longer continuation crosses the GUI4000 wait through original callback
`104be418(05040005)`. Its next first MMIO rejection is CPU0 `1036dafc`,
WRITE32 PA `2c08010c=0d820000`, in the separate audio format/control family.
Original `1036da82` derives that word from a byte configuration record by
RMW and ORs fixed bits `04800000`; the whole related control family is the
next source audit. No GUI first frame, scanout,
complete boot or physical-camera validation is established. Ignored receipts
are `work/runs/captureflow-20261022/`: source projections, actual argv,
native reds and final log, final control trace/snapshot, the separate
bounded GUI wait trace, and `confirmed` final-binary argv/trace/snapshot and
`confirmed-callchain.json` checked by `verify.py`. A first optional-stack snapshot failed on an unmapped
sampled SP and is excluded; the corrected read-only snapshot completes.
