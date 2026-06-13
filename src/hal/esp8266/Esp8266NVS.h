#ifndef ZENOPCB_ESP8266_NVS_H
#define ZENOPCB_ESP8266_NVS_H

/**
 * @file Esp8266NVS.h
 * @brief ESP8266 concrete impl of IZenoNVS — wraps the
 *        vshymanskyy/Preferences backport (LittleFS-backed).
 *
 * Mechanical mirror of Esp32NVS.{h,cpp} from Phase 4 (Plan 04-02).
 * See .planning/phases/06-esp8266-port/06-PATTERNS.md §Pattern A.
 *
 * D-05 REVISED: on ESP32 `<Preferences.h>` resolves to the bundled
 * arduino-esp32 core header; on ESP8266 we now use the in-repo vendored
 * vshymanskyy/Preferences@2.2.2 (Plan 06-2.5a vendored + Plan 06-2.5c
 * renamed class to ZenoPreferences for brand consistency). The backport
 * mirrors the Arduino-ESP32 Preferences surface line-for-line, so the
 * .cpp bodies are byte-identical with `s/Esp32/Esp8266/g` modulo the
 * `Preferences -> ZenoPreferences` class-name swap on ESP8266.
 *
 * Holds a single `ZenoPreferences` member and the `_open` flag so begin() /
 * end() pairing is preserved (Pitfall 1 — handle table is small even
 * on the backport).
 *
 * Deleted copy semantics per Pitfall 3 (ZenoPreferences wraps a single
 * underlying handle; duplicating the wrapper duplicates the reference).
 */

#include "../IZenoNVS.h"

// Header-body ESP8266 guard (Pattern B / Pitfall 7 — Plan 06-2.5a Rule 1
// deviation): the vendored Preferences's Preferences_setup.h emits
// `#error "For ESP32 devices, please use the native Preferences library"`
// when defined(ESP32). The previous lib_deps version of `<Preferences.h>`
// was resolved by PIO to the ESP32 Core builtin on ESP32 builds, so the
// `#error` never tripped. Pinning the include to the relative vendored
// path requires us to also TU-guard the include + class body to keep
// ESP32 envs (which still scan this header during library indexing) from
// hitting the `#error`. On ESP32 this becomes an empty translation unit.
#if defined(ESP8266)

// Preferences API from vendored vshymanskyy/Preferences (MIT, see
// lib/ZenoPCB/src/vendor/Preferences/LICENSE). Plan 06-2.5a moved this
// from `<Preferences.h>` lib_deps pull to in-repo vendored copy so the
// ZenoPCB Arduino library is self-contained (no external lib_deps).
#include "../../vendor/Preferences/Preferences.h"

namespace ZenoPCB {

class Esp8266NVS : public IZenoNVS {
public:
    Esp8266NVS() = default;
    ~Esp8266NVS() override {
        // Best-effort: release the namespace handle if the user forgot to
        // call end() before destruction. Avoids dangling NVS handle table
        // entries when the singleton is destroyed at chip reset.
        if (_open) {
            _prefs.end();
            _open = false;
        }
    }

    // Deleted copy semantics (Pitfall 3 — ZenoPreferences wraps a single NVS
    // handle; duplicating the wrapper duplicates the handle reference).
    Esp8266NVS(const Esp8266NVS&) = delete;
    Esp8266NVS& operator=(const Esp8266NVS&) = delete;

    bool begin(const char *namespaceName, bool readOnly = false) override;
    void end() override;

    bool putString(const char *key, const char *value) override;
    size_t getString(const char *key, char *out, size_t maxLen,
                     const char *defaultValue) override;

    bool putULong(const char *key, uint32_t value) override;
    uint32_t getULong(const char *key, uint32_t defaultValue) override;

    bool putBool(const char *key, bool value) override;
    bool getBool(const char *key, bool defaultValue) override;

    bool putUChar(const char *key, uint8_t value) override;
    uint8_t getUChar(const char *key, uint8_t defaultValue) override;

    bool remove(const char *key) override;
    bool clear() override;

private:
    ZenoPreferences _prefs;
    bool _open = false;
};

}  // namespace ZenoPCB

#endif  // defined(ESP8266)

#endif  // ZENOPCB_ESP8266_NVS_H
