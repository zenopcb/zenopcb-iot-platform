#ifndef ZENOPCB_STM32_OTA_H
#define ZENOPCB_STM32_OTA_H

/**
 * @file Stm32OTA.h
 * @brief STM32 (F1 + F4) concrete impl of IZenoOTA  failure-stub
 *        (CAP_OTA=0 default; no custom bootloader on default builds).
 *
 * Mechanical mirror of `Esp8266OTA.{h,cpp}` see
 * "Stm32OTA". STM32 OTA requires a custom bootloader
 * (dual-bank flash partition + IAP write + reset-to-bootloader handshake)
 * which is explicitly out of v1.0.0 scope per. Unlike
 * UnoR4OTA  which offers a `-DZENOPCB_ENABLE_UNOR4_OTA` opt-in flag 
 * Stm32OTA has NO opt-in flag: every method always returns the failure
 * stub. Future work (+ or downstream user fork) would author a
 * custom bootloader and conditionally enable real bodies behind a build
 * flag.
 *
 * Stm32Hal::capabilities() omits CAP_OTA so capability-gated callers
 * (post- in) receive `ZenoCapability::Unavailable`
 * before reaching these stubs in practice. The stubs exist for two
 * reasons:
 *  1. IZenoHal::ota() must return a valid reference even on stub
 *     platforms (interface contract).
 *  2. A misbehaving caller that ignores the capability bit gets a single
 *     warning log + a failure return, not a crash.
 *
 * deleted copy semantics for hygiene (no underlying state
 * but mirrors the rest of the Stm32 HAL).
 */

#include "../IZenoOTA.h"

// / TU guard at header surface. No STM32-specific
// system includes here because every method is a failure-return stub
// (no `<Updater.h>` analog on STM32duino default), but the guard keeps
// the class type from materialising on ESP32/ESP8266 envs during
// library scanning (symmetric to the rest of the Stm32*.h family).
#if defined(STM32F1xx) || defined(STM32F4xx)

namespace ZenoPCB {

class Stm32OTA : public IZenoOTA {
public:
    Stm32OTA() = default;
    ~Stm32OTA() override = default;

    // Deleted copy semantics (facade hygiene).
    Stm32OTA(const Stm32OTA&) = delete;
    Stm32OTA& operator=(const Stm32OTA&) = delete;

    bool begin(size_t expectedSize, const char *expectedMd5 = nullptr) override;
    size_t write(const uint8_t *data, size_t len) override;
    bool end() override;
    void abort() override;
    const char *errorString() override;
    bool canRollBack() override;
    bool rollBack() override;
};

}  // namespace ZenoPCB

#endif  // defined(STM32F1xx) || defined(STM32F4xx)

#endif  // ZENOPCB_STM32_OTA_H
