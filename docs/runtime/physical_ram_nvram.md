# Physical Camera RAM, NVRAM, and Calibration Ground Truth

This document details the direct extraction of live physical memory, operating
system runtime states, and factory NVRAM calibration structures from an attached
physical Nikon Z 7II (`0x04B0:0x044B`, firmware `V1.60`).

All memory dumps, NVRAM binaries, and metadata manifests are archived in
[`docs/public/evidence/physical/ram_dumps/`](/evidence/physical/ram_dumps/manifest.json).

## Physical memory extraction primitive (0xF032)

Static disassembly of the MAS firmware service table at VA `0x119E405C`
identified subcommand `0xF032`, implemented by validator `sub_104B010A` and
executor `sub_104B020C`.

### Memory range validation (`sub_104B0098`)

The validator enforces a bounded address sanitizer on incoming parameters:

```c
BOOL sub_104B0098(unsigned int start_addr, unsigned int end_addr)
{
    // Prohibits internal crypto/key ranges, secure SRAM, and ROM text:
    if ((end_addr < 0xB2BB0000 || start_addr > 0xB2BB105B) &&
        (end_addr < 0xBAA00000 || start_addr > 0xBB2FFFFF) &&
        (end_addr < 0xB1000000 || start_addr > 0xB17FFFFF) &&
        (end_addr < 0x10000000 || start_addr >= 0x14000000) &&
        start_addr >= 0x04000000)
    {
        // Must not conflict with protected block 8
        if (!sub_103A485C(8u, v6)) return true;
        ...
        return true;
    }
    return false;
}
```

Any request addressing valid DDR SDRAM (`start_addr >= 0x04000000`) outside the
blacklisted apertures passes validation and enters executor `sub_104B020C`:

```c
int sub_104B020C(int req, int ctx)
{
    uint32_t addr = *(uint32_t*)(ctx + 16);  // param[1]: Physical address
    uint32_t len  = *(uint32_t*)(ctx + 20);  // param[2]: Byte count
    return sub_1063C46E(ctx, addr, len, 5000); // Direct USB bulk streamer
}
```

This provides a direct physical memory read primitive over USB PTP Data In,
achieving transfer rates exceeding 16 MB/s (1,048,576 bytes in 0.060 seconds).

## Captured memory datasets

### 1. Multi-core ARM Cortex boot memory (0x04000000)

* **File**: [`dram_boot_04000000.bin`](/evidence/physical/ram_dumps/dram_boot_04000000.bin) (1,048,576 bytes)
* **Base Address**: `0x04000000`
* **SHA-256**: `344205f14f341f0d706f09b1e249bb5c061b78e57928edd5ede0c985ef669344`
* **Architecture**: ARMv7-A 32-bit Cortex-A9 multi-core reset vectors.

#### Instruction and vector analysis

```text
0x04000000: b4 f0 9f e5   LDR PC, [PC, #0xB4] ; Reset vector -> 0x040000C4
0x04000004: fe ff ff ea   B .                 ; Undefined instruction loop
0x04000008: fe ff ff ea   B .                 ; Software interrupt (SWI) loop
0x0400000C: fe ff ff ea   B .                 ; Prefetch abort loop
0x04000010: fe ff ff ea   B .                 ; Data abort loop
0x04000014: fe ff ff ea   B .                 ; Reserved loop
0x04000018: fe ff ff ea   B .                 ; IRQ loop
0x0400001C: fe ff ff ea   B .                 ; FIQ loop
...
0x0400005C: 10 0f 11 ee   MRC p15, 0, r0, c1, c0, 0 ; Read SCTLR
0x0400006C: 10 0f 01 ee   MCR p15, 0, r0, c1, c0, 0 ; Write SCTLR
0x04000070: 6f f0 7f f5   ISB                       ; Instruction barrier
0x04000074: b0 1f 10 ee   MRC p15, 0, r1, c0, c0, 5 ; Read MPIDR
0x04000078: 0f 10 01 e2   ANDS r1, r1, #0xF         ; Extract Core ID
0x0400007C: 00 00 51 e3   CMP r1, #0                ; Primary Core check
0x04000080: db 00 00 1a   BNE loc_secondary_core    ; Secondary core dispatch
```

