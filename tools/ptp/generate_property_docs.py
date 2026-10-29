#!/usr/bin/env python3
"""
Generate comprehensive, human-readable markdown documentation for all 259 Nikon vendor PTP device properties.
Output is placed at docs/runtime/ptp_vendor_properties.md with strict adherence to docs/writing.md.
"""

import json
import os
import re

WORKTREE = "/Users/auturora/.codex/worktrees/physical-ptp/nightglass-qemu"
PROPS_JSON = os.path.join(WORKTREE, "docs/public/evidence/physical/vendor_properties_259.json")
OUT_MD = os.path.join(WORKTREE, "docs/runtime/ptp_vendor_properties.md")

DTYPE_MAP = {
    '0x0001': 'INT8',
    '0x0002': 'UINT8',
    '0x0003': 'INT16',
    '0x0004': 'UINT16',
    '0x0005': 'INT32',
    '0x0006': 'UINT32',
    '0xffff': 'STRING'
}

def load_ptp_definitions():
    with open('/tmp/ptp_header.h', 'r', encoding='utf-8') as f:
        ptp_h = f.read()

    dpc_matches = re.findall(r'#define\s+(PTP_DPC_NIKON_\w+)\s+(0x[0-9a-fA-F]+)', ptp_h)
    code_to_sym = {int(code, 16): name for name, code in dpc_matches}
    return code_to_sym

