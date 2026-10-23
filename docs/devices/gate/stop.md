# Control2010 Stop Request and Event Confirmation

The Control2010 stop lifecycle allows the camera firmware to gracefully halt peripheral event inflows and synchronize background processing via an asynchronous internal confirmation interrupt.

```mermaid
sequenceDiagram
    participant App as Firmware Coordinator
    participant Gate as Gate Controller (0x20100000)
    participant Timer as 20 µs Virtual Drain Timer
    participant ISR as Gate ISR (0x103BCE38)
    participant Kernel as RTOS Event Flag (F0.bit2)

    Note over App,Gate: 1. Initiate Peripheral Stop
    App->>Gate: Write 0x20100004 = 0x00 (Close Inflow Gate)
    Gate->>Timer: Schedule 20 µs Control Drain
    App->>Kernel: Block wait on Event Flag (ID 0x01000088, mask 0x02)

    Note over Timer,ISR: 2. Internal Confirmation Assertion
    Timer->>Gate: Drain expired, set Raw Domain 1 Bit 0 (0x18.bit0)
    Gate->>Gate: Check Mask (0x0C bit 0 is unmasked)
    Gate->>ISR: Assert GIC SPI 102 Interrupt
    
    Note over ISR,Kernel: 3. Service & Task Unblock
    ISR->>Gate: Read 0x18 (confirm bit 0)
    ISR->>Gate: Write 0x24 = 0x01 (W1C ACK)
    ISR->>Kernel: Call event setter (Sets F0.bit2)
    Kernel->>App: Wake Coordinator Task (Wait returns success)
```

## Hardware Stop Lifecycle

1. **Stop Initiation**: Writing `0` to `0x20100004` closes the external event inflow gate and initiates an internal 20 µs virtual drain timer.
2. **Coalescing**: If firmware writes `0` again while a drain is actively pending, the request coalesces without resetting the timer.
3. **Confirmation Latch**: Upon drain expiration, the controller latches bit 0 in raw domain 1 (`0x20100018`). Unlike external events, this internal confirmation is **not** blocked by the gate closure, ensuring that the firmware receives the completion event.
4. **Reopening**: Writing `1` to `0x20100004` while a drain is pending cancels the unfinished drain timer while preserving any already-latched confirmation flags.
