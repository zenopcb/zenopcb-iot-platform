#ifndef ZENOPCB_STM32_SYSTEM_H
#define ZENOPCB_STM32_SYSTEM_H

/**
 * @file Stm32System.h
 * @brief STM32 (F1 + F4) concrete impl of IZenoSystem — wraps NVIC,
 *        HAL_GetUIDw0, IWatchdog + per-family heap totals.
 *
 * Mechanical mirror of `Esp8266System.{h,cpp}` Pattern A — see Phase 7
 * 07-PATTERNS.md §"Stm32System". STM32duino does NOT expose an `ESP.*`
 * namespace; the six method bodies route through CMSIS intrinsics +
 * ArduinoCore-STM32 wrappers instead:
 *   - `restart()`        → CMSIS `NVIC_SystemReset()`
 *   - `getFreeHeap()`    → newlib-nano `mallinfo().fordblks` (when available)
 *   - `getMaxAllocHeap()`→ same route
 *   - `getTotalHeap()`   → per-family static const (F4 192 KB, F1 20 KB SRAM)
 *   - `getUniqueId()`    → `HAL_GetUIDw0()` (96-bit MCU unique ID, top 32 bits)
 *   - `uptimeMs()`       → `millis()` (Arduino core) / `HAL_GetTick()` in CubeIDE
 *   - `feedWatchdog()`   → `IWatchdog.reload()` (ArduinoCore-STM32 IWatchdog lib)
 *
 * Per Phase 7 D-25 (NEW user 2026-06-03 #3) — Arduino-minimal: the bodies
 * call into `arduino_compat::` thin wrappers for `millis()` so a downstream
 * CubeIDE port can swap them for `HAL_GetTick()` via the
 * `arduino_compat_*` extern stubs documented in PORTING_TO_CUBEIDE.md.
 * The header itself does NOT pull `<Arduino.h>`; the compat layer does
 * (guarded by `#if defined(ARDUINO)`).
 *
 * Pattern D — deleted copy semantics (NVIC + IWatchdog are global
 * singletons; duplicating the wrapper duplicates the reference).
 *
 * `restart()` is declared `[[noreturn]]` to match the interface; the impl
 * also includes an unreachable infinite loop after `NVIC_SystemReset()`
 * as a defense against toolchains that drop the attribute.
 */

#include "../IZenoSystem.h"

// Pattern B / Pitfall 7 — TU guard at header surface. STM32-specific
// CMSIS / HAL intrinsics + IWatchdog.h live in the .cpp behind the same
// guard. Guarding at the header surface keeps the Stm32System type from
// materialising on non-STM32 envs during library scanning.
#if defined(STM32F1) || defined(STM32F4)

namespace ZenoPCB {

class Stm32System : public IZenoSystem {
public:
    Stm32System() = default;
    ~Stm32System() override = default;

    // Deleted copy semantics (Pattern D — NVIC + IWatchdog are globals).
    Stm32System(const Stm32System&) = delete;
    Stm32System& operator=(const Stm32System&) = delete;

    [[noreturn]] void restart() override;
    uint32_t getFreeHeap() override;
    uint32_t getMaxAllocHeap() override;
    uint32_t getTotalHeap() override;
    size_t getUniqueId(char *out, size_t outSize) override;
    uint32_t uptimeMs() override;
    void feedWatchdog() override;
};

}  // namespace ZenoPCB

#endif  // defined(STM32F1) || defined(STM32F4)

#endif  // ZENOPCB_STM32_SYSTEM_H
