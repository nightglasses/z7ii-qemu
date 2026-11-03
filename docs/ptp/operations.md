# Operations

The session dispatches on operation tables built into the image. There are eight of them,
28-byte rows in each, holding 161 distinct opcodes; their addresses, fields, callback
columns and complete listings are in [Dispatch tables](dispatch). The covered ranges are
the standard `0x1001`–`0x101B`, then vendor `0x9010`, `0x90C0`–`0x90CF`,
`0x90E8`–`0x90FB`, `0x9201`–`0x920F`, `0x92F0`–`0x92F7`, `0x9350`–`0x9361`,
`0x9400`–`0x9464`, `0x9509`–`0x9535` and `0x9801`–`0x9805`.

Dispatch runs the validate callback first, then the execute callback. An opcode that no
table contains answers `0x2005`; an opcode whose validate callback refuses answers
whatever code that callback chooses. This is why "accepted" and "produced data" are
separate questions on this surface. The tables hold 161 codes and the device-info dataset
advertises 94 operations; whether a given code appears in the advertised array has to be
read from that array rather than inferred from the table.

Handler addresses below are C 1.70 object code (MAS payload, `VA = 0x10020000 + O`), and
are listed so a later version can be compared rather than assumed.

## Data structures

### Device info dataset (`0x1001`)

531 bytes on this body, parsed in this order:

| Field | Type | Observed |
| --- | --- | --- |
| StandardVersion | u16 | `100` |
| VendorExtensionID | u32 | `0xffffffff` |
| VendorExtensionVersion | u16 | `0x6e` |
| VendorExtensionDesc | string | `Microsoft.com/DeviceServices: 1.0` |
| FunctionalMode | u16 | `0` |
| OperationsSupported | u16 array | 94 entries |
| EventsSupported | u16 array | 27 entries |
| DevicePropertiesSupported | u16 array | 21 entries |
| CaptureFormats | u16 array | `0x3801`, `0x3000` |
| ImageFormats | u16 array | `0x3801`, `0x3000`, `0x3001`, `0x380d`, `0x3002`, `0x300d`, `0xb982` |
| Manufacturer | string | `Nikon Corporation` |
| Model | string | `Z 7_2` |
| DeviceVersion | string | `V1.60` |
| SerialNumber | string | 24 hex characters |

A string is a one-byte character count followed by that many UTF-16LE code units; an
array is a `u32` byte count followed by that many bytes of `u16` elements. Reading the
string count as a byte count shifts every later field — the classic parse bug on this
dataset.

### Storage info dataset (`0x1005`)

| Field | Type | Observed on the CFexpress slot |
| --- | --- | --- |
| StorageType | u16 | `4` (removable RAM) |
| FilesystemType | u16 | `3` (DCF) |
| AccessCapability | u16 | `2` (read/write) |
| MaxCapacity | u64 | card capacity |
| FreeSpaceInBytes | u64 | free space |
| FreeSpaceInImages | u32 | free images |
| StorageDescription | string | vendor text |
| VolumeLabel | string | vendor text |

The empty SD slot answers `0x2001` with no data, which is different from an error.

### Object info dataset (`0x1008`)

This body omits the leading `ObjectHandle`, so the record starts at `StorageID`:

| Offset | Field | Type |
| --- | --- | --- |
| `0x00` | StorageID | u32 |
| `0x04` | ObjectFormat | u16 |
| `0x06` | ProtectionStatus | u16 |
| `0x08` | ObjectCompressedSize | u32 |
| `0x0c` | ThumbFormat | u16 |
| `0x0e` | ThumbCompressedSize | u32 |
| `0x12` | ThumbPixWidth | u32 |
| `0x16` | ThumbPixHeight | u32 |
| `0x1a` | ImagePixWidth | u32 |
| `0x1e` | ImagePixHeight | u32 |
| `0x22` | ImageBitDepth | u32 |
| `0x26` | ParentObject | u32 |
| `0x2a` | AssociationType | u16 |
| `0x2c` | AssociationDesc | u32 |
| `0x30` | SequenceNumber | u32 |
| `0x34` | Filename | string |
| ... | CaptureDate, ModificationDate, Keywords | strings |

