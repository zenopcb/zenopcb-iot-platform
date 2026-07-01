#ifndef ZENOPCB_ESP8266_SYSTEM_H
#define ZENOPCB_ESP8266_SYSTEM_H

/**
 * @file Esp8266System.h
 * @brief ESP8266 concrete impl of IZenoSystem  wraps ESP.* + software WDT.
 *
 * Mechanical mirror of Esp32System.{h,cpp} from.
 * See.planning/phases/06-esp8266-port/.
 *
 * The ESP8266 build does NOT have the ESP-IDF system header or the
 * ESP-IDF task-watchdog header (both are ESP32-only). Both `ESP.*`
 * and `millis()` are visible via plain `<Arduino.h>` on ESP8266
 * (/).
 *
 * Method-body divergences from the ESP32 analog (implemented in
 * Esp8266System.cpp):
 *   - `getMaxAllocHeap()` body calls `ESP.getMaxFreeBlockSize()`
 * (ESP8266 spells the method differently).
 * `getTotalHeap` returns hardcoded `81920` (ESP8266
 *     DRAM is statically partitioned at link time; no
 *     `ESP.getHeapSize()` equivalent).
 *   - `getUniqueId()` formats `ESP.getChipId()` (32-bit) instead of
 * `ESP.getEfuseMac` (48-bit).
 *   - `feedWatchdog()` calls `ESP.wdtFeed()` instead of the ESP-IDF
 * task-watchdog reset API.
 *
 * `restart()` is declared `[[noreturn]]` to match the interface
 * ; the impl also includes an unreachable infinite loop
 * after `ESP.restart()` as a defense against toolchains that drop
 * the attribute.
 */

#include "../IZenoSystem.h"

// / lifted to.h surface (, replicates the fix
// applied in to Esp8266NVS.h): although `<Arduino.h>` resolves
// on both ESP32 and ESP8266, the ESP.* class shape diverges between the two
// cores (`ESP.getMaxFreeBlockSize` only exists on ESP8266; the
// ESP32 ESP class has `getMaxAllocHeap()`). PIO's library scanner indexes
// this header on every env; guarding at the header surface keeps ESP32 envs
// from materialising an Esp8266System type during library indexing. The
// IZenoSystem abstract interface include stays OUTSIDE the guard because it
// is the cross-platform contract type.
#if defined(ESP8266)

#include <Arduino.h>           // millis(), ESP.* on ESP8266 (no ESP-IDF headers on this platform).

namespace ZenoPCB {

class Esp8266System : public IZenoSystem {
public:
    Esp8266System() = default;
    ~Esp8266System() override = default;

    // Deleted copy semantics (hygiene ESP.* is a single global).
    Esp8266System(const Esp8266System&) = delete;
    Esp8266System& operator=(const Esp8266System&) = delete;

    [[noreturn]] void restart() override;
    uint32_t getFreeHeap() override;
    uint32_t getMaxAllocHeap() override;
    uint32_t getTotalHeap() override;
    size_t getUniqueId(char *out, size_t outSize) override;
    uint32_t uptimeMs() override;
    void feedWatchdog() override;
};

}  // namespace ZenoPCB

#endif  // defined(ESP8266)

#endif  // ZENOPCB_ESP8266_SYSTEM_H
