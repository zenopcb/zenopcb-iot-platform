#ifndef ZENOPCB_ESP8266_TIME_H
#define ZENOPCB_ESP8266_TIME_H

/**
 * @file Esp8266Time.h
 * @brief ESP8266 concrete impl of IZenoTime  delegates to existing
 *        TimeManager so the two parallel time interfaces share state.
 *
 * Mechanical mirror of Esp32Time.{h,cpp} from.
 * See.planning/phases/06-esp8266-port/.
 *
 * Phasing TimeManager out is deferred to a later milestone per the
 * RESEARCH "Open Questions" #1 (avoid two
 * parallel configTime callers fighting over the global lwIP SNTP
 * state). ESP8266 SDK ships the same lwIP SNTP stack via Arduino Core
 * 3.x; `configTime()` and `time(nullptr)` have identical signatures
 * and semantics. adds a `#if defined(ESP32)` guard around
 * the `esp_sntp.h` include in TimeManager.h.
 *
 * Deleted copy semantics for hygiene.
 */

#include "../IZenoTime.h"

// / lifted to.h surface (, replicates the fix
// applied in to Esp8266NVS.h): `<time.h>` is portable, but the
// underlying `configTime()`/`time(nullptr)` semantics differ between ESP32
// and ESP8266 SNTP stacks (both cores expose the same lwIP SNTP
// surface but ESP8266 omits the `esp_sntp.h` helper header). Guarding at the
// header surface keeps the Esp8266Time type from materialising on ESP32 envs
// during library indexing symmetric to the rest of the Esp8266*.h family.
// The IZenoTime abstract interface include stays OUTSIDE the guard because
// it is the cross-platform contract type.
#if defined(ESP8266)

#include <time.h>

namespace ZenoPCB {

class Esp8266Time : public IZenoTime {
public:
    Esp8266Time() = default;
    ~Esp8266Time() override = default;

    // Deleted copy semantics (hygiene global SNTP state).
    Esp8266Time(const Esp8266Time&) = delete;
    Esp8266Time& operator=(const Esp8266Time&) = delete;

    void syncNTP(const char *server,
                 long gmtOffsetSec,
                 int daylightOffsetSec) override;
    time_t now() override;
    bool isSynced() override;
};

}  // namespace ZenoPCB

#endif  // defined(ESP8266)

#endif  // ZENOPCB_ESP8266_TIME_H
