# Shutter trigger and capture dispatch

This document establishes the firmware capture trigger and dispatch pipeline
for the Nikon Z7II (firmware C1.70), connecting physical button actuation and
PTP remote control to the capture engine and readout synchronization.

## Control flow overview

```
[EX Button Event]                [PTP InitiateCapture (0x100e)]
       |                                       |
       v                                       v
MAS 0x104343c6 (SSI1 frame)             MAS 0x104cf800 (PTP handler)
       |                                       |
       v                                       |
MAS 0x10433ebc (Cmd 7 snapshot)                |
       |                                       |
       v                                       |
MAS 0x10764958 (Shutter executor)              |
       |                                       |
       v                                       v
MAS 0x10502aa0 (Release worker) ------> MAS 0x103bafc2 (Capture dispatcher)
                                               |
                                               v
                                        MAS 0x103bbd7a (Guard check)
                                               |
                                               v
                                        MAS 0x103b9bfa (Cmd 14 dispatch)
                                               |
                                               v
                                   Capture State Machine 0xb186935c
                                     ("CapStStt" / "CapStCc2")
                                               |
                                               v
                                 MAS 0x103b9848 (State loop)
                                               |
                          +--------------------+--------------------+
                          |                                         |
                          v                                         v
                Exposure / Mode Setup                      Sensor Readout Wait
               Event 0x0410004b (op 75)                  twai_flg(0x2000 / 0x20000000)
                          |                                         |
                          v                                         v
                  Sensor Execution                     IRQ94 bit 29 completion
                          |                                         |
                          +--------------------+--------------------+
                                               |
                                               v
                                   MAS 0x103b9538 (Completion)
                                     set_flg(0x20000000)
                                               |
                                               v
                                     Save Pipeline Entry
```

## Original firmware evidence

### 1. Physical shutter trigger path

1. **Button frame reception**: Physical front and rear button states are sampled
   by the EX subsystem and packed into 124-byte telemetry frames transmitted
   across SSI1 to the MAS CPU.
2. **Key transition decoder (MAS VA `0x104343c6`)**: Decodes shutter half-press
   and full-press transitions from the SSI1 input frame into the internal state
   buffer at `0xb25eae30`.
3. **Command 7 snapshot worker (MAS VA `0x10433ebc`)**: Copies `0xb25eae30` into
   the stable snapshot word `0xb25eaeac`.
4. **Shutter request executor (MAS VA `0x10764958`)**: Evaluates exposure mode,
   focus lock, and release readiness. Invokes `0x103ba6ea` and `0x103ba164`,
   establishing a maximum capture timeout of 60,000 ms (`0xea60`).
5. **Release worker (MAS VA `0x10502aa0`)**: Formats the 8-byte parameter block
   on the stack (`[sp, #88]..[sp, #94]`) and invokes the unified capture
   dispatcher at `0x103bafc2`.

### 2. PTP remote capture path

1. **PTP Operation `0x100e` (`InitiateCapture`)**: Handled by MAS VA `0x104cf800`.
2. **Parameter extraction**: Decodes the PTP request parameters:
   - `+0`: Target Storage ID (halfword).
   - `+2`: Capture Object Format (halfword: `0xb101` for RAW, `0x3801` for JPEG).
   - `+4`: Reserved parameter 2.
   - `+6`: Reserved parameter 3.
3. **Invocation**: Calls unified dispatcher `0x103bafc2` with the parameter block.
   If dispatcher returns error code 27 (`0x1b`), translates to PTP response code
   `0x200f` (`PTP_RC_DeviceBusy`).

### 3. Capture guard and state machine

1. **Guard check (MAS VA `0x103bbd7a`)**: Inspects capture subsystem state. If a
   capture sequence is already active or resources are busy, returns error 27
   (`0x1b`).
2. **Command 14 dispatch (MAS VA `0x103b9bfa`)**: Decodes opcode 14 (`0x0e`) via
   a `tbb` jump table at `0x103b9c0e`. Case 14 jumps to `0x103b9d94`.
3. **State machine context**: The capture state machine resides in `z7ii.kernel`
   BSS at `0xb186935c`, identified by RTOS task names `CapStStt`, `CapStCcc`, and
   `CapStCc2`.
4. **Capture state loop (MAS VA `0x103b9848`)**: Handles capture events within
   message class `0x04100000`:
   - `0x0410007b` (event 123): Dispatches capture start.
   - `0x0410004b` (event 75): Dispatches exposure configuration via `0x102282b4`.
   - Readout synchronization: Calls `twai_flg` (`0x10361116`) waiting for event
     flag bit 13 (`0x2000`) or bit 29 (`0x20000000`) on `0xb186935c + 0x24`
     with a 10,000 ms (`0x2710`) timeout.

### 4. Readout completion and handoff

1. **IRQ94 completion callback (MAS VA `0x102391b2`)**: When CMOS DMA finishes,
   the readout pipeline asserts IRQ94 with raw0 bit 3 (mode 0) or bit 5 (mode 1).
2. **Event flag assertion (MAS VA `0x103b9538`)**: Upon receiving completion
   event `0x20000000`, executes:
   ```asm
   mov.w r4, #0x20000000
   ldr   r0, [0x103b9574]   @ 0xb186935c
   mov   r1, r4
   adds  r0, #36            @ 0x24
   bl    0x103610cc         @ set_flg(0xb186935c + 0x24, 0x20000000)
   ```
3. **Task wakeup**: Wakes the capture worker thread in `0x103b9848`, clearing the
   event flag and proceeding to the file save pipeline.
