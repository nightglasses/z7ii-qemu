# EX analog input contract

The machine no longer accepts a command-line ADC override. The complete bundle
composition installs the deterministic EX board-input record declared in
`hw/nikon/machines/z7ii.c`; the EX device still owns conversion, EOC, IRQ and
reset behavior. A stored sample does not complete a conversion until the
unchanged guest requests one.

Direct EX device tests may connect or disconnect individual ADC channels through
`nikon_ex_adc_input()` to exercise the source-backed peripheral contract. Those
tests are device fixtures, not machine startup inputs and do not imply a
physical voltage or battery state.
