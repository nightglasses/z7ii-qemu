# Synthetic EX factory configuration

The C1.70 update contains the complete 512 KiB EX component, but its factory
configuration window at `0x08004000..0x08020000` is erased in that payload.
The EX firmware nevertheless reads this window during cold start.

For the experimental `functional-gui` machine profile, QEMU installs a small
synthetic configuration structure held as a read-only C data object in the
machine model.  It is not a recovered Nikon factory dump and it never changes
the original EX component.  The object supplies the statically required
`MAKE` marker, deterministic calibration seed bytes, the configured-path
channel fields, and the gate at `0x0800422d`.

The main configuration view begins at `0x08004010` (the adjacent id0 view
begins at `0x08004000`), so the two important payload offsets are:

```text
config[276] -> 0x4124   (selects the power-snapshot path)
config[541] -> 0x422d   (enables the power predicate)
```

The same functional composition carries one deterministic board-input record:
ADC1 channel `0` uses the full-scale raw count `4095` because the configured
power path accepts channel 0 only above the firmware threshold `3819`; channels
`1..3` use raw count `3500`. Expander pads `0`, `6` and `9` are high, GPIOD.13
is high, and the observed active-low power-good wires are held low. These
values are model data for the functional fixture, not recovered factory
calibration or a physical voltage claim.

No external config file, byte patch, profile-version selector, guest event or
report is supplied at runtime; the finite board values above are owned by the
built-in functional fixture.  The guest still performs all validation, GPIO
polling, ADC conversion, debounce, event generation and report construction.
The default/non-functional machine path retains the unchanged erased EX
window.
