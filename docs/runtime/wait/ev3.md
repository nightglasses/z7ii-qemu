# EV3 producer boundary

This receipt records a fresh read-only run of the unchanged Z7II C1.70 MAS
payload. It isolates the current GUI-phase event producer and separates it from
the earlier EV88 wait.

## Run contract

- Payload: `02_eg1840_mas_01700.payload.bin`, 38,239,096 bytes, unchanged.
- Machine: `z7ii-mas-diagnostic`, two MAS Cortex-A7 CPUs, multi-thread TCG.
- Observer: GDB remote breakpoint at the event-set wrapper and a read-only
  watchpoint on the EV3 control-block pattern field.
- The observer wrote no event bits, IRQ state, ready cells, task fields, or
  device state. Debugger files remain ignored under `work/`.

## Direct producer observation

The event-set wrapper `0x103610cc` was entered on MAS CPU1 with:

```text
r0              0xb185e12c
*(uint32_t *)r0 0x01000003
r1 (mask)       0x00000002
lr              0x10200ceb
```

The handle cell therefore resolves to event ID `0x01000003` (EV3). The caller
return address is inside the role-0 peer callback at `0x10200d06`, matching the
source-side callback boundary. At wrapper entry the EV3 control block was:

```text
0xb2c55848: 0xb2c33008 0xb2c33290 0x01000003 0x00000000
0xb2c55858: 0x00000008 0x00000000 0x00000000 0x00000000
```

The linked waiters are guest task objects; the host did not add them. After
letting the guest continue, the same control block reached pattern `0x5` in a
separate read-only snapshot. This is consistent with the peer producer setting
the event and the waiter state advancing.

## Wait setup versus producer

The first write observed at the kernel event routine `0x101111c8` had:

```text
value 0x00000002
lr    0x1011ea2c
```

That write belongs to the wait setup path. It is not the producer. The current
wait task `0xb2c33368` later contains event ID `0x01000003`, output
`0xb51bb508`, requested pattern `0x10`, and mode `1`; its control block is
`0xb2c55848`. The separate CB `0xb2c568e8` has ID `0x01000088` (EV88) and is
not the current GUI-phase producer target.

## Model boundary

This run proves the guest-side producer edge:

```text
role-0 peer callback 0x10200d06
        -> event-set wrapper 0x103610cc
        -> EV3 handle cell 0xb185e12c
        -> event ID 0x01000003, mask 0x2
```

It does not prove the physical peer-ready/reply source before the callback.
The next required evidence is the original guest GPIO/bridge/USIO path that
enters `0x10200d06`. QEMU must keep that path source-bound and must not inject
EV3, an IRQ, a ready task, or a synthetic GUI frame. No QEMU model change is
justified by this receipt.

