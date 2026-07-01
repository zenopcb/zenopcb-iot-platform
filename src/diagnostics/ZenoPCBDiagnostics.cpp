// TU guard for ZENOPCB_MICRO_BASIC profile.
#if !defined(ZENOPCB_DISABLE_DIAGNOSTICS)

#include "ZenoPCBDiagnostics.h"
#include "../mqtt/ZenoPCBMQTT.h"
#include "../core/ZenoPCBDebug.h"
// platform HAL bridge picks the canonical singleton for
// the current target. will swap this to ctor-injected
// IZenoHal& and remove the bridge entirely.
#if defined(ESP32)
  #include "../hal/esp32/Esp32Hal.h"
#elif defined(ESP8266)
  #include "../hal/esp8266/Esp8266Hal.h"
#endif

namespace ZenoPCB
{

    ZenoPCBDiagnostics::ZenoPCBDiagnostics(const DeviceInfo *deviceInfo, ZenoPCBMQTT *mqtt)
        : _deviceInfo(deviceInfo),
          _mqtt(mqtt),
          _collector(nullptr),
          _handler(nullptr),
          _lastSendTime(0),
          _firstSendDone(false),
          _mqttWasConnected(false)
    {
        // Create collector and handler. Pass the canonical platform HAL
        // singleton for now; swaps ZenoPCBDiagnostics to
        // receive IZenoHal& via its ctor so the bridge can be removed.
#if defined(ESP32)
        _collector = new DiagnosticsCollector(getEsp32Hal(), deviceInfo);
#elif defined(ESP8266)
        _collector = new DiagnosticsCollector(getEsp8266Hal(), deviceInfo);
#endif
        _handler = new DiagnosticsHandler(_collector, mqtt);
    }

    ZenoPCBDiagnostics::~ZenoPCBDiagnostics()
    {
        if (_handler != nullptr)
        {
            delete _handler;
            _handler = nullptr;
        }

        if (_collector != nullptr)
        {
            delete _collector;
            _collector = nullptr;
        }
    }

    bool ZenoPCBDiagnostics::begin(const DiagnosticsConfig &config)
    {
        _config = config;

        // Set connection type
        if (_collector != nullptr)
        {
            _collector->setConnectionType(_config.connectionType);
        }

        // Set max retries
        if (_handler != nullptr)
        {
            _handler->setMaxRetries(_config.maxRetries);
        }

        ZENO_LOG_CORE("[DIAG] Initialized with interval: %d ms, passive: %s",
                      _config.interval,
                      _config.passiveEnabled ? "enabled" : "disabled");

        return true;
    }

    void ZenoPCBDiagnostics::setNetworkProvider(ZenoNetworkProvider *provider)
    {
        if (_collector != nullptr)
        {
            _collector->setNetworkProvider(provider);
        }
    }

    void ZenoPCBDiagnostics::loop()
    {
        if (_mqtt == nullptr || _handler == nullptr)
        {
            // ZENO_LOG_CORE("[DIAG-LOOP] ERROR: mqtt or handler is null"); // Tt debug ny
            return;
        }

        bool mqttConnected = _mqtt->isConnected();

        // Debug: Print loop status periodically (commented out - too verbose)
        // static unsigned long lastDebugPrint = 0;
        // if (millis() - lastDebugPrint > 30000) // Every 30 seconds
        // {
        // lastDebugPrint = millis();
        // ZENO_LOG_CORE("[DIAG-LOOP] Running - MQTT: %s, LastSend: %lu ms ago",
        // mqttConnected ? "connected" : "disconnected",
        // millis() - _lastSendTime);
        // }

        // Auto-send on first connection (delayed to avoid flooding 4G modem)
        if (mqttConnected && !_mqttWasConnected)
        {
            // MQTT just connected schedule first send after 3s
            // On 4G, sending immediately after connect causes TCP socket overflow
            ZENO_LOG_CORE("[DIAG] MQTT connected - scheduling first diagnostics in 3s");
            _firstSendDone = false;
            _firstConnectTime = millis();
        }

        // Delayed first diagnostics send (3 seconds after connect)
        if (mqttConnected && !_firstSendDone && _firstConnectTime > 0)
        {
            if (millis() - _firstConnectTime >= 3000)
            {
                ZENO_LOG_CORE("[DIAG] Sending first diagnostics (3s after connect)");
                sendNow();
                _firstSendDone = true;
                _lastSendTime = millis();
                _firstConnectTime = 0;
            }
        }

        _mqttWasConnected = mqttConnected;

        // Passive updates
        if (_config.passiveEnabled && mqttConnected)
        {
            unsigned long now = millis();

            // Check if interval elapsed
            if (now - _lastSendTime >= _config.interval)
            {
                ZENO_LOG_CORE("[DIAG] Interval elapsed - sending passive update");
                _handler->sendPassive();
                _lastSendTime = now;
            }
        }
    }

    void ZenoPCBDiagnostics::setInterval(uint32_t intervalMs)
    {
        _config.interval = intervalMs;
        ZENO_LOG_CORE("[DIAG] Interval set to %d ms", intervalMs);
    }

    void ZenoPCBDiagnostics::setConnectionType(const String &type)
    {
        if (_collector != nullptr)
        {
            _collector->setConnectionType(type);
            ZENO_LOG_CORE("[DIAG] ConnectionType set to: %s", type.c_str());
        }
    }

    void ZenoPCBDiagnostics::setConnectionType(DiagnosticsConnectionType type)
    {
        if (_collector != nullptr)
        {
            _collector->setConnectionType(type);
            ZENO_LOG_CORE("[DIAG] ConnectionType set to: %s", connectionTypeToString(type).c_str());
        }
    }

    void ZenoPCBDiagnostics::enablePassive(bool enable)
    {
        _config.passiveEnabled = enable;
        ZENO_LOG_CORE("[DIAG] Passive updates %s", enable ? "enabled" : "disabled");
    }

    void ZenoPCBDiagnostics::setMaxRetries(uint8_t maxRetries)
    {
        _config.maxRetries = maxRetries;
        if (_handler != nullptr)
        {
            _handler->setMaxRetries(maxRetries);
        }
        ZENO_LOG_CORE("[DIAG] Max retries set to %d", maxRetries);
    }

    bool ZenoPCBDiagnostics::sendNow()
    {
        if (_handler != nullptr)
        {
            return _handler->sendPassive();
        }
        return false;
    }

    void ZenoPCBDiagnostics::onRequest(DiagnosticsRequestCallback callback)
    {
        if (_handler != nullptr)
        {
            _handler->setRequestCallback(callback);
        }
    }

    void ZenoPCBDiagnostics::onSent(DiagnosticsSentCallback callback)
    {
        if (_handler != nullptr)
        {
            _handler->setSentCallback(callback);
        }
    }

    void ZenoPCBDiagnostics::onError(DiagnosticsErrorCallback callback)
    {
        if (_handler != nullptr)
        {
            _handler->setErrorCallback(callback);
        }
    }

    void ZenoPCBDiagnostics::handleRequest(const String &payload)
    {
        if (_handler != nullptr)
        {
            _handler->handleRequest(payload);
        }
    }

} // namespace ZenoPCB

#endif // !ZENOPCB_DISABLE_DIAGNOSTICS
