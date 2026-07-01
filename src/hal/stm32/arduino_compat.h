#ifndef ZENOPCB_STM32_ARDUINO_COMPAT_H
#define ZENOPCB_STM32_ARDUINO_COMPAT_H

/**
 * @file arduino_compat.h
 * @brief Arduino-minimal abstraction layer for the STM32 HAL (NEW
 * user direction 2026-06-03 #3).
 *
 * Concentrates Arduino-specific calls (`millis()`, `Serial.print()`,
 * `delay()`) so users who want to port `lib/ZenoPCB/src/hal/stm32/` into
 * an STM32CubeIDE / HAL / LL project (without ArduinoCore-STM32) can
 * provide a single `arduino_compat.c` stub with three `extern "C"`
 * functions and rebuild - see PORTING_TO_CUBEIDE.md for the recipe.
 *
 * When compiled under STM32duino + PlatformIO (default)
 * `ARDUINO` is defined and the `compat::` wrappers resolve inline to the
 * standard Arduino API. When compiled under raw CubeIDE without
 * ArduinoCore, `ARDUINO` is undefined and the wrappers route through
 * three `extern "C"` declarations the user implements in
 * `arduino_compat.c` using `HAL_GetTick()` / `printf()` / `HAL_Delay()`
 * (or whatever transport their project uses).
 *
 * invariant: the STM32duino + PlatformIO build is the
 * primary deliverable; the CubeIDE port is a DOWNSTREAM user recipe,
 * not a build target. The compat layer simply keeps that future
 * port cost low (a single C stub file instead of a global find-and-
 * replace across `lib/ZenoPCB/src/hal/stm32/`).
 *
 * Design constraints
 * - No business logic in this header - only thin inline wrappers.
 * - All call sites in `lib/ZenoPCB/src/hal/stm32/*.cpp` MUST route
 * through `ZenoPCB::stm32::compat::` instead of bare Arduino API.
 * Enforced by the Task 3 verify gate
 * grep -cE "Serial\.|millis\(\)|delay\(" \
 * lib/ZenoPCB/src/hal/stm32/*.cpp returns 0
 * - The bare `<Arduino.h>` include is permitted in this header (and
 * only this header) so the compat wrappers can resolve symbols
 * without dragging the include into every HAL .cpp body.
 */

#include <stdint.h>
#include <stddef.h>

#if defined(ARDUINO)
#include <Arduino.h>
#endif

namespace ZenoPCB {
namespace stm32 {
namespace compat {

/**
 * Milliseconds since boot. STM32duino exposes `millis()` which wraps
 * `HAL_GetTick()`. The CubeIDE port stub returns `HAL_GetTick()`
 * directly.
 */
inline uint32_t now_ms() {
#if defined(ARDUINO)
    return ::millis();
#else
    extern "C" uint32_t arduino_compat_millis(void); // user-provided stub
    return arduino_compat_millis();
#endif
}

/**
 * Emit a debug log line. STM32duino routes to `Serial.println()`
 * (UART2 by default). The CubeIDE port stub may route to `printf()`
 * over SWO / ITM / UART. Always NUL-terminated; no formatting (callers
 * use `snprintf` first per CLAUDE.md memory rule).
 */
inline void log(const char *msg) {
    if (!msg) return;
#if defined(ARDUINO)
    ::Serial.println(msg);
#else
    extern "C" void arduino_compat_log(const char *msg); // user-provided stub
    arduino_compat_log(msg);
#endif
}

/**
 * Blocking delay (milliseconds). STM32duino exposes `delay()` which
 * wraps `HAL_Delay()`. The CubeIDE port stub calls `HAL_Delay()` or
 * `vTaskDelay()` depending on whether FreeRTOS is configured.
 *
 * Note: per CLAUDE.md, library code should NOT use blocking delays in
 * hot paths. This wrapper exists to support short timing waits in
 * `Stm32System` (e.g., the watchdog reload guard) where a microsecond-
 * scale wait simplifies the state machine. Do NOT use `delay_ms()` in
 * loop()-driven code.
 */
inline void delay_ms(uint32_t ms) {
#if defined(ARDUINO)
    ::delay(ms);
#else
    extern "C" void arduino_compat_delay_ms(uint32_t ms); // user-provided stub
    arduino_compat_delay_ms(ms);
#endif
}

} // namespace compat
} // namespace stm32
} // namespace ZenoPCB

#endif // ZENOPCB_STM32_ARDUINO_COMPAT_H
