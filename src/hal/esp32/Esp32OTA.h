#ifndef ZENOPCB_ESP32_OTA_H
#define ZENOPCB_ESP32_OTA_H

/**
 * @file Esp32OTA.h
 * @brief ESP32 concrete impl of IZenoOTA  wraps Arduino-ESP32 Update.h.
 *
 * Part of HAL (plan 04-02). `Update` is a global singleton on
 * ESP32 : the wrapper holds no member state and forwards 1:1.
 * Deleted copy semantics so that accidental copies of Esp32Hal (which
 * by-value-owns one of these) cannot stamp out two wrappers around the
 * same underlying global  which would corrupt OTA mid-stream.
 */

#include "../IZenoOTA.h"

// TU-guard-at-header (symmetric to ESP8266
// mirror). The ESP32-only `<Update.h>` include sits behind the platform
// guard so PIO's library scanner does not pull it into ESP8266 envs.
#if defined(ESP32)

#include <Update.h>

namespace ZenoPCB {

class Esp32OTA : public IZenoOTA {
public:
    Esp32OTA() = default;
    ~Esp32OTA() override = default;

    // Deleted copy semantics (Update is a process-global
    // singleton; multiple wrappers around it corrupt OTA state).
    Esp32OTA(const Esp32OTA&) = delete;
    Esp32OTA& operator=(const Esp32OTA&) = delete;

    bool begin(size_t expectedSize, const char *expectedMd5 = nullptr) override;
    size_t write(const uint8_t *data, size_t len) override;
    bool end() override;
    void abort() override;
    const char *errorString() override;
    bool canRollBack() override;
    bool rollBack() override;
};

}  // namespace ZenoPCB

#endif  // defined(ESP32)

#endif  // ZENOPCB_ESP32_OTA_H