At `0x040000C4`, the CPU mode stacks are initialized:
* `SVC`: `0x04008080`
* `IRQ`: `0x04008100`
* `FIQ`: `0x04008380`
* `ABT`: `0x04008300`
* `UND`: `0x04008400`
* `SYS`: `0x04008600`

This locks down the exact initial CPU state required by QEMU's TCG/reset loader,
eliminating synthetic stack assumptions.

### 2. MAS operating system runtime globals (0xB188B000)

* **File**: [`dram_mas_globals_b188b000.bin`](/evidence/physical/ram_dumps/dram_mas_globals_b188b000.bin) (8,192 bytes)
* **Base Address**: `0xB188B000`
* **SHA-256**: `94bb4b8c4712fd3b6ebc2aff24c0cf37ac875bab8ace2625ff7915c7bcae72de`

#### Live operating system state verification

Inspection of the dumped memory validates the exact runtime state machine
deduced in [PTP command boundary](ptp.md):

| Address | Sym / Purpose | Observed physical value | Verification meaning |
|---|---|---|---|
| `0xB188B768` | PTPCMD task state byte | `0x01` | Service task active |
| `0xB188BC6C` | PTP session identifier | `0x00000001` | Matches open host session 1 |
| `0xB188BC7C` | Service mode predicate pointer | `0x104798D9` | Points to predicate function |
| `0xB188BC80` | Service mode active flag | `0x00000001` | Mode unlocked by `0xF001` |

## Factory NVRAM and calibration resource blocks

The camera firmware maintains 81 resource blocks indexed via `sub_103A485C`.
Subcommand `0xF029` (`param[1]=offset`, `param[2]=length`, `param[3]=selector`)
and dedicated subcommands expose these blocks directly:

| File | Subcommand / Sel | Size | SHA-256 | Description |
|---|---|---|---|---|
| [`nvram_resource_80_master.bin`](/evidence/physical/ram_dumps/nvram_resource_80_master.bin) | `0xF029 sel=0` | 64 KB | `88ef6c26...` | **Resource 80 Master Calibration**: Optical center coordinates, bad pixel defect maps, dark current offsets. |
| [`nvram_resource_71.bin`](/evidence/physical/ram_dumps/nvram_resource_71.bin) | `0xF029 sel=1` | 64 KB | `3bbf0bd2...` | **Resource 71**: Subsystem parameter block with increasing address calibration records. |
| [`nvram_resource_70.bin`](/evidence/physical/ram_dumps/nvram_resource_70.bin) | `0xF029 sel=2` | 32 KB | `d10b9178...` | **Resource 70**: Analog timing profile table. |
| [`nvram_resource_79.bin`](/evidence/physical/ram_dumps/nvram_resource_79.bin) | `0xF029 sel=3` | 64 KB | `88ef6c26...` | **Resource 79**: Shadow/mirror calibration store. |
| [`nvram_resource_11.bin`](/evidence/physical/ram_dumps/nvram_resource_11.bin) | `0xF029 sel=4` | 64 KB | `9cfff75c...` | **Resource 11**: AFE (Analog Front-End) gain tables and ADC trim. |
| [`nvram_resource_12.bin`](/evidence/physical/ram_dumps/nvram_resource_12.bin) | `0xF029 sel=5` | 64 KB | `5999079d...` | **Resource 12**: Exposure and photometric metering curves. |
| [`nvram_resource_67_afadjust.bin`](/evidence/physical/ram_dumps/nvram_resource_67_afadjust.bin) | `0xF029 sel=6` | 64 KB | `ef9597bf...` | **Resource 67**: Factory autofocus calibration (`"AFadjustkohatsu"` header). |
| [`nvram_block_17_audio.bin`](/evidence/physical/ram_dumps/nvram_block_17_audio.bin) | `0xF021` | 32 KB | `04b2c130...` | **Block 17**: Audio DSP codec tables and gain maps (`"AUDIO"` header). |
| [`nvram_block_53_serial_otp.bin`](/evidence/physical/ram_dumps/nvram_block_53_serial_otp.bin) | `0xF025` | 4 KB | `97a3e75b...` | **Block 53**: Body serial number OTP block (`"8027437"` ASCII string). |
| [`nvram_block_08.bin`](/evidence/physical/ram_dumps/nvram_block_08.bin) | `0xF02D` | 4 KB | `ce20ccc8...` | **Block 08**: Hardware security exclusion boundaries. |
| [`nvram_block_09.bin`](/evidence/physical/ram_dumps/nvram_block_09.bin) | `0xF03D` | 32 KB | `84312500...` | **Block 09**: Power supply and battery curve profiles. |
| [`nvram_block_10.bin`](/evidence/physical/ram_dumps/nvram_block_10.bin) | `0xF03A` | 32 KB | `2d864c0b...` | **Block 10**: Thermal monitor and temperature lookup tables. |

