#ifndef ZENOPCB_UNOR4_STORAGE_H
#define ZENOPCB_UNOR4_STORAGE_H

/**
 * @file UnoR4Storage.h
 * @brief Arduino UNO R4 WiFi (Renesas RA4M1) concrete impl of IZenoStorage.
 *
 * Mechanical Pattern A mirror of Esp8266Storage.{h,cpp} (Plan 06-01).
 * See .planning/phases/07-uno-r4-stm32-ports-capability-matrix/07-PATTERNS.md
 * §"UnoR4Storage" (lines 258-336).
 *
 * **STUB IMPLEMENTATION.** ArduinoCore-renesas ships no LittleFS / SPIFFS
 * for RA4M1 (RESEARCH §Architectural Responsibility Map line 109).
 * UnoR4Hal::capabilities() omits CAP_FS_FILES (CONTEXT D-10), so
 * capability-gated callers should never reach the stubbed methods. Each
 * method returns a failure value (false / 0 / no-op); `begin()` emits a
 * single warn log to surface the platform gap if a caller bypasses the
 * capability gate.
 *
 * Deleted copy semantics for Pitfall 3 hygiene — even though the stub
 * holds no real handle, the class is composed by value into UnoR4Hal
 * (which itself deletes copy/assign), and consistent hygiene reduces
 * the chance of a future divergence between siblings.
 */

#include "../IZenoStorage.h"

// Pattern B/Pitfall 7 lifted to .h surface (Plan 06-2.5d carry-forward):
// PIO's library scanner indexes every header on every env. Even though this
// header would compile as an empty TU on ESP32/ESP8266 once the body is
// guarded, we keep the TU guard for symmetry with the Esp8266Storage.h /
// Esp8266OTA.h family so the four HAL impl trees share an identical surface
// pattern. The IZenoStorage abstract interface include stays OUTSIDE the
// guard because it is the cross-platform contract type.
#if defined(ARDUINO_UNOR4_WIFI)

namespace ZenoPCB {

class UnoR4Storage : public IZenoStorage {
public:
    UnoR4Storage() = default;
    ~UnoR4Storage() override = default;

    // Deleted copy semantics (Pattern D — sibling-symmetry with the other
    // HAL impls; UnoR4Storage holds no real handle today but the class
    // is composed by value into UnoR4Hal which deletes copy/assign).
    UnoR4Storage(const UnoR4Storage&) = delete;
    UnoR4Storage& operator=(const UnoR4Storage&) = delete;

    bool begin() override;
    bool exists(const char *path) override;
    size_t readFile(const char *path, char *out, size_t maxLen) override;
    size_t writeFile(const char *path, const char *data, size_t len) override;
    bool deleteFile(const char *path) override;
    bool mkdir(const char *path) override;
    void listFiles(const char *prefix,
                   std::function<void(const char *)> callback) override;
};

}  // namespace ZenoPCB

#endif  // defined(ARDUINO_UNOR4_WIFI)

#endif  // ZENOPCB_UNOR4_STORAGE_H
