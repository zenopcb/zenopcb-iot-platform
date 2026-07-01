#ifndef ZENOPCB_ESP8266_HAL_H
#define ZENOPCB_ESP8266_HAL_H

/**
 * @file Esp8266Hal.h
 * @brief ESP8266 concrete facade implementing IZenoHal.
 *
 * Mechanical mirror of Esp32Hal.{h,cpp} from.
 * See.planning/phases/06-esp8266-port/.
 *
 * Composes the five Esp8266* sub-impls by value and exposes them via the
 * IZenoHal sub-getters. `capabilities()` returns all six bits because
 * ESP8266 has every facility (LittleFS, OTA via Updater.h, NVS via
 * vshymanskyy/Preferences backport, NTP, software watchdog, captive
 * portal via ESP8266WebServer) see + Plan
 * 07-06 CAP_CAPTIVE_PORTAL extension.
 *
 * deleted copy semantics. The contained Esp8266OTA wraps the
 * process-global `Update` singleton (still named `Update`, but declared
 * in `Updater.h` on ESP8266), so cloning Esp8266Hal would create a
 * second wrapper that silently shares state. Deleting copy + assign
 * makes that a compile error.
 *
 * The canonical instance is obtained via `getEsp8266Hal()`  a Meyers
 * singleton implemented in Esp8266Hal.cpp (lazy, not eager
 * to dodge static-init-order issues across translation units; same
 * pattern as Esp32Hal).
 */

#include <stdint.h>

#include "../IZenoHal.h"

// / lifted to.h surface (, replicates the fix
// applied in to Esp8266NVS.h): the contained Esp8266*.h sub-impls
// include ESP8266-only headers (`<Updater.h>`, the vendored ESP8266-only
// Preferences backport, etc.). PIO's library scanner indexes every header in
// lib/ZenoPCB/src/ on every env, including ESP32 envs, so we TU-guard the
// include of the sub-impls + the class body at the header surface so ESP32
// envs see an empty translation unit for this facade. ESP8266 behaviour
// unchanged; the IZenoHal abstract interface include stays OUTSIDE the guard
// because it is the cross-platform contract type.
#if defined(ESP8266)

#include "Esp8266Storage.h"
#include "Esp8266NVS.h"
#include "Esp8266OTA.h"
#include "Esp8266Time.h"
#include "Esp8266System.h"

namespace ZenoPCB {

class Esp8266Hal : public IZenoHal {
public:
    Esp8266Hal() = default;
    ~Esp8266Hal() override = default;

    // Deleted copy semantics (contained Esp8266OTA wraps the
    // global Update singleton; duplicating Esp8266Hal corrupts OTA state).
    Esp8266Hal(const Esp8266Hal&) = delete;
    Esp8266Hal& operator=(const Esp8266Hal&) = delete;

    IZenoStorage& storage() override { return _storage; }
    IZenoNVS&     nvs()     override { return _nvs; }
    IZenoOTA&     ota()     override { return _ota; }
    IZenoTime&    time()    override { return _time; }
    IZenoSystem&  system()  override { return _system; }

    uint32_t capabilities() const override {
        // CAP_CAPTIVE_PORTAL added so the
        // `Zeno::wifiProvisioning(const char*, const char*)` gate proceeds
        // to delegation on ESP8266 builds (rather than returning Unavailable).
        return CAP_FS_FILES | CAP_OTA | CAP_NVS | CAP_NTP | CAP_WATCHDOG | CAP_CAPTIVE_PORTAL;
    }

private:
    Esp8266Storage _storage;
    Esp8266NVS     _nvs;
    Esp8266OTA     _ota;
    Esp8266Time    _time;
    Esp8266System  _system;
};

/**
 * Returns the canonical ESP8266 HAL instance.
 *
 * implemented as a Meyers singleton in Esp8266Hal.cpp: lazy
 * function-local static, thread-safe in C++11+, no eager file-scope
 * global. wires this into the Zeno class default ctor via
 * a platform-guarded ZENOPCB_DEFAULT_HAL() macro.
 */
IZenoHal& getEsp8266Hal();

}  // namespace ZenoPCB

#endif  // defined(ESP8266)

#endif  // ZENOPCB_ESP8266_HAL_H
