# Z7II EXPEED 6 image-processing architecture

Nikon publicly states that the Z7II uses two EXPEED 6 image-processing
engines. Nikon publishes camera capabilities and external interfaces, but does
not publish an EXPEED 6 RTL block diagram or internal firmware ABI. The diagram
below therefore separates public product facts, QEMU source-backed interfaces,
and functional roles that remain hypotheses until C1.70 runtime evidence binds
them to a producer and a buffer.

## End-to-end image path

```mermaid
flowchart TD
  Light[Lens / Z mount optics]
  Sensor[45.7 MP FX BSI CMOS\npublic camera specification]
  Analog[Analog front end / ADC\ninternal implementation unknown]
  Timing[Exposure and sensor timing\nCMOS control]
  Ingest[Sensor ingest and DMA\nfunctional role hypothesis]

  subgraph Dual[Dual EXPEED 6 image-processing engines]
    E0[EXPEED 6 engine A\npublic: one of two engines]
    E1[EXPEED 6 engine B\npublic: one of two engines]
    Dispatch[Frame / burst scheduler\nfirmware role hypothesis]
    ISP[Image-processing stages\nfunctional roles, silicon blocks unknown]
    AF[AF / subject detection\npublic capability, internal path unknown]
    Color[Color / tone / noise / lens corrections\nfunctional roles, internal order unknown]
    Scale[Resize / preview composition\nfunctional role hypothesis]
    Codec[Still / movie encode\nJPEG/NEF/HEVC roles not mapped to blocks]
    E0 --> ISP
    E1 --> ISP
    Dispatch --> E0
    Dispatch --> E1
    ISP --> AF
    ISP --> Color
    Color --> Scale
    AF --> Dispatch
    Scale --> Codec
  end

  Buffer[External memory and frame buffers\nDDR / source-backed arenas]
  Display[EVF / LCD / HDMI\nscanout and output]
  Cards[CFexpress/XQD + SD\nstorage endpoints]
  USB[USB / external recorder]

  Light --> Sensor
  Sensor --> Analog
  Analog --> Ingest
  Timing --> Sensor
  Ingest --> Dispatch
  Ingest --> Buffer
  E0 --> Buffer
  Buffer --> E0
  E1 --> Buffer
  Buffer --> E1
  Codec --> Buffer
  Scale --> Display
  Codec --> Cards
  Codec --> USB
```

### Evidence tiers

| Block or edge | Status in this project |
|---|---|
| Dual EXPEED 6 engines | Public Nikon fact; internal partition is unknown |
| Z7II sensor resolution and external formats | Public Nikon specification |
| CMOS exposure/readout control | QEMU contract exists; original C1.70 producer and completion edge remain open |
| DMA and frame buffers | QEMU transport models exist; original framebuffer producer is not yet observed |
| AF / eye / animal detection | Public camera capability; internal EXPEED block and firmware path unresolved |
| Demosaic, denoise, color, tone, lens correction | Functional roles only; no public Nikon block map |
| JPEG/NEF/movie encoding | External behavior is public; internal engine ownership and codec path unresolved |
| EVF/LCD/HDMI scanout | Output capability is public; original scanout callback/framebuffer not yet observed |
| Card and USB save paths | QEMU endpoints are testable; original C1.70 save producer remains unjoined |

## QEMU evidence boundary

```mermaid
flowchart LR
  CMOS[CMOS endpoint\nmode/crop/scale/format/timing]
  Capture[capture-control]
  Readout[readout-control]
  DMA[DMA / buffer rotation]
  Process[processing boundary]
  Frame[frame descriptor]
  Scanout[scanout / GUI]
  Save[save / storage]

  CMOS --> Capture --> Readout
  Readout -. original producer unknown .-> DMA
  DMA -. original source buffer unknown .-> Process
  Process -. framebuffer geometry unknown .-> Frame
  Frame -. completion / scanout unknown .-> Scanout
  Frame -. file commit unknown .-> Save
```

The current QEMU model is deliberately modular: CMOS, readout, DMA, processing,
display, and save each have a focused contract. A synthetic frame or an 8x8
transport fixture is not evidence that the EXPEED 6 image path has been
recovered. The next runtime milestones are the original DMA producer, buffer
geometry, completion event, scanout callback, and first real framebuffer.

## Public references

- [Nikon: Dual EXPEED 6 Q&A](https://nij.nikon.com/support/faq/products/article?articleNo=000059926)
- [Nikon USA: Z7II/Z6II launch release](https://www.nikonusa.com/press-room/z7ii-z6ii)
- [Nikon Z7II technical specifications](https://www.nikonusa.com/p/z-7ii/1653/overview)

These sources establish the external product claims only. They do not identify
the internal EXPEED 6 block diagram, memory protocol, firmware task ownership,
or ASIC implementation.
