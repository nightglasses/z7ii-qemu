# EX DMA FIFO control writes

The EX functional profile accepts FIFO error interrupt enable changes while a
DMA stream is active. Protected FIFO configuration writes retain the current
configuration instead of raising a CPU bus fault. This is a finite STM32F4
software protocol model, not evidence of the physical camera's DMA identity or
FIFO timing.

The original C1.70 EX image, `ex1840_013000.bi`, contains a DMA start helper at
EX VA `0x08043760`. Its read/modify/write at EX VA `0x080437bc` sets FCR bit 7,
following a CR interrupt-enable update and preceding the final CR enable write.
A local instrumented run observed a rejected word write at this instruction to
EX PA `0x40026424`, the modeled DMA2 stream 0 FCR. Subsequent GPIO initialization
and an empty input callback cache are consistent with recovery after the bus
fault; the reset-triggering instruction was not captured in that run.

The model follows [ST RM0090 Rev 21, section 10.5.10, pages 336–337](https://www.st.com.cn/resource/en/reference_manual/dm00031020-stm32f405415-stm32f407417-stm32f427437-and-stm32f429439-advanced-armbased-32bit-mcus-stmicroelectronics.pdf):

| Field | Software write behavior |
| --- | --- |
| FEIE, bit 7 | Writable with either EN value; recompute the existing IRQ mask |
| FS, bits 5:3 | Read-only; retain the modeled status |
| DMDIS/FTH, bits 2:0 | Writable with EN=0; retain their values with EN=1 |
| Reserved bits | Nonzero writes remain unsupported errors |

An actual memory-to-memory enable sets DMDIS. Reset restores FCR to `0x21`.
Only aligned word accesses to the existing DMA aperture are accepted. Changing
FEIE does not create a transfer, completion flag, or IRQ. The current DMA model
does not simulate physical FIFO occupancy or generate new FIFO error causes.

The focused unit cases `/nikon/ex/dma-fcr-fields` and
`/nikon/ex/dma-native-feie-transfer` cover both banks' field protection and
reset, negative access boundaries, the original helper's MMIO ordering, and
actual SPI-requested DMA bytes followed by the existing completion IRQ. The
second fixture replays the register transactions; it does not execute factory
code. These fixture checks are separate from an original-firmware rerun and
physical-camera validation.
