# Battery authentication: algorithm and secret-source inventory

The EX field arithmetic and scalar ladder are reconstructed and checked on
the host. The later [wire-seed study](seed.md) now recovers a camera scalar
representative from a UART challenge and constructs a first-auth candidate
using public H. The later timing-assisted UART run establishes original EX
acceptance of a fresh first-auth reply, with transport0 and validator1, then
enters0c. It recovers no battery private scalar and has not established
real-time response or a complete live UART peer. The initial inventory
and its scoped negative findings below retain their historical scope.

The newer [reproducible-input peer](batterypeer.md) precomputes the first-auth
reply and now passes unchanged EX authentication plus explicit class1 0c/0e
initialization without an in-transaction debugger pause. It still claims no
physical battery identity or full battery operation; command02 is the next
unsupported transaction. The wire-only search results below keep their scope.

## Original algorithm and independent host check

The original Z7II EX image is `ex.img`; EX VA is file
offset plus`08000000`. Static instruction receipts are retained in
`run batteryreply/`. The request/error path is documented in
[compatibility.md](compatibility.md). Original bytes stay unchanged.

| Original routine | Reconstructed operation |
|---|---|
|`0804a70c`|XOR of nine16-bit limbs|
|`0804a3c0`, reduction`0804a5b0`|Carryless field multiplication modulo`x^131+x^8+x^3+x^2+1`|
|`0804a624`|Field squaring by bit expansion and the same reduction|
|`0804a2bc`|Field inversion used when converting the projective result|
|`0804ba64`|Differential projective addition|
|`0804babc`|Projective doubling|
|`0804bb78`|128-bit scalar ladder and affine x-coordinate conversion|

Write field addition as XOR and define`c=ROM0802001a`. The helpers give:

```text
D(X,Z) = ((X²+cZ²)², X²Z²)
t=X1Z2; v=Z1X2; w=(t+v)²
A((X1,Z1),(X2,Z2),u) = (tv+uw,w)
```

These are binary-curve Montgomery x-coordinate recurrences. The host-only
`run authentication/field.py` reconstructs these routines,
including the original128-bit scalar limit. An independent affine oracle
uses the curve equation`y²+xy=x³+a*x²+c²` and ordinary point addition.
Seven scalars `{1,2,3,7,16,127,12345(hex)}` match the affine oracle with
`a=0`; inversion and`L(L(G,7),11)=L(G,77)` also pass. The curve parameter
chosen for this algebraic self-check is not an independent identification of
a named physical battery chip. `selfcheck.json` retains exact outputs.
These are host algebra checks, not execution of the original Thumb routine
or comparison against a real battery transcript.

The directly observed verification relation is:

```text
G = LE18bytes 1f6085d2f5b6c44ed32f25501ba620070600 @0802002c
H = LE18bytes cf4e3fc5d93b3a3f762f86000f0fdc0f0000 @08020000
c = LE18bytes 5e05801b9b5aede287f7cf1d708a073d0000 @0802001a
C = L(G,k)                 # wire request carries first17 bytes
S = L(H,k)                 # computed locally by original EX
R = reply[11..27]          # copied17 bytes; retain adjacent eighteenth byte
tag = reply[3..10]
accept iff R != 0 and tag == first8bytes(R*S mod p)
```

The17-to18-byte storage qualification is important: the consumer reads the
existing eighteenth byte at`2001f49f`. The wire framing additionally checks
reply byte1==6 and byte28==XOR(bytes0..27). After authentication, the original
EX makes a separate`02 0c 00 XOR` request and consumes reply halfwords11/13.
Neither a correctly framed packet nor an isolated successful transcript is
an implementation of these variable challenge and later-command semantics.

Under the ladder interpretation, a provisioned scalar`d` relating the two
base points would let a peer derive`S` from the wire challenge. No such
scalar was identified here. A new virtual identity cannot simply substitute
a new public quantity for EX's fixed`H`. The host check never reads running
guest random state, never constructs a successful guest reply, and has no
transport/device integration.

## Local original-source inventory

