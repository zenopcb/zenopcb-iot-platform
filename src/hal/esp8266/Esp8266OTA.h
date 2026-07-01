#ifndef ZENOPCB_ESP8266_OTA_H
#define ZENOPCB_ESP8266_OTA_H

/**
 * @file Esp8266OTA.h
 * @brief ESP8266 concrete impl of IZenoOTA  wraps ESP8266 Updater.h.
 *
 * Mechanical mirror of Esp32OTA.{h,cpp} from.
 * See.planning/phases/06-esp8266-port/.
 *
 * ESP8266 Arduino Core spells the OTA library filename Updater.h, not
 * Update.h. The underlying type is still `Update` (global
 * `UpdaterClass Update;`), but the include header differs from ESP32.
 *
 * `Update` is a process-global singleton on both ESP32 and ESP8266
 * : the wrapper holds no member state and forwards 1:1
 * for the methods ESP8266 supports.
 *
 * stubs ESP8266 eboot is single-slot
 *   - `abort()`    no-op (Update has no abort() on ESP8266; eboot
 *                  refuses to swap a malformed partial sketch on next
 *                  boot, so a no-op is the correct behaviour).
 *   - `canRollBack()` / `rollBack()`  return false (no spare bootable
 *                  partition; the old sketch is overwritten in place).
 *   - `errorString()`  ESP8266 returns `String`; we cache via a static
 *                  `char[64]` to honour the IZenoOTA contract's
 *                  "pointer remains valid until the next OTA call".
 *
 * Deleted copy semantics so that accidental copies of Esp8266Hal
 * (which by-value-owns one of these) cannot stamp out two wrappers
 * around the same underlying global Update  which would corrupt OTA
 * mid-stream.
 */

#include "../IZenoOTA.h"

// / lifted to.h surface (, replicates the fix
// applied in to Esp8266NVS.h): `<Updater.h>` is the ESP8266
// Arduino Core OTA library filename (ESP32 spells it `<Update.h>`
// without trailing `r`). The header does not exist on ESP32 Core, so PIO's
// library scanner fails the ESP32 build with `fatal error: Updater.h: No such
// file or directory` (deferred-items.md baseline regression). TU-guarding
// the include + class body at the header surface makes ESP32 envs skip the
// entire translation unit (empty TU). The IZenoOTA abstract interface include
// stays OUTSIDE the guard because it is the cross-platform contract type.
#if defined(ESP8266)

// ESP8266 Arduino Core spells the OTA library filename Updater.h, not Update.h.
#include <Updater.h>

namespace ZenoPCB {

class Esp8266OTA : public IZenoOTA {
public:
    Esp8266OTA() = default;
    ~Esp8266OTA() override = default;

    // Deleted copy semantics (Update is a process-global
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
