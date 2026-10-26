# Explicit GPIO pad input

[IRQ and event dependency audit](/devices/gate/dependencies). The complete
original GPIO getter at `10385d76` reads the object's PDR address, shifts by
its pin byte and returns bit0. The fixed TOOL source
`57546e1cbed483ac82dff7246c5d8dccbad02279`,
`research/z7ii/qemu-rtos-startup-contract.md:808..814`, follows the original
logical lookup, constructor candidate and whole COPY0 object table. Service
`60` (hexadecimal, 96 decimal) selects object `b180399c`, source O`237d930`.
Its data address `04282018` and pin3 describe port6 bit3; its external bit12,
IRQ77 and wrapper `10386075` agree with the existing EXIU software route.
Neither a captured live object nor a physical external peer is established.

The saved whole GPIO contract already requires input/alternate pad reads to
consume supplied external levels, with pulls used for floating inputs. The
previous device had only pad outputs: every input read was derived from its
pull configuration or virtual low. The finite `external-level` qdev input
now supplies one line for each of the existing twelve ports and sixteen
pins, index `port*16+pin`.
Publication of either level, including explicit low, marks that pad driven;
unpublished pads retain the prior pull/virtual-low profile. A driven level
overrides pulls. GPIO output mode continues to expose the guest output latch
and takes precedence over an external input, an explicit functional
convention rather than an electrical contention model. Alternate mode uses
the explicitly supplied level and does not invent a peripheral provider.

External publications update the same `pad` outputs used by PDR readback and
the existing GPIO-to-EXIU connections. EXIU therefore samples those actual
levels through its established polarity/edge/mask/W1C law, and the upstream
GIC/guest handles any admitted IRQ. There is no host callback, task flag,
timer, key event or request completion. Reset clears the guest GPIO
configuration and latch but preserves the external publications; it cannot
invent a transition in the external provider state. Resetting direction or
function can still change the resolved pad level, just as their guest writes
can. This diagnostic board has fixed
topology: the interface does not specify hot-unplug or high-impedance changes.

In the original default table port6 direction`1780` has pin3 configured as
input and pull-enable`8c80` leaves pin3 floating. With no external publication
its value remains virtual low. The later private SLA peer supplies the actual
modeled handshake through this pad; an arbitrary fixed ready level is not a
substitute for that peer. Physical polarity/pad wiring and electrical reset
remain unmeasured.

The sole builder owns C qtests and native registration. Focused cases should
cover explicit low overriding a pull-up; driven high readback; output-latch
precedence with input updates retained for a later direction change;
alternate selection; first/last pad indexing; provider state across reset;
and existing EXIU12/GIC77 edge, mask and selected ACK through port6 pin3.
Keep existing strict MMIO width/hole/high-field refusal checks. These are
synthetic interface cases, not original peer or whole-boot acceptance. This
initial interface design did not itself establish a native/test result.

## Whole source-family audit

The C1.70 MAS source audit covers all 232 GPIO descriptors and 31 direct
`10385d76` read-helper call sites in 29 functions. The descriptor table is copied
from ROM VA `1239c430` to RAM VA `b180249c`, with 56-byte records; the larger
COPY0 owner starts at ROM VA `12399f94` and RAM VA `b1800000`. A read-only runtime
join filters the service registry at VA `b18aedc8` by object PDR addresses in
PA `[04282000,04282030)` and finds 187 registered GPIO objects. This counts
initialized objects, not distinct physical pads or proved accessory roles.

| Input class | Source-established pad/service | Finite provider and boundary |
| --- | --- | --- |
| EX handshake | Services 86/87/108; P5.9/P5.10/P6.15 | Existing dynamic EX peer; no static ready substitution |
| CComp acknowledgement | Service 84; P5.7 | Existing request/ACK peer, with cancellation and IRQ effects |
| SLA handshake | Service 96; P6.3 | Existing private-domain peer |
| SD insertion | Service 91; P5.14 | Explicit high means absent; separate SD transport remains unimplemented |
| Other inspected inputs | Services 5/33/36/83/95/98/106/107/143/159/169 | Source retains getters and direction/pull state; no unproved accessory identity or static presence is assigned |

The SD join is confirmed by service-map VA `120659c0`, object VA `b1803884`,
pin byte 14 and PDR PA `04282014`. Before the correction, its undriven,
pull-disabled low level falsely meant inserted. The board now publishes high
for this absent slot through `external-level`; it does not change the generic
cold state of other GPIO pads. See [storage](../storage/protocol.md#insertion-before-link-training).

The runtime audit also prevents a blanket pull-up workaround. Services 5 and
33 already read high through guest-selected pulls; service 159/P10.2 reads low
where the storage power-feedback consumer expects low. Some descriptors select
alternate mode. Unknown roles do not establish cover, lens, HDMI, headphone or
write-protect detection. Those identities require their actual callers and
providers; the audit is not evidence for setting every undriven pin high.

Six native GPIO cases pass, including the new absent-card case across reset,
pull changes and output/input direction changes. The existing IRQ fixture now
expects both its driven ACK bit and the independent absent-SD bit in whole-port
readback. Historical five-case results above are not relabeled as this run.
