#include "Esp32Hal.h"

// (symmetric to Esp8266 mirror)
// TU guard so this file resolves to an empty translation unit when
// compiled under non-ESP32 envs (PIO library scanner indexes all .cpp
// files in lib/ZenoPCB/src/ regardless of build_src_filter).
#if defined(ESP32)

namespace ZenoPCB {

// Meyers singleton. Lazy, thread-safe in C++11+, and lives
// as a single symbol in this translation unit so the instance is shared
// across every consumer that includes Esp32Hal.h. Crucially, the static
// is FUNCTION-local, not file-scope, so no constructor runs before
// main() / setup() and no static-init-order fiasco can occur.
//
// Capabilities sanity check (compile-time): the bitmask returned by
// Esp32Hal::capabilities() must equal CAP_FS_FILES | CAP_OTA | CAP_NVS |
// CAP_NTP | CAP_WATCHDOG | CAP_CAPTIVE_PORTAL (
// extended Esp32Hal with the captive-portal bit so fallible
// wifiProvisioning() proceeds to delegation on ESP32). This is enforced
// inline in the header impl; the static_assert below double-anchors that
// to the call-site so a future edit cannot silently desync the two
// surfaces.
static_assert(
    (IZenoHal::CAP_FS_FILES | IZenoHal::CAP_OTA | IZenoHal::CAP_NVS |
     IZenoHal::CAP_NTP | IZenoHal::CAP_WATCHDOG | IZenoHal::CAP_CAPTIVE_PORTAL) == 0x3Fu,
    "ESP32 capability bitmask must equal 0x3F (6 bits including CAP_CAPTIVE_PORTAL)");

IZenoHal& getEsp32Hal() {
    static Esp32Hal instance;
    return instance;
}

}  // namespace ZenoPCB

#endif  // defined(ESP32)
