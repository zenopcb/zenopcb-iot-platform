#ifndef ZENOPCB_STM32_STORAGE_H
#define ZENOPCB_STM32_STORAGE_H

/**
 * @file Stm32Storage.h
 * @brief STM32 (F1 + F4) concrete impl of IZenoStorage  STUB
 *        (CAP_FS_FILES=0; no LittleFS on STM32duino default Arduino core).
 *
 * Mechanical mirror of `Esp8266Storage.{h,cpp}` see
 * "Stm32Storage" (STUB section). STM32duino's default
 * Arduino core does NOT bundle LittleFS; persistence on this platform
 * is provided by Stm32NVS (ZenoFlashStorage wrapping the last 2-8 KB of
 * internal Flash as a length-prefixed KV store). Adding LittleFS would
 * require an external SPI flash chip + a vendored mid-2024 STM32 LittleFS
 * port out of v1.0.0 scope per.
 *
 * Stm32Hal::capabilities() omits CAP_FS_FILES so capability-gated callers
 * never reach these stubs in practice. The stubs exist for two reasons:
 *  1. IZenoHal::storage() must return a valid reference even on stub
 *     platforms (interface contract).
 *  2. A misbehaving caller that ignores the capability bit gets a single
 *     warning log + a failure return, not a crash.
 *
 * deleted copy semantics for hygiene (no underlying state
 * but mirrors the rest of the Stm32 HAL).
 */

#include "../IZenoStorage.h"

// / TU guard at header surface. No STM32-specific
// system includes here because every method is a failure-return stub, but
// the guard keeps the class type from materialising on ESP32/ESP8266 envs
// during library scanning (symmetric to the rest of the Stm32*.h family).
#if defined(STM32F1xx) || defined(STM32F4xx)

namespace ZenoPCB {

class Stm32Storage : public IZenoStorage {
public:
    Stm32Storage() = default;
    ~Stm32Storage() override = default;

    // Deleted copy semantics (facade hygiene).
    Stm32Storage(const Stm32Storage&) = delete;
    Stm32Storage& operator=(const Stm32Storage&) = delete;

    bool begin() override;
    bool exists(const char *path) override;
    size_t readFile(const char *path, char *out, size_t maxLen) override;
    size_t writeFile(const char *path, const char *data, size_t len) override;
    bool deleteFile(const char *path) override;
    bool mkdir(const char *path) override;
    void listFiles(const char *prefix,
                   std::function<void(const char *)> callback) override;
};

}  // namespace ZenoPCB

#endif  // defined(STM32F1xx) || defined(STM32F4xx)

#endif  // ZENOPCB_STM32_STORAGE_H
