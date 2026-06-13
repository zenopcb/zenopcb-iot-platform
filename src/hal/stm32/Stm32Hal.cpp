#include "Stm32Hal.h"

#if defined(STM32F1) || defined(STM32F4)

namespace ZenoPCB {

// Meyers singleton. Lazy, thread-safe in C++11+, and lives as a single
// symbol in this translation unit so the instance is shared across every
// consumer that includes Stm32Hal.h. Crucially, the static is FUNCTION-
// local, not file-scope, so no constructor runs before main() / setup()
// and no static-init-order fiasco can occur (same Pitfall 7 pattern as
// getEsp32Hal() / getEsp8266Hal()).
//
// Capabilities sanity check (compile-time): the bitmask returned by
// Stm32Hal::capabilities() must equal CAP_NVS | CAP_NTP | CAP_WATCHDOG
// (= 0x1Cu) on F1 and additionally OR-in CAP_DIAGNOSTICS (= 0x80u) on
// F4 for a total of 0x9Cu. D-10 + D-12 OQ-1 RESOLVED — F103 MICRO drops
// CAP_DIAGNOSTICS pre-emptively because its 20 KB SRAM budget cannot
// support DiagnosticsCollector accumulator. The static_asserts below
// double-anchor the per-family expected hex to the call-site so a future
// edit cannot silently desync the two surfaces.
#if defined(STM32F4)
static_assert(
    (IZenoHal::CAP_NVS | IZenoHal::CAP_NTP | IZenoHal::CAP_WATCHDOG |
     IZenoHal::CAP_DIAGNOSTICS) == 0x9Cu,
    "STM32F4 capability bitmask must equal 0x9C");
#elif defined(STM32F1)
static_assert(
    (IZenoHal::CAP_NVS | IZenoHal::CAP_NTP | IZenoHal::CAP_WATCHDOG) == 0x1Cu,
    "STM32F1 MICRO capability bitmask must equal 0x1C");
#endif

IZenoHal& getStm32Hal() {
    static Stm32Hal instance;
    return instance;
}

}  // namespace ZenoPCB

#endif  // defined(STM32F1) || defined(STM32F4)
