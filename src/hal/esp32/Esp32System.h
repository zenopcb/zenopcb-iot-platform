#ifndef ZENOPCB_ESP32_SYSTEM_H
#define ZENOPCB_ESP32_SYSTEM_H

/**
 * @file Esp32System.h
 * @brief ESP32 concrete impl of IZenoSystem — wraps ESP.* + esp_task_wdt.
 *
 * Part of Phase 4 HAL (plan 04-02). Methods forward 1:1 to the Arduino-ESP32
 * `ESP` singleton and the ESP-IDF task watchdog. `restart()` is declared
 * `[[noreturn]]` to match the interface (Pitfall 4); the impl also includes
 * an unreachable infinite loop after `ESP.restart()` as a defense against
 * toolchains that drop the attribute.
 */

#include "../IZenoSystem.h"

// Plan 06-03 — TU-guard-at-header (symmetric to Plan 06-2.5d ESP8266
// mirror). The ESP-IDF headers `<esp_system.h>` + `<esp_task_wdt.h>`
// do not exist on ESP8266, so the include block and the class body sit
// behind the platform guard.
#if defined(ESP32)

#include <Arduino.h>           // millis()
#include <esp_system.h>
#include <esp_task_wdt.h>

namespace ZenoPCB {

class Esp32System : public IZenoSystem {
public:
    Esp32System() = default;
    ~Esp32System() override = default;

    // Deleted copy semantics (Pitfall 3 hygiene — ESP.* is a single global).
    Esp32System(const Esp32System&) = delete;
    Esp32System& operator=(const Esp32System&) = delete;

    [[noreturn]] void restart() override;
    uint32_t getFreeHeap() override;
    uint32_t getMaxAllocHeap() override;
    uint32_t getTotalHeap() override;
    size_t getUniqueId(char *out, size_t outSize) override;
    uint32_t uptimeMs() override;
    void feedWatchdog() override;
};

}  // namespace ZenoPCB

#endif  // defined(ESP32)

#endif  // ZENOPCB_ESP32_SYSTEM_H