Worked example: a folder handle returns `StorageID 0x00010001`, `ObjectFormat 0x3001`,
size `0`, parent `0`, filename `DCIM`. A photo returns `0x3801` or `0x3000`, a non-zero
size and a parent folder handle. A parser that assumes the standard `ObjectHandle`
prefix reads every field four bytes early and produces nonsense.

### Device property descriptor (`0x1014`) and value (`0x1015` / `0x1016`)

Standard layout: property code `u16`, data type `u16`, get/set byte, factory default,
current value, then a form flag and optional form data whose size depends on the data
type. `0x1014` reads the descriptor, `0x1015` reads the current value, `0x1016` writes
it. The 259 vendor property codes, their types, advertised access and captured values are
catalogued in [Vendor properties](properties).

Advertised `GetSet` does not prove a working setter: the inventory records what the
descriptor claims, not a successful write.

### Event container

Events arrive on the interrupt endpoint as type-4 containers: `u32 length, u16 type,
u16 code, u32 transaction`, then a `u32` parameter array. One observed example is
`length 16, code 0x4006, transaction 0xffffffff, params [0xc62d]`. The advertised event
codes are the standard `0x4001`–`0x400D` range plus vendor codes `0xc101`–`0xc122`;
their payload semantics are not established here.

## Standard operations

### `0x1001` GetDeviceInfo

No parameters, no data phase. Returns the device info dataset. On this body it is 531
bytes: standard version 100, vendor extension `Microsoft.com/DeviceServices: 1.0`,
functional mode 0, 94 operations, 27 events, 21 device properties, 2 capture formats,
7 image formats, then manufacturer `Nikon Corporation`, model `Z 7_2`, version `V1.60`
and the serial string.

This is the authoritative capability list. Anything not in the operation array is
rejected with `0x2005`. Note that being listed does not mean being functional: `0x100C`
and `0x100D` are listed and always refuse.

Handlers: `0x104643cf`, `0x104643f7`, `0x10464355`.

### `0x1002` OpenSession

Parameter is the session id, observed as `1`. Returns `0x2001`, or `0x201e` when a
session is already open. All later requests are only meaningful inside a session.

Handlers: `0x104797b9`, `0x10479839`, `0x104797a9`.

### `0x1003` CloseSession

No parameters. Returns `0x2001`. Sending it when no session is open is harmless and is a
useful way to clear a half-open session left by a failed run.

### `0x1004` GetStorageIDs

Returns a count and an array of storage ids. On this body: `0x00010001` (CFexpress) and
`0x00020000` (SD slot, unmounted).

Handlers: `0x10771e03`, `0x10771e85`, `0x10771e01`.

### `0x1005` GetStorageInfo

Parameter is a storage id. Returns a 70-byte dataset: storage type, filesystem type,
access capability, maximum capacity, free space, free images, description and volume
label. The CFexpress slot reports access capability 2, i.e. read/write. The unmounted SD
slot returns an empty dataset.

### `0x1007` GetObjectHandles

Parameters are `storage, format, parent`. `parent = 0` returns the object list; on this
body it yields 708 handles covering the card root and the nested `DCIM` tree.
`parent = 0xFFFFFFFF` returns a single handle, which makes it useless as an enumeration
shortcut here.

Handlers: `0x107710e1`, `0x107711c3`, `0x107710bb`.

### `0x1008` GetObjectInfo

Parameter is an object handle. Returns the object info dataset.

This body returns it **without the leading `ObjectHandle` field**, so the layout starts
at `StorageID`:

| Offset | Field |
| --- | --- |
| `0x00` | StorageID |
| `0x04` | ObjectFormat |
| `0x06` | ProtectionStatus |
| `0x08` | ObjectCompressedSize |
| `0x0c` | ThumbFormat |
| `0x0e` | ThumbCompressedSize |
| `0x12` | ThumbPixWidth |
| `0x16` | ThumbPixHeight |
| `0x1a` | ImagePixWidth |
| `0x1e` | ImagePixHeight |
| `0x22` | ImageBitDepth |
| `0x26` | ParentObject |
| `0x2a` | AssociationType |
| `0x2c` | AssociationDesc |
| `0x30` | SequenceNumber |
| `0x34` | Filename (PTP string) |

