# Battery report and MAS classification

The normal UART continuation recorded in
`run batterycomplete-20261027/joined-snapshot.json` reaches MAS BatCk
value **1** with a synthetic class1 battery profile and provider gate
`2001fba6=0`. This is an observed battery-service result from original EX
and MAS execution. It does not establish physical battery identity or
complete camera startup.

The joined capture was taken after five host seconds of normal UART
continuation; `paced-snapshot.json` also records BatCk1 after 20 host seconds.
EX main state `2001fa50+41` is 2, substate+42 is 0, diagnostic+44 is 0 and
charge+26 is 100. Snapshot `20016040` has byte2=2, byte7=100 and byte9=100.
Report `20015fc0` is 112 bytes; its nonzero bytes are:

| Decimal offset | Value |
|---|---|
|0|3|
|8|1|
|18|100|
|20|100|
|24..25|`28 23` (LE16 9000)|

The actual MAS input `b23bd29c` is a different 112-byte record. Its nonzero
bytes are 0=1, 7=1, 24=100, 32=100, 36=`28` and 37=`23`. In particular,
its byte8 is 0. MAS normalized record `b23bcd9c` has type byte+4=1, fault
byte+8=0, bytes+9/+10=1, and words+16=100, +24=100 and+28=9000. The
unknown-type flag `b1865220` and settings-override byte `b1865230` are both 0;
BatCk `b18651d8` is 1. Thus the earlier inference that EX state2 or the
captured EX report byte8 necessarily implies final MAS fault/status2 was
incorrect.

## Source offsets and the necessary distinction

All `080xxxxx`/`200xxxxx` addresses are EX VA; `10xxxxxx`/`bxxxxxxx` are
MAS VA. Record offsets below are decimal; words are little-endian 32-bit
values unless a different width is stated. EX evidence is the retained original listing
`ex.dis`. MAS evidence is the saved original code in
`run batterycompat-20261022/0x103f8c1a.json`, `0x103f8b50.json` and
`0x103f8ec6.json`; the relevant normalizer and copy instructions were also
checked read-only against the loaded `eg1840_mas_01700` database.

Let E be the captured EX buffer, R the actual input passed to MAS normalizer
`103f8c1a`, and N its destination. These names must remain distinct until
the received record and publication generation are paired.

| Source operation | Original instruction address |
|---|---|
|EX state2 maps to snapshot byte2=2|`080279ca` table index2 → `08027a22`|
|snapshot byte2=2 sets E[8]=1|`080255ca..080255f6`|
|snapshot byte9 → E[18]|`0802565a..0802565c`|
|snapshot byte7 → E[20]|`08025660..08025662`|
|snapshot halfword4 → E halfword24|`08025666..0802566a`|
|R[4]=0 maps to N[4]=1|`103f8c3c` → `103f8c82`|
|R[8]!=0 → N[8]|`103f8c6a..103f8c70`|
|R[7]!=0 → N[9], and selects N[10]=1|`103f8c72..103f8c7e`|
|R word24 → N word16|`103f8cc2..103f8cc4`|
|R word32 → N word24|`103f8cca..103f8cd0`|
|R word36 → N word28|`103f8cd2..103f8cd8`|

The known MAS ingress copies at `1038a6e0` and `103f970a` use ordinary
112-byte memory copies (`10360fac`/`10360f44`); those copy functions do not
reorder report fields.

The normalizer ends by calling original MAS function `103f8b50`. When byte
`b1865230` is nonzero, this function applies guest settings276/277/528 and
can change N[4], N[6], N[8], N[9], N[11], N word24 and N word28. In
particular, `103f8bd8`, `103f8be4` and `103f8c02` clear N[8] on their
respective source branches. Consequently, the initial boolean copy is not
the complete final-fault rule. No host setting override was introduced by
this research.

`103f8ec6` checks final fault bytes N[8], N[32] and N[56]. If any equals1,
`103f8f30` stores status2. On the first publication, when the normalized
record's first word was zero, the no-fault branch stores status1 at
`103f8f58`. Later no-fault publications do not themselves restore status1.

## Actual received record matches the healthy consumer result

The joined capture supplies R and N plus both flags, so the following
mapping is directly consistent with the original consumer:

| Actual input R | Normalized N | Result |
|---|---|---|
|byte4=0|byte4=1|recognized main type|
|byte8=0|byte8=0|main fault clear|
|byte7=1|bytes9/10=1|separate boolean/selector fields, not the main-fault byte|
|word24=100|word16=100|direct copy|
|word32=100|word24=100|direct copy|
|word36=9000|word28=9000|direct copy|
|bytes64/88=0|bytes32/56=0|both optional slot blocks cleared|

No settings override explains this result: its captured enable byte is 0.
The three final fault bytes are 0, consistent with the observed BatCk1 and
the original first-publication no-fault path. N word0=1 is the publication
marker written by `103f8ec6`, not a copied input tag.

E and R are visibly unequal: their first bytes are3 and1 respectively;
E[8]=1 while R[8]=0; E word24=9000 while R word24=100. This capture does
not identify E's channel/publication as the origin of R. Do not infer a
uniform header shift, field transformation, or unconditional fault from
those two different buffers. The healthy R→N→BatCk consumer boundary is
closed by the actual input, output and disabled override; tracing E to a
particular transport generation is separate work if that attribution is
needed. Changing provider gate `2001fba6` merely to make BatCk pass is
neither needed nor supported by this observation.
