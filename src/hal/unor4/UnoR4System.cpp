#include "UnoR4System.h"

#if defined(ARDUINO_UNOR4_WIFI)

#include <Arduino.h>
#include <stdio.h>
#include <string.h>

// newlib mallinfo for heap stats. Available on ArduinoCore-renesas
// because RA4M1 ships with the standard newlib-nano allocator. If a
// future core trim drops the symbol, the `#if defined(__has_include)`
// guard below will fall back to the PLACEHOLDER path.
#if defined(__has_include)
#  if __has_include(<malloc.h>)
#    include <malloc.h>
#    define ZENOPCB_HAS_MALLINFO 1
#  endif
#endif

namespace ZenoPCB {

void UnoR4System::restart() {
    // NVIC_SystemReset is the canonical ARMv7-M / ARMv8-M system reset
    // intrinsic. On RA4M1 it triggers the AIRCR SYSRESETREQ which the
    // Renesas reset controller honours within microseconds.
    NVIC_SystemReset();
    // Defense for toolchains that drop [[noreturn]] on virtual methods.
    // The loop is unreachable in practice included so the compiler
    // proves the function body cannot fall through.
    for (;;) {
        // Spin until the chip resets.
    }
}

uint32_t UnoR4System::getFreeHeap() {
#if defined(ZENOPCB_HAS_MALLINFO)
    // `fordblks` = total bytes in free blocks (newlib-nano semantics).
    // Matches "free heap" for the IZenoSystem contract closely enough
    // for the DiagnosticsCollector formula.
    struct mallinfo info = mallinfo();
    return static_cast<uint32_t>(info.fordblks);
#else
    // PLACEHOLDER surface 0 so consumers see a "no heap stats
    // available" signal rather than a stale-but-plausible number.
    // UAT hardware spike replaces this if mallinfo is
    // unavailable on the shipped Renesas core.
    return 0;
#endif
}

uint32_t UnoR4System::getMaxAllocHeap() {
#if defined(ZENOPCB_HAS_MALLINFO)
    // newlib-nano does not expose largest-free-block directly; use the
    // free-blocks total as a conservative upper bound. UAT
    // hardware spike refines this if the allocator exposes a richer API.
    struct mallinfo info = mallinfo();
    return static_cast<uint32_t>(info.fordblks);
#else
    return 0;
#endif
}

uint32_t UnoR4System::getTotalHeap() {
    // RA4M1 DRAM total is 32 KB (hardcoded, RESEARCH UNO R4 spike).
    // Same pattern as Esp8266System::getTotalHeap : there is
    // no `ESP.getHeapSize()` analog and the link-time partition is
    // platform-fixed. The DiagnosticsCollector formula
    // `used = getTotalHeap() - getFreeHeap()` is therefore an
    // approximation skewed by the WiFiS3 co-processor RPC reservation;
    // acceptable for telemetry purposes.
    return 32768u;
}

size_t UnoR4System::getUniqueId(char *out, size_t outSize) {
    if (!out || outSize < 9) return 0;

    // RA4M1 has a 128-bit unique ID accessible via the Renesas FSP
    // `R_FACI_LP_*` API (`R_BSP_UniqueIdGet()` is the umbrella accessor
    // in some FSP versions). Wave 1 spike (planner UAT) will
    // confirm the exact API path. Until then, this is a deterministic
    // PLACEHOLDER so the surface compiles and existing
    // WiFiProvisioning + DeviceCredentials call sites expecting an
    // 8-hex-char unique identifier get a stable string.
    //
    // snprintf only (CLAUDE.md memory rule no sprintf). Width matches
    // the ESP32 / ESP8266 8-hex-char output surface.
    const uint32_t placeholderId = 0xDEADBEEFu;
    int written = snprintf(out, outSize, "%08X", placeholderId);
    if (written < 0) {
        out[0] = '\0';
        return 0;
    }
    if (static_cast<size_t>(written) >= outSize) {
        return outSize - 1;
    }
    return static_cast<size_t>(written);
}

uint32_t UnoR4System::uptimeMs() {
    return millis();
}

void UnoR4System::feedWatchdog() {
    // ArduinoCore-renesas exposes a `WatchdogTimer` global (header
    // `<WDT.h>` / equivalent) for IWDT (Independent Watchdog Timer)
    // control on RA4M1. The `.refresh()` call kicks the IWDT counter
    // once the application has previously enabled it via
    // `WatchdogTimer.begin(...)`.
    //
    // Wave 1 spike confirms exact symbol name + header path
    // (Standard Stack row 143 logs `WatchdogTimer` as the
    // observed symbol on the shipped ArduinoCore-renesas). Until the
    // spike confirms the include path, we leave this body as a
    // no-op-compatible placeholder so the surface compiles and the
    // CAP_WATCHDOG capability bit honours its "call is safe but may
    // be a no-op while spike is pending" contract.
    //
    // TODO (UAT): once `<WDT.h>` (or the actual header) is
    // confirmed on hardware, replace this no-op with the live
    // `WatchdogTimer.refresh()` call.
}

}  // namespace ZenoPCB

#endif  // defined(ARDUINO_UNOR4_WIFI)
