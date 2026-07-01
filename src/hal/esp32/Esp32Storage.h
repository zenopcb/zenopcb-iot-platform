#ifndef ZENOPCB_ESP32_STORAGE_H
#define ZENOPCB_ESP32_STORAGE_H

/**
 * @file Esp32Storage.h
 * @brief ESP32 concrete impl of IZenoStorage  wraps Arduino-ESP32 LittleFS.
 *
 * Part of HAL (plan 04-02). Confined to hal/esp32/ no LittleFS
 * include leaks into the interface layer or into consumer code. Deleted
 * copy semantics per (avoid duplicated wrappers around the
 * underlying global LittleFS state).
 */

#include "../IZenoStorage.h"

// TU-guard-at-header (symmetric to ESP8266
// mirror). The Arduino-ESP32 `<LittleFS.h>` ABI differs from the
// ESP8266 LittleFS (e.g. `begin(bool)` vs `begin()`, `File::path()`),
// so guarding the include is necessary even though the header name
// collides.
#if defined(ESP32)

#include <LittleFS.h>

namespace ZenoPCB {

class Esp32Storage : public IZenoStorage {
public:
    Esp32Storage() = default;
    ~Esp32Storage() override = default;

    // Deleted copy semantics (LittleFS is process-global state).
    Esp32Storage(const Esp32Storage&) = delete;
    Esp32Storage& operator=(const Esp32Storage&) = delete;

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

#endif  // defined(ESP32)

#endif  // ZENOPCB_ESP32_STORAGE_H
