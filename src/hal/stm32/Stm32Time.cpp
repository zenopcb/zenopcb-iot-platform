#include "Stm32Time.h"

#if defined(STM32F1) || defined(STM32F4)

#include "../../core/TimeManager.h"

namespace ZenoPCB {

void Stm32Time::syncNTP(const char *server,
                        long gmtOffsetSec,
                        int daylightOffsetSec) {
    // Delegate to TimeManager so a single owner controls the global lwIP
    // SNTP state (Pitfall 5 carry-forward from Plan 04-02 / 06-01).
    // TimeManager::syncNTP has additional secondary/tertiary server
    // defaults; pass through the primary and let the defaults supply
    // fallbacks. STM32duino exposes the same `<time.h>` + `configTime`
    // surface as ESP32 / ESP8266 (07-PATTERNS §"Stm32Time"); the lone
    // ESP32-only `esp_sntp.h` include in TimeManager.h is already guarded
    // under `#if defined(ESP32)` from Plan 06-03.
    TimeManager::syncNTP(server ? server : "pool.ntp.org",
                         "time.google.com",
                         "time.cloudflare.com",
                         gmtOffsetSec,
                         daylightOffsetSec);
}

time_t Stm32Time::now() {
    return time(nullptr);
}

bool Stm32Time::isSynced() {
    return TimeManager::isSynced();
}

}  // namespace ZenoPCB

#endif  // defined(STM32F1) || defined(STM32F4)
