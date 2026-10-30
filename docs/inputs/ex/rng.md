# Reproducible EX RNG input

The optional `nikon-ex-rng-functional.input-word` QOM property supplies one
explicit unsigned32 input word to the existing EX RNG device at PA50060800.
An absent property retains host random input. A present zero is distinct from
absence; the complete range0..ffffffff is accepted. QEMU's integer parser
accepts decimal and base-prefixed notation; malformed, negative or overflowing
values fail realization before guest execution.

```sh
-global nikon-ex-rng-functional.input-word=0
```

This is a repeatable external-input profile, not an entropy or physical RNG
claim. It changes only the word returned by an already-ready data read. The
existing RNGEN/DRDY protocol, consumed-word clearing, disabled reads, error and
access boundaries remain in force. A subsequent enable write supplies the
same declared word; no counter, guest-read sampling or hidden seed sequence is
introduced. Reset clears control/ready state and retains the selected input.

Original EX08023e04 attempts a hardware read and reseeds its software ring at
08023e68 only on success. The precomputed [battery peer](../../runtime/batterypeer.md)
uses the same declared word but still compares every received challenge byte.
It never reads the guest ring, nonce or authentication result to choose a reply.
The setting applies to this device's other legitimate ready reads as well;
it is deliberately explicit rather than a special case at an authentication PC.

## Raw EX UART endpoint

`nikon-ex.uart0-chardev` links an explicitly created Chardev to EX USART0
at PA40011000. It selects the backend before the existing upstream USART is
realized; the USART MMIO, RX/IRQ and guest driver remain unchanged. With no
link the established serial routing remains in use. An explicit link avoids
sharing MAS USIO0's multiplexed serial backend:

```sh
-serial none
-chardev socket,id=bat,path=/tmp/nikonbattery.sock,server=on,wait=on
-global nikon-ex.uart0-chardev=/chardevs/bat
```

Start the peer after that socket exists. QEMU waits for its connection before
starting the guest; the peer computes its arithmetic before connecting.
The read-only debugger receipt instead uses `wait=off` with the normal initial
`-S`, then starts all CPUs after connection. No running transaction is paused
for computation. These options augment the normal unchanged
bundle/functional-descriptor launch; they are not a replacement firmware mode.

## Validation

`run reproducible/` retains the actual native old-source failures
and new-source results. Five RNG qtests cover host/default protocol, fixed zero,
all ones, another fixed word, invalid configuration/accesses, consumed-ready,
disable cancellation and reset. Two UART qtests cover default and dedicated
backend routing, raw01/02 bytes, isolation and reset retention. The normal
entry point is `make check MTESTARGS='nikon_rng nikon_uart'`.

The fresh original EX run separately accepts the precomputed authentication
reply and the explicit class1 0c/0e profile, without debugger stops during those
transactions. See the peer document for observed retries and remaining scope.
The current receipt is macOS; physical baud timing and full battery operation
are unverified.