# English descriptions for handbook compliance
SYMBOL_DESCRIPTIONS_EN = {
    # RAW & Image Quality
    "RawCompression": "RAW compression algorithm (0=Lossless, 1=Compressed, 2=Uncompressed)",
    "RawBitMode": "RAW quantization bit depth (0=12-bit high-speed, 1=14-bit high dynamic range)",
    "RawImageSize": "RAW image size tier (0=RAW-Large 45.4MP, 1=RAW-Medium 25.6MP, 2=RAW-Small 11.4MP)",
    "ColorSpace": "Color space standard (1=sRGB standard display, 2=AdobeRGB wide gamut)",
    "FlickerReduction": "Still photo anti-flicker control (detects 100/120Hz flicker and synchronizes shutter release, 0=Off, 1=On)",
    "FlickerReductionSetting": "Anti-flicker detection sensitivity and clock timing parameters",
    "ActiveDLighting": "Active D-Lighting (ADL) shadow/highlight balancing (0=Off, 1=Extra High, 2=High, 3=Normal, 4=Low, 255=Auto)",
    "VignetteControl": "Vignette peripheral illumination compensation (0=Off, 1=High, 2=Normal, 3=Low)",
    "VignetteCtrl": "Vignette correction profile strength",
    "AutoDistortionControl": "Automatic geometric distortion correction using embedded lens grid polynomials (0=Off, 1=On)",
    "DiffractionCompensatipn": "Diffraction compensation against small aperture softness (0=Off, 1=On)",
    "LongExposureNoiseReduction": "Long exposure noise reduction via hardware dark frame subtraction for exposures >1s (0=Off, 1=On)",
    "HighISONoiseReduction": "High ISO noise reduction strength (0=Off, 1=Low, 2=Normal, 3=High)",
    "HDRMode": "In-camera automated HDR mode (0=Off, 1=Single photo HDR, 2=Continuous series HDR)",
    "HDRHighDynamic": "In-camera HDR exposure differential EV (0=Auto, 1=1EV, 2=2EV, 3=3EV)",
    "HDRSmoothing": "In-camera HDR edge synthesis smoothing (0=High, 1=Normal, 2=Low)",
    "HDRSaveIndividualImages": "Retain all individual single exposures when shooting HDR (0=Off, 1=On)",
    "ActivePicCtrlItem": "Active Picture Control profile preset (Standard, Neutral, Vivid, Monochrome, Portrait, Landscape, Flat, Creative)",
    "ChangePicCtrlItem": "Picture Control parameter adjustment (Quick Sharp, Sharpening, Mid-range Sharpening, Clarity, Contrast, Brightness, Saturation, Hue)",

    # White Balance
    "WhiteBalanceAutoBias": "Auto White Balance (AWB) fine-tuning bias coordinates (A-B amber/blue, G-M green/magenta, 612=zero bias)",
    "WhiteBalanceTungstenBias": "Tungsten incandescent white balance fine-tuning bias",
    "WhiteBalanceFluorescentBias": "Fluorescent white balance fine-tuning bias",
    "WhiteBalanceDaylightBias": "Daylight white balance fine-tuning bias",
    "WhiteBalanceFlashBias": "Flash white balance fine-tuning bias",
    "WhiteBalanceCloudyBias": "Cloudy white balance fine-tuning bias",
    "WhiteBalanceShadeBias": "Shade white balance fine-tuning bias",
    "WhiteBalanceColorTemperature": "Manual Kelvin color temperature setting (2500K to 10000K, observed: 6500K)",
    "WhiteBalancePresetNo": "Manual preset white balance active slot (PRE d-1 to d-6, observed: slot 1)",
    "WhiteBalancePresetName1": "Manual preset white balance d-1 user comment string",
    "WhiteBalancePresetName2": "Manual preset white balance d-2 user comment string",
    "WhiteBalancePresetName3": "Manual preset white balance d-3 user comment string",
    "WhiteBalancePresetName4": "Manual preset white balance d-4 user comment string",
    "WhiteBalancePresetName5": "Manual preset white balance d-5 user comment string",
    "WhiteBalancePresetName6": "Manual preset white balance d-6 user comment string",
    "WhiteBalancePresetVal1": "Manual preset white balance d-1 chromaticity calibration matrix",
    "WhiteBalancePresetVal2": "Manual preset white balance d-2 chromaticity calibration matrix",
    "WhiteBalancePresetVal3": "Manual preset white balance d-3 chromaticity calibration matrix",
    "WhiteBalancePresetVal4": "Manual preset white balance d-4 chromaticity calibration matrix",
    "WhiteBalancePresetValue5": "Manual preset white balance d-5 chromaticity calibration matrix",
    "WhiteBalancePresetValue6": "Manual preset white balance d-6 chromaticity calibration matrix",
    "WhiteBalanceTunePreset5": "Manual preset white balance d-5 fine-tuning bias",
    "WhiteBalanceTunePreset6": "Manual preset white balance d-6 fine-tuning bias",
    "WhiteBalancePresetProtect5": "Manual preset white balance d-5 write protection lock (0=Unlocked, 1=Protected)",
    "WhiteBalancePresetProtect6": "Manual preset white balance d-6 write protection lock",
    "TunePreset1": "Manual preset white balance d-1 fine-tuning bias",
    "TunePreset2": "Manual preset white balance d-2 fine-tuning bias",
    "TunePreset3": "Manual preset white balance d-3 fine-tuning bias",
    "TunePreset4": "Manual preset white balance d-4 fine-tuning bias",
    "WBPresetProtect1": "Manual preset white balance d-1 write protection lock",
    "WBPresetProtect2": "Manual preset white balance d-2 write protection lock",
    "WBPresetProtect3": "Manual preset white balance d-3 write protection lock",
    "WBPresetProtect4": "Manual preset white balance d-4 write protection lock",
    "WBAutoType": "Auto White Balance program subtype (0=Keep white, 1=Standard, 2=Keep warm lighting colors)",
    "WhiteBalanceAutoBias1": "Auto White Balance AWB 0 (Keep white) dedicated bias",
    "WhiteBalanceAutoBias2": "Auto White Balance AWB 1 (Standard) dedicated bias",
    "WhiteBalanceAutoBias3": "Auto White Balance AWB 2 (Keep warm) dedicated bias",
    "WhiteBalanceDaylightFluorescent": "Daylight fluorescent light fine-tuning bias",
    "WhiteBalanceNaturalLightAutoBias": "Natural Light Auto white balance fine-tuning bias",
    "FlourescentType": "Fluorescent lamp subtype (Sodium, Warm-white, Cool-white, Day-white, Daylight, High-temp mercury)",
    "TuneColourTemperature": "Manual Kelvin color temperature step tuning",
    "WhiteBalanceBracketProgram": "White balance bracketing program configuration",

    # Autofocus & Focus Assist
    "AFStillLockOnAcross": "Still AF tracking lock-on responsiveness to crossing obstacles (1=Quick to 5=Delayed)",
    "AFStillLockOnMotion": "Still AF target motion characteristics (0=Erratic, 1=Smooth steady motion)",
    "AFAssist": "Built-in AF assist illuminator beam (0=Off, 1=On)",
    "FocusPointWrap": "Focus point selection wrap-around behavior (0=Lock at borders, 1=Wrap around to opposite edge)",
    "AFAreaPoint": "Focus point selection density (0=Full 493 points, 1=Reduced point alternate grid)",
    "AFTuneSavedVal": "AF fine-tuning saved offset for current lens mount profile",
    "SaveFocusPosition": "Save focus position on camera power-off (0=Off/reset to infinity, 1=On/preserve position)",
    "LowLightAF": "Low-light AF detection enhancement algorithm (0=Off, 1=On)",
    "DetectionPeaking": "Focus peaking highlight detection master switch",
    "FocusPeaking": "Focus peaking enhancement sensitivity level (0=Off, 1=Low, 2=Standard, 3=High)",
    "PeakingColor": "Focus peaking highlight outline color (1=Red, 2=Yellow, 3=Blue, 4=White)",
    "A1AFCModePriority": "AF-C continuous servo release priority (0=Release, 1=Release+Focus, 2=Focus)",
    "A2AFSModePriority": "AF-S single servo release priority (0=Release, 1=Focus)",
    "StillFocusMeteringMode": "Still photography focus point linked spot metering mode",
    "StillFocusMode": "Still photography real-time focus servo state",
    "AFLockStatus": "Autofocus lock (AF-L) active status flag",

    # Lens Optics & IBIS
    "FmmManualSetting": "Non-CPU lens manual focal length registration (0 to 1000mm, used for IBIS and metering baseline)",
    "F0ManualSetting": "Non-CPU lens manual maximum aperture registration (f/1.2 to f/22)",
    "ManualSetLensNo": "Non-CPU lens memory slot index (slots 1 to 9)",
    "RetractableLensWarning": "Retractable lens unextended caution prompt (0=Off, 1=On)",
    "LensID": "Lens hardware telemetry ID (identifies mount protocol, manufacturing batch, and electrical profile)",
    "LensSort": "Lens physical mount category (Native Z mount / FTZ adapter F mount)",
    "LensType": "Lens aperture actuation mechanism (Type-E electromagnetic / Type-G mechanical / Type-D manual ring)",
    "FocalLengthMin": "Minimum physical focal length at wide end (mm, observed: 16mm)",
    "FocalLengthMax": "Maximum physical focal length at telephoto end (mm, observed: 50mm)",
    "MaxApAtMinFocalLength": "Maximum physical aperture available at minimum focal length (e.g. f/3.5)",
    "MaxApAtMaxFocalLength": "Maximum physical aperture available at maximum focal length (e.g. f/6.3)",
    "LensTypeML": "Mirrorless macro focus and stepping motor actuation profile",
    "VibrationReduction": "In-Body Image Stabilization (IBIS 5-axis VR) master switch (0=Off, 1=Normal, 2=Sport)",

    # Exposure, Shutter & Metering
    "CaptureAreaCrop": "Image sensor crop mode (0=FX 3:2 full frame, 1=DX 1.5x crop, 2=5:4, 3=1:1 square, 4=16:9)",
    "EVStep": "Exposure and compensation EV adjustment increment step (0=1/3 EV, 1=1/2 EV, 2=1 EV)",
    "ExposureBaseMatrix": "Matrix metering dedicated exposure baseline fine-tuning offset",
    "ExposureBaseCompCenter": "Center-weighted metering dedicated exposure baseline fine-tuning offset",
    "ExposureBaseCompMatrix": "Matrix metering global baseline fine-tuning offset",
    "ExposureBaseCompSpot": "Spot metering dedicated exposure baseline fine-tuning offset",
    "ExposureBaseCompHighlight": "Highlight-weighted metering dedicated exposure baseline fine-tuning offset",
    "CenterWeightArea": "Center-weighted metering circle diameter (8mm, 12mm, or frame average)",
    "ContinuousShootingNum": "Continuous burst shooting count setting",
    "D1ShootingSpeed": "Continuous Low (CL) burst target frame rate (1 to 5 fps)",
    "D2MaximumShots": "Maximum continuous shot release limit per burst trigger (1 to 200 shots)",
    "ExposureDelayMode": "Exposure delay mode release buffer timer (0=Off, 1=0.2s, 2=0.5s, 3=1s, 4=2s, 5=3s)",
    "BracketingSet": "Automated bracketing parameter target (AE & Flash / AE only / WB / ADL)",
    "BracketOrder": "Bracketing shot release sequence order (0=Metered>Under>Over, 1=Under>Metered>Over)",
    "AngleLevelPitching": "Electronic virtual horizon pitch angle sensor telemetry",
    "AngleLevelYawing": "Electronic virtual horizon roll/yaw angle sensor telemetry",
    "ExposureIndexEx": "Extended sensitivity gain tier (ISO Lo 1.0 to Hi 2.0)",
    "ISOControlSensitivity": "ISO sensitivity dial stepping sensitivity",
    "Bracketing": "Automated bracketing master switch",
    "ADLBracketingPattern": "Active D-Lighting bracketing sequence pattern",
    "ADLBracketingStep": "Active D-Lighting bracketing span increment",
    "RemainingExposureTime": "Bulb or Time long exposure elapsed / remaining countdown (milliseconds)",
    "SilentPhotography": "Silent photography mode using full electronic shutter (0=Off, 1=On)",
    "ShutterType": "Shutter actuation mechanism selection (0=Auto, 1=Mechanical, 2=Electronic Front-Curtain EFCS)",
    "UserMode": "User mode dial memory bank position (U1, U2, or U3)",
    "ISOAutoShutterTime": "Auto ISO minimum shutter speed threshold (1/4000s to 30s / Auto focal-length linked)",
    "ISO_Auto": "Auto ISO sensitivity control master enable (0=Off, 1=On)",
    "ISOAutoHiLimit": "Auto ISO maximum floating upper ceiling limit (ISO 100 to 102400)",
    "ExposureIndicateLightup": "Top sub-display / viewfinder exposure scale backlight behavior",
    "ExposureRemaining": "Remaining available frames capacity estimate on current storage card",
    "ISOAutoShutterCorrectionTime": "Auto ISO shutter threshold speed offset tuning (Slower -2 to Faster +2)",
    "ElectronicFrontCurtainShutter": "Electronic Front-Curtain Shutter (EFCS) mode",
    "ContinousShootingCount": "Mechanical shutter hardware total release actuations counter",
    "ExposureDisplayStatus": "Viewfinder / monitor live exposure simulation scale display state",
    "ExposureIndicateStatus": "Exposure deviation bar graph indicator telemetry",
    "ISOAuto": "Auto ISO control master switch (0=Off, 1=On)",
    "ExposureBaseCenter": "Center-weighted baseline tuning",
    "ExposureBaseSpot": "Spot metering baseline tuning",
    "AngleLevel": "Electronic virtual horizon dual-axis accelerometer telemetry",
    "BracketSet": "Automated bracketing parameter target selection",
    "E6ManualModeBracketing": "Manual mode bracketing linkage (Flash+Speed / Flash+Aperture / Speed only / Aperture only)",
    "AutoExposureBracketProgram": "Auto exposure bracketing program mode (AE+Flash / AE only)",
    "AutoExposureBracketCount": "Auto exposure bracketing shot count (3, 5, 7, or 9 shots)",
    "AutoExposureBracketStep": "Auto exposure bracketing EV step span (0.3EV, 0.7EV, 1.0EV, 2.0EV, 3.0EV)",
    "WhiteBalanceBracketStep": "White balance bracketing color shift step span (1 to 3 steps)",
    "ExposureTime": "Current calibrated shutter exposure duration value",
    "AELockStatus": "Auto exposure lock (AE-L) active status flag",
    "FVLockStatus": "Flash value lock (FV-L) active status flag",
    "FlexibleProgram": "Program Mode (P) flexible program shift step offset",
    "TVLockSetting": "Shutter speed command dial lock switch",
    "AVLockSetting": "Aperture value command dial lock switch",

    # Flash Control
    "FlashSyncSpeed": "Flash sync shutter speed ceiling (1/200s, 1/250s, 1/250s Auto FP high-speed sync)",
    "FlashShutterSpeedLim": "Slow sync minimum shutter speed limit (1/60s to 30s)",
    "FlashCompensation": "Flash exposure compensation value (-3.0 EV to +1.0 EV)",
    "ExposureCompFlashUsed": "Exposure compensation linkage when flash is triggered (0=Entire frame, 1=Background only)",
    "FlashModeCommanderPower": "Commander mode master flash optical output power tier",
    "AutoFlashIsoSensitivityControl": "Flash auto ISO sensitivity integration policy",
    "FlashISOAutoHighLimit": "Flash auto ISO maximum sensitivity upper limit",
    "WirelessFlashControl": "Wireless flash triggering mode (0=Off, 1=Optical AWL, 2=Radio AWL, 3=Mixed)",
    "SBWirelessMultipleFlashMode": "Multiple wireless flash communication mode (Optical / Radio / Mixed)",
    "RadioMultipleFlashChannel": "Radio AWL wireless communication channel (Channels 5 to 18)",
    "OpticalMultipleFlashChannel": "Optical AWL wireless communication channel (Channels 1 to 4)",
    "FlashRangeDisplay": "Flash effective illumination distance range display",
    "SBUsableGroup": "Active wireless speedlight group mask (Groups A, B, and C)",
    "WirelessCLSEntryMode": "Creative Lighting System (CLS) remote pairing connection mode",
    "SBPINCode": "Radio remote trigger 4-digit pairing PIN authentication code",
    "SBIntegrationFlashReady": "External speedlight recharge recycle ready flag",
    "AllTestFiringDisable": "Master test firing safety disable interlock",
    "SBSettingMemberLock": "Speedlight remote configuration parameters lock",
    "FlashShutterSpeed": "Flash synchronization shutter speed threshold",
    "ExternalFlashAttached": "Hot-shoe external speedlight physical attachment status (0=None, 1=Mounted)",
    "ExternalFlashStatus": "Hot-shoe external flash recycle ready status (0=Charging, 1=Ready)",
    "ExternalFlashSort": "External flash electrical protocol category (i-TTL compatible / legacy single-pin)",
    "ExternalFlashCompensation": "External flash dedicated exposure compensation",
    "NewExternalFlashMode": "Modern Speedlight advanced operating mode",
    "FlashExposureCompensation": "Overall flash exposure compensation bias",
    "ExternalFlashMultiFlashMode": "External flash repeating stroboscopic multi-flash mode",
    "SBWirelessMode": "Wireless Creative Lighting System mode protocol",

    # Movie Recording & Timecode
    "MovScreenSize": "Movie frame resolution and frame rate profile (4K UHD 60p/50p/30p/25p/24p, FHD 120p/100p/60p/50p/30p)",
    "MovMicrophone": "Movie built-in stereo microphone channel routing",
    "MovRecProhibitCondition": "Movie recording prohibited condition mask (card full, thermal throttle, shutter busy)",
    "MovQuality": "Movie compression bitrate tier (High Quality / Normal bitrate)",
    "MovWindNoiseReduction": "Movie microphone wind noise reduction high-pass filter (0=Off, 1=On)",
    "MovISOAutoControl": "Movie auto ISO sensitivity control master switch (0=Off, 1=On)",
    "MovISOAutoHighLimit": "Movie auto ISO maximum floating upper limit ceiling",
    "MovFileType": "Movie container file format (0=MOV broadcast grade, 1=MP4 general web)",
    "DiffractionCompensation": "Movie optical diffraction compensation control",
    "MovieLogOutput": "Movie color gamma curve output mode (0=SDR Rec.709, 1=N-Log 10-bit log, 2=HLG HDR hybrid log gamma)",
    "MovieAutoDistortion": "Movie real-time optical geometric distortion correction",
    "MovieRemainingTime": "Current storage card remaining movie recording headroom (seconds)",
    "MovieLogSetting": "Movie Log shooting monitor color assistance (View Assist switch)",
    "HDMIOutputDataDepth": "HDMI clean output video stream bit depth (0=8-bit, 1=10-bit 4:2:2 ProRes/RAW output)",
    "MovieAfSpeed": "Movie AF drive speed damping step (-5 cinematic slow transition to +5 snappy pull)",
    "MovieAfSpeedWhenToApply": "Movie AF speed application condition (0=Always, 1=Only during active recording)",
    "MovieAfTrackingSensitivity": "Movie AF target tracking sensitivity (1=Responsive to 7=Smooth cinematic delay)",
    "MovieMicrophoneSensitivity": "Built-in / 3.5mm external microphone input sensitivity gain (0=Auto AGC, 1 to 20=Manual, 255=Mute)",
    "MovieMicrophoneFrequencyResponse": "Microphone frequency response curve (0=Wide for music/nature, 1=Voice vocal optimization)",
    "MovieWindNoiseReduction": "Microphone wind noise reduction high-pass filter (0=Off, 1=On)",
    "MovieHeadphoneVolume": "3.5mm stereo headphone monitoring output volume (0 to 30)",
    "MovieZebraPattern": "Zebra overexposure warning stripe pattern (0=Off, 1=Pattern 1, 2=Pattern 2)",
    "MovieZebraHighlightLevel": "Zebra overexposure highlight detection threshold (120 to 255 IRE)",
    "MovieReleaseButton": "Shutter release button assignment in video mode (Take photo vs Start/stop movie recording)",
    "MovieRecFrameCount": "Movie active recorded frame counter for current clip",
    "MovieFocusMeteringMode": "Movie focus and metering linked mode configuration",
    "MovieVibrationReduction": "Movie optical and electronic stabilization master switch",
    "MovieShutterSpeed": "Movie mode dedicated shutter speed control (e.g. 1/50s, 1/100s, 1/120s)",
    "MovieFNumber": "Movie mode dedicated aperture control",
    "MovieISO": "Movie mode dedicated ISO sensitivity control",
    "MovieExposureBiasCompensation": "Movie mode dedicated exposure compensation bias",
    "MovieExposureMeteringMode": "Movie mode dedicated metering pattern (Matrix / Center-weighted / Highlight-weighted)",
    "ExternalRecordingControl": "HDMI external recorder remote trigger control (0=Off, 1=On, triggers Atomos Ninja / Blackmagic)",
    "HighlightBrightness": "Highlight display monitoring baseline tuning",
    "RecordTimeCodes": "Timecode recording master switch (0=Off, 1=On)",
    "CountUpMethod": "Timecode count-up method (0=Rec Run only during active recording, 1=Free Run continuous 24-hour)",
    "TimeCodeOrigin": "Timecode initial origin preset (0=Reset to 00:00:00:00, 1=Manual Entry)",
    "DropFrame": "Timecode drop-frame compensation mode (0=Non-Drop Frame NDF, 1=Drop Frame DF compensating 29.97/59.94fps clock drift)",
    "MovieHDMIOutputTimeCode": "HDMI video stream embedded SMPTE timecode metadata (0=Disabled, 1=Embedded)",
    "MovieCaptureAreaCrop": "Movie sensor crop mode (FX full-frame downsampling vs DX crop)",
    "MovieWbPresetDataComment1": "Movie mode preset white balance d-1 comment string",
    "MovieWbPresetDataComment2": "Movie mode preset white balance d-2 comment string",
    "MovieWbPresetDataComment3": "Movie mode preset white balance d-3 comment string",
    "MovieWbPresetDataComment4": "Movie mode preset white balance d-4 comment string",
    "MovieWbPresetDataComment5": "Movie mode preset white balance d-5 comment string",
    "MovieWbPresetDataComment6": "Movie mode preset white balance d-6 comment string",
    "MovieWbPresetDataValue1": "Movie mode preset white balance d-1 measured chromaticity",
    "MovieWbPresetDataValue2": "Movie mode preset white balance d-2 measured chromaticity",
    "MovieWbPresetDataValue3": "Movie mode preset white balance d-3 measured chromaticity",
    "MovieWbPresetDataValue4": "Movie mode preset white balance d-4 measured chromaticity",
    "MovieWbPresetDataValue5": "Movie mode preset white balance d-5 measured chromaticity",
    "MovieWbPresetDataValue6": "Movie mode preset white balance d-6 measured chromaticity",
    "MovieWbTunePreset1": "Movie mode preset white balance d-1 fine-tuning bias",
    "MovieWbTunePreset2": "Movie mode preset white balance d-2 fine-tuning bias",
    "MovieWbTunePreset3": "Movie mode preset white balance d-3 fine-tuning bias",
    "MovieWbTunePreset4": "Movie mode preset white balance d-4 fine-tuning bias",
    "MovieWbTunePreset5": "Movie mode preset white balance d-5 fine-tuning bias",
    "MovieWbTunePreset6": "Movie mode preset white balance d-6 fine-tuning bias",
    "MovieWbPresetProtect1": "Movie mode preset white balance d-1 write protection lock",
    "MovieWbPresetProtect2": "Movie mode preset white balance d-2 write protection lock",
    "MovieWbPresetProtect3": "Movie mode preset white balance d-3 write protection lock",
    "MovieWbPresetProtect4": "Movie mode preset white balance d-4 write protection lock",
    "MovieWbPresetProtect5": "Movie mode preset white balance d-5 write protection lock",
    "MovieWbPresetProtect6": "Movie mode preset white balance d-6 write protection lock",
    "MovieNrHighISO": "Movie high ISO noise reduction strength",
    "MovieActivePicCtrlItem": "Movie mode independent Picture Control profile item",
    "MovieWhiteBalance": "Movie mode independent white balance selection (Auto, Daylight, Cloudy, Tungsten, Kelvin, Preset)",
    "MovieActiveDLighting": "Movie mode independent Active D-Lighting strength",
    "MovieWbTuneNatural": "Movie mode natural light auto white balance fine-tuning bias",
    "MovieAttenuator": "Movie microphone attenuator against audio clipping and acoustic distortion (0=Off, 1=On)",
    "MovieVignetteControl": "Movie mode peripheral illumination vignette compensation",
    "MovieDiffractionCompensation": "Movie mode optical diffraction correction",
    "MovieWbAutoType": "Movie mode auto white balance subtype (AWB0 / AWB1 / AWB2)",
    "MovieWbTuneAuto": "Movie mode auto white balance dedicated bias",
    "MovieWbTuneIncandescent": "Movie mode incandescent tungsten dedicated bias",
    "MovieWbFlourescentType": "Movie mode fluorescent lamp subtype",
    "MovieWbTuneFlourescent": "Movie mode fluorescent dedicated bias",
    "MovieWbTuneSunny": "Movie mode daylight sunny dedicated bias",
    "MovieWbTuneCloudy": "Movie mode cloudy dedicated bias",
    "MovieWbTuneShade": "Movie mode shade dedicated bias",
    "MovieWbColorTemp": "Movie mode Kelvin color temperature setting (K)",
    "MovieWbTuneColorTemp": "Movie mode color temperature step fine tuning",
    "MovieWbPresetData0": "Movie mode preset white balance slot 0 raw data",
    "MovieFocusMode": "Movie mode focus servo mode (AF-C / AF-F / MF)",
    "MovFileSlot": "Movie storage destination slot (0=Slot 1 CFexpress, 1=Slot 2 SD)",
    "MovRecordMicrophoneLevelValue": "Movie real-time audio peak recording level telemetry",
    "MovRecordingZone": "Movie sensor crop recording zone (FX full-frame downsampled vs DX crop)",

    # Live View & Display
    "ApplyLiveViewSetting": "Live view preview exposure simulation (0=Off/constant brightness, 1=On/WYSIWYG exposure)",
    "CameraLiveViewStatus": "Live view streaming engine hardware state (0=Idle, 1=Active stream, 2=Paused)",
    "LiveViewTFTStatus": "Tilting LCD monitor open/closed and eye-sensor EVF switching status",
    "LiveViewImageStatus": "Live view frame stream output readiness status flag",
    "LiveViewImageCompression": "Live view preview JPEG stream compression tier (High, Normal, Low)",
    "LiveViewZoomArea": "Live view magnification loupe coordinates and zoom ratio",
    "LiveViewStatus": "Live view stream subsystem controller state",
    "LiveViewProhibitCondition": "Live view prohibited condition mask (battery depleted, sensor thermal protection, hardware busy)",
    "LiveViewSelector": "Physical rear-panel live view mode selector lever (0=Photo LV, 1=Movie LV)",
    "LiveViewImageSize": "PTP live view preview frame resolution format (0=QVGA, 1=VGA 640x424, 2=SVGA 800x600)",
    "LiveViewPhotography": "Silent still photography live view actuation mode",
    "MonitorOffDelay": "Monitor and viewfinder energy-saving auto-off timer delay (4s, 10s, 30s, 1m, 10m, Unlimited)",
    "ViewfinderBrightness": "3.69M-dot OLED Electronic Viewfinder (EVF) brightness (Auto / Manual -5 to +5)",
    "MonitorBrightness": "3.2-inch tilting TFT LCD monitor backlight brightness (Manual -5 to +5)",
    "ViewfinderColorBalance": "Electronic Viewfinder (EVF) display color balance (A-B amber/blue, G-M green/magenta)",
    "MonitorColorBalance": "TFT LCD monitor display color balance",
    "ElectronicVR": "Movie electronic vibration reduction (Electronic VR / DIS) 1.1x margin crop (0=Off, 1=On)",

    # Storage & Dual Card Slots
    "Slot2SaveMode": "Secondary slot 2 role (0=Overflow, 1=Backup duplicate, 2=RAW slot 1 / JPEG slot 2)",
    "PrimarySlot": "Primary storage card slot assignment (0=Slot 1 CFexpress, 1=Slot 2 SD)",
    "ActiveFolder": "Active DCIM directory folder sequence number (e.g. 100NCZ_7)",
    "ActiveSlot": "Physical active hardware memory slot (Slot 1 vs Slot 2)",
    "FileNumberSequence": "File numbering continuity sequence behavior (0=Off/reset, 1=On/continuous, 2=Reset)",
    "NoCFCard": "Shutter release interlock when memory card is missing (0=Release allowed for testing, 1=Release locked)",
    "RecordingMedia": "Current active recording destination media (Slot 1 / Slot 2)",

    # System & Telemetry
    "CleanImageSensor": "Image sensor piezoelectric ultrasonic vibration dust cleaning (0=Off, 1=Startup, 2=Shutdown, 3=Startup+Shutdown)",
    "ImageCommentEnable": "User image comment metadata embedding switch (0=Off, 1=On)",
    "ImageCommentString": "User image comment UTF-16LE text string",
    "EnableCopyright": "Exif copyright and artist metadata embedding switch (0=Off, 1=On)",
    "ArtistName": "Photographer artist name string",
    "CopyrightInfo": "Copyright holder declaration string",
    "ACPower": "External power supply and USB-PD adapter status (0=Battery, 1=EH-7P AC adapter / USB power delivery)",
    "WarningStatus": "Global system warning and error telemetry bitmask (lens fault, card error, over-temperature, low battery)",
    "CameraOrientation": "Tri-axis orientation accelerometer posture (0=Landscape, 1=Portrait clockwise, 2=Portrait counter-clockwise, 3=Inverted)",
    "ConnectionPath": "Active network and system transport topology (0=USB local bus, 1=WT-7 network adapter, 2=Built-in Wi-Fi/Bluetooth)",
    "USBSpeed": "USB physical bus negotiated link speed (0=High Speed 480Mbps, 1=SuperSpeed 5Gbps USB 3.2 Gen1)",
    "InfoDispErrStatus": "Information display screen diagnostic error code telemetry",
    "AngleLevel": "Electronic virtual horizon dual-axis accelerometer inclination telemetry"
}

