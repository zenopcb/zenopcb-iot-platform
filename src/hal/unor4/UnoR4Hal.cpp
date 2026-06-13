#include "UnoR4Hal.h"

#if defined(ARDUINO_UNOR4_WIFI)

namespace ZenoPCB {

// Meyers singleton. Lazy, thread-safe in C++11+, and lives as a single
// symbol in this translation unit so the instance is shared across every
// consumer that includes UnoR4Hal.h. The static is FUNCTION-local, not
// file-scope, so no constructor runs before main() / setup() and no
// static-init-order fiasco can occur.
//
// Capabilities sanity check (compile-time): the baseline bitmask returned
// by UnoR4Hal::capabilities() must equal CAP_NVS | CAP_NTP | CAP_WATCHDOG
// (= 0x1C). CAP_OTA is conditionally OR'd in via the `#ifdef` inside the
// inline body; the static_assert below double-anchors the baseline to the
// call-site so a future edit cannot silently desync the two surfaces.
// CAP_FS_FILES is deliberately omitted per CONTEXT D-10 (no LittleFS on
// RA4M1).
static_assert(
    (IZenoHal::CAP_NVS | IZenoHal::CAP_NTP | IZenoHal::CAP_WATCHDOG) == 0x1Cu,
    "UNO R4 baseline capability bitmask must equal 0x1C");

IZenoHal& getUnoR4Hal() {
    static UnoR4Hal instance;
    return instance;
}

}  // namespace ZenoPCB

#endif  // defined(ARDUINO_UNOR4_WIFI)
