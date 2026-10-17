# Synthetic EX factory-config profile

The C1.70 update contains the complete 512 KiB EX component, but its factory
configuration window at `0x08004000..0x08020000` is erased in that payload.
The EX firmware nevertheless reads this window during cold start.  The
`power-on-v1` profile supplies a deterministic **synthetic external input**
for that missing board data; it does not modify the original component or
claim to recover a Nikon factory dump.

The existing `functional-gui-v1` profile selects it automatically.  An
explicit selection is also available:

```text
-machine z7ii-mas-diagnostic,bundle=<decoded-container>,\
  ex-config-profile=power-on-v1
```

The profile is intentionally separate from the EX firmware loader.  At
realization it copies the immutable component to a private flash image, fills
the external window with deterministic calibration bytes, writes the `MAKE`
validator marker, clears the channel-0 enable byte used by the power snapshot,
and enables the statically observed power predicate gate at payload offset
`0x422d` (`VMA 0x0800422d`); the main config view begins at payload offset
`0x4010`.  The guest still performs all validation, GPIO
polling, ADC conversion, debounce, event generation and report construction.

For a file supplied through `ex-config-input`, the file begins at the id0
window (`0x08004000`), so `config[276]` is file offset `0x124` and
`config[541]` is file offset `0x22d` (the extra `0x10` is the id1 view base).

`ex-config-input=FILE` remains available for a later complete external block;
`ex-config-bytes=off:value;...` remains the narrow override mechanism.  When
combined, the order is profile baseline, file block, then byte overrides.  This
allows a focused experiment without baking a synthetic value into the EX
payload.  For a functional GUI run, no extra config option is needed.  Passing
`ex-config-profile=none` explicitly disables the automatic synthetic input;
outside `functional-gui-v1`, `none` remains the default and preserves
unchanged-firmware behavior.

The profile is only the configuration side of the boundary.  The board inputs
remain explicit independent properties: `ex-gpiod13=on` for the early
configured-path wait, `ex-power-good=on` for the low-active P0.10/P1.1/P6.2
power predicate, and `ex-adc-counts` for the battery voltage lane.  A profile
selection is therefore a synthetic-input run, not evidence that the original
erased update image naturally reaches the GUI.