def get_subsystem(code_str, sym):
    code = int(code_str, 16)
    s = sym.upper()

    if 'WHITEBALANCE' in s or 'WB' in s or 'TUNEPRESET' in s or (s.startswith('PRESET') and 'COLOR' not in s and 'PICT' not in s) or code in [0xd0a3, 0xd0c2, 0xd0c3, 0xd0c4, 0xd0c5, 0xd0d0, 0xd108, 0xd14f, 0xd150, 0xd152, 0xd153, 0xd154, 0xd155, 0xd235, 0xd238, 0xd23a, 0xd23c, 0xd240, 0xd241, 0xd242, 0xd243, 0xd244, 0xd245, 0xd246, 0xd247, 0xd248]:
        return ('1_WB_COLOR', 'White balance and color calibration', 'White Balance & Color Calibration')

    if 'MOV' in s or 'TIMECODE' in s or 'DROPFRAME' in s or 'COUNTUP' in s or code in [0xd0bd, 0xd0cc, 0xd1d0, 0xd1d1, 0xd1d2, 0xd1de, 0xd1df, 0xd1ff, 0xd23d, 0xd310]:
        return ('2_MOVIE_AUDIO_TC', 'Movie recording, audio, and timecode engine', 'Movie Recording, Audio & Timecode Engine')

    if 'AF' in s or 'FOCUS' in s or 'PEAKING' in s or code in [0xd0f2, 0xd115]:
        return ('3_AUTOFOCUS', 'Autofocus and focus peaking assistance', 'Autofocus & Focus Peaking Assistance')

    if 'LENS' in s or 'FOCALLENGTH' in s or 'MAXAP' in s or code in [0xd02e, 0xd02f, 0xd138]:
        return ('4_LENS_IBIS', 'Lens optics, manual non-CPU profile, and IBIS', 'Lens Optics, Non-CPU Profile & IBIS')

    if 'RAW' in s or 'PICCTRL' in s or 'COLORSPACE' in s or 'FLICKER' in s or 'VIGNETTE' in s or 'DIFFRACTION' in s or 'NOISE' in s or 'DLIGHTING' in s or 'DISTORTION' in s or 'HDR' in s or code in [0xd048, 0xd049, 0xd04a, 0xd070, 0xd071, 0xd133, 0xd200, 0xd201, 0xd236, 0xd237, 0xd23b, 0xd23e, 0xd23f]:
        return ('5_IMAGE_PROCESSING', 'Image processing pipeline, RAW compression, and Picture Controls', 'Image Pipeline, RAW & Picture Controls')

    if 'FLASH' in s or 'SB' in s or code in [0xd08e, 0xd094, 0xd095, 0xd19c, 0xd1e0, 0xd1e1, 0xd1e2, 0xd1e3, 0xd1e4, 0xd1e5, 0xd1e6, 0xd1e8, 0xd1ea, 0xd1ec]:
        return ('6_FLASH_LIGHTING', 'Creative Lighting System (CLS) and flash control', 'Creative Lighting System & Flash Control')

    if 'LIVEVIEW' in s or 'MONITOR' in s or 'VIEWFINDER' in s or code in [0xd1a0, 0xd1a5, 0xd1a7, 0xd1ae, 0xd1b0, 0xd1b1, 0xd1b2, 0xd1b5, 0xd1b6, 0xd1c0, 0xd314]:
        return ('7_LIVEVIEW_DISPLAY', 'Live view streaming, EVF, and monitor control', 'Live View Streaming, EVF & Monitor')

    if 'EXPOSURE' in s or 'SHUTTER' in s or 'ISO' in s or 'BRACKET' in s or 'EV' in s or 'BURST' in s or 'CONTINUOUS' in s or 'SPEED' in s or code in [0xd030, 0xd034, 0xd035, 0xd04e, 0xd055, 0xd059, 0xd05b, 0xd062, 0xd067, 0xd068, 0xd069, 0xd073, 0xd075, 0xd076, 0xd077, 0xd0be, 0xd0c0, 0xd0c1, 0xd0d5, 0xd0d6, 0xd0fc, 0xd105, 0xd106, 0xd109, 0xd110, 0xd111, 0xd112, 0xd113, 0xd118, 0xd119, 0xd11a, 0xd11b, 0xd11c, 0xd11d, 0xd1b4, 0xd239]:
        return ('8_EXPOSURE_DRIVE', 'Exposure mechanics, metering base, and shutter drive', 'Exposure Mechanics, Metering & Shutter')

    if 'SLOT' in s or 'STORAGE' in s or 'CARD' in s or 'FILE' in s or 'FOLDER' in s or 'MEDIA' in s or code in [0xd04b, 0xd04c, 0xd107, 0xd108, 0xd109, 0xd10a, 0xd10b, 0xd10d, 0xd10f]:
        return ('9_STORAGE_FILES', 'Dual card slots and storage naming', 'Dual Card Slots & Storage Architecture')

    return ('10_SYSTEM_TELEMETRY', 'System configuration, orientation, and power telemetry', 'System Configuration & Telemetry')

