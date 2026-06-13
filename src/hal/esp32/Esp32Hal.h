#ifndef ZENOPCB_ESP32_HAL_H
#define ZENOPCB_ESP32_HAL_H

/**
 * @file Esp32Hal.h
 * @brief ESP32 concrete facade implementing IZenoHal.
 *
 * Composes the five Esp32* sub-impls by value and exposes them via the
 * IZenoHal sub-getters. `capabilities()` returns all six bits because
 * ESP32 has every facility (LittleFS, OTA, NVS, NTP, watchdog, captive
 * portal — AP-mode HTTP server via WebServer.h).
 *
 * Pitfall 3 — deleted copy semantics. The contained Esp32OTA wraps the
 * process-global `Update` singleton, so cloning Esp32Hal would create a
 * second wrapper that silently shares state. Deleting copy + assign
 * makes that a compile error.
 *
 * The canonical instance is obtained via `getEsp32Hal()` — a Meyers
 * singleton implemented in Esp32Hal.cpp (Pitfall 7 — lazy, not eager,
 * to dodge static-init-order issues across translation units).
 */

#include <stdint.h>

#include "../IZenoHal.h"

// Plan 06-03 — TU-guard-at-header (symmetric to Plan 06-2.5d ESP8266
// facade fix). The contained Esp32*.h sub-impls include ESP32-only
// headers (`<Preferences.h>`, `<Update.h>`, `<LittleFS.h>`,
// `<esp_system.h>`, etc.). PIO's library scanner indexes every header
// in lib/ZenoPCB/src/ on every env, including ESP8266 envs, so we
// TU-guard the include of the sub-impls + the class body at the
// header surface so ESP8266 envs see an empty translation unit for
// this facade. IZenoHal stays OUTSIDE the guard because it is the
// cross-platform contract type.
#if defined(ESP32)

#include "Esp32Storage.h"
#include "Esp32NVS.h"
#include "Esp32OTA.h"
#include "Esp32Time.h"
#include "Esp32System.h"

namespace ZenoPCB {

class Esp32Hal : public IZenoHal {
public:
    Esp32Hal() = default;
    ~Esp32Hal() override = default;

    // Deleted copy semantics (Pitfall 3 — contained Esp32OTA wraps the
    // global Update singleton; duplicating Esp32Hal corrupts OTA state).
    Esp32Hal(const Esp32Hal&) = delete;
    Esp32Hal& operator=(const Esp32Hal&) = delete;

    IZenoStorage& storage() override { return _storage; }
    IZenoNVS&     nvs()     override { return _nvs; }
    IZenoOTA&     ota()     override { return _ota; }
    IZenoTime&    time()    override { return _time; }
    IZenoSystem&  system()  override { return _system; }

    uint32_t capabilities() const override {
        // Phase 7 Plan 07-06 — CAP_CAPTIVE_PORTAL added so the Pattern G
        // `Zeno::wifiProvisioning(const char*, const char*)` gate proceeds
        // to delegation on ESP32 builds (rather than returning Unavailable).
        return CAP_FS_FILES | CAP_OTA | CAP_NVS | CAP_NTP | CAP_WATCHDOG | CAP_CAPTIVE_PORTAL;
    }

private:
    Esp32Storage _storage;
    Esp32NVS     _nvs;
    Esp32OTA     _ota;
    Esp32Time    _time;
    Esp32System  _system;
};

/**
 * Returns the canonical ESP32 HAL instance.
 *
 * Pitfall 7 — implemented as a Meyers singleton in Esp32Hal.cpp: lazy
 * function-local static, thread-safe in C++11+, no eager file-scope
 * global. Plan 04-05 wires this into the Zeno class default ctor.
 */
IZenoHal& getEsp32Hal();

}  // namespace ZenoPCB

#endif  // defined(ESP32)

#endif  // ZENOPCB_ESP32_HAL_H
