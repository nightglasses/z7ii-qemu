# EX key assistance profile

This profile describes how a host controller drives the seventeen EX key pads.
It is an **explicit external input**: the model only converts host keys into the
pad levels the EX firmware already scans.  It creates no host command, no reply,
no ready/status bit and no frame.

## Mapping

`hw/nikon/devices/input.c` maps seventeen host keys onto the EX GPIO pads the
original software selects:

| Host key | EX pad | Firmware Identity | Host key | EX pad | Firmware Identity |
| --- | --- | --- | --- | --- | --- |
| F1 | `0x0a` | Unbound | F10 | `0x61` | Shutter S1 (half-press) |
| F2 | `0x11` | Unbound | F11 | `0x62` | AF-ON button |
| F3 | `0x1f` | Unbound | F12 | `0x6d` | Shutter S2 (full-press) |
| F4 | `0x25` | MAIN power switch | 1 | `0x6e` | Unbound |
| F5 | `0x1b` | Unbound | 2 | `0x6f` | Unbound |
| F6 | `0x47` | Dial1 contact A | 3 | `0x1d` | Unbound |
| F7 | `0x4d` | Dial1 contact B | 4 | `0x53` | Unbound |
| F8 | `0x5b` | Dial0 contact A | 5 | `0x1c` | Unbound |
| F9 | `0x5e` | Dial0 contact B | | | |

Pads are active low: a pressed key drives level 0, a released key level 1.
From original EX firmware inspection (`0x0804e458` button scan table and
`0x0803dd00` scanner):
- `0x61` (PG1, pad 9, `KEY_F10`) is the physical shutter half-press contact (S1).
- `0x62` (PG2, pad 10, `KEY_F11`) is the physical AF-ON button contact.
- `0x6d` (PG13, pad 11, `KEY_F12`) is the physical shutter full-press contact (S2).
  In the scanner, S2 is conditionally qualified by S1 (`cmp r4, #2; ldrb r1, [r6, #1]; cbz r1 -> force to 0`).
- `0x25` (PC5, pad 3, `KEY_F4`) is the physical `MAIN` power switch contact (SW0 `MAIN`, `0x200`),
  NOT a shutter half-press contact.

In addition to host keys, `/machine/input` exposes direct boolean QOM properties:
`half-press` / `shutter-half-press`, `full-press` / `shutter-full-press`, `af-on`,
and `main-power`. Setting these updates the corresponding active-low EX pad levels.

## Driving it

With a QMP socket on the machine:

```sh
qemu-system-arm -machine z7ii-mas-diagnostic,bundle=<container>,... \
                -qmp unix:/tmp/nikon.qmp,server,nowait -display none ...
```

```python
{"execute": "qmp_capabilities"}
{"execute": "input-send-event",
 "arguments": {"events": [{"type": "key",
                            "data": {"down": True,
                                     "key": {"type": "qcode", "data": "f1"}}}]}}
```

`run ex-6b-ercd/run-assist.sh` performs four press/release rounds over
`F1..F5` and `1..3` at 8/16/24/32 s, with a `-dfilter` over the display frontier
and a panel screendump at the end.  It is the receipt script for this profile.

## Measured receipts

The channel is verified at the pad level.  The device logs every transition:

```text
nikon-input: pad=0  ex-pad=0x0a down active-low level=0
nikon-input: pad=0  ex-pad=0x0a up   active-low level=1
nikon-input: pad=1  ex-pad=0x11 down active-low level=0
nikon-input: pad=12 ex-pad=0x6e down active-low level=0
```

The guest does not act on those pads in this boot.  Over the same 60 s run the
display frontier stays cold - EGL present `0x10568F60`, GUI event dispatcher
`0x1053E19A`, display owner `0x104DC804`, banks start `0x1046BF0C`, mailbox post
`0x104BD99E`, frame queue `0x105A6906`, service router `0x104C6B14` and the F091
handler `0x10502D0A` all record 0 - while the app keeps drawing (27 times), and
the scanned panel frame stays black.

So this profile is a working input channel whose firmware consumer is inactive;
it is not evidence of a button-driven UI and not a frame.

## Physical dial transition receipts

The reported Nikon Z 7II (`V1.60`) dial session records PTP property-change
notifications through interrupt endpoint `0x83` (`EP_INT`) and vendor polling
operation `0x90C7` (`GetEvent`). These are host-level notifications, not an
electrical contact trace:

```text
[02:34:48] >>> [0x90C7 Vendor] ExposureTime (Shutter Dial) CHANGED: 2500 -> 3333
[02:34:48] >>> [EVENT 0x4006] ExposureTime (Shutter Dial) CHANGED: 3333 -> 2500
[02:34:50] >>> [EVENT 0x4006] ExposureTime (Shutter Dial) CHANGED: 2500 -> 2000
[02:34:50] >>> [EVENT 0x4006] Prop 0x501C CHANGED: FocusMode updated
[02:34:51] >>> [EVENT 0x4006] ExposureTime (Shutter Dial) CHANGED: 2000 -> 2500
[02:34:51] >>> [0x90C7 Vendor] ExposureTime (Shutter Dial) CHANGED: 2500 -> 2000
```

The reported shutter values correspond nominally to `1/40s`, `1/30s` and
`1/50s`. They are consistent with ordinary shutter adjustment but do not count
physical detents, prove one-third-EV motion per contact cycle or identify EX
GPIO wiring. Polling and interrupt delivery can observe the same change at
different times; these timestamps do not measure contact phase duration.

`hw/nikon/devices/input.c` separately declares four-phase sequences and a
50 ms virtual interval per phase. Its dial0 uses pad-array indices 7/8, mapped
to EX GPIO hexadecimal IDs `5b/5e`; dial1 uses indices 5/6, IDs `47/4d`. These are model
input choices, not physical measurements derived from PTP. A synchronized
contact trace and property observation would be needed to establish the
physical sequence, direction, debounce and timing.

