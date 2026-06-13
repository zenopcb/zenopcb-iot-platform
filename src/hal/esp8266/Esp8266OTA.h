#ifndef ZENOPCB_ESP8266_OTA_H
#define ZENOPCB_ESP8266_OTA_H

/**
 * @file Esp8266OTA.h
 * @brief ESP8266 concrete impl of IZenoOTA — wraps ESP8266 Updater.h.
 *
 * Mechanical mirror of Esp32OTA.{h,cpp} from Phase 4 (Plan 04-02).
 * See .planning/phases/06-esp8266-port/06-PATTERNS.md §Pattern A.
 *
 * ESP8266 Arduino Core spells the OTA library filename Updater.h, not
 * Update.h (Pitfall 6). The underlying type is still `Update` (global
 * `UpdaterClass Update;`), but the include header differs from ESP32.
 *
 * `Update` is a process-global singleton on both ESP32 and ESP8266
 * (Pitfall 3): the wrapper holds no member state and forwards 1:1
 * for the methods ESP8266 supports.
 *
 * Pitfall 2 stubs — ESP8266 eboot is single-slot:
 *   - `abort()`   → no-op (Update has no abort() on ESP8266; eboot
 *                  refuses to swap a malformed partial sketch on next
 *                  boot, so a no-op is the correct behaviour).
 *   - `canRollBack()` / `rollBack()` → return false (no spare bootable
 *                  partition; the old sketch is overwritten in place).
 *   - `errorString()` → ESP8266 returns `String`; we cache via a static
 *                  `char[64]` to honour the IZenoOTA contract's
 *                  "pointer remains valid until the next OTA call".
 *
 * Deleted copy semantics so that accidental copies of Esp8266Hal
 * (which by-value-owns one of these) cannot stamp out two wrappers
 * around the same underlying global Update — which would corrupt OTA
 * mid-stream.
 */

#include "../IZenoOTA.h"

// Pattern B/Pitfall 7 lifted to .h surface (Plan 06-2.5d, replicates the fix
// applied in Plan 06-2.5a to Esp8266NVS.h): `<Updater.h>` is the ESP8266
// Arduino Core OTA library filename (Pitfall 6 — ESP32 spells it `<Update.h>`
// without trailing `r`). The header does not exist on ESP32 Core, so PIO's
// library scanner fails the ESP32 build with `fatal error: Updater.h: No such
// file or directory` (deferred-items.md D-1 baseline regression). TU-guarding
// the include + class body at the header surface makes ESP32 envs skip the
// entire translation unit (empty TU). The IZenoOTA abstract interface include
// stays OUTSIDE the guard because it is the cross-platform contract type.
#if defined(ESP8266)

// ESP8266 Arduino Core spells the OTA library filename Updater.h, not Update.h (Pitfall 6).
#include <Updater.h>

namespace ZenoPCB {

class Esp8266OTA : public IZenoOTA {
public:
    Esp8266OTA() = default;
    ~Esp8266OTA() override = default;

    // Deleted copy semantics (Pitfall 3 — Update is a process-global
    // singleton; multiple wrappers around it corrupt OTA state).
    Esp8266OTA(const Esp8266OTA&) = delete;
    Esp8266OTA& operator=(const Esp8266OTA&) = delete;

    bool begin(size_t expectedSize, const char *expectedMd5 = nullptr) override;
    size_t write(const uint8_t *data, size_t len) override;
    bool end() override;
    void abort() override;
    const char *errorString() override;
    bool canRollBack() override;
    bool rollBack() override;
};

}  // namespace ZenoPCB

#endif  // defined(ESP8266)

#endif  // ZENOPCB_ESP8266_OTA_H
