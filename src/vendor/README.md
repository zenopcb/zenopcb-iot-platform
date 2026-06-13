# Vendored Libraries

Các thư viện bên dưới được bundle trực tiếp vào ZenoPCB để user **không cần cài thêm** trong `lib_deps`.

| Library | Version | License | Source | Class / namespace (post 2026-06-02 renames) |
|---------|---------|---------|--------|----------------------------------------------|
| TinyGSM | 0.12.0 | LGPL-3.0 | https://github.com/vshymanskyy/TinyGSM | `TinyGsm` (GPL3 — **NOT renamed**; class identity preserved per license) |
| StreamDebugger | ~1.0.1 | MIT (per upstream file header) | https://github.com/vshymanskyy/StreamDebugger | `ZenoStreamDebugger` (renamed Plan 06-2.5c 2026-06-02) |
| Preferences | 2.2.2 | MIT | https://github.com/vshymanskyy/Preferences | `ZenoPreferences` (renamed Plan 06-2.5c 2026-06-02) |
| PubSubClient | ~2.8 (Knolleary fork) | MIT | https://github.com/knolleary/pubsubclient | `ZenoPubSubClient` (renamed Plan 06-2.5c 2026-06-02) |
| ArduinoJson | 7.4.3 | MIT | https://github.com/bblanchon/ArduinoJson | namespace `ZenoJson` (renamed Plan 06-2.5b 2026-06-02; class names like `JsonDocument`/`JsonObject` unchanged — only the wrapping namespace renamed to avoid conflict with any external ArduinoJson copy the user has installed) |
| FlashStorage_STM32 | 1.2.0 | MIT | https://github.com/khoih-prog/FlashStorage_STM32 | `ZenoFlashStorage` (renamed Plan 07-03 2026-06-03 from upstream `EEPROMClass`; instance `EEPROM` → `ZenoEEPROM`; STM32F/L/H/G/WB/MP1 only — guarded by `#if defined(STM32F1)\|\|defined(STM32F4)\|\|...`; consumed by `hal/stm32/Stm32NVS` in Plan 07-04) |

## Cập nhật version

Khi muốn nâng version:
1. Cài version mới qua `lib_deps` tạm thời
2. Copy `src/` → `vendor/<Library>/`
3. Copy `LICENSE` → `vendor/<Library>/LICENSE`
4. Cập nhật `vendor/<Library>/VENDORED.md` với version mới + date
5. Xóa khỏi `lib_deps`
6. Cập nhật bảng trên
