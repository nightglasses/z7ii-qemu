# Synthetic virtual deadlines and observed failures

This historical change repairs synthetic-test wait budgets only. Device C,
original MAS timing, the5-second host acceptance deadline and icount shift0
remain unchanged. Fixture code is
[fb05c3f40a049e4cc9f3c0aa84237c2b7e5ea114](https://github.com/mashiro01/nightglass-qemu/blob/fb05c3f40a049e4cc9f3c0aa84237c2b7e5ea114/scripts/device-smoke-test.py);
the then-maintained path was scripts/devices.py. Four-byte generated
instruction inputs are synthetic, not recovered firmware.

Actions run37840824780 built, then failed timing-reset-cancels-pending with
final loop not reached. Its uploaded synthetic.bin matched generator
2dd05337a79093039d065702a88a5b70970d1b20 byte-for-byte. Uploaded logs lack
the failure PC, so the exact Linux stop cannot be inferred.

A read-only replay of that input found CNTP_CTL polling at
100200f4/100200f8/100200fc after the old2ms settle. It reached final
10020118 with R1=5 and normal exit after3.678845 host seconds. Replay used
native578f2610ff7697e9be3c00ecaffdb88df126f975, not the remote publication
binary. There was no transaction error.

With [icount](https://github.com/qemu/qemu/blob/4fc49f46dc95d4a27de2509e7fceb2931e91faeb/docs/devel/tcg-icount.rst),
virtual nanoseconds follow instruction execution; long busy loops amplify
host work. Host and virtual deadlines are different. Each budget follows
the actual Clock/event implementation. Cancel/pause occurs before completion;
checks occur after the original completion deadline. Success still polls
causally produced status rather than assuming it after a sleep.

| Fixture family | Model deadline/order | Observation budget |
|---|---|---|
| SDI arbitration reset |10us deferred event; reset pending, or await actual done before reset |20us/CNTP960 ticks; unchanged here |
| USIO reset pending |BGR363,independent100MHz,8N1=86.8us; UPCL cancels earlier |100us/4800 ticks, beyond old TX deadline |
| SPI disconnected slave |No external clock, no TX dequeue/RX/done |20us/960 ticks |
| SPI reset pending |DIV7,16-bit,48MHz=5.334us; reset then reopen |20us; preserve retention/FIFO/pending assertions |
| I2C timeout disable |Actual ACK/HOLD; timeout15=7.334us; write0 cancels |20us beyond old deadline, still HOLD, then re-enable real timeout |
| I2C deadline replace |Old timeout2=1.375us; replacement31=14.667us |Check old deadline did not terminate at10us; poll new timeout |
| Twelve-channel timing |Independent3GHz,period29999=10us; reset/disable/bitmap-pause before expiry |20us; one-shot no restart,periodic two actual expiry/ACKs,frozen counts/IRQ183 retained |
| Audio/HDMAC demand |Default opaque-word drain64/48MHz=1.334us; DREQ/capacity per beat; at most two words in each of two queues |20us; retain pressure/cancel/reset/disable/multichannel/missing sample assertions |
| Four-channel copy cancel |513 bytes,<=256 per chunk; full request about10.7us at48MHz; cancel before first chunk |20us; unchanged destination and cancel/error status still checked |

These are implementation timings from usio.c,spi.c,i2c.c,timing.c,audio.c,
hdmac.c and copy.c in hw/nikon/devices, not physical baud/latency/audio or
EXPEED measurements. The shortened timing period also changes its one-shot
count expectation to30000, without relaxing assertions.

Other CNTP waits were audited separately. secure-timer/active-init retain48000
ticks for the explicit48MHz/1ms/PPI29 baseline; timestamp-whole-guest-init
retains its original48000 ticks. timestamp-elapsed-freeze-carry keeps freeze,
32-bit carry and elapsed>=4800; clock-pll-start keeps non-icount lock testing.
Other completion/MXIC/SPI/HDMAC/I2C/copy paths use actual status/IRQ without
extra1ms settle. No global icount quantum change merges low-latency ordering.

Exactly21 fixtures whose generated instructions/expectations changed passed
in1.953951 seconds. Native578f261 also contained later finite NOR/ring code,
which these cases did not use. Fixturefb05c3f used upstream
4fc49f46dc95d4a27de2509e7fceb2931e91faeb. Initial resource-path setup failed
before QEMU started; after fixing the frozen generator's resource paths, the
21 cases ran. Older receipts were preserved. Full CI and original MAS were
not repeated for this budget change.

A future normal/reset entry missing its final loop within5 host seconds
records phase, expected loop PC, actual last registers and QMP status in a
failure receipt and remains failed. This adds diagnostics, not longer waits,
retries or fallback success.