`inventory.py` records all scanned paths, sizes and file offsets in
`inventory.json`. It scans11 artifacts: Z7II EX and MAS, plus all extracted
EX/TP/MAS/VR/SLA payloads available from the official Z6II C1.70 and Z7
C3.80 packages inventoried in [id34.md](../inputs/nor/id34.md).

| Input | Exact18-byte matches |
|---|---|
|Z7II EX retained image|H at`20000`, c at`2001a`, G at`2002c`|
|Z6II `00_ex1850_012000.bi.payload.bin`|Same offsets and bytes|
|Z7 `00_ex1610_037000.bi.payload.bin`|Same offsets and bytes|
|Z7II MAS; all remaining Z6II/Z7 component payloads|No exact H/c/G match|

The shared constants support reuse of the same public arithmetic context;
they do not turn H into a recovered private scalar. As a bounded hypothesis
check,64 halfword-aligned16-byte candidates in Z7II EX file range
`[20000,20080)` were interpreted as scalars and tested for`L(G,d)==H`.
None matched. This excludes only those explicit candidates. It does not
exclude differently encoded, derived, external or elsewhere stored material,
and the full firmware was not exhaustively tested as possible scalar data.

The retained WonderLantern Z9 `qemu/*.c,*.h` source was searched for battery
and these public-constant spellings; no responder source was located by
that scoped textual search. The fixed Z7 GPU source is rendering code and
provides no battery authentication evidence. Neither search is a full
repository proof of absence.

## Public primary-source leads and disposition

