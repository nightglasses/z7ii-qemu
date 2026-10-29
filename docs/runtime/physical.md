# Physical camera PTP and service interface

The archived Nikon Z 7II baseline reports `V1.60` and records USB capabilities,
property descriptors, preview bytes and service responses. It is separate from
the C1.70 source/execution target in [PTP command boundary](ptp.md): compatible
addresses, internal layouts and numerical algorithms have not been established.

All observations below were gathered from an attached physical Nikon Z 7II
without intrusive disassembly or firmware modification.

## Physical test baseline

| Parameter | Observed value |
|---|---|
| Hardware model | `NIKON DSC Z 7_2` |
| USB Vendor / Product ID | `0x04B0` / `0x044B` |
| Firmware version | `V1.60` (C1.70 compatibility unverified) |
| Serial number | `00000000000000000000000008027437` |
| USB interface | Class `0x06` (Still Imaging), SubClass `0x01`, Protocol `0x01` (PTP) |
| Endpoint configuration | EP `0x02` (Bulk OUT, 512 B), EP `0x81` (Bulk IN, 512 B), EP `0x83` (Interrupt IN, 8 B) |

The capability and property capture is archived in
[`ptp_profile.json`](/evidence/physical/ptp_profile.json).

## PTP capability and storage mapping

Standard `GetDeviceInfo` (`0x1001`) reports:

* **Standard version**: PTP 1.00 with vendor extension `0xFFFFFFFF` (version 1.10, `Microsoft.com/DeviceServices: 1.0`);
* **Operations**: 19 standard PTP operations plus 75 advertised vendor operations (`0x90C0..0x90CF`, `0x9201..0x920F`, `0x9400..0x945A`, `0x9520..0x9535`);
* **Events**: 27 asynchronous events on interrupt endpoint `0x83`;
* **Properties**: 21 exposed device properties covering exposure mode, shutter speed, aperture, ISO, white balance, battery level, and client handshake status.

`GetStorageIDs` (`0x1004`) and `GetStorageInfo` (`0x1005`) resolve the dual media slots:

| Storage ID | Slot | Physical media | File system | Capacity | Free space |
|---|---|---|---|---|---|
| `0x00010001` | Slot 1 | CFexpress Type B (512 GB) | DCF / Removable RAM | 476.88 GB | 452.97 GB |
| `0x00020000` | Slot 2 | SD slot (unpopulated) | Unmounted | 0.00 GB | 0.00 GB |

## Live view preview stream and framing header

Live view operation sequence:

1. `0x9201` (`StartLiveView`): initiates the camera live view stream;
2. `0x9203` (`GetLiveViewImg`): requests the next frame from the camera buffer;
3. `0x9202` (`EndLiveView`): stops live view and returns optical/sensor state to idle.

The archived `0x9203` sample has a 384-byte prefix and a 16,664-byte JPEG,
for 17,048 payload bytes. Header offsets below are hexadecimal and values are
decimal. The saved JPEGs decode to `640 × 424`. These are
sample observations, not a statement about every live-view mode.

The first fields decode as follows in big-endian order. Field names beyond
the lengths and JPEG dimensions are collector interpretations, not recovered
sensor/scaler semantics.

| Header offset | Width | Observed value | Interpretation limit |
| --- | --- | --- | --- |
| `00` | u32 | 376 | Reported header length; JPEG actually starts at 384 |
| `04` | u32 | 16,664 | Matches this sample's JPEG byte count |
| `08/0a` | u16/u16 | 640/424 | Matches decoded JPEG width/height |
| `0c/0e` | u16/u16 | 5408/3600 | Coordinate or extent fields; full sensor identity unproved |
| `10/12` | u16/u16 | 5408/3600 | Repeated extent fields; crop/full-raster roles unproved |
| `14/16` | u16/u16 | 2704/1800 | Half the repeated extents; coordinate role unproved |
| `18/1a` | u16/u16 | 276/306 | Collector labels these a focus-box extent |
| `1c/1e` | u16/u16 | 2704/1800 | Collector labels these a focus target |

