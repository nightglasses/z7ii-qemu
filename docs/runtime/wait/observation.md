# Event Flag Wait & Wakeup Lifecycle

Tasks in the camera RTOS synchronize asynchronously via kernel event flag objects, transitioning from blocked wait queues to active execution upon peripheral interrupt service completion.

```mermaid
sequenceDiagram
    participant Worker as Worker Task (B2C33368)
    participant Kernel as RTOS Event Flag Object
    participant ISR as Hardware ISR (IRQ 102 / 237 / 250)
    participant Sched as Task Scheduler (0x10107098)

    Note over Worker,Kernel: 1. Task Blocks on Event Flag
    Worker->>Kernel: Call event_wait(Flag ID, Mask, Mode)
    Kernel->>Kernel: Enqueue Task TCB to Event Wait Queue
    Kernel->>Sched: Yield CPU, set Current TCB = 0
    Sched->>Sched: Enter Dispatcher Idle Loop (0x10107094)

    Note over ISR,Kernel: 2. Peripheral Event Arrival
    ISR->>Kernel: Call event_set(Flag ID, Pattern)
    Kernel->>Kernel: Match pattern with queued wait mask
    Kernel->>Kernel: Dequeue Task TCB, mark Task Ready

    Note over Kernel,Sched: 3. Task Context Restoration
    Sched->>Sched: Scheduler discovers runnable candidate
    Sched->>Worker: Restore context, return from event_wait
```

## Event Synchronization Protocols

1. **Wait Specifications**:
   - Tasks register a wait pattern and mode (e.g. `mode 1` for ANY bit match, `mode 0` for ALL bits match).
   - If the requested pattern is not currently set on the target event flag object, the calling task is added to the object's blocked wait queue, and control yields to the scheduler.
2. **Asynchronous Pattern Setting**:
   - Hardware ISRs acknowledge physical device interrupts (e.g., Control2010 gate stop confirmation, Signal controller edge detection), invoke registered C callbacks, and call the kernel's `event_set` API.
   - The kernel matches the newly asserted pattern against active waiters, moves satisfied tasks to the ready queue, and triggers scheduling dispatch.
