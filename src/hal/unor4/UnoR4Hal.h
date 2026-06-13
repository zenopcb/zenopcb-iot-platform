#ifndef ZENOPCB_UNOR4_HAL_H
#define ZENOPCB_UNOR4_HAL_H

/**
 * @file UnoR4Hal.h
 * @brief Arduino UNO R4 WiFi (Renesas RA4M1) concrete facade implementing IZenoHal.
 *
 * Mechanical Pattern A mirror of Esp8266Hal.{h,cpp} from Phase 6 (Plan 06-01).
 * See .planning/phases/07-uno-r4-stm32-ports-capability-matrix/07-PATTERNS.md
 * §"UnoR4Hal" (lines 75-185).
 *
 * Composes the five UnoR4* sub-impls by value and exposes them via the
 * IZenoHal sub-getters. Capability bitmask returns `CAP_NVS | CAP_NTP |
 * CAP_WATCHDOG` (= 0x1C) baseline per CONTEXT D-10. Conditionally OR'd
 * with `CAP_OTA` when `-DZENOPCB_ENABLE_UNOR4_OTA` is defined (D-16
 * RESCOPED — custom WiFiClient + RA4M1 Flash API impl; opt-in only).
 *
 * `CAP_FS_FILES` deliberately omitted: ArduinoCore-renesas ships no
 * LittleFS / SPIFFS for RA4M1 (RESEARCH §Architectural Responsibility
 * Map line 109). UnoR4Storage methods all return failure stubs.
 *
 * Pitfall 3 — deleted copy semantics. The contained UnoR4OTA would, when
 * the opt-in flag is on, wrap shared Renesas FSP Flash open-handle state;
 * cloning UnoR4Hal could create a second wrapper that corrupts OTA
 * mid-stream. Deleting copy + assign makes that a compile error.
 *
 * The canonical instance is obtained via `getUnoR4Hal()` — a Meyers
 * singleton implemented in UnoR4Hal.cpp (lazy, not eager, to dodge
 * static-init-order issues across translation units; same pattern as
 * Esp32Hal / Esp8266Hal).
 */

#include <stdint.h>

#include "../IZenoHal.h"

// Pattern B/Pitfall 7 lifted to .h surface (Plan 06-2.5d carry-forward): the
// contained UnoR4*.h sub-impls include RA4M1-only headers (`<EEPROM.h>`
// pulls Renesas FSP EEPROM emulation, `<WiFiS3.h>` is UNO R4 WiFi-only, the
// Renesas FSP Flash API used by UnoR4OTA is not visible to other cores).
// PIO's library scanner indexes every header in lib/ZenoPCB/src/ on every
// env, including ESP32 envs, so we TU-guard the include of the sub-impls +
// the class body at the header surface so ESP32 / ESP8266 envs see an empty
// translation unit for this facade. The IZenoHal abstract interface include
// stays OUTSIDE the guard because it is the cross-platform contract type.
#if defined(ARDUINO_UNOR4_WIFI)

#include "UnoR4Storage.h"
#include "UnoR4NVS.h"
#include "UnoR4OTA.h"
#include "UnoR4Time.h"
#include "UnoR4System.h"

namespace ZenoPCB {

class UnoR4Hal : public IZenoHal {
public:
    UnoR4Hal() = default;
    ~UnoR4Hal() override = default;

    // Deleted copy semantics (Pitfall 3 — contained UnoR4OTA wraps shared
    // Renesas FSP Flash open-handle state when -DZENOPCB_ENABLE_UNOR4_OTA
    // is on; duplicating UnoR4Hal corrupts OTA state mid-stream).
    UnoR4Hal(const UnoR4Hal&) = delete;
    UnoR4Hal& operator=(const UnoR4Hal&) = delete;

    IZenoStorage& storage() override { return _storage; }
    IZenoNVS&     nvs()     override { return _nvs; }
    IZenoOTA&     ota()     override { return _ota; }
    IZenoTime&    time()    override { return _time; }
    IZenoSystem&  system()  override { return _system; }

    uint32_t capabilities() const override {
        // D-10 baseline: CAP_FS_FILES omitted (no LittleFS on RA4M1).
        // CAP_OTA opt-in via -DZENOPCB_ENABLE_UNOR4_OTA (D-16 RESCOPED).
        uint32_t caps = CAP_NVS | CAP_NTP | CAP_WATCHDOG;
    #ifdef ZENOPCB_ENABLE_UNOR4_OTA
        caps |= CAP_OTA;
    #endif
        return caps;
    }

private:
    UnoR4Storage _storage;
    UnoR4NVS     _nvs;
    UnoR4OTA     _ota;
    UnoR4Time    _time;
    UnoR4System  _system;
};

/**
 * Returns the canonical UNO R4 HAL instance.
 *
 * Implemented as a Meyers singleton in UnoR4Hal.cpp: lazy
 * function-local static, thread-safe in C++11+, no eager file-scope
 * global. Plan 07-06 wires this into the Zeno class default ctor via
 * a platform-guarded ZENOPCB_DEFAULT_HAL() macro extension.
 */
IZenoHal& getUnoR4Hal();

}  // namespace ZenoPCB

#endif  // defined(ARDUINO_UNOR4_WIFI)

#endif  // ZENOPCB_UNOR4_HAL_H
