#include "Esp8266Time.h"

#if defined(ESP8266)

#include "../../core/TimeManager.h"

namespace ZenoPCB {

void Esp8266Time::syncNTP(const char *server,
                          long gmtOffsetSec,
                          int daylightOffsetSec) {
    // delegate to TimeManager so a single owner controls the
    // global lwIP SNTP state. TimeManager::syncNTP has additional
    // secondary/tertiary server defaults; pass through the primary and
    // let the defaults supply fallbacks.
    //
    // ESP8266 Arduino Core 3.x ships the same lwIP SNTP stack; the
    // TimeManager API surface is platform-neutral (`configTime()` +
    // `time(nullptr)`). The lone ESP32-specific include in TimeManager.h
    // (esp_sntp.h) is guarded under `#if defined(ESP32)` by
    // (Wave 2) file-author step does not need to touch
    // TimeManager.h.
    TimeManager::syncNTP(server ? server : "pool.ntp.org",
                         "time.google.com",
                         "time.cloudflare.com",
                         gmtOffsetSec,
                         daylightOffsetSec);
}

time_t Esp8266Time::now() {
    return time(nullptr);
}

bool Esp8266Time::isSynced() {
    return TimeManager::isSynced();
}

}  // namespace ZenoPCB

#endif  // defined(ESP8266)
