# SLA exception vectors and GIC access boundary

This receipt separates the apparent CPU2/CPU3 undefined-instruction storm from
the original SLA code and from the QEMU GIC profile. It uses the unchanged
C1.70 decoded container with two MAS CPUs, two SLA CPUs and one EX CPU. The
observer stops the guest; it does not alter registers, exception vectors,
guest memory or interrupt state.

## Runtime observation

The observation is reproducible with the unchanged decoded bundle and the
diagnostic machine:

```text
work/build/qemu/qemu-system-arm \
  -smp cpus=2,maxcpus=5 -nodefaults -display none -vga none \
  -machine z7ii-mas-diagnostic,bundle=/path/to/decoded-container.bin,\
sla-boot-prefix=erased,mas-boot-prefix=erased \
  -accel tcg,thread=multi -serial none -monitor none -S \
  -gdb tcp:127.0.0.1:12682 \
  -qmp tcp:127.0.0.1:12683,server=on,wait=off
```

Resume through the GDB endpoint, then query without sending `quit`:

```text
{ "execute": "qmp_capabilities" }
{ "execute": "human-monitor-command",
  "arguments": { "command-line": "info registers -a" } }
```

The register values below are from that stopped observer. The source offset
and translation checks can be repeated with the GDB commands `p/x $TTBR0_S`,
`x/4wx 0xc18e4400` and `x/8wx 0x10035074`.

After resume, QMP `human-monitor-command "info registers -a"` reports the
following stable states for the SLA CPUs:

| CPU | PC | CPSR | LR | SP | selected registers |
|---:|---:|---:|---:|---:|---|
| 2 | `0x1003410c` | `0x80000197` (`abt32`) | `0x10034110` | `0xc1918000` | `r0=2`, `r4=c18b2cd4`, `r6=21`, `r10=c3f75000` |
| 3 | `0x1003410c` | `0x20000197` (`abt32`) | `0x10034110` | `0x00000093` | `r0=c1920a50`, `r1=10000aaa`, `r2=aa`, `r7=c10064b4`, `r8=20102800` |

`0x1003410c` is the fourth ARM vector word at the SLA VBAR, not an
application instruction. The vector words read from guest memory are:

```text
10034100: e59ff018 ea0003da e59ff014 ea0003fe
10034110: ea0003ea eafffffe e59ff008 eafffffe
```

The CPU mode is abort mode (`0x17`) and the PC is the vector branch. The
repeated QEMU lines labelled `Undefined Instruction` therefore do not identify
a missing ARM opcode at the reported PC. They are an exception/abort loop
whose original faulting PC must be captured before the vector branch runs.
Treating the line as a guest `BKPT`, advancing the PC, or suppressing the
exception would destroy the evidence.

## Source and translation evidence

The same words occur in the C1.70 SLA payload at component offset `0x14100`
under the established local projection `VA = 0x10020000 + O`. The bytes at
offset `0x15074`, the target selected by the vector branch, are present in the
source payload (`64d19fe5 00e08de5 00e04fe1 04e08de5`). They are not an
unknown or undefined opcode in the original component.

At the runtime stop, the secure translation-table base is
`TTBR0_S=0xc18e4000`. The first-level descriptor for VA section `0x100`,
read at `0xc18e4400`, is `0x10000432`, an identity section descriptor for
`0x10000000..0x100fffff`. A guest-translated read of `VA 0x10035074` returns
zero in this state, while the source payload contains the handler bytes above.
This is a mapping/ROM-read or guest-copy boundary to resolve; it is not a
reason to add an arbitrary alias or to map a zero page. The next receipt must
capture the first exception entry and the active translation/physical read
before changing the model.

## GIC register boundary

The SLA timer initializer performs the source-observed read/OR/write sequence
against Distributor offsets `0x300..0x303`. These are the low-byte view of the
GIC Distributor active-set register. The selected functional profile uses the
mature revision-1 read-only active view, so these stores are retained as
unsupported/ignored writes and do not clear or manufacture a pending IRQ.

The initializer also reads CPU-interface offset `0x10`, the GICC EOIR
register. EOIR is write-only in the upstream profile; its read remains an
upstream guest error and no readback value is added. Neither boundary supplies
the missing SLA peer event or a completion IRQ.

## Required next observation

Use a fresh run with a read-only breakpoint on the first SLA exception entry,
before the vector branch, and record the banked return state (`LR_abt` or
`LR_und`, `SPSR_*`) and the translated instruction fetch. The result will
choose between:

1. an original guest exception caused by an unsupported source operation;
2. a guest-created page-table/copy state that leaves the vector target
   unmapped; or
3. a QEMU ROM/MEMC access-model mismatch.

Until that evidence exists, no GIC version change, IRQ injection, peer reply,
exception skip or code patch is justified.
