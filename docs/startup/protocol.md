# Functional model of original startup

Issue [331](https://github.com/mashiro01/nightglass/issues/331). Architectural principles governing the functional simulation of Nikon firmware startup on QEMU.

## Guiding principles

1. **Deterministic Hardware Responses**: Peripheral devices model exact, finite register state machines and deterministic timing callbacks rather than generic approximations.
2. **No Guest Outcome Injection**: The host never modifies guest register state, sets task flags, forces branch conditions, or draws UI frames directly to artificially advance firmware execution.
3. **Explicit Boundary Errors**: Any unmodeled register offset, unauthorized access width, or unsupported command results in an immediate `MEMTX_ERROR`, pausing execution and emitting diagnostic details for verification.
4. **Separation of Evidence**: Direct runtime observations, verified hardware contracts, inferred behaviors, and speculative hypotheses are documented separately.