Folder objects report format `0x3001`, JPEG `0x3801`, and scene files `0x3000`. A parse
that assumes the standard `ObjectHandle` prefix reads every field four bytes early and
produces nonsense; this offset table is the one that matched the observed names and sizes.

### `0x100c` / `0x100d` SendObjectInfo / SendObject

Both are advertised and both are useless on this body. Every combination tried returned
`0x2020` (`SpecificationOfDestinationUnsupported`):

- object formats `0x3000`, `0x3801`, `0x3001`, `0x380d`, `0x3002`, `0x300d`, `0xb982`
  (all seven advertised image formats);
- parents: card root `0`, the `DCIM` folder handle, the `DCIM/102NZ7_2` folder handle,
  and `0xFFFFFFFF`;
- storages: the mounted CFexpress id and an unmounted id;
- datasets built both with and without the `ObjectHandle` prefix, and one built by
  copying a real JPEG object's own dataset and patching only size, name and format.

An unsupported opcode answers `0x2005`, so `0x2020` is a deliberate refusal from the
object-transfer path and not a missing handler. Treat card upload over USB PTP as
unavailable. This is the reason a firmware file cannot be planted by cable.

Handlers: `0x10771c3b`, `0x10771c63`, `0x10771c39`.

## Vendor operations

### `0x90c0` SDRAM capture

Reads a region through the object registry (object 9) and returns an object-style payload.
It calls the file service path, so it is the closest thing to a host-visible capture
primitive outside the service family. Observed output is a short metadata block and a
thumbnail-sized image, not a raw dump.

Handlers: `0x10490a7b`, `0x10490ac9`, `0x1049063f`.

### `0x90ca` Vendor property codes

Returns the vendor property inventory: a count followed by property codes. On this body
it is 522 bytes, 259 unique codes from `0xD016` to `0xD314`. Per-property descriptors and
values are read with the standard device-property operations.

Handlers: `0x105019ad`, `0x105019d5`.

### `0x9201` / `0x9202` / `0x9203` Live view

Start, stop and one frame. `0x9203` returns a header followed by a JPEG; the header
carries length and dimension fields. These are the operations the earlier physical work
used and they are unchanged by the service-mode surface.

### `0x9207` / `0x920c` Picture transfer

Handlers `0x10490acf` / `0x10490b2f` and `0x10490b9f` / `0x10490bdb`, both reaching the
file service at `0x104b3fda`. They belong to the download path and are not exercised for
memory access.

### `0x92f0` Diagnostic console

Takes no parameters and a host data phase containing null-terminated ASCII, e.g.
`debug memdump <address> <length>`. On this body the command is accepted by the
transport but **never answered**: no response container arrives, and the siblings
`0x92f3`–`0x92f7` stall the endpoint instead. The routing is unusual: `0x92E0`–`0x92FF` is
the only range that the classifier sends to class 8, and class 8 is the one class whose
table is reached as an offset from another (see [Dispatch tables](dispatch)). The receive
callback `0x10478fb3` is shared with `0x100C`, `0x100D`, `0x1016` and several `0x90` and
`0x94` rows, so the gate state matters; the path stayed silent both before and after the
diagnostic configuration byte was set.

Handlers: `0x10478fb3`, `0x1044415f`, `0x1044418b`, `0x1044415d`.

### `0x94fd` Service command

The workhorse. Parameter 0 carries a service subcommand; parameters 1–4 are the
subcommand's own. **It has no host data phase**; a data phase sent here leaves the camera
waiting and hangs the session.

Handlers: `0x107722e5`, `0x107722eb`, `0x107722e3`.

### `0x94fe` Service command with data

Same parameter layout, plus the receive callback that accepts a host data phase
(`0x107722f0 → 0x104c6aec`). This is the only way to deliver bytes to a service, which
makes it the write path for `0xF033`.

Handlers: `0x10772301`, `0x10772307`, `0x107722e3`.

### `0x94ff` Service command variant

A third entry in the same family with its own handler triple. Accepted, but no behaviour
beyond the family dispatch was established.

Handlers: `0x1077230d`, `0x10772313`, `0x107722e3`.

## Control

### `0x10ff` Unimplemented opcode

Used as a control: it is absent from the operation table and answers `0x2005`. Any future
measurement of a "refusal code" should compare against this, otherwise `0x201d` and
`0x2020` look like generic failures when they are not.
