# SLA ROM read contract

The loaded C1.70 SLA component owns the private physical image window
`[0x10020000, 0x108ecd40)`. The vector branch at component offset
`0x14100` targets `0x10035074`, which is payload offset `0x15074` in that
same window. The optional external boot prefix is a separate 128 KiB input at
`0x10000000`; it does not provide the SLA handler bytes.

`nikon.sla.image` is a QEMU ROM device. Normal reads use its ROMD backing and
return the loaded component bytes. Its read callback is only the error path
when ROMD is disabled; it intentionally reports the unsupported indirect NOR
read through MEMC. No alias, clone RAM, exception skip, or IRQ injection is
part of this contract.

The native qtest `/arm/nikon/boot` writes a vector word at component offset
`0x14100` and a handler word at `0x15074`, then reads both the image-relative
address and physical `0x10035074`. The test passes in the unchanged 93-case
Nikon qtest run, proving that the model's physical ROM owner serves source
bytes at the abort target.

The decoded-bundle abort receipt still reports a guest prefetch abort after
MMU activation. That runtime boundary is therefore not closed by adding a
second ROM mapping: the next required evidence is the active translation
state and producer for the guest physical fetch that returned zero.
