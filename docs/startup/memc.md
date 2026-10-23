# Z7II C1.70 original MAS traversing MEMC initialization

Issue [331](https://github.com/mashiro01/nightglass/issues/331). This stage documents the original MAS startup sequence successfully executing beyond early memory controller (MEMC) initialization.

## Observed execution path

1. **Reset and CP15 Initialization**: Both Cortex-A7 cores boot from vector `0x10020000`, configure CP15 system control registers, set up TTBR0, and transition into SVC mode.
2. **Clock and Power Gates**: Early initialization routines program system control registers (`0x04264000`), configuring clock multipliers and releasing internal resets.
3. **MEMC Configuration**: Original code programs the memory controller registers (`0x20010000` aperture) to configure DDR timing, refresh rates, and rank topologies.
4. **Subsequent Stop Point**: After completing MEMC configuration, execution proceeds to the peripheral initialization phase, encountering subsequent device access requirements at `0x20020000` (SDI/DDR).

## Model boundaries

- The memory controller model adheres strictly to observed software register access patterns.
- Register reads return verified default and status bits.
- Writing to undefined offsets returns `MEMTX_ERROR` and halts execution for diagnostic capture.
