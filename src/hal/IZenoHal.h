#ifndef ZENOPCB_IZENOHAL_H
#define ZENOPCB_IZENOHAL_H

/**
 * @file IZenoHal.h
 * @brief Facade aggregating the five ZenoPCB Hardware Abstraction Layer
 *        sub-interfaces: storage, NVS, OTA, time, system.
 *
 * Consumers receive an `IZenoHal&` and call sub-getters: e.g.
 *   hal.nvs().begin("zeno_creds"); hal.nvs().getString(...);
 *
 * Concrete impl on ESP32 = `Esp32Hal` (Plan 04-02). Phase 6 will add
 * `Esp8266Hal`; Phase 7 will add `UnoR4Hal` and `Stm32Hal`.
 *
 * `capabilities()` returns a bitmask of `Capability` values. ESP32
 * returns all 5; Phase 6/7 ports return fewer. Consumers should gate
 * optional features with `if (hal.capabilities() & CAP_X) { ... }`
 * even on ESP32 where the branch is always taken — forward-compat for
 * Phase 6/7 ports without a second refactor pass.
 *
 * Sub-getters return non-const references because consumers mutate
 * underlying state (e.g. nvs().putString writes NVS). `capabilities()`
 * is `const` because it is a pure introspection method.
 */

#include <stdint.h>

#include "IZenoStorage.h"
#include "IZenoNVS.h"
#include "IZenoOTA.h"
#include "IZenoTime.h"
#include "IZenoSystem.h"

namespace ZenoPCB {

/**
 * @brief Return type for fallible Zeno facade methods (Pattern G, Phase 7 D-06).
 *
 * Declared at namespace scope (alongside `IZenoHal::Capability` bitmask) so
 * callers can write `if (zeno.ota(url) != ZenoCapability::OK) { ... }` without
 * needing to qualify a nested type name.
 *
 * Replaces `bool` returns on the new `Zeno::ota(const char*)` and
 * `Zeno::wifiProvisioning(const char*, const char*)` overloads so callers can
 * distinguish four distinct outcomes:
 *   - `OK`           — operation succeeded (or was queued successfully).
 *   - `Unavailable`  — platform does not support this feature
 *                     (`hal.capabilities() & CAP_X == 0`); single warn log emitted.
 *   - `Error`        — operation attempted but failed at runtime
 *                     (network down, MD5 mismatch, write failure, etc.).
 *   - `Pending`      — operation in progress (e.g., OTA download streaming;
 *                     a second call while the first is still active).
 *
 * Pattern G differs from Phase 6 Pattern F (silent no-op + `return *this` on
 * `enableXxx()` builders, preserved per D-07): builders must keep `Zeno&` for
 * the fluent chain, whereas fallible runtime methods can change return type.
 */
enum class ZenoCapability : uint8_t {
    OK          = 0,   ///< Operation succeeded (or queued successfully).
    Unavailable = 1,   ///< Platform does not support this feature (capabilities() bit = 0).
    Error       = 2,   ///< Operation attempted but failed at runtime.
    Pending     = 3,   ///< Operation in progress (e.g., OTA download streaming).
};

class IZenoHal {
public:
    virtual ~IZenoHal() = default;

    /**
     * Capability bitmask. Bits set indicate the platform supports the
     * corresponding sub-interface meaningfully.
     */
    enum Capability : uint32_t {
        CAP_FS_FILES        = 1u << 0,  ///< Has filesystem (LittleFS or equivalent).
        CAP_OTA             = 1u << 1,  ///< Supports OTA firmware update.
        CAP_NVS             = 1u << 2,  ///< Has persistent namespaced KV store.
        CAP_NTP             = 1u << 3,  ///< Can sync wall-clock via NTP.
        CAP_WATCHDOG        = 1u << 4,  ///< Has task / hardware watchdog.
        CAP_CAPTIVE_PORTAL  = 1u << 5,  ///< NEW Phase 7 — AP-mode WiFi captive-portal supported (UNO R4/STM32 opt-out per D-09/D-10).
        CAP_TLS             = 1u << 6,  ///< NEW Phase 7 — runtime introspection of -DZENOPCB_ENABLE_TLS opt-in (D-13/D-27).
        CAP_DIAGNOSTICS     = 1u << 7,  ///< NEW Phase 7 — F1 MICRO profile drops Diagnostics per D-12.
    };

    // ---- Sub-interface accessors --------------------------------------

    virtual IZenoStorage& storage() = 0;
    virtual IZenoNVS&     nvs()     = 0;
    virtual IZenoOTA&     ota()     = 0;
    virtual IZenoTime&    time()    = 0;
    virtual IZenoSystem&  system()  = 0;

    // ---- Capabilities --------------------------------------------------

    /**
     * Bitmask of `Capability` values. ESP32 returns all 5; Phase 6/7
     * ports return fewer. Consumers should gate optional features with
     * `if (hal.capabilities() & CAP_X) { ... }` even on ESP32 where the
     * branch is always taken — forward-compat for Phase 6/7.
     */
    virtual uint32_t capabilities() const = 0;
};

}  // namespace ZenoPCB

#endif  // ZENOPCB_IZENOHAL_H
