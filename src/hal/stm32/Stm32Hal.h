#ifndef ZENOPCB_STM32_HAL_H
#define ZENOPCB_STM32_HAL_H

/**
 * @file Stm32Hal.h
 * @brief STM32 (F1 + F4) concrete facade implementing IZenoHal.
 *
 * Unified F1+F4 class per. Mechanical mirror of
 * `Esp8266Hal.{h,cpp}` see "Stm32Hal".
 *
 * Composes the five Stm32* sub-impls by value and exposes them via the
 * IZenoHal sub-getters. Per-family divergence is concentrated inside
 * `capabilities` (F4 adds CAP_DIAGNOSTICS per;
 * F1 MICRO drops it pre-emptively) and inside Stm32NVS partition size
 * and Stm32System heap totals  all via `#if defined(STM32F4xx)` /
 * `#elif defined(STM32F1xx)` internal switches.
 *
 * deleted copy semantics. The contained Stm32System wraps the
 * NVIC + IWatchdog process-global singletons; the contained Stm32NVS wraps
 * the global ZenoFlashStorage RAM mirror. Cloning Stm32Hal would create
 * a second wrapper sharing state. Deleting copy + assign makes that a
 * compile error.
 *
 * The canonical instance is obtained via `getStm32Hal()`  a Meyers
 * singleton implemented in Stm32Hal.cpp (lazy function-local static,
 * thread-safe in C++11+, no eager file-scope global; same pattern as
 * `getEsp32Hal()` / `getEsp8266Hal()`).
 */

#include <stdint.h>

#include "../IZenoHal.h"

// / lifted to.h surface (mirrors fix for
// Esp8266 family): the contained Stm32*.h sub-impls include STM32-only
// headers (`<IWatchdog.h>`, the vendored ZenoFlashStorage from
// CMSIS HAL intrinsics, etc.). PIO's library scanner indexes every header
// under lib/ZenoPCB/src/ on every env, including ESP32 + ESP8266 envs, so
// we TU-guard the include of the sub-impls + the class body at the header
// surface so non-STM32 envs see an empty translation unit for this facade.
// The IZenoHal abstract interface include stays OUTSIDE the guard because
// it is the cross-platform contract type.
#if defined(STM32F1xx) || defined(STM32F4xx)

#include "Stm32Storage.h"
#include "Stm32NVS.h"
#include "Stm32OTA.h"
#include "Stm32Time.h"
#include "Stm32System.h"

namespace ZenoPCB {

class Stm32Hal : public IZenoHal {
public:
    Stm32Hal() = default;
    ~Stm32Hal() override = default;

    // Deleted copy semantics (contained Stm32System wraps
    // NVIC + IWatchdog process-global singletons; contained Stm32NVS
    // wraps the ZenoFlashStorage RAM mirror).
    Stm32Hal(const Stm32Hal&) = delete;
    Stm32Hal& operator=(const Stm32Hal&) = delete;

    IZenoStorage& storage() override { return _storage; }
    IZenoNVS&     nvs()     override { return _nvs; }
    IZenoOTA&     ota()     override { return _ota; }
    IZenoTime&    time()    override { return _time; }
    IZenoSystem&  system()  override { return _system; }

    uint32_t capabilities() const override {
        // STM32 baseline: NVS (ZenoFlashStorage), NTP (configTime + lwIP),
        // WATCHDOG (IWatchdog). No CAP_FS_FILES (no LittleFS on STM32duino
        // default), no CAP_OTA (custom bootloader required).
        // / : F4 adds CAP_DIAGNOSTICS (heap budget);
        // F1 MICRO drops it pre-emptively.
        uint32_t caps = CAP_NVS | CAP_NTP | CAP_WATCHDOG;
#if defined(STM32F4xx)
        caps |= CAP_DIAGNOSTICS;
#endif
        return caps;
    }

private:
    Stm32Storage _storage;
    Stm32NVS     _nvs;
    Stm32OTA     _ota;
    Stm32Time    _time;
    Stm32System  _system;
};

/**
 * Returns the canonical STM32 HAL instance.
 *
 * Meyers singleton: lazy function-local static, thread-safe in C++11+,
 * no eager file-scope global. wires this into the Zeno class
 * default ctor via a platform-guarded ZENOPCB_DEFAULT_HAL() macro.
 */
IZenoHal& getStm32Hal();

}  // namespace ZenoPCB

#endif  // defined(STM32F1xx) || defined(STM32F4xx)

#endif  // ZENOPCB_STM32_HAL_H
