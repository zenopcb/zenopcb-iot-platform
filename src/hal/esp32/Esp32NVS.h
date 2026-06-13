#ifndef ZENOPCB_ESP32_NVS_H
#define ZENOPCB_ESP32_NVS_H

/**
 * @file Esp32NVS.h
 * @brief ESP32 concrete impl of IZenoNVS — wraps Arduino-ESP32 Preferences.
 *
 * Part of Phase 4 HAL (plan 04-02). Holds a single `Preferences` member
 * and the `_open` flag so begin() / end() pairing is preserved (Pitfall 1).
 * Deleted copy semantics per Pitfall 3.
 */

#include "../IZenoNVS.h"

// Plan 06-03 — TU-guard-at-header (symmetric to the Plan 06-2.5d fix on
// the ESP8266 sub-impls). PIO's library scanner indexes every header in
// `lib/ZenoPCB/src/` on every env, including ESP8266 envs, so the
// `<Preferences.h>` include below must sit behind the platform guard.
// IZenoNVS stays OUTSIDE the guard because it is the cross-platform
// contract type.
#if defined(ESP32)

#include <Preferences.h>

namespace ZenoPCB {

class Esp32NVS : public IZenoNVS {
public:
    Esp32NVS() = default;
    ~Esp32NVS() override {
        // Best-effort: release the namespace handle if the user forgot to
        // call end() before destruction. Avoids dangling NVS handle table
        // entries when the singleton is destroyed at chip reset.
        if (_open) {
            _prefs.end();
            _open = false;
        }
    }

    // Deleted copy semantics (Pitfall 3 — Preferences wraps a single NVS
    // handle; duplicating the wrapper duplicates the handle reference).
    Esp32NVS(const Esp32NVS&) = delete;
    Esp32NVS& operator=(const Esp32NVS&) = delete;

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
    Preferences _prefs;
    bool _open = false;
};

}  // namespace ZenoPCB

#endif  // defined(ESP32)

#endif  // ZENOPCB_ESP32_NVS_H
