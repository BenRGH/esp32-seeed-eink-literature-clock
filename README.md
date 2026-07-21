# ESP32 E-Ink Literature Clock

Firmware for a literature quote clock on:

- SEEED XIAO ESP32-C3
- MH-ET LIVE 2.13" 3-color e-ink (GDEW0213Z16)
- RV3028 RTC

This repository is intentionally focused on ESP32 firmware and its quote-data generation pipeline.

## Project Lineage

This repository was originally forked from:

- https://github.com/JohsEnevoldsen/literature-clock

Credit to Johs Enevoldsen and original contributors for the upstream project,
which itself credits earlier inspirations in its README.

## Build

1. Install PlatformIO.
2. From repo root, build:

```powershell
platformio run --environment seeed_xiao_esp32c3
```

3. Upload:

```powershell
platformio run --environment seeed_xiao_esp32c3 --target upload
```

## Quote Data Pipeline

- Source data: litclock_annotated.csv
- Generated header used by firmware: src/litclock_data.h
- Generator: generate_litclock_data.ps1

Generate quote header:

```powershell
.\generate_litclock_data.ps1
```

Notes:

- Data is written as UTF-8 without BOM.
- Characters outside U+00FF are compatibility-folded for display-font safety.
- Overlength rows are retained by generator-side truncation around the time phrase.
- Truncation is whole-word only and keeps contiguous source text.
- The firmware does a final display-aware fit at render time: it trims the quote end first, then trims the start if needed to keep the time visible, and falls back to balancing around the time phrase for very large quotes.
- Attribution is rendered on one or two lines depending on how much vertical space the quote leaves on the display.

## Wi-Fi Credentials (Optional for NTP)

Copy:

- src/wifi_credentials.h.template -> src/wifi_credentials.h

Then fill in WIFI_SSID and WIFI_PASS.

src/wifi_credentials.h is git-ignored.

## Licensing And Attribution

Project firmware and generator code are licensed under GNU GPL v3.0-or-later.
See LICENCE.md.

This means the project is open source and free to use, modify, and redistribute,
including commercial use, provided GPL terms are followed.

Important:

- No additional project-specific restrictions are imposed beyond upstream and legal requirements.
- Third-party libraries keep their own licenses; you must comply with each when redistributing firmware.
- This repository includes fork-derived material from the original literature-clock project.
- Upstream literature-clock is published under CC BY-NC-SA 2.5; inherited or derived material from that source remains subject to its terms unless replaced with independently authored content.
- Quote text excerpts and book metadata in litclock_annotated.csv are not owned by this repository and are not relicensed here.

## Upstream Sources

The project uses these upstream libraries/repos via PlatformIO:

- GxEPD2: https://github.com/ZinggJM/GxEPD2
- Adafruit GFX Library: https://github.com/adafruit/Adafruit-GFX-Library
- Adafruit BusIO: https://github.com/adafruit/Adafruit_BusIO
- U8g2: https://github.com/olikraus/u8g2
- U8g2_for_Adafruit_GFX: https://github.com/olikraus/U8g2_for_Adafruit_GFX
- RTC RV-3028-C7 Arduino Library: https://github.com/constiko/RV-3028_C7-Arduino_Library

See THIRD_PARTY_NOTICES.md for license notes.
