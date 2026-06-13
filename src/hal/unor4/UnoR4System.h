#ifndef ZENOPCB_UNOR4_SYSTEM_H
#define ZENOPCB_UNOR4_SYSTEM_H

/**
 * @file UnoR4System.h
 * @brief Arduino UNO R4 WiFi (Renesas RA4M1) concrete impl of IZenoSystem —
 *        wraps NVIC_SystemReset + newlib mallinfo + RA4M1 unique-ID + the
 *        ArduinoCore-renesas WatchdogTimer library.
 *
 * Mechanical Pattern A mirror of Esp8266System.{h,cpp} (Plan 06-01).
 * See .planning/phases/07-uno-r4-stm32-ports-capability-matrix/07-PATTERNS.md
 * §"UnoR4System" (lines 695-749).
 *
 * Method-body divergences from the ESP8266 analog (implemented in
 * UnoR4System.cpp):
 *   - `restart()` calls `NVIC_SystemReset()` (Cortex-M4 ARMv7-M intrinsic
 *     visible via `<Arduino.h>` on RA4M1) instead of `ESP.restart()`.
 *   - `getFreeHeap()` / `getMaxAllocHeap()` use `mallinfo()` from newlib
 *     (RA4M1 ships with the standard newlib-nano allocator); a
 *     PLACEHOLDER 0 is returned if `mallinfo` is unavailable.
 *   - `getTotalHeap()` returns hardcoded `32768` (RA4M1 DRAM total ~32 KB
 *     per RESEARCH §F1/F4 memory budget table — same pattern as
 *     Esp8266System Pitfall 4).
 *   - `getUniqueId()` formats the lower 32 bits of the Renesas FSP 128-bit
 *     unique ID. Wave 1 spike confirms exact API path; a deterministic
 *     PLACEHOLDER is used until then so the surface compiles.
 *   - `feedWatchdog()` calls `WatchdogTimer.refresh()` from the
 *     ArduinoCore-renesas WatchdogTimer library.
 *
 * `restart()` is declared `[[noreturn]]` to match the interface; the impl
 * also includes an unreachable infinite loop after `NVIC_SystemReset()`
 * as a defense against toolchains that drop the attribute on virtual
 * methods.
 */

#include "../IZenoSystem.h"

// Pattern B/Pitfall 7 lifted to .h surface (Plan 06-2.5d carry-forward):
// `<Arduino.h>` resolves on every Arduino core, but the underlying
// `NVIC_SystemReset` / `mallinfo` / `WatchdogTimer` symbols diverge between
// cores (ESP32 has no Cortex-M NVIC; ESP8266 lacks the standard newlib
// `mallinfo`). Guarding at the header surface keeps PIO's library scanner
// from materialising an UnoR4System type during indexing on non-UnoR4 envs.
// The IZenoSystem abstract interface include stays OUTSIDE the guard
// because it is the cross-platform contract type.
#if defined(ARDUINO_UNOR4_WIFI)

#include <Arduino.h>           // millis(), NVIC_SystemReset on RA4M1

namespace ZenoPCB {

class UnoR4System : public IZenoSystem {
public:
    UnoR4System() = default;
    ~UnoR4System() override = default;

    // Deleted copy semantics (Pitfall 3 hygiene — WatchdogTimer is a
    // process-global singleton on ArduinoCore-renesas).
    UnoR4System(const UnoR4System&) = delete;
    UnoR4System& operator=(const UnoR4System&) = delete;

    [[noreturn]] void restart() override;
    uint32_t getFreeHeap() override;
    uint32_t getMaxAllocHeap() override;
    uint32_t getTotalHeap() override;
    size_t getUniqueId(char *out, size_t outSize) override;
    uint32_t uptimeMs() override;
    void feedWatchdog() override;
};

}  // namespace ZenoPCB

#endif  // defined(ARDUINO_UNOR4_WIFI)

#endif  // ZENOPCB_UNOR4_SYSTEM_H
