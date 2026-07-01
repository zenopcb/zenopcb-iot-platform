#ifndef ZENOPCB_ESP8266_STORAGE_H
#define ZENOPCB_ESP8266_STORAGE_H

/**
 * @file Esp8266Storage.h
 * @brief ESP8266 concrete impl of IZenoStorage  wraps ESP8266 LittleFS.
 *
 * Mechanical mirror of Esp32Storage.{h,cpp} from.
 * See.planning/phases/06-esp8266-port/.
 *
 * `<LittleFS.h>` is the canonical include name on both ESP32 (Arduino
 * Core 3.x) and ESP8266 (Arduino Core 3.x.x); no platform switch is
 * needed here. The `writeFile` body diverges from the ESP32 analog by
 * calling a `.cpp`-local `ensureParentDirs(path)` helper before opening
 * (ESP8266 LittleFS does not auto-create parent dirs).
 *
 * Deleted copy semantics per (LittleFS is process-global
 * state  duplicating the wrapper duplicates the mount reference).
 */

#include "../IZenoStorage.h"

// / lifted to.h surface (, replicates the fix
// applied in to Esp8266NVS.h): although `<LittleFS.h>` resolves
// on both ESP32 and ESP8266 Arduino Core, the resolved file is platform-
// specific (different class implementations, header guard collisions
// possible). PIO's library scanner indexes this header on every env; guarding
// at the header surface keeps the ESP32 envs from pulling in the ESP8266
// LittleFS variant during library indexing. The IZenoStorage abstract
// interface include stays OUTSIDE the guard because it is the cross-platform
// contract type.
#if defined(ESP8266)

#include <LittleFS.h>

namespace ZenoPCB {

class Esp8266Storage : public IZenoStorage {
public:
    Esp8266Storage() = default;
    ~Esp8266Storage() override = default;

    // Deleted copy semantics (LittleFS is process-global state).
    Esp8266Storage(const Esp8266Storage&) = delete;
    Esp8266Storage& operator=(const Esp8266Storage&) = delete;

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

#endif  // defined(ESP8266)

#endif  // ZENOPCB_ESP8266_STORAGE_H
