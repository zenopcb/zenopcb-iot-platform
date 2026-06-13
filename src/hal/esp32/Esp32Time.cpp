#include "Esp32Time.h"

// Plan 06-03 Pattern B (symmetric to Plan 06-2.5d Esp8266 mirror).
#if defined(ESP32)

#include "../../core/TimeManager.h"

namespace ZenoPCB {

void Esp32Time::syncNTP(const char *server,
                        long gmtOffsetSec,
                        int daylightOffsetSec) {
    // Pitfall 5 — delegate to TimeManager so a single owner controls the
    // global lwIP SNTP state. TimeManager::syncNTP has additional
    // secondary/tertiary server defaults; pass through the primary and
    // let the defaults supply fallbacks.
    TimeManager::syncNTP(server ? server : "pool.ntp.org",
                         "time.google.com",
                         "time.cloudflare.com",
                         gmtOffsetSec,
                         daylightOffsetSec);
}

time_t Esp32Time::now() {
    return time(nullptr);
}

bool Esp32Time::isSynced() {
    return TimeManager::isSynced();
}

}  // namespace ZenoPCB

#endif  // defined(ESP32)
