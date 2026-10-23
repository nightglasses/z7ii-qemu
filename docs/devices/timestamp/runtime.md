# Timestamp Counter Kernel Runtime Protocol

The camera kernel initializes and coordinates the 64-bit hardware timestamp counter during early RTOS bootstrap.

```mermaid
sequenceDiagram
    participant Boot as Kernel Init (PC 0x10020150)
    participant Timer as Timestamp Block (0x04266000)
    participant CP15 as ARM CP15 (CNTFRQ)

    Note over Boot,Timer: Phase 1: Halt Counter & Reset
    Boot->>Timer: Read CTRL (0x00)
    Boot->>Timer: Write CTRL (0x00) = 0x00 (Halt)
    Boot->>Timer: Write COUNT_LO (0x08) = 0x00000000
    Boot->>Timer: Write COUNT_HI (0x0C) = 0x00000000

    Note over Boot,Timer: Phase 2: Program Frequency
    Boot->>Timer: Write FREQ (0x10) = 0x02DC6C00 (48 MHz)
    Boot->>CP15: MCR p15, 0, r0, c14, c0, 0 (Set CNTFRQ to 48 MHz)

    Note over Boot,Timer: Phase 3: Resume Monotonic Count
    Boot->>Timer: Write CTRL (0x00) = 0x03 (Start)
```

## Bootstrap Synchronization
1. **Early Polling**: During initial bootstrap, firmware samples `TIMESTAMP_COUNT_LO` (`0x04266008`) in tight spin loops to implement microsecond-scale hardware settling delays.
2. **Kernel Initialization**: Once the memory subsystem is established, the kernel halts the counter, zeroes both 32-bit count words, explicitly programs `48 MHz` into `TIMESTAMP_FREQ`, synchronizes ARM architectural timer `CNTFRQ`, and restarts the counter with control value `0x03`.
