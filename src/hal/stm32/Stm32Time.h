#ifndef ZENOPCB_STM32_TIME_H
#define ZENOPCB_STM32_TIME_H

/**
 * @file Stm32Time.h
 * @brief STM32 (F1 + F4) concrete impl of IZenoTime — delegates to the
 *        existing TimeManager so all platforms share a single configTime
 *        owner.
 *
 * Mechanical mirror of `Esp8266Time.{h,cpp}` Pattern A — see Phase 7
 * 07-PATTERNS.md §"Stm32Time". STM32duino exposes `<time.h>` with the
 * same `configTime()` / `time(nullptr)` signature and semantics as ESP32
 * lwIP SNTP and ESP8266 lwIP SNTP. On F4 the transport is Ethernet (W5500
 * + ArduinoCore-STM32's Ethernet lib); on F1 the transport is the AT-mode
 * WiFiEspAT bridge through an ESP8266 co-processor. Both surface the
 * `configTime` global lwIP SNTP wiring identically; Plan 07-05 wires the
 * env-specific transport.
 *
 * Pattern D — deleted copy semantics for hygiene (global SNTP state).
 */

#include "../IZenoTime.h"

// Pattern B / Pitfall 7 — TU guard at header surface. `<time.h>` is
// portable, but the underlying `configTime()` wiring depends on a
// platform-specific lwIP / SNTP stack that only exists on the STM32
// Ethernet / WiFiEspAT envs (Plan 07-05). Guarding here keeps the
// Stm32Time type from materialising on ESP32/ESP8266 envs during
// library indexing — symmetric to the rest of the Stm32*.h family.
#if defined(STM32F1) || defined(STM32F4)

#include <time.h>

namespace ZenoPCB {

class Stm32Time : public IZenoTime {
public:
    Stm32Time() = default;
    ~Stm32Time() override = default;

    // Deleted copy semantics (Pattern D hygiene — global SNTP state).
    Stm32Time(const Stm32Time&) = delete;
    Stm32Time& operator=(const Stm32Time&) = delete;

    void syncNTP(const char *server,
                 long gmtOffsetSec,
                 int daylightOffsetSec) override;
    time_t now() override;
    bool isSynced() override;
};

}  // namespace ZenoPCB

#endif  // defined(STM32F1) || defined(STM32F4)

#endif  // ZENOPCB_STM32_TIME_H
