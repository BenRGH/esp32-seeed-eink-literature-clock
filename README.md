# ESP32 E-Ink Literature Clock

Firmware for a literature quote clock on:

- SEEED XIAO ESP32-C3
- MH-ET LIVE 2.13" 3-color e-ink (GDEW0213Z16)
- RV3028 RTC

This repository is intentionally ESP32-firmware-only.

## Build

1. Install PlatformIO.
1. From repo root, build:

```powershell
platformio run --environment seeed_xiao_esp32c3
```

1. Upload:

```powershell
platformio run --environment seeed_xiao_esp32c3 --target upload
```

## Quote Data Pipeline

- Source data: `litclock_annotated.csv`
- Generated header used by firmware: `src/litclock_data.h`
- Generator: `generate_litclock_data.ps1`

Generate quote header:

```powershell
.\generate_litclock_data.ps1
```

Notes:

- Data is normalized to UTF-8 without BOM.
- Characters outside U+00FF are compatibility-folded for font safety.
- Overlong leading quote text is pre-trimmed (whole words only) with a small `...` marker to keep the time phrase visible.

## Wi-Fi Credentials (Optional for NTP)

Copy:

- `src/wifi_credentials.h.template` -> `src/wifi_credentials.h`

Then fill in `WIFI_SSID` and `WIFI_PASS`.

`src/wifi_credentials.h` is git-ignored.

## Repository Scope

The project has been cleaned to one Git repository focused on ESP32 firmware and its data-generation pipeline.