def main():
    with open(PROPS_JSON, "r", encoding="utf-8") as f:
        props = json.load(f)

    code_to_sym = load_ptp_definitions()

    # Manual overrides for newly observed codes
    code_to_sym[0xd0bd] = "PTP_DPC_NIKON_MovieRemainingTime"
    code_to_sym[0xd19c] = "PTP_DPC_NIKON_WirelessFlashControl"
    code_to_sym[0xd1ff] = "PTP_DPC_NIKON_MovieHDMIOutputTimeCode"

    grouped = {}

    for k in sorted(props.keys()):
        code_int = int(k, 16)
        sym = code_to_sym.get(code_int, f"UNKNOWN_{k}")
        sub_id, sub_en, sub_zh = get_subsystem(k, sym)
        grouped.setdefault((sub_id, sub_en, sub_zh), []).append((k, sym, props[k]))

    lines = []
    lines.append("# Nikon vendor device properties inventory (0xD016..0xD314)")
    lines.append("")
    lines.append("This document provides the complete technical register of all **259 Nikon vendor device properties**")
    lines.append("queried from the physical Nikon Z 7II (`V1.60`) via PTP opcode `0x90ca` (`GetVendorPropCodes`)")
    lines.append("and decoded through standard `0x1014` (`GetDevicePropDesc`) and `0x1015` (`GetDevicePropValue`).")
    lines.append("")
    lines.append("Source dataset: [`vendor_properties_259.json`](/evidence/physical/vendor_properties_259.json)")
    lines.append("")
    lines.append("## Subsystem summary")
    lines.append("")
    lines.append("| ID | Subsystem | Domain | Properties |")
    lines.append("|---|---|---|---|")
    for (sub_id, sub_en, sub_zh), items in sorted(grouped.items()):
        clean_id = sub_id.split('_', 1)[1]
        lines.append(f"| `{clean_id}` | [{sub_en}](#{clean_id.lower().replace('_', '-')}) | {sub_zh} | {len(items)} |")
    lines.append("")

    for (sub_id, sub_en, sub_zh), items in sorted(grouped.items()):
        clean_id = sub_id.split('_', 1)[1]
        lines.append(f"## {sub_en}")
        lines.append("")
        lines.append(f"Domain: **{sub_zh}** ({len(items)} properties).")
        lines.append("")
        lines.append("| Code | Symbol | Type | Access | Current | Physical description / Camera menu mapping |")
        lines.append("|---|---|---|---|---|---|")
        for k, sym, info in items:
            code_hex = k.upper()
            clean_sym = sym.replace("PTP_DPC_NIKON_", "")
            dtype = DTYPE_MAP.get(info.get("data_type", ""), info.get("data_type", ""))
            access = info.get("access", "")
            cur = str(info.get("current_value", ""))
            if len(cur) > 20:
                cur = cur[:17] + "..."
            raw_hex = info.get("raw_value_hex", "")

            # Look up description by symbol
            desc = SYMBOL_DESCRIPTIONS_EN.get(clean_sym)
            if not desc:
                desc = f"Controls {clean_sym} state and parameters (raw: `{raw_hex}`)"

            lines.append(f"| `{code_hex}` | `{clean_sym}` | `{dtype}` | `{access}` | `{cur}` | {desc} |")
        lines.append("")

    with open(OUT_MD, "w", encoding="utf-8") as f:
        f.write("\n".join(lines) + "\n")

    print(f"[+] Successfully wrote {len(props)} properties to {OUT_MD}")

if __name__ == "__main__":
    main()
