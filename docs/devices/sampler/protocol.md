# Requested word sampling

The device owns `20201000..202011a3`, with its board IRQ connected to GIC116. The ec048f saved native source closes initializer, configuration, activation, ISR and the software event consumer. Original entries include `10294364` (ISR), `102944a6` (configuration), `102946ae` (request), and `10244a62` (request/wait consumer). Their complete original bodies are indexed static evidence; a physical image source has not been identified.

`102946ae` calls command1, which writes `44=3`, `48=7`, then writes `14=1`. The model implements this as one outstanding sample request. Named `sample-request` GPIO rises after this sequence and falls when an explicit provider completes the word. No provider is connected by default. A request alone creates no data, pending, IRQ or software event.

The provider writes all32 `data-bit` GPIO inputs after each request, then raises `external-event[0]`. Only fresh complete publication while armed atomically locks the word into1a0 and latches pending2c bit0. Partial, unsolicited and stale publications cannot complete it. Held-high inputs create no new edge. This handshake is a functional convention; the source does not identify physical lanes, packing or a sensor/ISP join.

ISR `10294364` reads2c, writes the same raw value back, then reads1a0 if bit0 was present. ACK therefore retains the sample. Explicit external events1/2 latch raw bits1/6 without inventing data. Modeled pending is W1C for bits0/1/6 and drives GIC116. Offset20 remains a guest parameter, with no inferred interrupt mask.

The ISR's bits1/6 branches and packet configurator write `00=1` followed by0. These controls are accepted and retained, without inferred reset, clearing or completion effects. They preserve sample and pending state. Object callbacks and later RTOS flag operations execute in guest code; the model never writes callbacks, flags or objects.

| Aligned32-bit fields | Software-generated values |
|---|---|
| 00,04,4c,c4,11c,164 | bool |
| 60 /b8,bc,c0 /100 | low8 /low11 /bits0,2,3 |
| 20,50,54,5c,64,c8,cc,104,108,10c,110,118,120,124,128,12c,160,168,16c,170 | fullword |
| a0..b4,130..14c,178..194 | finite fullword arrays |
| 44 /48 | independent selections1/2/3 and0/1/7 |

Reset clears configuration, pending, validity, request and publication tracking. Actual input levels survive without an invented edge. Cold zero, readback, handshake and W1C are functional conventions rather than physical reset claims. Unknown accesses and widths fail explicitly; the device is unmigratable.

Native regression `tests/qtest/nikon/sampling.c` covers requested publication, stale/unsolicited rejection, ACK-before-data retention, original00 pulses, rearm and freshness through device GPIOs and the board GIC. Build/run evidence belongs to the sole native builder's fixed batch receipt; no run is claimed before it.
