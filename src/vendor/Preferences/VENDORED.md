# vshymanskyy/Preferences — VENDORED

**Upstream:** https://github.com/vshymanskyy/Preferences
**Version:** 2.2.2 (vendored 2026-06-02 from PlatformIO Registry)
**License:** MIT (see LICENSE)
**Author:** Volodymyr Shymanskyy
**Purpose:** LittleFS-backed `Preferences` API backport for ESP8266 (the ESP8266 Arduino Core has no built-in Preferences library, unlike ESP32 Arduino Core).

## Why vendored

Per user direction 2026-06-02 (Phase 6, ZenoPCB IoT Library):

- ZenoPCB Arduino library must be self-contained (no `lib_deps` external pulls during Arduino IDE compile).
- Avoid collision with any external Preferences library a user may have installed globally.

## Modifications from upstream

- **2026-06-02 (vendor time):** SPDX MIT header prepended to each .h/.cpp file for license compliance scanning. The original license text was preserved verbatim in `LICENSE`. No body changes.
- **2026-06-02 (Plan 06-2.5c):** Renamed class `Preferences` → `ZenoPreferences` to claim as ZenoPCB internal component. Brand consistency + conflict avoidance with user's installed Preferences libraries (e.g. ESP32 Core's built-in `Preferences`). SPDX MIT headers preserved; `LICENSE` file unchanged. File names kept (`Preferences.h/.cpp`) to minimize git history churn. Header guards (`_PREFERENCES_H_`, `_PREFERENCES_SETUP_H_`) kept as-is (unique tokens). Affected files: `Preferences.h`, `Preferences.cpp`, `Preferences_impl_fs.h`, `Preferences_impl_dct.h`. The platform-selector `Preferences_setup.h` retains its ESP32 `#error` message string for upstream-compatibility documentation (string literal, not code).

Future modifications must be documented here with date + rationale.

## Usage

Only `lib/ZenoPCB/src/hal/esp8266/Esp8266NVS.h` references this. The include path is relative: `#include "../../vendor/Preferences/Preferences.h"`. As of Plan 06-2.5c the class is **renamed to `ZenoPreferences`** (per user direction 2026-06-02) to claim as a ZenoPCB internal component and to avoid colliding with any user-installed `Preferences` library or with ESP32 Core's built-in `Preferences`. The Esp8266NVS.{h,cpp} translation unit is also wrapped in `#if defined(ESP8266) ... #endif` (Plan 06-01 Pitfall 7 / Pattern B lifted to the .h surface per Plan 06-2.5a) so ESP32 builds never instantiate the vendored ZenoPreferences class regardless.

## File inventory

| File | Role |
|------|------|
| `Preferences.h` / `Preferences.cpp` | Public `Preferences` class + put/get helpers |
| `Preferences_setup.h` | Platform-selector preprocessor (selects FS impl by `defined(ESP8266)` etc.) |
| `Preferences_impl_dct.h` | Particle Gen3 DCT-backed impl (not used on ESP8266) |
| `Preferences_impl_fs.h` | Filesystem-backed impl (active on ESP8266 — LittleFS) |
| `prefs_impl_arduino.h` | Arduino LittleFS/SPIFFS adapter (active on ESP8266) |
| `prefs_impl_posix.h` | POSIX filesystem adapter (Particle) |
| `prefs_impl_wifinina.h` | WiFiNINA storage adapter (SAMD MKR boards) |
| `prefs_impl_dummy.h` | No-op stub (testing) |
| `LICENSE` | MIT license text (verbatim from upstream) |