## Subsystem internal firmware versions (0xF003)

Querying subcommand `0xF003` with selectors 1..9 resolves internal module
revisions matching the firmware package hierarchy:

| Selector | Label | Physical string | Subsystem meaning |
|---|---|---|---|
| 1 | `ModelName` | `Z 7_2` | Camera product string |
| 2 | `Subsystem_Boot_A` | `Z 7_2` | Bootloader A core ID |
| 3 | `Subsystem_Boot_B` | `Z 7_2` | Bootloader B core ID |
| 4 | `Firmware_C_Main` | `0160b1` | Main MAS application (`V1.60 build 1`) |
| 5 | `Firmware_LF_Lens` | `012000` | Lens communication engine (`LF 1.20.00`) |
| 6 | `Region_Lang_Pack` | `003` | Regional language pack identifier |
| 8 | `Distortion_Profile`| `------` | Optical distortion correction descriptor |
| 9 | `Firmware_MA_App` | `010100` | Machine-control assistant engine (`MA 1.01.00`) |

## Direct SDRAM frame capture (0x90C0)

Vendor operation `0x90C0` (`InitiateCaptureRecInSdram`) triggers the entire
imaging sensor pipeline without writing to external CFexpress/SD media:

```text
Host -> Camera: 0x90C0 [StorageID=0xFFFFFFFF, Param1=0]
Camera -> Host: Response 0x2001 (OK)
Camera: Sensor capture -> ISP Bayer demosaic -> Color matrix -> JPEG engine
Host -> Camera: 0x1007 [StorageID=0xFFFFFFFF] -> 706 object handles
```

The capture operation created object pair:
* `0x9198E10`: Format `0x3000` (Nikon NEF RAW, 21,538,310 bytes);
* `0x29198E10`: Format `0x3801` (Fine JPEG, 9,006,794 bytes).

A 160×120 EXIF thumbnail was extracted directly from the SDRAM buffer:

![SDRAM Captured Thumbnail](/evidence/physical/sdram_captured_thumb.jpg)

## Direct impact on QEMU emulation

1. **Deterministic Boot Trampoline**:
   The exception vectors and mode stacks in `dram_boot_04000000.bin` eliminate
   synthetic boot stubs. QEMU's MAS machine loader can map this 1MB block directly
   at `0x04000000`.
2. **True NVRAM Calibration Injection**:
   Replacing flat zero arrays with `nvram_resource_80_master.bin` (Resource 80)
   provides real sensor geometry, preventing division-by-zero or default fallback
   hangs during ISP initialization.
3. **Serial Number and Battery Profiles**:
   `nvram_block_53_serial_otp.bin` and `nvram_block_09.bin` supply physical
   OTP serial formatting and battery voltage tables for peripheral test fixtures.
4. **State Machine Verification**:
   The 8KB dump of `dram_mas_globals_b188b000.bin` proves the exact memory
   layout of operating system state cells (`0xb188b768`, `0xb188bc6c`,
   `0xb188bc80`), guaranteeing binary parity for QEMU trace assertions.
