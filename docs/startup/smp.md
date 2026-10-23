# Z7II original dual-core reset and clock blockage

Issue [331](https://github.com/mashiro01/nightglass/issues/331). Analysis of multi-core startup behavior and secondary core parking in the Z7II MAS domain.

## Multi-core execution flow

1. **Primary Core (CPU0)**:
   - Evaluates MPIDR, identifies itself as core 0, and proceeds with global hardware setup (clocks, MMU, memory controller, NOR flash).
   - Prepares secondary core boot trampolines and stack allocations before signaling secondary wake-up.
2. **Secondary Core (CPU1)**:
   - Evaluates MPIDR, detects core 1 identity, and branches to an early wait loop at `0x1002bfa8`.
   - Spins on memory flags awaiting primary core release.
3. **Synchronization and Clock Blockage**:
   - The primary core configures peripheral clock gates and inter-processor interrupt channels via the system configuration aperture.
   - When required peripheral clocks remain disabled or unsupported in the model, CPU0 halts on a subsequent transaction fault while CPU1 remains safely parked in its wait loop.

## Invariant verification

- Both cores retain independent stack pointers and architectural register state across reset and exception delivery.
- Memory barriers (`DMB`, `DSB`, `ISB`) in original instructions are executed natively by QEMU TCG.
