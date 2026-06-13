#ifndef ZENOPCB_UNOR4_TIME_H
#define ZENOPCB_UNOR4_TIME_H

/**
 * @file UnoR4Time.h
 * @brief Arduino UNO R4 WiFi (Renesas RA4M1) concrete impl of IZenoTime —
 *        wraps WiFiS3's native `WiFi.getTime()` NTP helper.
 *
 * Mechanical Pattern A mirror of Esp8266Time.{h,cpp} (Plan 06-01) with the
 * body diverged entirely: WiFiS3 ships a self-contained NTP path that
 * returns Unix epoch seconds directly from the ESP32-S3 co-processor
 * (UNO R4's onboard companion — see 07-RESEARCH UNO R4 hardware note).
 * No `configTime()` / lwIP SNTP boilerplate.
 *
 * See .planning/phases/07-uno-r4-stm32-ports-capability-matrix/07-PATTERNS.md
 * §"UnoR4Time" (lines 624-680).
 *
 * Deleted copy semantics for Pitfall 3 hygiene — global SNTP-style state
 * lives inside the WiFiS3 co-processor RPC layer.
 */

#include "../IZenoTime.h"

// Pattern B/Pitfall 7 lifted to .h surface (Plan 06-2.5d carry-forward):
// `<WiFiS3.h>` is the WiFi co-processor library for UNO R4 only; it does
// not exist on ESP32 / ESP8266 cores. Guarding the include + class body
// at the header surface keeps PIO's library scanner from failing on the
// non-UnoR4 envs while indexing this header. The IZenoTime abstract
// interface include stays OUTSIDE the guard because it is the cross-
// platform contract type.
#if defined(ARDUINO_UNOR4_WIFI)

#include <WiFiS3.h>
#include <time.h>

namespace ZenoPCB {

class UnoR4Time : public IZenoTime {
public:
    UnoR4Time() = default;
    ~UnoR4Time() override = default;

    // Deleted copy semantics (Pitfall 3 — WiFiS3 RPC state is process-
    // global; duplicating the wrapper risks racing two `syncNTP` callers
    // against the same co-processor link).
    UnoR4Time(const UnoR4Time&) = delete;
    UnoR4Time& operator=(const UnoR4Time&) = delete;

    void syncNTP(const char *server,
                 long gmtOffsetSec,
                 int daylightOffsetSec) override;
    time_t now() override;
    bool isSynced() override;
};

}  // namespace ZenoPCB

#endif  // defined(ARDUINO_UNOR4_WIFI)

#endif  // ZENOPCB_UNOR4_TIME_H
