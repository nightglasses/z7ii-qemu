# Nikon vendor device properties inventory (0xD016..0xD314)

This inventory records the 259 unique vendor property codes returned by the
archived Nikon Z 7II (`V1.60`) `90ca` response. The JSON records descriptor
fields from `1014` and value bytes from `1015`; it is a capture of that device
state, not an exhaustive registry for C1.70 or every camera mode.

The descriptors advertise 169 `GetSet` and 90 `GetOnly` entries. `GetSet` is an
advertised flag, not a successful setter test: the property dumper issues no
`1016` writes. Device state and other policy may constrain a later write.

Types, flags and current values below come from the archived JSON. Symbol
names come from an external, unversioned header lookup plus manual aliases;
subsystem groups and descriptions are editorial interpretations. No pinned
header identity, per-property menu experiment or source consumer is supplied.
The lookup column therefore does not establish physical hardware roles,
enumeration meanings, units, image algorithms or a register-level interface.

Source dataset: [`vendor_properties_259.json`](/evidence/physical/vendor_properties_259.json)

## Editorial subsystem grouping

| ID | Subsystem | Domain | Properties |
|---|---|---|---|
| `SYSTEM_TELEMETRY` | [System configuration, orientation, and power telemetry](#system-telemetry) | System Configuration & Telemetry | 9 |
| `WB_COLOR` | [White balance and color calibration](#wb-color) | White Balance & Color Calibration | 80 |
| `MOVIE_AUDIO_TC` | [Movie recording, audio, and timecode engine](#movie-audio-tc) | Movie Recording, Audio & Timecode Engine | 42 |
| `AUTOFOCUS` | [Autofocus and focus peaking assistance](#autofocus) | Autofocus & Focus Peaking Assistance | 10 |
| `LENS_IBIS` | [Lens optics, manual non-CPU profile, and IBIS](#lens-ibis) | Lens Optics, Non-CPU Profile & IBIS | 13 |
| `IMAGE_PROCESSING` | [Image processing pipeline, RAW compression, and Picture Controls](#image-processing) | Image Pipeline, RAW & Picture Controls | 16 |
| `FLASH_LIGHTING` | [Creative Lighting System (CLS) and flash control](#flash-lighting) | Creative Lighting System & Flash Control | 26 |
| `LIVEVIEW_DISPLAY` | [Live view streaming, EVF, and monitor control](#liveview-display) | Live View Streaming, EVF & Monitor | 15 |
| `EXPOSURE_DRIVE` | [Exposure mechanics, metering base, and shutter drive](#exposure-drive) | Exposure Mechanics, Metering & Shutter | 41 |
| `STORAGE_FILES` | [Dual card slots and storage naming](#storage-files) | Dual Card Slots & Storage Architecture | 7 |

## System configuration, orientation, and power telemetry

Editorial group: **System Configuration & Telemetry** (9 properties).

| Code | Lookup symbol | Type | Advertised access | Captured current | Unverified lookup description |
|---|---|---|---|---|---|
| `0XD053` | `EnableCopyright` | `UINT8` | `GetSet` | `0` | Exif copyright and artist metadata embedding switch (0=Off, 1=On) |
| `0XD072` | `ArtistName` | `STRING` | `GetSet` | `` | Photographer artist name string |
| `0XD08F` | `CleanImageSensor` | `UINT8` | `GetSet` | `2` | Image sensor piezoelectric ultrasonic vibration dust cleaning (0=Off, 1=Startup, 2=Shutdown, 3=Startup+Shutdown) |
| `0XD090` | `ImageCommentString` | `STRING` | `GetSet` | `` | User image comment UTF-16LE text string |
| `0XD091` | `ImageCommentEnable` | `UINT8` | `GetSet` | `0` | User image comment metadata embedding switch (0=Off, 1=On) |
| `0XD101` | `ACPower` | `UINT8` | `GetOnly` | `0` | External power supply and USB-PD adapter status (0=Battery, 1=EH-7P AC adapter / USB power delivery) |
| `0XD102` | `WarningStatus` | `UINT32` | `GetOnly` | `0` | Global system warning and error telemetry bitmask (lens fault, card error, over-temperature, low battery) |
| `0XD10E` | `CameraOrientation` | `UINT8` | `GetOnly` | `0` | Tri-axis orientation accelerometer posture (0=Landscape, 1=Portrait clockwise, 2=Portrait counter-clockwise, 3=Inverted) |
| `0XD12E` | `ConnectionPath` | `UINT8` | `GetOnly` | `0` | Active network and system transport topology (0=USB local bus, 1=WT-7 network adapter, 2=Built-in Wi-Fi/Bluetooth) |

## White balance and color calibration

Editorial group: **White Balance & Color Calibration** (80 properties).

| Code | Lookup symbol | Type | Advertised access | Captured current | Unverified lookup description |
|---|---|---|---|---|---|
| `0XD017` | `WhiteBalanceAutoBias` | `UINT16` | `GetSet` | `612` | Auto White Balance (AWB) fine-tuning bias coordinates (A-B amber/blue, G-M green/magenta, 612=zero bias) |
| `0XD018` | `WhiteBalanceTungstenBias` | `UINT16` | `GetSet` | `612` | Tungsten incandescent white balance fine-tuning bias |
| `0XD019` | `WhiteBalanceFluorescentBias` | `UINT16` | `GetSet` | `612` | Fluorescent white balance fine-tuning bias |
| `0XD01A` | `WhiteBalanceDaylightBias` | `UINT16` | `GetSet` | `612` | Daylight white balance fine-tuning bias |
| `0XD01B` | `WhiteBalanceFlashBias` | `UINT16` | `GetSet` | `612` | Flash white balance fine-tuning bias |
| `0XD01C` | `WhiteBalanceCloudyBias` | `UINT16` | `GetSet` | `612` | Cloudy white balance fine-tuning bias |
| `0XD01D` | `WhiteBalanceShadeBias` | `UINT16` | `GetSet` | `612` | Shade white balance fine-tuning bias |
| `0XD01E` | `WhiteBalanceColorTemperature` | `UINT16` | `GetSet` | `6500` | Manual Kelvin color temperature setting (2500K to 10000K, observed: 6500K) |
| `0XD01F` | `WhiteBalancePresetNo` | `UINT8` | `GetSet` | `1` | Manual preset white balance active slot (PRE d-1 to d-6, observed: slot 1) |
| `0XD021` | `WhiteBalancePresetName1` | `STRING` | `GetSet` | `                 ...` | Manual preset white balance d-1 user comment string |
| `0XD022` | `WhiteBalancePresetName2` | `STRING` | `GetSet` | `                 ...` | Manual preset white balance d-2 user comment string |
| `0XD023` | `WhiteBalancePresetName3` | `STRING` | `GetSet` | `                 ...` | Manual preset white balance d-3 user comment string |
| `0XD024` | `WhiteBalancePresetName4` | `STRING` | `GetSet` | `                 ...` | Manual preset white balance d-4 user comment string |
| `0XD026` | `WhiteBalancePresetVal1` | `UINT32` | `GetOnly` | `230165216` | Manual preset white balance d-1 chromaticity calibration matrix |
| `0XD027` | `WhiteBalancePresetVal2` | `UINT32` | `GetOnly` | `230165216` | Manual preset white balance d-2 chromaticity calibration matrix |
| `0XD028` | `WhiteBalancePresetVal3` | `UINT32` | `GetOnly` | `230165216` | Manual preset white balance d-3 chromaticity calibration matrix |
| `0XD029` | `WhiteBalancePresetVal4` | `UINT32` | `GetOnly` | `230165216` | Manual preset white balance d-4 chromaticity calibration matrix |
| `0XD038` | `WhiteBalancePresetName5` | `STRING` | `GetSet` | `                 ...` | Manual preset white balance d-5 user comment string |
| `0XD039` | `WhiteBalancePresetName6` | `STRING` | `GetSet` | `                 ...` | Manual preset white balance d-6 user comment string |
| `0XD03A` | `WhiteBalanceTunePreset5` | `UINT16` | `GetSet` | `612` | Manual preset white balance d-5 fine-tuning bias |
| `0XD03B` | `WhiteBalanceTunePreset6` | `UINT16` | `GetSet` | `612` | Manual preset white balance d-6 fine-tuning bias |
| `0XD03C` | `WhiteBalancePresetProtect5` | `UINT8` | `GetSet` | `0` | Manual preset white balance d-5 write protection lock (0=Unlocked, 1=Protected) |
| `0XD03D` | `WhiteBalancePresetProtect6` | `UINT8` | `GetSet` | `0` | Manual preset white balance d-6 write protection lock |
| `0XD03E` | `WhiteBalancePresetValue5` | `UINT32` | `GetOnly` | `230165216` | Manual preset white balance d-5 chromaticity calibration matrix |
| `0XD03F` | `WhiteBalancePresetValue6` | `UINT32` | `GetOnly` | `230165216` | Manual preset white balance d-6 chromaticity calibration matrix |
| `0XD0A3` | `MovFileSlot` | `UINT8` | `GetSet` | `1` | Movie storage destination slot (0=Slot 1 CFexpress, 1=Slot 2 SD) |
| `0XD0C2` | `AutoExposureBracketProgram` | `UINT16` | `GetSet` | `0` | Auto exposure bracketing program mode (AE+Flash / AE only) |
| `0XD0C3` | `AutoExposureBracketCount` | `UINT8` | `GetOnly` | `1` | Auto exposure bracketing shot count (3, 5, 7, or 9 shots) |
| `0XD0C4` | `WhiteBalanceBracketStep` | `UINT8` | `GetOnly` | `1` | White balance bracketing color shift step span (1 to 3 steps) |
| `0XD0C5` | `WhiteBalanceBracketProgram` | `UINT16` | `GetOnly` | `0` | White balance bracketing program configuration |
| `0XD141` | `WBAutoType` | `UINT8` | `GetSet` | `0` | Auto White Balance program subtype (0=Keep white, 1=Standard, 2=Keep warm lighting colors) |
| `0XD149` | `RawBitMode` | `UINT8` | `GetSet` | `14` | RAW quantization bit depth (0=12-bit high-speed, 1=14-bit high dynamic range) |
| `0XD14F` | `FlourescentType` | `UINT8` | `GetSet` | `3` | Fluorescent lamp subtype (Sodium, Warm-white, Cool-white, Day-white, Daylight, High-temp mercury) |
| `0XD150` | `TuneColourTemperature` | `UINT8` | `GetSet` | `24` | Manual Kelvin color temperature step tuning |
| `0XD152` | `TunePreset1` | `UINT16` | `GetSet` | `612` | Manual preset white balance d-1 fine-tuning bias |
| `0XD153` | `TunePreset2` | `UINT16` | `GetSet` | `612` | Manual preset white balance d-2 fine-tuning bias |
| `0XD154` | `TunePreset3` | `UINT16` | `GetSet` | `612` | Manual preset white balance d-3 fine-tuning bias |
| `0XD155` | `TunePreset4` | `UINT16` | `GetSet` | `612` | Manual preset white balance d-4 fine-tuning bias |
| `0XD158` | `WBPresetProtect1` | `UINT8` | `GetSet` | `0` | Manual preset white balance d-1 write protection lock |
| `0XD159` | `WBPresetProtect2` | `UINT8` | `GetSet` | `0` | Manual preset white balance d-2 write protection lock |
| `0XD15A` | `WBPresetProtect3` | `UINT8` | `GetSet` | `0` | Manual preset white balance d-3 write protection lock |
| `0XD15C` | `WBPresetProtect4` | `UINT8` | `GetSet` | `0` | Manual preset white balance d-4 write protection lock |
| `0XD15E` | `WhiteBalanceNaturalLightAutoBias` | `UINT16` | `GetSet` | `612` | Natural Light Auto white balance fine-tuning bias |
| `0XD211` | `MovieWbAutoType` | `UINT8` | `GetSet` | `0` | Movie mode auto white balance subtype (AWB0 / AWB1 / AWB2) |
| `0XD212` | `MovieWbTuneAuto` | `UINT16` | `GetSet` | `612` | Movie mode auto white balance dedicated bias |
| `0XD213` | `MovieWbTuneIncandescent` | `UINT16` | `GetSet` | `612` | Movie mode incandescent tungsten dedicated bias |
| `0XD214` | `MovieWbFlourescentType` | `UINT8` | `GetSet` | `3` | Movie mode fluorescent lamp subtype |
| `0XD215` | `MovieWbTuneFlourescent` | `UINT16` | `GetSet` | `612` | Movie mode fluorescent dedicated bias |
| `0XD216` | `MovieWbTuneSunny` | `UINT16` | `GetSet` | `612` | Movie mode daylight sunny dedicated bias |
| `0XD218` | `MovieWbTuneCloudy` | `UINT16` | `GetSet` | `612` | Movie mode cloudy dedicated bias |
| `0XD219` | `MovieWbTuneShade` | `UINT16` | `GetSet` | `612` | Movie mode shade dedicated bias |
| `0XD21A` | `MovieWbColorTemp` | `UINT16` | `GetSet` | `5000` | Movie mode Kelvin color temperature setting (K) |
| `0XD21B` | `MovieWbTuneColorTemp` | `UINT8` | `GetSet` | `24` | Movie mode color temperature step fine tuning |
| `0XD21C` | `MovieWbPresetData0` | `UINT8` | `GetSet` | `1` | Movie mode preset white balance slot 0 raw data |
| `0XD21D` | `MovieWbPresetDataComment1` | `STRING` | `GetSet` | `                 ...` | Movie mode preset white balance d-1 comment string |
| `0XD21E` | `MovieWbPresetDataComment2` | `STRING` | `GetSet` | `                 ...` | Movie mode preset white balance d-2 comment string |
| `0XD21F` | `MovieWbPresetDataComment3` | `STRING` | `GetSet` | `                 ...` | Movie mode preset white balance d-3 comment string |
| `0XD220` | `MovieWbPresetDataComment4` | `STRING` | `GetSet` | `                 ...` | Movie mode preset white balance d-4 comment string |
| `0XD221` | `MovieWbPresetDataComment5` | `STRING` | `GetSet` | `                 ...` | Movie mode preset white balance d-5 comment string |
| `0XD222` | `MovieWbPresetDataComment6` | `STRING` | `GetSet` | `                 ...` | Movie mode preset white balance d-6 comment string |
| `0XD223` | `MovieWbPresetDataValue1` | `UINT32` | `GetOnly` | `230165216` | Movie mode preset white balance d-1 measured chromaticity |
| `0XD224` | `MovieWbPresetDataValue2` | `UINT32` | `GetOnly` | `230165216` | Movie mode preset white balance d-2 measured chromaticity |
| `0XD225` | `MovieWbPresetDataValue3` | `UINT32` | `GetOnly` | `230165216` | Movie mode preset white balance d-3 measured chromaticity |
| `0XD226` | `MovieWbPresetDataValue4` | `UINT32` | `GetOnly` | `230165216` | Movie mode preset white balance d-4 measured chromaticity |
| `0XD227` | `MovieWbPresetDataValue5` | `UINT32` | `GetOnly` | `230165216` | Movie mode preset white balance d-5 measured chromaticity |
| `0XD228` | `MovieWbPresetDataValue6` | `UINT32` | `GetOnly` | `230165216` | Movie mode preset white balance d-6 measured chromaticity |
| `0XD229` | `MovieWbTunePreset1` | `UINT16` | `GetSet` | `612` | Movie mode preset white balance d-1 fine-tuning bias |
| `0XD22A` | `MovieWbTunePreset2` | `UINT16` | `GetSet` | `612` | Movie mode preset white balance d-2 fine-tuning bias |
| `0XD22B` | `MovieWbTunePreset3` | `UINT16` | `GetSet` | `612` | Movie mode preset white balance d-3 fine-tuning bias |
| `0XD22C` | `MovieWbTunePreset4` | `UINT16` | `GetSet` | `612` | Movie mode preset white balance d-4 fine-tuning bias |
| `0XD22D` | `MovieWbTunePreset5` | `UINT16` | `GetSet` | `612` | Movie mode preset white balance d-5 fine-tuning bias |
| `0XD22E` | `MovieWbTunePreset6` | `UINT16` | `GetSet` | `612` | Movie mode preset white balance d-6 fine-tuning bias |
| `0XD22F` | `MovieWbPresetProtect1` | `UINT8` | `GetSet` | `0` | Movie mode preset white balance d-1 write protection lock |
| `0XD230` | `MovieWbPresetProtect2` | `UINT8` | `GetSet` | `0` | Movie mode preset white balance d-2 write protection lock |
| `0XD231` | `MovieWbPresetProtect3` | `UINT8` | `GetSet` | `0` | Movie mode preset white balance d-3 write protection lock |
| `0XD232` | `MovieWbPresetProtect4` | `UINT8` | `GetSet` | `0` | Movie mode preset white balance d-4 write protection lock |
| `0XD233` | `MovieWbPresetProtect5` | `UINT8` | `GetSet` | `0` | Movie mode preset white balance d-5 write protection lock |
| `0XD234` | `MovieWbPresetProtect6` | `UINT8` | `GetSet` | `0` | Movie mode preset white balance d-6 write protection lock |
| `0XD23A` | `MovieWhiteBalance` | `UINT16` | `GetSet` | `32789` | Movie mode independent white balance selection (Auto, Daylight, Cloudy, Tungsten, Kelvin, Preset) |
| `0XD23C` | `MovieWbTuneNatural` | `UINT16` | `GetSet` | `612` | Movie mode natural light auto white balance fine-tuning bias |

## Movie recording, audio, and timecode engine

Editorial group: **Movie Recording, Audio & Timecode Engine** (42 properties).

| Code | Lookup symbol | Type | Advertised access | Captured current | Unverified lookup description |
|---|---|---|---|---|---|
| `0XD0A0` | `MovScreenSize` | `0x0008` | `GetSet` | `1080873187699851264` | Movie frame resolution and frame rate profile (4K UHD 60p/50p/30p/25p/24p, FHD 120p/100p/60p/50p/30p) |
| `0XD0A2` | `MovMicrophone` | `UINT8` | `GetSet` | `0` | Movie built-in stereo microphone channel routing |
| `0XD0A4` | `MovRecProhibitCondition` | `UINT32` | `GetOnly` | `24576` | Movie recording prohibited condition mask (card full, thermal throttle, shutter busy) |
| `0XD0A7` | `MovQuality` | `UINT8` | `GetOnly` | `0` | Movie compression bitrate tier (High Quality / Normal bitrate) |
| `0XD0A8` | `MovRecordMicrophoneLevelValue` | `UINT8` | `GetSet` | `15` | Movie real-time audio peak recording level telemetry |
| `0XD0AA` | `MovWindNoiseReduction` | `UINT8` | `GetSet` | `0` | Movie microphone wind noise reduction high-pass filter (0=Off, 1=On) |
| `0XD0AC` | `MovRecordingZone` | `UINT8` | `GetSet` | `0` | Movie sensor crop recording zone (FX full-frame downsampled vs DX crop) |
| `0XD0AD` | `MovISOAutoControl` | `UINT8` | `GetSet` | `0` | Movie auto ISO sensitivity control master switch (0=Off, 1=On) |
| `0XD0AE` | `MovISOAutoHighLimit` | `UINT32` | `GetSet` | `25600` | Movie auto ISO maximum floating upper limit ceiling |
| `0XD0AF` | `MovFileType` | `UINT8` | `GetSet` | `0` | Movie container file format (0=MOV broadcast grade, 1=MP4 general web) |
| `0XD0BB` | `MovieLogOutput` | `UINT8` | `GetOnly` | `0` | Movie color gamma curve output mode (0=SDR Rec.709, 1=N-Log 10-bit log, 2=HLG HDR hybrid log gamma) |
| `0XD0BC` | `MovieAutoDistortion` | `UINT8` | `GetOnly` | `1` | Movie real-time optical geometric distortion correction |
| `0XD0BD` | `MovieRemainingTime` | `UINT32` | `GetOnly` | `0` | Current storage card remaining movie recording headroom (seconds) |
| `0XD0BF` | `MovieLogSetting` | `UINT8` | `GetSet` | `1` | Movie Log shooting monitor color assistance (View Assist switch) |
| `0XD0CC` | `HDMIOutputDataDepth` | `UINT8` | `GetSet` | `1` | HDMI clean output video stream bit depth (0=8-bit, 1=10-bit 4:2:2 ProRes/RAW output) |
| `0XD17C` | `MovieAfSpeed` | `INT8` | `GetSet` | `0` | Movie AF drive speed damping step (-5 cinematic slow transition to +5 snappy pull) |
| `0XD17D` | `MovieAfSpeedWhenToApply` | `UINT8` | `GetSet` | `0` | Movie AF speed application condition (0=Always, 1=Only during active recording) |
| `0XD17E` | `MovieAfTrackingSensitivity` | `UINT8` | `GetSet` | `4` | Movie AF target tracking sensitivity (1=Responsive to 7=Smooth cinematic delay) |
| `0XD197` | `MovieReleaseButton` | `UINT8` | `GetSet` | `0` | Shutter release button assignment in video mode (Take photo vs Start/stop movie recording) |
| `0XD1A8` | `MovieShutterSpeed` | `UINT32` | `GetOnly` | `65561` | Movie mode dedicated shutter speed control (e.g. 1/50s, 1/100s, 1/120s) |
| `0XD1A9` | `MovieFNumber` | `UINT16` | `GetOnly` | `0` | Movie mode dedicated aperture control |
| `0XD1AA` | `MovieISO` | `UINT32` | `GetSet` | `64` | Movie mode dedicated ISO sensitivity control |
| `0XD1AB` | `MovieExposureBiasCompensation` | `INT16` | `GetOnly` | `-1333` | Movie mode dedicated exposure compensation bias |
| `0XD1AF` | `MovieExposureMeteringMode` | `UINT16` | `GetSet` | `3` | Movie mode dedicated metering pattern (Matrix / Center-weighted / Highlight-weighted) |
| `0XD1B7` | `MovieRecFrameCount` | `UINT8` | `GetOnly` | `20` | Movie active recorded frame counter for current clip |
| `0XD1DE` | `ExternalRecordingControl` | `UINT8` | `GetSet` | `1` | HDMI external recorder remote trigger control (0=Off, 1=On, triggers Atomos Ninja / Blackmagic) |
| `0XD1DF` | `HighlightBrightness` | `UINT16` | `GetSet` | `248` | Highlight display monitoring baseline tuning |
| `0XD1F8` | `MovieFocusMeteringMode` | `UINT16` | `GetSet` | `32800` | Movie focus and metering linked mode configuration |
| `0XD1F9` | `MovieVibrationReduction` | `UINT8` | `GetSet` | `1` | Movie optical and electronic stabilization master switch |
| `0XD1FA` | `MovieFocusMode` | `UINT8` | `GetSet` | `2` | Movie mode focus servo mode (AF-C / AF-F / MF) |
| `0XD1FB` | `RecordTimeCodes` | `UINT8` | `GetOnly` | `0` | Timecode recording master switch (0=Off, 1=On) |
| `0XD1FC` | `CountUpMethod` | `UINT8` | `GetOnly` | `0` | Timecode count-up method (0=Rec Run only during active recording, 1=Free Run continuous 24-hour) |
| `0XD1FD` | `TimeCodeOrigin` | `0x4002` | `GetOnly` | `None` | Timecode initial origin preset (0=Reset to 00:00:00:00, 1=Manual Entry) |
| `0XD1FE` | `DropFrame` | `UINT8` | `GetOnly` | `1` | Timecode drop-frame compensation mode (0=Non-Drop Frame NDF, 1=Drop Frame DF compensating 29.97/59.94fps clock drift) |
| `0XD1FF` | `MovieHDMIOutputTimeCode` | `UINT16` | `GetSet` | `1` | HDMI video stream embedded SMPTE timecode metadata (0=Disabled, 1=Embedded) |
| `0XD20F` | `MovieCaptureAreaCrop` | `UINT8` | `GetOnly` | `2` | Movie sensor crop mode (FX full-frame downsampling vs DX crop) |
| `0XD236` | `MovieNrHighISO` | `UINT16` | `GetSet` | `3` | Movie high ISO noise reduction strength |
| `0XD237` | `MovieActivePicCtrlItem` | `UINT16` | `GetSet` | `8` | Movie mode independent Picture Control profile item |
| `0XD23B` | `MovieActiveDLighting` | `UINT16` | `GetSet` | `3` | Movie mode independent Active D-Lighting strength |
| `0XD23D` | `MovieAttenuator` | `UINT8` | `GetSet` | `0` | Movie microphone attenuator against audio clipping and acoustic distortion (0=Off, 1=On) |
| `0XD23E` | `MovieVignetteControl` | `UINT16` | `GetSet` | `100` | Movie mode peripheral illumination vignette compensation |
| `0XD23F` | `MovieDiffractionCompensation` | `UINT8` | `GetSet` | `1` | Movie mode optical diffraction correction |

## Autofocus and focus peaking assistance

Editorial group: **Autofocus & Focus Peaking Assistance** (10 properties).

| Code | Lookup symbol | Type | Advertised access | Captured current | Unverified lookup description |
|---|---|---|---|---|---|
| `0XD046` | `AFStillLockOnAcross` | `UINT8` | `GetSet` | `2` | Still AF tracking lock-on responsiveness to crossing obstacles (1=Quick to 5=Delayed) |
| `0XD048` | `A1AFCModePriority` | `UINT8` | `GetSet` | `0` | AF-C continuous servo release priority (0=Release, 1=Release+Focus, 2=Focus) |
| `0XD049` | `A2AFSModePriority` | `UINT8` | `GetSet` | `3` | AF-S single servo release priority (0=Release, 1=Focus) |
| `0XD05D` | `StillFocusMeteringMode` | `UINT16` | `GetSet` | `32800` | Still photography focus point linked spot metering mode |
| `0XD061` | `StillFocusMode` | `UINT8` | `GetSet` | `1` | Still photography real-time focus servo state |
| `0XD08D` | `AFAreaPoint` | `UINT8` | `GetSet` | `0` | Focus point selection density (0=Full 493 points, 1=Reduced point alternate grid) |
| `0XD0CD` | `SaveFocusPosition` | `UINT8` | `GetSet` | `1` | Save focus position on camera power-off (0=Off/reset to infinity, 1=On/preserve position) |
| `0XD104` | `AFLockStatus` | `UINT8` | `GetOnly` | `0` | Autofocus lock (AF-L) active status flag |
| `0XD17A` | `LowLightAF` | `UINT8` | `GetSet` | `1` | Low-light AF detection enhancement algorithm (0=Off, 1=On) |
| `0XD1B9` | `DetectionPeaking` | `UINT8` | `GetSet` | `3` | Focus peaking highlight detection master switch |

## Lens optics, manual non-CPU profile, and IBIS

Editorial group: **Lens Optics, Non-CPU Profile & IBIS** (13 properties).

| Code | Lookup symbol | Type | Advertised access | Captured current | Unverified lookup description |
|---|---|---|---|---|---|
| `0XD02E` | `FmmManualSetting` | `UINT16` | `GetSet` | `0` | Non-CPU lens manual focal length registration (0 to 1000mm, used for IBIS and metering baseline) |
| `0XD02F` | `F0ManualSetting` | `UINT16` | `GetSet` | `0` | Non-CPU lens manual maximum aperture registration (f/1.2 to f/22) |
| `0XD093` | `ManualSetLensNo` | `UINT8` | `GetSet` | `0` | Non-CPU lens memory slot index (slots 1 to 9) |
| `0XD09C` | `RetractableLensWarning` | `UINT8` | `GetOnly` | `0` | Retractable lens unextended caution prompt (0=Off, 1=On) |
| `0XD0E0` | `LensID` | `UINT16` | `GetOnly` | `11` | Lens hardware telemetry ID (identifies mount protocol, manufacturing batch, and electrical profile) |
| `0XD0E1` | `LensSort` | `UINT8` | `GetOnly` | `1` | Lens physical mount category (Native Z mount / FTZ adapter F mount) |
| `0XD0E2` | `LensType` | `0x0008` | `GetOnly` | `0` | Lens aperture actuation mechanism (Type-E electromagnetic / Type-G mechanical / Type-D manual ring) |
| `0XD0E3` | `FocalLengthMin` | `UINT32` | `GetOnly` | `1600` | Minimum physical focal length at wide end (mm, observed: 16mm) |
| `0XD0E4` | `FocalLengthMax` | `UINT32` | `GetOnly` | `5000` | Maximum physical focal length at telephoto end (mm, observed: 50mm) |
| `0XD0E5` | `MaxApAtMinFocalLength` | `UINT16` | `GetOnly` | `350` | Maximum physical aperture available at minimum focal length (e.g. f/3.5) |
| `0XD0E6` | `MaxApAtMaxFocalLength` | `UINT16` | `GetOnly` | `630` | Maximum physical aperture available at maximum focal length (e.g. f/6.3) |
| `0XD0E7` | `LensTypeML` | `0x0008` | `GetOnly` | `176` | Mirrorless macro focus and stepping motor actuation profile |
| `0XD138` | `VibrationReduction` | `UINT8` | `GetSet` | `2` | In-Body Image Stabilization (IBIS 5-axis VR) master switch (0=Off, 1=Normal, 2=Sport) |

## Image processing pipeline, RAW compression, and Picture Controls

Editorial group: **Image Pipeline, RAW & Picture Controls** (16 properties).

| Code | Lookup symbol | Type | Advertised access | Captured current | Unverified lookup description |
|---|---|---|---|---|---|
| `0XD016` | `RawCompression` | `UINT8` | `GetSet` | `0` | RAW compression algorithm (0=Lossless, 1=Compressed, 2=Uncompressed) |
| `0XD032` | `ColorSpace` | `UINT8` | `GetSet` | `1` | Color space standard (1=sRGB standard display, 2=AdobeRGB wide gamut) |
| `0XD034` | `FlickerReduction` | `UINT8` | `GetSet` | `255` | Still photo anti-flicker control (detects 100/120Hz flicker and synchronizes shutter release, 0=Off, 1=On) |
| `0XD06B` | `LongExposureNoiseReduction` | `UINT8` | `GetSet` | `0` | Long exposure noise reduction via hardware dark frame subtraction for exposures >1s (0=Off, 1=On) |
| `0XD070` | `NrHighISO` | `UINT16` | `GetSet` | `0` | Controls NrHighISO state and parameters (raw: `0000`) |
| `0XD0B6` | `RawImageSize` | `STRING` | `GetSet` | `5408x3600` | Image-size lookup name; captured STRING 5408x3600 does not establish native sensor dimensions |
| `0XD0B7` | `FlickerReductionSetting` | `UINT8` | `GetSet` | `0` | Anti-flicker detection sensitivity and clock timing parameters |
| `0XD0BA` | `DiffractionCompensatipn` | `UINT8` | `GetSet` | `1` | Diffraction compensation against small aperture softness (0=Off, 1=On) |
| `0XD0F7` | `VignetteCtrl` | `UINT16` | `GetSet` | `4` | Vignette correction profile strength |
| `0XD0F8` | `AutoDistortionControl` | `UINT8` | `GetOnly` | `1` | Automatic geometric distortion correction using embedded lens grid polynomials (0=Off, 1=On) |
| `0XD130` | `HDRMode` | `UINT16` | `GetSet` | `0` | In-camera automated HDR mode (0=Off, 1=Single photo HDR, 2=Continuous series HDR) |
| `0XD131` | `HDRHighDynamic` | `UINT8` | `GetSet` | `255` | In-camera HDR exposure differential EV (0=Auto, 1=1EV, 2=2EV, 3=3EV) |
| `0XD132` | `HDRSmoothing` | `UINT16` | `GetSet` | `3` | In-camera HDR edge synthesis smoothing (0=High, 1=Normal, 2=Low) |
| `0XD133` | `HDRSaveIndividualImages` | `UINT8` | `GetSet` | `1` | Retain all individual single exposures when shooting HDR (0=Off, 1=On) |
| `0XD14E` | `ActiveDLighting` | `UINT16` | `GetSet` | `65535` | Active D-Lighting (ADL) shadow/highlight balancing (0=Off, 1=Extra High, 2=High, 3=Normal, 4=Low, 255=Auto) |
| `0XD200` | `ActivePicCtrlItem` | `UINT16` | `GetSet` | `203` | Active Picture Control profile preset (Standard, Neutral, Vivid, Monochrome, Portrait, Landscape, Flat, Creative) |

## Creative Lighting System (CLS) and flash control

Editorial group: **Creative Lighting System & Flash Control** (26 properties).

| Code | Lookup symbol | Type | Advertised access | Captured current | Unverified lookup description |
|---|---|---|---|---|---|
| `0XD074` | `FlashSyncSpeed` | `UINT32` | `GetSet` | `2147549384` | Flash sync shutter speed ceiling (1/200s, 1/250s, 1/250s Auto FP high-speed sync) |
| `0XD075` | `FlashShutterSpeed` | `UINT32` | `GetSet` | `65538` | Flash synchronization shutter speed threshold |
| `0XD10C` | `USBSpeed` | `UINT8` | `GetOnly` | `1` | USB physical bus negotiated link speed (0=High Speed 480Mbps, 1=SuperSpeed 5Gbps USB 3.2 Gen1) |
| `0XD118` | `ExposureCompFlashUsed` | `UINT8` | `GetSet` | `0` | Exposure compensation linkage when flash is triggered (0=Entire frame, 1=Background only) |
| `0XD120` | `ExternalFlashAttached` | `UINT8` | `GetOnly` | `0` | Hot-shoe external speedlight physical attachment status (0=None, 1=Mounted) |
| `0XD121` | `ExternalFlashStatus` | `UINT8` | `GetOnly` | `0` | Hot-shoe external flash recycle ready status (0=Charging, 1=Ready) |
| `0XD122` | `ExternalFlashSort` | `UINT8` | `GetOnly` | `0` | External flash electrical protocol category (i-TTL compatible / legacy single-pin) |
| `0XD124` | `ExternalFlashCompensation` | `INT8` | `GetOnly` | `0` | External flash dedicated exposure compensation |
| `0XD125` | `NewExternalFlashMode` | `UINT8` | `GetOnly` | `0` | Modern Speedlight advanced operating mode |
| `0XD126` | `FlashExposureCompensation` | `INT8` | `GetSet` | `0` | Overall flash exposure compensation bias |
| `0XD12D` | `ExternalFlashMultiFlashMode` | `UINT8` | `GetOnly` | `0` | External flash repeating stroboscopic multi-flash mode |
| `0XD16E` | `FlashModeCommanderPower` | `UINT8` | `GetSet` | `1` | Commander mode master flash optical output power tier |
| `0XD199` | `FlashISOAutoHighLimit` | `UINT32` | `GetSet` | `4294967295` | Flash auto ISO maximum sensitivity upper limit |
| `0XD19C` | `WirelessFlashControl` | `UINT8` | `GetSet` | `0` | Wireless flash triggering mode (0=Off, 1=Optical AWL, 2=Radio AWL, 3=Mixed) |
| `0XD1C5` | `AutoFlashIsoSensitivityControl` | `UINT8` | `GetSet` | `1` | Flash auto ISO sensitivity integration policy |
| `0XD1E2` | `SBWirelessMode` | `UINT8` | `GetOnly` | `0` | Wireless Creative Lighting System mode protocol |
| `0XD1E3` | `SBWirelessMultipleFlashMode` | `UINT8` | `GetOnly` | `0` | Multiple wireless flash communication mode (Optical / Radio / Mixed) |
| `0XD1E4` | `SBUsableGroup` | `UINT32` | `GetOnly` | `0` | Active wireless speedlight group mask (Groups A, B, and C) |
| `0XD1E5` | `WirelessCLSEntryMode` | `UINT8` | `GetOnly` | `0` | Creative Lighting System (CLS) remote pairing connection mode |
| `0XD1E6` | `SBPINCode` | `UINT32` | `GetOnly` | `0` | Radio remote trigger 4-digit pairing PIN authentication code |
| `0XD1E7` | `RadioMultipleFlashChannel` | `UINT8` | `GetOnly` | `0` | Radio AWL wireless communication channel (Channels 5 to 18) |
| `0XD1E8` | `OpticalMultipleFlashChannel` | `UINT8` | `GetOnly` | `255` | Optical AWL wireless communication channel (Channels 1 to 4) |
| `0XD1E9` | `FlashRangeDisplay` | `UINT8` | `GetOnly` | `0` | Flash effective illumination distance range display |
| `0XD1EA` | `AllTestFiringDisable` | `UINT8` | `GetOnly` | `1` | Master test firing safety disable interlock |
| `0XD1EC` | `SBSettingMemberLock` | `UINT8` | `GetSet` | `0` | Speedlight remote configuration parameters lock |
| `0XD1ED` | `SBIntegrationFlashReady` | `UINT8` | `GetOnly` | `0` | External speedlight recharge recycle ready flag |

## Live view streaming, EVF, and monitor control

Editorial group: **Live View Streaming, EVF & Monitor** (15 properties).

| Code | Lookup symbol | Type | Advertised access | Captured current | Unverified lookup description |
|---|---|---|---|---|---|
| `0XD17B` | `ApplyLiveViewSetting` | `UINT8` | `GetSet` | `1` | Live view preview exposure simulation (0=Off/constant brightness, 1=On/WYSIWYG exposure) |
| `0XD1A2` | `LiveViewStatus` | `UINT8` | `GetOnly` | `0` | Live view stream subsystem controller state |
| `0XD1A4` | `LiveViewProhibitCondition` | `UINT32` | `GetOnly` | `0` | Live view prohibited condition mask (battery depleted, sensor thermal protection, hardware busy) |
| `0XD1A6` | `LiveViewSelector` | `UINT8` | `GetOnly` | `0` | Physical rear-panel live view mode selector lever (0=Photo LV, 1=Movie LV) |
| `0XD1AC` | `LiveViewImageSize` | `UINT8` | `GetSet` | `2` | Preview-size lookup name; value enumeration and relation to the 640x424 sample are unverified |
| `0XD1AD` | `LiveViewPhotography` | `UINT8` | `GetSet` | `0` | Silent still photography live view actuation mode |
| `0XD1B0` | `ExposureDisplayStatus` | `UINT8` | `GetOnly` | `0` | Viewfinder / monitor live exposure simulation scale display state |
| `0XD1B1` | `ExposureIndicateStatus` | `INT8` | `GetOnly` | `-6` | Exposure deviation bar graph indicator telemetry |
| `0XD1B2` | `InfoDispErrStatus` | `UINT8` | `GetOnly` | `0` | Information display screen diagnostic error code telemetry |
| `0XD1B8` | `CameraLiveViewStatus` | `UINT8` | `GetOnly` | `1` | Live view streaming engine hardware state (0=Idle, 1=Active stream, 2=Paused) |
| `0XD1BA` | `LiveViewTFTStatus` | `UINT8` | `GetOnly` | `0` | Tilting LCD monitor open/closed and eye-sensor EVF switching status |
| `0XD1BB` | `LiveViewImageStatus` | `UINT8` | `GetOnly` | `0` | Live view frame stream output readiness status flag |
| `0XD1BC` | `LiveViewImageCompression` | `UINT8` | `GetSet` | `0` | Live view preview JPEG stream compression tier (High, Normal, Low) |
| `0XD1BD` | `LiveViewZoomArea` | `UINT16` | `GetSet` | `0` | Live view magnification loupe coordinates and zoom ratio |
| `0XD314` | `ElectronicVR` | `UINT8` | `GetSet` | `0` | Movie electronic vibration reduction (Electronic VR / DIS) 1.1x margin crop (0=Off, 1=On) |

## Exposure mechanics, metering base, and shutter drive

Editorial group: **Exposure Mechanics, Metering & Shutter** (41 properties).

| Code | Lookup symbol | Type | Advertised access | Captured current | Unverified lookup description |
|---|---|---|---|---|---|
| `0XD030` | `CaptureAreaCrop` | `UINT8` | `GetOnly` | `2` | Image sensor crop mode (0=FX 3:2 full frame, 1=DX 1.5x crop, 2=5:4, 3=1:1 square, 4=16:9) |
| `0XD054` | `ISOAuto` | `UINT8` | `GetSet` | `0` | Auto ISO control master switch (0=Off, 1=On) |
| `0XD056` | `EVStep` | `UINT8` | `GetSet` | `2` | Exposure-step lookup name; enumeration is not established by this capture |
| `0XD059` | `CenterWeightArea` | `UINT8` | `GetSet` | `12` | Center-weighted metering circle diameter (8mm, 12mm, or frame average) |
| `0XD05A` | `ExposureBaseMatrix` | `INT8` | `GetSet` | `0` | Matrix metering dedicated exposure baseline fine-tuning offset |
| `0XD05B` | `ExposureBaseCenter` | `INT8` | `GetSet` | `0` | Center-weighted baseline tuning |
| `0XD05C` | `ExposureBaseSpot` | `INT8` | `GetSet` | `0` | Spot metering baseline tuning |
| `0XD067` | `AngleLevel` | `INT32` | `GetOnly` | `212993` | Electronic virtual horizon dual-axis accelerometer inclination telemetry |
| `0XD068` | `D1ShootingSpeed` | `UINT8` | `GetSet` | `5` | Continuous Low (CL) burst target frame rate (1 to 5 fps) |
| `0XD069` | `D2MaximumShots` | `UINT8` | `GetSet` | `200` | Maximum continuous shot release limit per burst trigger (1 to 200 shots) |
| `0XD06A` | `ExposureDelayMode` | `UINT8` | `GetSet` | `0` | Exposure delay mode release buffer timer (0=Off, 1=0.2s, 2=0.5s, 3=1s, 4=2s, 5=3s) |
| `0XD073` | `CopyrightInfo` | `STRING` | `GetSet` | `` | Copyright holder declaration string |
| `0XD078` | `BracketSet` | `UINT8` | `GetSet` | `0` | Automated bracketing parameter target selection |
| `0XD079` | `E6ManualModeBracketing` | `UINT8` | `GetSet` | `0` | Manual mode bracketing linkage (Flash+Speed / Flash+Aperture / Speed only / Aperture only) |
| `0XD07A` | `BracketOrder` | `UINT8` | `GetSet` | `0` | Bracketing shot release sequence order (0=Metered>Under>Over, 1=Under>Metered>Over) |
| `0XD07C` | `BracketingSet` | `UINT8` | `GetOnly` | `1` | Automated bracketing parameter target (AE & Flash / AE only / WB / ADL) |
| `0XD07D` | `AngleLevelPitching` | `INT32` | `GetOnly` | `21219947` | Electronic virtual horizon pitch angle sensor telemetry |
| `0XD07E` | `AngleLevelYawing` | `INT32` | `GetOnly` | `-1` | Electronic virtual horizon roll/yaw angle sensor telemetry |
| `0XD0B4` | `ExposureIndexEx` | `UINT32` | `GetSet` | `64` | Extended sensitivity gain tier (ISO Lo 1.0 to Hi 2.0) |
| `0XD0B5` | `ISOControlSensitivity` | `UINT32` | `GetOnly` | `64` | ISO sensitivity dial stepping sensitivity |
| `0XD0BE` | `RemainingExposureTime` | `UINT16` | `GetOnly` | `0` | Bulb or Time long exposure elapsed / remaining countdown (milliseconds) |
| `0XD0C0` | `Bracketing` | `UINT8` | `GetSet` | `0` | Automated bracketing master switch |
| `0XD0C1` | `AutoExposureBracketStep` | `UINT8` | `GetSet` | `6` | Auto exposure bracketing EV step span (0.3EV, 0.7EV, 1.0EV, 2.0EV, 3.0EV) |
| `0XD0C6` | `ADLBracketingPattern` | `UINT16` | `GetOnly` | `0` | Active D-Lighting bracketing sequence pattern |
| `0XD0C7` | `ADLBracketingStep` | `UINT16` | `GetOnly` | `65535` | Active D-Lighting bracketing span increment |
| `0XD0FC` | `UserMode` | `UINT8` | `GetOnly` | `19` | User mode dial memory bank position (U1, U2, or U3) |
| `0XD100` | `ExposureTime` | `UINT32` | `GetOnly` | `65538` | Current calibrated shutter exposure duration value |
| `0XD105` | `AELockStatus` | `UINT8` | `GetOnly` | `0` | Auto exposure lock (AE-L) active status flag |
| `0XD106` | `FVLockStatus` | `UINT8` | `GetOnly` | `0` | Flash value lock (FV-L) active status flag |
| `0XD109` | `FlexibleProgram` | `INT8` | `GetOnly` | `0` | Program Mode (P) flexible program shift step offset |
| `0XD112` | `TVLockSetting` | `UINT8` | `GetOnly` | `0` | Shutter speed command dial lock switch |
| `0XD113` | `AVLockSetting` | `UINT8` | `GetSet` | `0` | Aperture value command dial lock switch |
| `0XD164` | `ISOAutoShutterTime` | `UINT32` | `GetSet` | `4294967295` | Auto ISO minimum shutter speed threshold (1/4000s to 30s / Auto focal-length linked) |
| `0XD16A` | `ISO_Auto` | `UINT8` | `GetSet` | `1` | Auto ISO sensitivity control master enable (0=Off, 1=On) |
| `0XD183` | `ISOAutoHiLimit` | `UINT32` | `GetSet` | `1000` | Auto ISO maximum floating upper ceiling limit (ISO 100 to 102400) |
| `0XD1B3` | `ExposureIndicateLightup` | `UINT8` | `GetOnly` | `0` | Top sub-display / viewfinder exposure scale backlight behavior |
| `0XD1B4` | `ContinousShootingCount` | `UINT16` | `GetOnly` | `20` | Mechanical shutter hardware total release actuations counter |
| `0XD1F1` | `ExposureRemaining` | `UINT32` | `GetOnly` | `8282` | Remaining available frames capacity estimate on current storage card |
| `0XD1F4` | `ISOAutoShutterCorrectionTime` | `INT8` | `GetSet` | `0` | Auto ISO shutter threshold speed offset tuning (Slower -2 to Faster +2) |
| `0XD20D` | `ElectronicFrontCurtainShutter` | `UINT8` | `GetSet` | `2` | Electronic Front-Curtain Shutter (EFCS) mode |
| `0XD239` | `ExposureBaseCompHighlight` | `INT8` | `GetSet` | `0` | Highlight-weighted metering dedicated exposure baseline fine-tuning offset |

## Dual card slots and storage naming

Editorial group: **Dual Card Slots & Storage Architecture** (7 properties).

| Code | Lookup symbol | Type | Advertised access | Captured current | Unverified lookup description |
|---|---|---|---|---|---|
| `0XD06C` | `FileNumberSequence` | `UINT8` | `GetSet` | `1` | File numbering continuity sequence behavior (0=Off/reset, 1=On/continuous, 2=Reset) |
| `0XD08A` | `NoCFCard` | `UINT8` | `GetSet` | `1` | Shutter release interlock when memory card is missing (0=Release allowed for testing, 1=Release locked) |
| `0XD10B` | `RecordingMedia` | `UINT8` | `GetSet` | `0` | Current active recording destination media (Slot 1 / Slot 2) |
| `0XD148` | `Slot2SaveMode` | `UINT8` | `GetSet` | `0` | Secondary slot 2 role (0=Overflow, 1=Backup duplicate, 2=RAW slot 1 / JPEG slot 2) |
| `0XD156` | `PrimarySlot` | `UINT8` | `GetSet` | `1` | Primary storage card slot assignment (0=Slot 1 CFexpress, 1=Slot 2 SD) |
| `0XD15B` | `ActiveFolder` | `UINT16` | `GetOnly` | `102` | Active DCIM directory folder sequence number (e.g. 100NCZ_7) |
| `0XD1F2` | `ActiveSlot` | `UINT8` | `GetOnly` | `1` | Physical active hardware memory slot (Slot 1 vs Slot 2) |


## Usable validation

The inventory is useful for matching advertised codes, types and captured
values. Establish a menu meaning or write behavior with a version-bound source
consumer and a controlled before/after observation. PTP notifications alone do
not identify GPIO contacts or internal C-matrix controls; see the
[physical baseline](physical.md) and [dial receipt](../inputs/camera/keys.md#physical-dial-transition-receipts).
