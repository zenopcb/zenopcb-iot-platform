#ifndef ZENOPCB_ESP32_TIME_H
#define ZENOPCB_ESP32_TIME_H

/**
 * @file Esp32Time.h
 * @brief ESP32 concrete impl of IZenoTime — delegates to existing
 *        TimeManager so the two parallel time interfaces share state.
 *
 * Phasing TimeManager out is deferred to Phase 5/6 per 04-RESEARCH.md
 * §"Open Questions" #1 (Pitfall 5 — avoid two parallel configTime callers
 * fighting over the global lwIP SNTP state).
 *
 * Deleted copy semantics for Pitfall 3 hygiene.
 */

#include "../IZenoTime.h"

// Plan 06-03 — TU-guard-at-header (symmetric to Plan 06-2.5d ESP8266
// mirror). `<time.h>` is portable but the Esp32Time impl delegates to
// the ESP32-specific TimeManager which pulls in `esp_sntp.h`; keep the
// whole class body behind the guard to mirror the pattern.
#if defined(ESP32)

#include <time.h>

namespace ZenoPCB {

class Esp32Time : public IZenoTime {
public:
    Esp32Time() = default;
    ~Esp32Time() override = default;

    // Deleted copy semantics (Pitfall 3 hygiene — global SNTP state).
    Esp32Time(const Esp32Time&) = delete;
    Esp32Time& operator=(const Esp32Time&) = delete;

    void syncNTP(const char *server,
                 long gmtOffsetSec,
                 int daylightOffsetSec) override;
    time_t now() override;
    bool isSynced() override;
};

}  // namespace ZenoPCB

#endif  // defined(ESP32)

#endif  // ZENOPCB_ESP32_TIME_H
