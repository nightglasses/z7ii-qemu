# Native NDS32 system execution

The implementation lives in sim/nds32/, include/gdb/nds32system.h and
cmake/nds32.cmake. The pinned official Andes provider remains read-only;
CMake connects its native GNU build graph through an ignored source view.
GNU simulator execution, bounded mappings, exception unwind and virtual
events remain the execution mechanism; no ISA decoder is added.

## Architectural basis

- [AndeStar ISA v1.3](https://download.paulk.fr/coreboot/mt8173-spm/documentation/isa/AndeStar_ISA_v1.3.pdf): IRET76–77,MFSR/MTSR124/135,SETEND/SETGIE162/163.
- [AndeStar SPA V2 v1.9](https://www.scribd.com/document/980985425/AndeStar-SPA-V2-UM026-V1-9): sections4.2,4.4,9.3. This manual mirror does not identify Nikon silicon.
- [Andes priority note](https://www.andestech.com/wp-content/uploads/%E5%9C%A8%E6%99%B6%E5%BF%83%E5%B9%B3%E5%8F%B0%E8%BF%90%E8%A1%8C%E5%85%B7OSC%E7%9A%84FreeRTOS.pdf): pages4–8.
- Linux v5.17 [bitfields](https://github.com/torvalds/linux/blob/v5.17/arch/nds32/include/asm/bitfield.h) and [IRQ driver](https://github.com/torvalds/linux/blob/v5.17/drivers/irqchip/irq-ativic32.c).

Interrupt level0→1 saves IPC/IPSW;1→2 also saves P_* and P0/P1;2→3 keeps
previous registers, saving OIPC and increasing INTL. IRET restores the tier
selected by entry INTL. GNU sim_engine_restart prevents a faulting instruction
from continuing destination writes.

IRQs are selected at instruction boundaries using GIE,mask,fixed or two-bit
programmable priority and CPL. Real device inputs own signals: writing level
pending cannot remove an external signal; edge latches use W1C.
The four original standby wake_grant instructions use explicit consent/wake;
even masked external IRQs may wake a consented core. Without consent, the
instruction does not advance and waits only on GNU virtual events.

## TPJ functional profile

Complete original source contains25 vector slots,23 IRETs,MASK/PENDffff
and IVB0, with no PRI/CTRL/TRIGGER/MASK2/PEND2 consumer. This does not prove
NIVIC or hardware revision. --nds32-system selects16-input IVICv1,fixed
priority,no shadow SP,physical addressing and little endian. It does not
establish Nikon chip,BootROM or physical reset. Guest code initializes its
own SP,COPY,ZERO and interrupts.

No default64MiB RAM is created. The caller supplies finite ROM/RAM and binds
read/write callbacks with nds32_system_attach(). Missing callbacks and rejected
addresses/widths raise bus exceptions; device regions are non-executable.
The device copies callback values; caller opaque data lives through sim_close().
Software interrupt outputs beyond six inputs require explicit wiring.
nds32_system_run() budgets GNU instruction/event ticks, not measured time.
Enabling unimplemented MMU translation explicitly stops; no identity fallback.

## Build and native validation

The build owner supplies NDS_UPSTREAM,native_file(path,content) and a read-only
view. cmake/nds32.cmake requires unique source seams for system.o,boundary
hardware and decoder hooks. configure.ac uses SIM_AC_OPTION_HARDWARE;
compatible Autoconf regenerates configure/config.in, then GNU configure/Make
builds without editing the official provider or adding a Python wrapper.

tests/unit/nds/system.c generates synthetic instructions with GNU opcode
macros and executes the real decoder. Assertions cover nested recovery,
overflow return,priority/CPL,level/edge lifecycle,actual callback reads and
fault unwind. The integration owner built provider,system.o,libsim.a and run
with WITH_HW=1. Five ELF-less groups with --endian=little exited0:
nested IRET,IRQ priority/CPL,finite MMIO/fault unwind,privileged SR and
standby consent/wake. These synthetic results do not prove original TPJ boot.

## Native process and wire contract

sim/nds32/runner.c uses BFD,GNU simulator and nonblocking POSIX socket/poll.
The full ELF entry is0; allocatable sections must be loadable,nonempty,
VMA=LMA,nonoverlapping and cover[0,e000). GNU readelf confirmed the inspected
artifact. e000 is this update component's length, not measured ROM capacity.
ROM is read-only/executable; guest COPY/ZERO/stack setup remain original.

Transport shares include/hw/nikon/wire.h with the QEMU socket CharBackend
server. CLOCK completes only from actual device shifting; SIGNAL carries
line levels and CANCEL cancels its token. Input validates the complete40-byte
frame. Bounded output queues and partial I/O/backpressure preserve events;
no ACK or guest protocol packet is fabricated. --ticks is only a GNU execution
quantum; --consent 0|1 provides explicit external power permission, default0.

sim/nds32/tpj.c binds11 finite domains:40000,41000,42000,50000,53000,60000,
61000,70000,71000,72000,80000. fields.h retains194 separate constant-address/
width expressions without merging control/call flows. Qualified helpers cover
flash,six-halfword transform,60000 ingress/packet/reply and800c0 initialization.
Holes and unproven widths reject. Scan RAM is only[2a000,2a344) and
[2b000,2b344), not four speculative4KiB banks.
[Original source contract](https://github.com/mashiro01/nightglass/blob/feat/tpj-complete-source/research/z7ii/tpj-source-device-contract.md).

## Explicit flash and numerical inputs

--flash FILE requires exactly64KiB of external NOR. Alternatively, explicit
--update FILE puts a57,344-byte component into a64KiB erased-FF snapshot.
Original constant1fd808=(2000−8)*ff supports erased bytes[e000,fff8);
the last eight FF bytes are an external-input hypothesis, not camera reads.
This profile never silently replaces --flash.

Unlock55/aa/66 at41005/06/07 drives actual ab/b9,06,3b,20,02 commands.
41004 is count−1;41012 is a stable read latch, including the helper's second
read. Erase uses the proven1000 stride; programming uses explicit NOR bitwise
AND. Awake,write-enable,extent and full payload are checked.

Original little-endian wordcffc equals the sum of[0,cffc);dffc equals
[d000,dffc). Oca4 adds those words,both bytewise footer sums,erased tail
and final eight bytes, then compares41018. The device computes41018 from
actual full flash contents after erase/program; it does not inject a
checksum-pass flag. --save exports complete idle NOR without modifying the
input. Completion uses a one-tick GNU event as functional timing. Missing
backing,invalid ranges and unknown commands remain busy and report the boundary.

--transform FILE holds24-byte records: six LE input halfwords followed by six
actual output halfwords. A53002 request must match the input before an event
writes outputs and sets53001 ready. No match means no ready,input copying
or invented transform algorithm.

--snapshot FILE lines contain hexadecimal-address,decimal-width,hexadecimal-value
for qualified readable fields only. It cannot inject busy,flash latch/sum or
transform ready. Inputs6001c..21,50014/54/94,72024,721c0,80050 feed established
original ISR consumers. Inactive events,zero RW configuration and71010 idle-high
backing are functional cold conventions. Notification wires wait for an actual
guest71010 write; initial backing does not emit ready/high. Guest hardware
flag clearing deasserts level IRQ, without host RAM event writes.

## Original execution checkpoints

--limit TICKS reports real PC/sleep at a finite instruction/event budget and
works without sockets. The first missing producer or illegal field reports
PC,address,width and cause; it is not complete boot.

Normal touch uses typed I2C START/WRITE/READ/NACK/STOP with MAS address55,
cmd10/read2 andcmd11/read7. Hardware fields drive original ISR/callbacks;
READ consumes only60040 output written by the guest, not command-number
reply synthesis. See [touch contract](../devices/touch/protocol.md) for source
and functional wiring. SPI CLOCK keeps its token pending its own binding;
it does not borrow I2C/EX frames.720xx remains configuration/status/buffers.

tests/unit/nds/tpj.c uses synthetic external files for NOR latch,missing payload,
count−1/programming,external transform outputs,level deassertion,exact reply
bounds and missing-producer/hole rejection. The integration owner compiled
the GNU runner and executed its endpoint case with exit0.

The first original ELF probe stopped at Ob3fe,bnes38 in COPY,after4096 ticks.
Unknown signal0 was a host budget enum error: SIM_SIGNONE is64. Fixing that
signal preserved nonzero fault handling and original PC/COPY. An independent
tests/unit/nds/budget.c case verified two budget stop/continue rounds and real
nonzero fault rejection,exiting0. Original execution then reached Ob22,
READ41011,missing external flash.

Typed I2C,live NOR sums/save,explicit update and50018 count/IRQ/ACK passed six
groups. With --update,original code passed Oca4 NOR validation and reached
Oa966 READ80042. Repeated handling of that first boundary was not full boot.

The80043 request/80042 completion+ACK,11 channel setups,384 halfword fields/
stream indices and80216 self-clear groups then passed nine focused groups.
Original execution advanced to Oa588 WRITE8 PA801a0 and stopped after one real
bus fault. Source analysis also qualified801a0..a3,nine-page801ea/801e0 sensing
configuration and three standby60003.bit4 predicates. These retain actual
configuration/command causes, not ADC samples,scan counts,720 ready or contacts.

The normal sample layer passed12 groups. Fixed3935ace9 normal processing and
explicit quiescence passed16 native groups,including11 expected negative
boundaries and architectural faults. A complete original-source run with14
explicit startup records exited naturally at PC8ec8 within2,000,000 ticks,
normal9/capture2/reference2/BUFFER4/quiesce1,without a source boundary.
Wire command and guest reply counts were both0: complete original boot and
functional menu operation remain unverified.
