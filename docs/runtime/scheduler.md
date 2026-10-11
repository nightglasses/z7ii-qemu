# Raw MAS scheduler affinity

This receipt records a fresh read-only run of the C1.70 raw MAS payload against
the QEMU main machine. It answers whether the empty successor observed on the
secondary MAS CPU is a missing QEMU ready producer.

## Run contract

- Payload: `02_eg1840_mas_01700.payload.bin`, 38,239,096 bytes, read-only.
- Machine: `z7ii-mas-diagnostic`, two MAS Cortex-A7 CPUs, multi-thread TCG.
- QEMU binary: `work/build/qemu/qemu-system-arm` built from the current QEMU
  main tree (`e2ae6263`).
- Observation: GDB remote read-only breakpoints at `0x10107098` and register/
  memory reads only. No guest memory, event flag, IRQ, ready queue, or device
  state was written.

## Direct observations

The first natural successor branch was taken by GDB thread 1, identified by the
stub as MAS CPU0:

```text
PC       0x10107098
R0/R8    0xb2c33008
task id  0x01000001
entry    0x10103bc8
```

At that stop the per-core scheduler cells were:

```text
b2b41604: 0xb2c33008 0x00000000 0x00000000 0x00000000
b2b42258: 0x00000000 0x00000001 0x00000000 0x00000000
b2b42308: 0x00000000 0x00000000 0x00000000 0x00000000
b2b415f4: 0xb2c33008 0x00000000 0x00000000 0x00000000
```

After the initial task ran, the next non-initial successor was again selected
by CPU0:

```text
PC       0x10107098
R0/R8    0xb2c34f40
task id  0x01000026
entry    0xb20a1710
```

The second stop still showed CPU1 in the idle scheduler (`0x10107094`), while
CPU0 owned the selected task:

```text
b2b41604: 0xb2c34f40 0x00000000 0x00000000 0x00000000
b2b42258: 0x00000000 0x00000001 0x00000000 0x00000000
b2b42308: 0x00000000 0x00000000 0x00000000 0x00000000
b2b415f4: 0xb2c34f40 0x00000000 0x00000000 0x00000000
```

The scheduler selector at `0x10107094` calls `0x1011e7b8` with the CPU index.
That function indexes the per-core task arrays; the selected task is returned
through `R0`, and `0x10107098` is reached only when that return is nonzero.

## Interpretation

The observed empty successor on CPU1 is a CPU-affinity/startup handoff state.
The main CPU creates and selects the initial tasks; CPU1 remains in the common
dispatch loop until a task assigned to that core becomes ready. The same run
produced two real CPU0 successor tasks without any QEMU-side event or ready
queue mutation. Therefore the empty CPU1 successor is not evidence of a
missing generic QEMU ready producer and does not justify changing scheduler
behavior.

The next runtime boundary remains the first task or peer path that should make
a CPU1-affined task ready. Evidence required before a QEMU change is a guest
write to the relevant ready/task cell or an unsupported MMIO access on that
path.

## CPU1 candidate watch

A follow-up run placed a read-only GDB watchpoint on the CPU1 candidate cell
`0xb2b41608` and counted 200 natural entries at `0x10107094`. The watchpoint
never fired. At the bounded stop the cells were:

```text
b2b41604: 0xb2c34f40 0x00000000 0x00000000 0x00000000
b2b42258: 0x00000002 0x00000001 0x00000000 0x00000000
b2b415f4: 0x00000000 0x00000000 0x00000000 0x00000000
```

This is a negative observation only: no CPU1 candidate write occurred during
the bounded sample. It does not establish that CPU1 can never receive a task;
the next useful evidence is the original guest producer that should write
`0xb2b41608`, or a real peer/event path that triggers that producer.