The remaining prefix bytes have not been assigned validated field meanings.
The archived property value `5408x3600` is another reported image-size string;
it does not independently establish the sensor's native photosite dimensions.

![Physical Live View Sample](/evidence/physical/liveview_sample.jpg)

### Relevance to QEMU display pipeline

The preview supplies a physical image and framing example for transport tests.
It does not establish a full native sensor raster, the internal panel-buffer
size or an exact linear focus-coordinate transform. Those bindings require a
version-matched producer/consumer and controlled changes of image area, preview
size and focus position. The emulator's internal display formats remain scoped
to their own source and native contracts.

## Undocumented service mode operations (0x94fd..0x94ff)

Opcodes `0x94fd`, `0x94fe`, and `0x94ff` are not advertised in `GetDeviceInfo`,
but are dispatched by the MAS table at VA `0x119E1E74` as described in
[PTP command boundary](ptp.md).

The historical response report below records admission before/after `F001`.
The source function/address labels belong to software analysis; the capture
does not establish their V1.60 identity or directly observe execution at them.

The observed response sequence is consistent with a service-admission gate.
The runtime-memory capture does not confirm the previously proposed flag cell
or predicate pointer; see the [byte-level correction](physical_ram_nvram.md#runtime-range-bytes).
No direct RAM write or particular internal handler execution follows solely
from a successful PTP response.

### Reported historical response matrix

The state labels in this report describe session ordering, not observed
values at a validated RAM flag. Only F001/F091 are recorded in the archived
capability profile; other rows remain historical reported observations.

| Opcode | Subcommand | Reported admission state | Response | Params | Interpretation limit |
|---|---|---|---|---|---|
| `0x94FD` | `0xF001` | Locked (`0`) | `0x2001` (OK) | `0x2001` | Reported admission after F001; internal flag binding unverified |
| `0x94FD` | `0xF001` | Active (`1`) | `0x2001` (OK) | `0x201E` | Already unlocked / session active |
| `0x94FD` | `0xF091` | Locked (`0`) | `0xA004` (Error) | `0x0000` | Reported rejection; internal predicate path not directly observed |
| `0x94FD` | `0xF091` | Active (`1`) | `0x2001` (OK) | `0x2001` | Reported successful response; archived profile latency 516.74 ms; historical report 514 ms |
| `0x94FD` | `0xF092` | Active (`1`) | `0x2001` (OK) | `0x2001` | Reported successful response; handler effects not observed |
| `0x94FD` | `0xF093` | Active (`1`) | `0x2001` (OK) | `0x2001` | Reported successful response; display effects not observed |
| `0x94FD` | `0xF094` | Active (`1`) | `0x2001` (OK) | `0x2001` | Reported successful response; display effects not observed |
| `0x94FD` | `0xF165` | Active (`1`) | `0x2001` (OK) | `0x2001` | Reported successful response; bootstrap effects not observed |
| `0x94FD` | `0xF002` | Active (`1`) | `0x2001` (OK) | `0x2001` | Reported lock response; internal flag binding unverified |

The report can be compared with the source hypotheses in [PTP command boundary](ptp.md):

* The software analysis describes outer envelopes and service rows; response
  captures alone do not verify every dispatch path.
* The reported F001/F091 ordering supports an admission dependency.
* The archived capability profile records successful F001 and F091 responses,
  not their internal side effects or all rows of the historical matrix.

## Full vendor device property enumeration (0xD016..0xD314)

While standard `GetDeviceInfo` reports only 21 general properties, vendor opcode
`0x90CA` returns an inventory of **259 unique vendor property codes** in this capture:

* Raw inventory binary: [`payload_90ca.bin`](/evidence/physical/payload_90ca.bin) (522 bytes, 259 `u16` codes);
* Range: `0xD016` to `0xD314`;
* Full dump: [`vendor_properties_259.json`](/evidence/physical/vendor_properties_259.json), recording data types, advertised access flags, default values and current values;
* Annotated lookup registry: [Nikon Vendor Device Properties Inventory](ptp_vendor_properties.md), separating captured descriptor fields from editorial lookup names and unverified menu interpretations.

## Exhaustive F0 service table mapping (0xF000..0xF200)

A systematic scan of the `0xF000..0xF200` range through `0x94FD` in unlocked state
classified **490 responding subcommands** (previously associated with the MAS firmware table
at MAS VA `0x119E405C`, a version/address binding not independently verified by this scan):

* **Success responses**: 111 subcommands return `0x2001` (OK) directly with zero or default arguments;
* **Data-returning subcommands**: 11 subcommands stream structured binary responses:
  * `0xF006`: 4-byte engine status word;
  * `0xF04C`: 60-byte task and clock telemetry record;
  * `0xF050` / `0xF051` / `0xF052`: 28, 28, and 80-byte component identification tables containing ASCII `'01000160'`;
  * `0xF06E`: 296-byte body parameter record;
  * `0xF113` / `0xF114`: 8 and 12-byte sensor/timing timestamps;
  * `0xF162` / `0xF163`: 12-byte status and 1402-byte bootstrap scratch block;
  * `0xF1F9`: 8-byte subsystem status.

All 11 payload captures are archived under [`service_payloads/`](/evidence/physical/service_payloads/manifest.json).

## Physical RAM, NVRAM, and calibration extraction

Using service-address request `0xF032`, NVRAM resource selector `0xF029`,
and subsystem block commands, the collector saved requested ranges and prefixes.
The archived manifest lacks response codes and complete block extents:

* **Request address `0x04000000`**: 1 MiB containing instruction patterns and stack literals; effective execution mapping remains unresolved;
* **Request address `0xB188B000`**: 8 KiB of runtime bytes; earlier state-cell bindings contradict several captured values;
* **F029 selectors 0..6**: requested NVRAM prefixes with collector-assigned resource names, including an `AFadjustkohatsu` signature; no complete resource80 input is established;
* **Other service payloads**: `AUDIO` and serial text signatures; battery/thermal/OTP interpretations remain unverified, and the F03A capture is entirely erased bytes;
* **Reported SDRAM capture (`0x90C0`)**: object metadata and a recovered thumbnail, without archived full RAW/JPEG objects or proof of every pipeline stage.

See [Physical memory and NVRAM capture bounds](physical_ram_nvram.md)
for exact byte observations, corrected interpretations and input limitations.

## Receipts and verification tools

* Probe client: `tools/ptp/ptp_client.py`
* Automated profile scanner: `tools/ptp/probe_full.py`
* Vendor opcode scanner: `tools/ptp/scan_vendor_ops.py`
* F0 service table scanner: `tools/ptp/scan_service_table.py`
* Vendor property dumper: `tools/ptp/dump_vendor_properties.py`
* Service payload dumper: `tools/ptp/dump_service_payloads.py`
* RAM and calibration dumper: `tools/ptp/dump_camera_ram.py`
* Complete capability JSON: [ptp_profile.json](/evidence/physical/ptp_profile.json)
* Vendor properties database: [vendor_properties_259.json](/evidence/physical/vendor_properties_259.json)
* F0 service table scan results: [service_keys_scan.json](/evidence/physical/service_keys_scan.json)
* RAM and calibration manifest: [ram_dumps/manifest.json](/evidence/physical/ram_dumps/manifest.json)
* Binary live view header: [liveview_header.bin](/evidence/physical/liveview_header.bin)
* Live view sample image: [liveview_sample.jpg](/evidence/physical/liveview_sample.jpg)
* SDRAM captured thumbnail: [sdram_captured_thumb.jpg](/evidence/physical/sdram_captured_thumb.jpg)
