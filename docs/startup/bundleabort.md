# Bundle first-abort receipt

This receipt closes the current decoded-bundle exception question without
changing guest state. It records what the existing read-only observer proves
and the external contract that is still missing.

## Reproduction

Start the diagnostic machine with the unchanged decoded container and both
counterfactual erased boot prefixes:

```text
qemu-system-arm \
  -smp cpus=2,maxcpus=5 -nodefaults -display none -vga none \
  -machine z7ii-mas-diagnostic,bundle=/path/to/decoded-container.bin,\
sla-boot-prefix=erased,mas-boot-prefix=erased \
  -accel tcg,thread=multi -serial none -monitor none -S \
  -gdb tcp:127.0.0.1:12682 \
  -qmp tcp:127.0.0.1:12683,server=on,wait=off
```

Resume through the GDB endpoint, then use QMP only for read-only register
queries:

```text
{ "execute": "qmp_capabilities" }
{ "execute": "human-monitor-command",
  "arguments": { "command-line": "info registers -a" } }
```

Do not inject an exception, advance the vector PC, acknowledge an IRQ, or
preload the clone region. Those actions remove the evidence being measured.

## Observed first-abort state

The two SLA vCPUs repeatedly settle in ARM abort mode at the vector branch:

| vCPU | PC | CPSR | LR | SP | selected registers |
|---:|---:|---:|---:|---:|---|
| SLA0 | `0x1003410c` | `0x80000197` (`abt32`) | `0x10034110` | `0xc1918000` | `r0=2`, `r4=0xc18b2cd4`, `r6=0x21`, `r10=0xc3f75000` |
| SLA1 | `0x1003410c` | `0x20000197` (`abt32`) | `0x10034110` | `0x00000093` | `r0=0xc1920a50`, `r1=0x10000aaa`, `r2=0xaa`, `r7=0xc10064b4`, `r8=0x20102800` |

The words at the vector branch are:

```text
0x10034100: e59ff018 ea0003da e59ff014 ea0003fe
0x10034110: ea0003ea eafffffe e59ff008 eafffffe
```

Therefore `0x1003410c` is the fourth vector word/branch target. The reported
PC is not an application instruction and is not a `BKPT`. `LR_abt` points at
the next vector word because the CPU is already inside the abort vector; it is
not the original faulting PC.

## Translation and storage boundary

The stopped secure translation state is:

```text
TTBR0_S = 0xc18e4000
TTBR1_S = 0x00000000
TTBCR_S = 0x00000000
DACR_S  = 0x55555555
SCTLR_S = 0x00c5107d
```

The first-level descriptor for VA section `0x100`, at
`0xc18e4400`, is `0x10000432`. It is an identity section descriptor for
`0x10000000..0x100fffff`. A guest-translated read at `0x10035074` returns
zero in this stopped state, while the unchanged SLA component contains the
handler bytes at component offset `0x15074` (`64d19fe5 00e08de5 00e04fe1
04e08de5`) under the established `VA = 0x10020000 + offset` projection.

This is a ROM/read or guest-copy boundary: the page-table descriptor exists,
but the physical source visible through that descriptor does not contain the
expected handler bytes. It is not evidence for a missing GIC interrupt, a
missing peer reply, or an undefined ARM instruction.

## Missing external contract

The next required artifact is a read-only capture *before* the vector branch
of the original exception entry, containing:

1. the faulting PC and instruction fetch result;
2. `LR_abt`, `SPSR_abt`, `DFAR`, `DFSR`, `IFAR`, and `IFSR` from the same stop;
3. the physical address selected for the `0x10035074` fetch; and
4. the producer that populates the identity ROM view after the guest changes
   the first-level descriptor.

Until that producer/physical mapping is identified, adding an alias, seeding
the seven MiB clone RAM, changing GIC behavior, or skipping the abort would
be an unsupported model change. The current implementation should retain the
real exception and report this boundary.
