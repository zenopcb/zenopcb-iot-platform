#ifndef ZENOPCB_UNOR4_OTA_H
#define ZENOPCB_UNOR4_OTA_H

/**
 * @file UnoR4OTA.h
 * @brief Arduino UNO R4 WiFi (Renesas RA4M1) concrete impl of IZenoOTA 
 * CUSTOM implementation authored from scratch per RESCOPED.
 *
 * mirror of Esp8266OTA.h only at the IZenoOTA
 * contract shape (7 public methods); the bodies diverge entirely.
 * See.planning/phases/07-uno-r4-stm32-ports-capability-matrix/
 * "UnoR4OTA" (lines 485-579).
 *
 * ** RESCOPED rationale (CONTEXT line 89):**
 *   - `Arduino_ESP32_OTA`  REJECTED (GPL-3.0 license + esp32-only HAL
 *     surface; license-incompatible with the MIT OSS lib that consumes
 *     this HAL).
 *   - `jandrassy/ArduinoOTA`  REJECTED (LGPL  not vendored under
 * /16 LGPL isolation rule).
 *   - **Decision:** author UnoR4OTA from scratch using `WiFiClient`
 *     (WiFiS3) for HTTP firmware download + Renesas RA4M1 FSP
 *     Flash API (`R_FLASH_LP_*` symbols exposed via ArduinoCore-renesas)
 * for the partition write. Heavy lift ~3-5 days; delivers
 *     compile-clean PLACEHOLDER bodies + clearly-marked TODO so opt-in
 *     users can complete the spike or wait for a follow-up plan.
 *
 * **Opt-in gate `-DZENOPCB_ENABLE_UNOR4_OTA`:** without this build flag
 * every method logs once on first call + returns failure. With the flag,
 * the bodies are PLACEHOLDER (per lines 546-572) until
 * UAT hardware validation fills the live Renesas FSP Flash
 * API sequence. This mirrors the pattern where Esp8266OTA
 * shipped with PENDING hardware soak.
 *
 * Deleted copy semantics per when the opt-in flag is on
 * UnoR4OTA wraps shared Renesas FSP Flash open-handle state; cloning
 * the wrapper would let two writers race over the same flash region.
 */

#include "../IZenoOTA.h"

// / lifted to.h surface (carry-forward)
// the opt-in branch includes RA4M1-only headers (`<WiFiS3.h>` for the
// WiFi co-processor TCP client and the Renesas FSP Flash umbrella include
// pulled through `<Arduino.h>`). PIO's library scanner indexes this header
// on every env, including ESP32 / ESP8266 envs that would otherwise fail
// the WiFiS3 / FSP includes. Guarding at the header surface keeps non-UnoR4
// envs from materialising the UnoR4OTA type during library indexing. The
// IZenoOTA abstract interface include stays OUTSIDE the guard because it
// is the cross-platform contract type.
#if defined(ARDUINO_UNOR4_WIFI)

#include <Arduino.h>           // size_t, uint8_t also drags Renesas FSP
                               // headers when -DZENOPCB_ENABLE_UNOR4_OTA
                               // is on (FSP Flash API umbrella).

namespace ZenoPCB {

class UnoR4OTA : public IZenoOTA {
public:
    UnoR4OTA() = default;
    ~UnoR4OTA() override = default;

    // Deleted copy semantics (shared Renesas FSP Flash
    // open-handle state when -DZENOPCB_ENABLE_UNOR4_OTA is on).
    UnoR4OTA(const UnoR4OTA&) = delete;
    UnoR4OTA& operator=(const UnoR4OTA&) = delete;

    bool begin(size_t expectedSize, const char *expectedMd5 = nullptr) override;
    size_t write(const uint8_t *data, size_t len) override;
    bool end() override;
    void abort() override;
    const char *errorString() override;
    bool canRollBack() override;
    bool rollBack() override;
};

}  // namespace ZenoPCB

#endif  // defined(ARDUINO_UNOR4_WIFI)

#endif  // ZENOPCB_UNOR4_OTA_H
