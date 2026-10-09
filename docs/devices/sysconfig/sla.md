# SLA system-configuration registration and control

Official mode1 registration `100548FE → 10067600` supplies object152 to
`1006664E` with literal `100676FC=C1006488`. It initializes five software
mutexes from `10067700=C144DC8C`; these are guest RAM objects. The matching
initializer `10054952 → 10067626` is a complete `BX LR`. No interrupt
definition, interrupt enable, MMIO bootstrap write or physical completion
producer appears in those two complete functions.

The complete `100676C2..100676FA` takes index in `r1` and boolean request
in `r2`, splits the index into `bank=index>>5` and `bit=index&31`, locks
the corresponding guest mutex, reads a full word, sets or clears one bit,
writes the full word, and unlocks. Literal `10067704=04280004` plus `74`
selects `04280078+bank*4`; thus the existing `TYPE_NIKON_SYSCONFIG`
region must be mapped at `04280000`, not at the literal's `04280004`.

Complete callers include `10059F26/10059F3C` for index7B, the paired
`10093EA0/10093EC4` calls for index6D, the `100956B2..100956FE` calls
for indices6A/6B/6C, and `10095D62..10095DC2` calls for indices66..69. These are
set/clear request-state operations. The general helper is not proof of
unbounded MMIO or additional physical effects. The existing finite model
retains five full words at offsets78..88 and already supports these RMW
operations without clearing other bits or synthesizing an IRQ or reset.
No production change is needed for this source-proved interface.

The recovered functions and literals are preserved in officialsource5
`sla.S` and `sla-rows.jsonl`, source HEAD
`8c451b553c582b8fd30224e3347e30cc602eea83`. Missing
`1006762C..100676C1` bodies remain unqualified in officialsource10; the
sole source consumer has the family supplement request and requires a real
method-table/BLX or natural execution witness. Existing MAS direct fields
and their recorded functional reset assumptions are not widened by this
audit. This is a proved reuse of the finite RMW interface, not proof of all
SLA configuration variants or module gating behavior.

The fresh private-SLA native helper in `tests/qtest/nikon/status.c` checks
five independent banks, actual source indices7B/6D and preservation of
unmodified bits during set/clear. The sole builder owns its build and run;
no new native result is claimed here.
