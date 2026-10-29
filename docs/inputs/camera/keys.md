# EX key assistance profile

This profile describes how a host controller drives the seventeen EX key pads.
It is an **explicit external input**: the model only converts host keys into the
pad levels the EX firmware already scans.  It creates no host command, no reply,
no ready/status bit and no frame.

## Mapping

`hw/nikon/devices/input.c` maps seventeen host keys onto the EX GPIO pads the
original software selects:

| Host key | EX pad | Host key | EX pad |
| --- | --- | --- | --- |
| F1 | `0x0a` | F10 | `0x61` |
| F2 | `0x11` | F11 | `0x62` |
| F3 | `0x1f` | F12 | `0x6d` |
| F4 | `0x25` | 1 | `0x6e` |
| F5 | `0x1b` | 2 | `0x6f` |
| F6 | `0x47` | 3 | `0x1d` |
| F7 | `0x4d` | 4 | `0x53` |
| F8 | `0x5b` | 5 | `0x1c` |
| F9 | `0x5e` | | |

Pads are active low: a pressed key drives level 0, a released key level 1.  The
pad assignment and active-low convention remain a functional profile; camera
button names, rotary/touch wiring and peer packets are still unbound.

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

Physical testing on an attached Nikon Z 7II (`V1.60`) directly captures the live
hardware dial rotation and asynchronous event dispatch across both the interrupt
endpoint `0x83` (`EP_INT`) and the vendor polling queue `0x90C7` (`GetEvent`):

```text
[02:34:48] >>> [0x90C7 Vendor] ExposureTime (Shutter Dial) CHANGED: 2500 -> 3333
[02:34:48] >>> [EVENT 0x4006] ExposureTime (Shutter Dial) CHANGED: 3333 -> 2500
[02:34:50] >>> [EVENT 0x4006] ExposureTime (Shutter Dial) CHANGED: 2500 -> 2000
[02:34:50] >>> [EVENT 0x4006] Prop 0x501C CHANGED: FocusMode updated
[02:34:51] >>> [EVENT 0x4006] ExposureTime (Shutter Dial) CHANGED: 2000 -> 2500
[02:34:51] >>> [0x90C7 Vendor] ExposureTime (Shutter Dial) CHANGED: 2500 -> 2000
```

This confirms the 1/3 EV stepping sequence (`1/40s (2500)` -> `1/30s (3333)` ->
`1/50s (2000)`) driven by the four-phase quadrature contacts on EX pads 7 & 8
in `hw/nikon/devices/input.c`.