The original NikonHacker author repository is inspectable at fixed commit
`de8019ee30db7fe7bc61e520da289078ae82dcaa`. Its
[D3200 source](https://github.com/simeonpilgrim/nikon-firmware-tools/blob/de8019ee30db7fe7bc61e520da289078ae82dcaa/Nikon-Patch/LocalUI/D3200.cs)
implements non-brand battery support by replacing original instructions
with patch bytes. It is a guest firmware patch, not a battery responder or
private-key provisioning record. An exact copy is retained as`D3200.cs` in
the ignored research directory. The complete1317-entry repository tree was
available; filename searches for battery/ECC/crypto/ORIGA and code searches
for battery/elliptic did not identify a responder. The filename/code search
coverage is narrower than auditing every source file.

Infineon's original
[ORIGA solution brief](https://www.infineon.com/dgdl/ORIGA_Solution_Brief.pdf?fileId=db3a30432313ff5e01232cb21fc07d8f)
describes asymmetric accessory authentication, public host information and
private device-key personalization in its secure facility. This explains
why an authentication library can be recoverable independently of a peer
secret; it does not establish that this Nikon battery uses that chip or key.

Skorobogatov, Kuhn and You's original
[2020 ECC authentication research presentation](https://www.cl.cam.ac.uk/~sps32/HWIO_OTB.pdf)
discusses Infineon devices and131-bit ECC, with hardware reverse engineering
and cloning limitations. It is a relevant primary research lead, not an
identified EN-EL15 responder, key file or matching Nikon transcript.

Nikon's
[JP5082660B2 patent](https://patents.google.com/patent/JP5082660B2/en)
describes battery authenticity determination in a multi-battery holder. The
inspected disclosure did not supply this firmware's G/H constants or an
executable131-bit responder; patent ownership alone does not establish an
implementation match.

Web and GitHub queries covered Nikon/EN-EL15 authentication, reverse
engineering, ECC, private key and responder/protocol terms. Accessible
results did not yield an original runnable EN-EL15 responder or provisioning
artifact. NikonHacker site fetches and direct GitHub API requests had access
failures; connected GitHub search/tree/file reads provided the repository
evidence above. Packet pictures, unrelated smart-battery emulators and
firmware-container XOR keys were excluded as sources for this protocol.

A battery-side implementation/provisioning artifact or variable real-peer
captures could validate the arithmetic and framing. They are not the only
remaining route: the later [wire-seed study](seed.md) identifies a bounded
camera-scalar search using public constants. Neither route yet supplies an
implemented, validated authentication-success device model.

## Narrow follow-up: scalar width is not entropy

Direct original instructions show that`080200ec` is a software additive
generator, not eight independent hardware-random reads. It sums two32-bit
state words, stores the sum, advances two pointers with a220-byte wrap,
and clears output bit31. The55-word ring is`20022f24..20022fff`; its two
pointers are stored at`20023000/20023004`. `08042606` calls it eight times,
retains only each low halfword, then writes the zero ninth halfword.

The seeder`08020468` initializes that ring from **one32-bit input** using
the recurrence`x=69069*x+0x66d619e1 (mod 2^32)` and stored words
`x+(x>>16)`. It sets the two initial pointers to`20022fa0/20022f24`.
Startup`08020074` calls`08020492` at`0802007a`; this passes fixed seed1.
Thus a repeatable startup state exists before later reseeding.

Battery initialization`08023e04` explicitly attempts reseeding:
`08023e36..08023e44` builds a stack descriptor with peripheral base
`50060800`, then calls init`08044cd2`. Only init success leads to
`08044c6c` at`08023e5e`; only read success seeds`08020468` at`08023e68`
with the returned32-bit word. The driver enables base+0 bit2, waits for
base+4 bit0 (with a tick timeout), and reads base+8 at`08044cc6`.
If init/read fails it does not reseed; the prior generator state survives.
The final deinit failure clears an unrelated initialization-success flag
but does not undo a seed already installed.

This is an identified hardware RNG register path, matching the existing
`hw/nikon/devices/exrng.c` finite model. That model's data read uses
`g_random_int()`, rather than a constant. The static firmware path does not
prove the physical source's entropy quality or which success/failure branch
the current run took. Regardless, one reseed supplies at most32 bits of
seed information to the following128-bit scalar, not128 independent bits.
The shared generator's call history can also affect the sequence. This
finding narrows the unpredictability claim; it does not recover a battery
secret or establish a wire-only response generator. No guest-state oracle,
seed enumeration, successful reply, build or guest experiment was used.

Original ranges are retained in
`prng.dis`: generator`080200ec..011a`,
startup`08020074..007e`, seeder`08020468..0494`, battery caller
`08023e04..3e76`, and RNG driver`08044c40..4d10`.

The parent's subsequent unchanged-firmware, read-only receipt
`run authseed/seed.json` records successful reseeding at
`08023e68`, structure`2001fa50`, with seed2082591 and the first21-byte
wire request. `seedcheck.py` reproduces the seeder, ring and first eight
low-halfword outputs from that single recorded seed. The resulting scalar
is`0x0a472d2a4e52997034fcab973131d770b`; its reconstructed`L(G,k)` starts
`f2b22009879b787ac3fdf87578d6f64a06`, matching **all17 challenge bytes**
in request[3..19]. `seedcheck.json` retains inputs/outputs. This ties the
host reconstruction to an actual original request and confirms the
32-bit reseeding path for this captured generation. It is a diagnostic
comparison using an explicitly recorded seed, not a wire-only peer:
no`H`, shared quantity`S`, tag or response was computed, no seed was
enumerated, and no guest/device reply was produced.

The subsequent [wire-only seed study](seed.md) derives an exact16+15-bit
parameterization and proves that toggling the seed's top bit leaves the first
scalar unchanged. At most2^31 representatives need testing. A wire-matching
scalar makes`L(H,k)` computable from public H, so that route does not
require a battery private scalar. The initial32-candidate Python experiment
had no match. Native PMULL/projective comparison now measures453,385/s with
four threads and naturally finds a representative in18.589 seconds while
enumerating the full configured domain from0. Independent Python recomputation
matches all17 bytes. A pure-host29-byte first-auth candidate then passes the
reconstructed tag/framing/XOR predicates. The retained R tail has original
startup-zero evidence; it is not read as a reply-generation input. Receipts
are in`run seednative/`. A later run in
`run batteryaccept/` sends a fresh wire-derived29-byte reply
through the actual upstream USART/chardev: original transport0, validator1
and state+4=1 lead to0c. All vCPUs were paused during1077 seconds of search.
This is timing-assisted original acceptance; no0c reply was sent and neither
real-time response nor complete battery initialization is established.
Z72's inspected public responder instead reads guest nonce RAM, while Z8 uses
a synthetic report. These do not meet the wire-only contract.
