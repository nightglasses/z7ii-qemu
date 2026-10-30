# Refmap frontier — research gaps (curated)

Generated from `REFS.tsv` + `chains/*.gaps`. Full hypothesis list is in `REFS.tsv`.

## Chain gaps (code recovered, not naturally executed)
### boot-to-app-start
- `0x1020d5fa` app-service message entry
- `0x102017a0` unknown
- `0x1020d41a` unknown
- `0x1020d384` unknown
- `0x1038b7b2` unknown

### display-first-frame
- `0x104d7c60` provider list/event producer
- `0x104db90e` normal main restart callsite
- `0x20145200` display bank word (observed 0x12 = paused)
- `0xcb4a7000` YUVA107 primary plane

### input-to-shutter
- `0x10764958` shutter request executor
- `0x103ba164` InitiateCapture

### provider-admission
- `0x1020d5fa` app-service message entry
- `0x10773ed4` F165 bootstrap handler
- `0x10502db8` F092 manager setter handler
- `0x10502d0a` F091 admission handler
- `0x104d7e78` provider admission wrapper
- `0x104d7d7e` provider domain dispatcher
- `0x104d7c60` provider list/event producer
- `0x1051d506` provider object builder
- `0x104d731c` service event/list publisher
- `0x104dc14e` owner mailbox method
- `0xb1d020a0` pending provider mailbox
- `0x104dc804` owner lifecycle callback
- `0x104dc690` owner state 25
- `0x104dc6c0` owner state 26
- `0x104dbb40` provider type-1 import
- `0x104db90e` normal main restart callsite

### readout-liveview
- `0x0410001b` ?
- `0x041000ac` ?
- `0x103f1244` capture-service start
- `0x103263e8` capture state callback
- `0x102347b6` MAS unknown (EX-input issue context)
- `0x1023869e` readout descriptor builder
- `0x1020c7c6` readout service submit
- `0x2006b400` readout generation marker

## Meaningful hypothesis/unknown nodes (18)
- `0x8020188` (EX) unknown (EX thread5 PC at narrow stop) — observed terminal PC, role unknown  [run ex-battery-runtime/receipt.md]
- `0x8032c39` (EX) EX system-kind3 builder r0 argument (code pointer) — r0 value at EX_SYSK3_BUILDER; role unstated  [run lcd-normal-parent/parent-combined-ex-mas-a546016/gdb.log]
- `0x80425a2` (EX) unknown (EX W4 to 0x4002642c SPI4 TX NDTR) — EX cpu=4 pc=0x080425a2 unknown W4 @0x4002642c; role unstated  [run lcd-normal-parent/resume-frontier-head-120/qemu.log]
- `0x102347b6` (MAS) MAS unknown (EX-input issue context) —   [run ex-battery-runtime/battery-current.txt run analysis-gui-programs/receipt.md,run analysis-gui-programs/static-anchors.txt run analysis-readout-gate/receipt.md]
- `0x103299c6` (MAS) MAS unknown (EX-input issue context) —   [run ex-battery-runtime/battery-current.txt run analysis-readout-gate/receipt.md]
- `0x1032a3ba` (MAS) Goodix GT9 panel-ID dispatch — accepts touch model bytes  [run analysis-readout-gate/receipt.md]
- `0x103f9577` (MAS) battery classifier r1 argument — r1=0x103f9577 at BATTERY_CLASSIFIER; role unstated  [run lcd-normal-parent/parent-combined-ex-mas-a546016/gdb.log]
- `0x104343c6` (MAS) cmd8 shutter frame handler — EX kind-8/top-key candidate  [run analysis-readout-gate/receipt.md]
- `0x104486f8` (MAS) pause-path probe site (no hit recorded) — breakpoint site in pause-owner.gdb; role unstated  [run lcd-normal-parent/pause-owner.gdb]
- `0x10449f8a` (MAS) pause-path probe site (no hit recorded) — breakpoint site in pause-owner.gdb; role unstated  [run lcd-normal-parent/pause-owner.gdb]
- `0x1044a08a` (MAS) pause-path probe site (no hit recorded) — breakpoint site in pause-owner.gdb; role unstated  [run lcd-normal-parent/pause-owner.gdb]
- `0x10620721` (MAS) Thumb reference-scan target (helper script constant) — TARGETS={0x10620720,0x10620721} in scan script; role unstated  [run lcd-normal-parent/find_thumb_address_refs.py]
- `0x106ee3e2` (MAS) IRQ131 handler — Handler/producer is not established by unchanged execution.  [docs/devices/transfer/protocol.md]
- `0x10764958` (MAS) shutter request executor — builds shutter request  [run analysis-readout-gate/receipt.md]
- `0x1d000000` (MAS) historical CBAR candidate (superseded by 0xf8100000) — caused DFAR 0x1d001000/0x1d002004 faults  [docs/startup/peripherals.md]
- `0x131` (QEMU) GIC IRQ131 unknown graphics producer — Transfer IRQ output 2 is held low; producer remains unknown.  [hw/nikon/devices/transfer.c,docs/devices/transfer/protocol.md]
- `0x1e800000` (QEMU) Socionext m10v SPI reference base (3 channels) — candidate IP header only; Nikon 11 channels and 60/64 extensions are a version hypothesis  [docs/devices/spi/protocol.md]
- `0x2018001c` (QEMU) IRQ131 ACK — Address is transfer setup offset 0x1c and the named IRQ131 ACK; unsupported IRQ131 path.  [docs/devices/transfer/protocol.md]
