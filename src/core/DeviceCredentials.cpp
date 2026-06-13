#include "DeviceCredentials.h"
#include "ZenoPCBDebug.h"
// Plan 06-03 Rule 3 (Wave 0 grep miss — auto-fix) — DeviceCredentials.cpp
// includes the platform HAL header directly so the default-ctor can wire
// in the canonical singleton. ESP32 keeps `getEsp32Hal()`; ESP8266 picks
// up `getEsp8266Hal()` via the parallel header. Plan 04-05 still wires
// explicit DI from Zeno, so the default ctor is the back-compat path.
// Phase 7 Plan 07-06.5 (Area F) — switch extended with UnoR4 + STM32
// arms so the IZenoHal& reference member is always initialised across
// all 4 platforms. Renesas + STM32 g++ enforce `-Werror=permissive` —
// the prior `#if defined(ESP32)` / `#elif defined(ESP8266)` arms left
// the reference uninitialised on UNO R4 / STM32 builds (Rule 1 bug
// caught by Plan 07-05 cross-compile gate).
#if defined(ESP32)
  #include "../hal/esp32/Esp32Hal.h"
#elif defined(ESP8266)
  #include "../hal/esp8266/Esp8266Hal.h"
#elif defined(ARDUINO_UNOR4_WIFI)
  #include "../hal/unor4/UnoR4Hal.h"
#elif defined(STM32F1) || defined(STM32F4)
  #include "../hal/stm32/Stm32Hal.h"
#endif

namespace ZenoPCB
{
    namespace
    {
        // Buffer sizes per 04-03-AUDIT.md §1.2 — 32–36-char credentials plus
        // generous padding to absorb future format changes.
        constexpr size_t CREDENTIAL_BUF_SIZE = 64;
    } // namespace

    DeviceCredentials::DeviceCredentials(IZenoHal &hal)
        : _hal(hal), _provisioned(false)
    {
    }

    // Backward-compat default ctor — uses the canonical platform HAL
    // singleton (Esp32Hal on ESP32, Esp8266Hal on ESP8266 per Plan 06-03).
    // Plan 04-05 may replace this with explicit injection from Zeno; until
    // then existing call sites (`DeviceCredentials credentials;`) continue
    // to compile without changes.
    DeviceCredentials::DeviceCredentials()
#if defined(ESP32)
        : _hal(getEsp32Hal()), _provisioned(false)
#elif defined(ESP8266)
        : _hal(getEsp8266Hal()), _provisioned(false)
#elif defined(ARDUINO_UNOR4_WIFI)
        : _hal(getUnoR4Hal()), _provisioned(false)
#elif defined(STM32F1) || defined(STM32F4)
        : _hal(getStm32Hal()), _provisioned(false)
#else
#error "DeviceCredentials default ctor: unsupported platform (no canonical HAL singleton). \
        ZenoPCB v0.3.0 supports ESP32, ESP8266, UNO R4 WiFi, STM32 F1/F4."
#endif
    {
    }

    DeviceCredentials::~DeviceCredentials()
    {
        // Defensive end() — safe to call even when no namespace is open
        // (per IZenoNVS contract).
        _hal.nvs().end();
    }

    bool DeviceCredentials::begin()
    {
        ZENO_LOG_CORE("DeviceCredentials: Initializing...");
        return _loadFromNVS();
    }

    bool DeviceCredentials::isProvisioned() const
    {
        return _provisioned;
    }

    bool DeviceCredentials::provision(const char *deviceId, const char *token)
    {
        if (deviceId == nullptr || token == nullptr)
        {
            ZENO_LOG_CORE("DeviceCredentials: Invalid null credentials");
            return false;
        }

        String devId = String(deviceId);
        String tok = String(token);

        if (!isValidCredential(devId))
        {
            ZENO_LOG_CORE("DeviceCredentials: Invalid deviceId length %d (must be %d-%d chars)",
                          devId.length(), CREDENTIAL_LENGTH_MIN, CREDENTIAL_LENGTH_MAX);
            return false;
        }

        if (!isValidCredential(tok))
        {
            ZENO_LOG_CORE("DeviceCredentials: Invalid token length %d (must be %d-%d chars)",
                          tok.length(), CREDENTIAL_LENGTH_MIN, CREDENTIAL_LENGTH_MAX);
            return false;
        }

        _deviceId = devId;
        _token = tok;
        _provisioned = true;

        if (_saveToNVS())
        {
            ZENO_LOG_CORE("DeviceCredentials: Provisioned successfully");
            ZENO_LOG_CORE("  DeviceId: %s", _deviceId.c_str());
            ZENO_LOG_CORE("  Token: %s...%s", _token.substring(0, 4).c_str(), _token.substring(28).c_str());
            return true;
        }

        return false;
    }

    void DeviceCredentials::clear()
    {
        if (!_hal.nvs().begin("zeno_creds" /* == NVS_NAMESPACE */, false))
        {
            ZENO_LOG_CORE("DeviceCredentials: Failed to open NVS for clearing");
            return;
        }

        _hal.nvs().clear();
        _hal.nvs().end();

        _deviceId = "";
        _token = "";
        _provisioned = false;

        ZENO_LOG_CORE("DeviceCredentials: Cleared all credentials");
    }

    bool DeviceCredentials::isValidCredential(const String &credential)
    {
        size_t len = credential.length();
        return len >= CREDENTIAL_LENGTH_MIN && len <= CREDENTIAL_LENGTH_MAX;
    }

    bool DeviceCredentials::_loadFromNVS()
    {
        if (!_hal.nvs().begin("zeno_creds" /* == NVS_NAMESPACE */, true)) // Read-only
        {
            ZENO_LOG_CORE("DeviceCredentials: NVS namespace not found, device not provisioned");
            _provisioned = false;
            return false;
        }

        char devIdBuf[CREDENTIAL_BUF_SIZE];
        char tokenBuf[CREDENTIAL_BUF_SIZE];
        _hal.nvs().getString("deviceId", devIdBuf, sizeof(devIdBuf), "");
        _hal.nvs().getString("token", tokenBuf, sizeof(tokenBuf), "");
        _hal.nvs().end();

        _deviceId = String(devIdBuf);
        _token = String(tokenBuf);

        if (isValidCredential(_deviceId) && isValidCredential(_token))
        {
            _provisioned = true;
            ZENO_LOG_CORE("DeviceCredentials: Loaded from NVS");
            ZENO_LOG_CORE("  DeviceId: %s", _deviceId.c_str());
            ZENO_LOG_CORE("  Token: %s...%s", _token.substring(0, 4).c_str(), _token.substring(28).c_str());
            return true;
        }

        ZENO_LOG_CORE("DeviceCredentials: No valid credentials in NVS");
        _provisioned = false;
        return false;
    }

    bool DeviceCredentials::_saveToNVS()
    {
        if (!_hal.nvs().begin("zeno_creds" /* == NVS_NAMESPACE */, false)) // Read-write
        {
            ZENO_LOG_CORE("DeviceCredentials: Failed to open NVS for writing");
            return false;
        }

        bool success = true;
        success &= _hal.nvs().putString("deviceId", _deviceId.c_str());
        success &= _hal.nvs().putString("token", _token.c_str());
        _hal.nvs().end();

        if (!success)
        {
            ZENO_LOG_CORE("DeviceCredentials: Failed to save to NVS");
        }

        return success;
    }

} // namespace ZenoPCB
