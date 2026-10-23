# Explicit GPIO pad input

Issue [398](https://github.com/mashiro01/nightglass/issues/398). The complete
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
pins, index `port*16+pin`. No machine wire or provider is added by this change.
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
its value remains virtual low, exactly as before. The original ring handshake
may still wait on this input. Supplying an invented peer level to make it
return would violate the source boundary. Physical polarity/pad wiring,
electrical reset and peer protocol remain unknown.

The sole builder owns C qtests and native registration. Focused cases should
cover explicit low overriding a pull-up; driven high readback; output-latch
precedence with input updates retained for a later direction change;
alternate selection; first/last pad indexing; provider state across reset;
and existing EXIU12/GIC77 edge, mask and selected ACK through port6 pin3.
Keep existing strict MMIO width/hole/high-field refusal checks. These are
synthetic interface cases, not original peer or whole-boot acceptance. This
child did not compile or run a native/test job.
