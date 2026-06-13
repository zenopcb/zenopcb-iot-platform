#include "UnoR4Time.h"

#if defined(ARDUINO_UNOR4_WIFI)

#include <WiFiS3.h>
#include <time.h>

namespace ZenoPCB {

// Cached epoch from the most recent successful WiFi.getTime() call. The
// RA4M1 ArduinoCore-renesas RTC API path is still spike-pending (07-PATTERNS
// line 671 — "Set system clock via Renesas FSP RTC ... planner specs exact
// API"). Until the spike confirms the precise RTC.setTime overload, we cache
// the last-known epoch + the millis() snapshot at sync time so `now()` can
// return a plausible wall-clock without relying on the system clock having
// been pushed forward. This is the same pattern Esp8266Time falls back to
// when the underlying SNTP stack is still warming up.
static volatile uint32_t s_lastEpoch = 0;
static volatile uint32_t s_lastSyncMillis = 0;

void UnoR4Time::syncNTP(const char *server,
                        long gmtOffsetSec,
                        int /*daylightOffsetSec*/) {
    // WiFiS3 has built-in NTP via WiFi.getTime() — returns Unix epoch
    // seconds when the WiFi co-processor's NTP client has succeeded, or
    // 0 while still warming up.
    //
    // Per 07-PATTERNS line 668, the `server` and `daylightOffsetSec`
    // params are accepted for IZenoTime contract compatibility but are
    // not forwarded to WiFiS3 — WiFiS3 picks its own NTP server and
    // does not expose DST configuration. `gmtOffsetSec` is folded into
    // the cached epoch so callers reading `now()` get a local-zone time.
    unsigned long epoch = WiFi.getTime();
    if (epoch > 100000UL) {
        s_lastEpoch = static_cast<uint32_t>(epoch) +
                      static_cast<uint32_t>(gmtOffsetSec);
        s_lastSyncMillis = millis();
        // TODO (Plan 07-09 UAT): once the Renesas FSP RTC API path is
        // confirmed on hardware, also push the epoch into the RTC so
        // `time(nullptr)` returns the correct value without the cache
        // fallback below.
    }
    (void)server;  // intentionally unused — WiFiS3 picks default NTP server
}

time_t UnoR4Time::now() {
    // Prefer the system clock if newlib `time()` has been seeded by the
    // RTC (spike-pending). Otherwise fall back to (cachedEpoch + elapsed
    // millis since sync) so monotonic forward motion is preserved between
    // syncNTP calls.
    time_t sys = time(nullptr);
    if (sys > 100000) return sys;
    if (s_lastEpoch == 0) return 0;
    uint32_t deltaSec = (millis() - s_lastSyncMillis) / 1000u;
    return static_cast<time_t>(s_lastEpoch + deltaSec);
}

bool UnoR4Time::isSynced() {
    // Either path is acceptable: a successful WiFi.getTime() now, or a
    // cached epoch from an earlier syncNTP call.
    return (s_lastEpoch > 100000UL) || (WiFi.getTime() > 100000UL);
}

}  // namespace ZenoPCB

#endif  // defined(ARDUINO_UNOR4_WIFI)
