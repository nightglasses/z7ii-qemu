# Functional boot shim

The `functional-gui` composition installs the bounded loader contract for the embedded Z7II
machine. It loads the decoded five-component container, supplies the two
package-external 128 KiB prefix views as erased NOR when no explicit prefix is
provided, maps the finite early copy destinations observed in the unchanged
trace, including the source-backed MAS inner-image destination at
`0xb1000000..0xb1204a00` and its 64-byte handoff at `0x04008000`, and leaves
the firmware entry, CPU reset, CP15/MMU setup, SMP release,
interrupts, DMA completion, and display ownership to the guest.

Run it with:

```sh
qemu-system-arm \
  -M z7ii-mas-diagnostic,bundle=/absolute/path/decoded-container.bin,functional-profile=functional-gui \
  -smp cpus=2,maxcpus=5 -accel tcg,thread=single
```

The profile does not modify any component bytes and does not inject a GUI,
IRQ, DMA completion, or pixel. A successful run must therefore show the
original firmware advancing through its own startup and producing the first
display frame; a shim log or a stopped COPY transaction is not a GUI claim.
The profile is deliberately a single named functional composition.  If a future
board-specific loader is needed, it must be introduced as a separate, explicitly
documented contract rather than by silently changing this one.
