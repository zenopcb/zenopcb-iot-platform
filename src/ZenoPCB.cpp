#include "ZenoPCB.h"
#include "core/ZenoPCBDebug.h"
#include "core/ZenoPCBCloud.h"
// Plan 06-03 D-03 — Modbus subsystem is ESP32-only. The inline helpers
// (initializeModbusSystem, loadSavedModbusConfigs, loopModbusSystem) and
// MQTTControlHandler stay out of ESP8266 translation units; every call
// site below is wrapped in a parallel `#if defined(ESP32) ... #endif`.
#if defined(ESP32)
  #include "modbus/ModbusIntegration.h"
  #include "modbus/MQTTControlHandler.h"
#endif
// Storage consumers that use static-pointer HAL injection (Plan 04-05 wiring).
#include "schedule/ScheduleStorage.h"
#include "storage/LittleFSManager.h"
#include "ZenoJson.h"  // ArduinoJson API from vendored copy (namespace ZenoJson; see ZenoJson/LICENSE.md)

namespace ZenoPCB
{

    // ============================================
    // OTA result NVS persistence (T-4-02 byte-compat: namespace="ota_result",
    // key="payload"). Buffer size justified in 04-05-AUDIT.md §3.
    // ============================================
    static const size_t OTA_PAYLOAD_BUF_SIZE = 512;

    // ============================================
    // Claim-ack callback state (file-scope so PubSubClient::setCallback
    // can be wired with a non-capturing lambda → plain function pointer).
    // Capturing lambdas don't convert on strict toolchains (Renesas + STM32
    // g++); ESP32 / ESP8266 cores happen to allow the conversion. The
    // claim flow is sequential single-threaded — at most one claim cycle
    // is active at a time, so a single static slot is sufficient.
    // ============================================
    static volatile bool s_claimAckReceived = false;
    static volatile bool s_claimAckSuccess = false;

    // ============================================
    // Constructor / Destructor
    // ============================================

    Zeno::Zeno(IZenoHal &hal)
        : _hal(hal),
          _wifiProvisioning(nullptr),
          _mqtt(nullptr),
          _diagnostics(nullptr),
          _scheduleExecutor(nullptr),
          _alarmEngine(nullptr),
          _ota(nullptr),
#ifdef ZENOPCB_ENABLE_TLS
          _wifiClientSecure(nullptr),
#endif
          _networkProvider(nullptr),
          _registeredNetworkProvider(nullptr),
          _mqttPort(1883),
          _mqttEnabled(false),
          _mqttTLS(false),
          _tlsEnabled(false),
          _storageEnabled(false),
          _dataMonitorStorageEnabled(false),
          _scheduleEnabled(false),
          _irrigationEnabled(false),
          _diagnosticsEnabled(true),
          _diagnosticsIntervalMs(600000),
          _zKeysEnabled(false),
          _alarmEnabled(false),
          _otaEnabled(false),
          _modbusTelemetryInterval(5000),
          _pendingOTAStart(false),
          _lastFailedOTATime(0),
          _otaStartTimeMs(0),
          _state(ZenoState::IDLE),
          _provisioningEnabled(false),
          _wifiConfigured(false),
          _ntpSyncPending(false),
          _ntpSyncStartMs(0)
    {
    }

    Zeno::~Zeno()
    {
#if !defined(ZENOPCB_DISABLE_PROVISIONING)
        if (_wifiProvisioning)
        {
            delete _wifiProvisioning;
            _wifiProvisioning = nullptr;
        }
#endif
        if (_mqtt)
        {
            delete _mqtt;
            _mqtt = nullptr;
        }
#if !defined(ZENOPCB_DISABLE_DIAGNOSTICS)
        if (_diagnostics)
        {
            delete _diagnostics;
            _diagnostics = nullptr;
        }
#endif
#if !defined(ZENOPCB_DISABLE_SCHEDULE)
        if (_scheduleExecutor)
        {
            delete _scheduleExecutor;
            _scheduleExecutor = nullptr;
        }
#endif
#if !defined(ZENOPCB_DISABLE_ALARM)
        if (_alarmEngine)
        {
            delete _alarmEngine;
            _alarmEngine = nullptr;
        }
#endif
#if !defined(ZENOPCB_DISABLE_OTA)
        if (_ota)
        {
            delete _ota;
            _ota = nullptr;
        }
#endif
#ifdef ZENOPCB_ENABLE_TLS
        if (_wifiClientSecure)
        {
            delete _wifiClientSecure;
            _wifiClientSecure = nullptr;
        }
#endif
        // Note: _networkProvider is NOT owned by Zeno (caller owns it)
    }

    // ============================================
    // Fluent Configuration - WiFi
    // ============================================

    Zeno &Zeno::wifi(const char *ssid, const char *password)
    {
        _wifiSSID = ssid;
        _wifiPassword = password;
        _wifiConfigured = true;
        return *this;
    }

    Zeno &Zeno::wifiProvisioning(uint8_t buttonPin, uint32_t holdTimeMs)
    {
        _provisioningEnabled = true;
        _provConfig.buttonPin = buttonPin;
        _provConfig.buttonHoldTimeMs = holdTimeMs;
        return *this;
    }

    // Pattern G (Phase 7 D-06) — fallible captive-portal start. Distinct overload
    // from the builder above; ESP32 + ESP8266 advertise CAP_CAPTIVE_PORTAL so they
    // proceed to delegation, STM32 / UNO R4 (Plan 07-02 / 07-04) leave the bit unset
    // and hit the Unavailable arm with a single warn log.
    ZenoCapability Zeno::wifiProvisioning(const char *apSsid, const char *apPassword)
    {
        if (!(_hal.capabilities() & IZenoHal::CAP_CAPTIVE_PORTAL))
        {
            ZENO_LOG_CORE("[WARN] WiFi provisioning not available on this platform — capabilities() & CAP_CAPTIVE_PORTAL == 0");
            return ZenoCapability::Unavailable;
        }

        // Record the requested AP SSID + password into the existing provisioning
        // config so the WiFiProvisioning lifecycle (allocated in begin()) starts
        // with the user-supplied credentials. Plan 07-06 refines the per-platform
        // delegation surface; this body needs only to compile-link on ESP32 today.
        _provisioningEnabled = true;
        if (apSsid != nullptr)
        {
            _provConfig.apSSIDPrefix = apSsid;
        }
        if (apPassword != nullptr)
        {
            _provConfig.apPassword = apPassword;
        }

        // If begin() has already allocated the WiFiProvisioning instance, request
        // an immediate AP-mode start; otherwise the config above is consumed when
        // begin() runs. Either way we return OK — the call succeeded in the sense
        // that captive-portal mode has been requested on a supported platform.
#if !defined(ZENOPCB_DISABLE_PROVISIONING)
        if (_wifiProvisioning != nullptr)
        {
            _wifiProvisioning->startAPMode();
        }
#endif
        return ZenoCapability::OK;
    }

    Zeno &Zeno::apPrefix(const char *prefix)
    {
        _provConfig.apSSIDPrefix = prefix;
        return *this;
    }

    Zeno &Zeno::apPassword(const char *password)
    {
        _provConfig.apPassword = password;
        return *this;
    }

    Zeno &Zeno::apTimeout(uint32_t timeoutMs)
    {
        _provConfig.apTimeoutMs = timeoutMs;
        return *this;
    }

    Zeno &Zeno::statusLED(int8_t pin)
    {
        _provConfig.ledPin = pin;
        return *this;
    }

    // ============================================
    // Fluent Configuration - Network Provider
    // ============================================

    Zeno &Zeno::setNetworkProvider(ZenoNetworkProvider *provider)
    {
        _networkProvider = provider;
        _registeredNetworkProvider = provider; // Preserve original for mode switching
        if (provider)
        {
            ZENO_LOG_CORE("Network provider set: %s", provider->getName());
        }
        return *this;
    }

    Zeno &Zeno::device(const char *id)
    {
        _deviceId = id;
        return *this;
    }

    Zeno &Zeno::device(const char *id, const char *token)
    {
        // Mirror setDeviceCredentials() but as a fluent builder step. Both
        // arguments are nullable-friendly: if either is null we leave the
        // existing value untouched so callers can opt-in incrementally.
        //
        // Bug fix 2026-06-06 (live UAT on ex_io_00_hello_zsignals): the
        // earlier implementation only assigned _deviceId / _deviceToken and
        // skipped the auto-MQTT-config block in setDeviceCredentials(),
        // so the example sketch's `.device(DEVICE_ID, DEVICE_TOKEN).begin()`
        // chain left _mqttClientId, _mqttUsername, _mqttPassword empty
        // → broker rejected with BAD_CREDENTIALS. Delegate to
        // setDeviceCredentials() once we have both fields so the fluent
        // chain configures MQTT clientId / username / password the same
        // way the non-fluent setter does.
        if (id != nullptr && token != nullptr)
        {
            setDeviceCredentials(String(id), String(token));
            return *this;
        }
        if (id != nullptr)
        {
            _deviceId = id;
        }
        if (token != nullptr)
        {
            _deviceToken = token;
        }
        return *this;
    }

    Zeno &Zeno::deviceName(const char *name)
    {
        _deviceName = name;
        return *this;
    }

    Zeno &Zeno::deviceInfo(DeviceType type, const char *typeName, const char *modelName,
                           const char *version, const char *manufacturer, ConnectionFlags supportedConn)
    {
        _deviceInfo = DeviceInfo(type, typeName, modelName, version, manufacturer, supportedConn);
        return *this;
    }

    // ============================================
    // Fluent Configuration - MQTT
    // ============================================

    Zeno &Zeno::mqtt(const char *broker, uint16_t port)
    {
        _mqttBroker = broker;
        _mqttPort = port;
        _mqttEnabled = true;
        _mqttTLS = false;
        return *this;
    }

    Zeno &Zeno::mqttTLS(const char *broker, uint16_t port)
    {
        _mqttBroker = broker;
        _mqttPort = port;
        _mqttEnabled = true;
        _mqttTLS = true;
        return *this;
    }

    Zeno &Zeno::enableTLS()
    {
#ifdef ZENOPCB_ENABLE_TLS
        if (!_wifiClientSecure)
        {
            _wifiClientSecure = new WiFiClientSecure();
            _tlsEnabled = true;
            ZENO_LOG_CORE("TLS enabled (+150KB Flash)");
        }
#else
        ZENO_LOG_CORE("ERROR: TLS not available. Add -DZENOPCB_ENABLE_TLS to build_flags");
#endif
        return *this;
    }

    // Phase 7 D-27 — pin a PEM-encoded root CA for MQTT TLS. Pattern F silent
    // no-op when ZENOPCB_ENABLE_TLS is not defined; otherwise the stored pointer
    // is consumed inside _initMQTT() to call WiFiClientSecure::setCACert
    // (ESP32). On ESP8266 BearSSL the call site logs a [WARN] and falls back
    // to setInsecure — full BearSSL X509List integration deferred (D-27 note).
    Zeno &Zeno::setRootCA(const char *pemBuffer)
    {
#ifdef ZENOPCB_ENABLE_TLS
        _rootCA = pemBuffer;
        if (pemBuffer != nullptr)
        {
            ZENO_LOG_CORE("Root CA pinned for TLS (caller owns lifetime)");
        }
#else
        (void)pemBuffer;  // Pattern F: silent no-op when TLS not opt-in
#endif
        return *this;
    }

    Zeno &Zeno::enableStorage()
    {
        _storageEnabled = true;
        ZENO_LOG_CORE("Storage module enabled");
        return *this;
    }

    Zeno &Zeno::onConfigCreated(std::function<void(const ConnectionConfig &)> callback)
    {
        _configCreatedCallback = callback;
        return *this;
    }

    Zeno &Zeno::onConfigUpdated(std::function<void(const ConnectionConfig &)> callback)
    {
        _configUpdatedCallback = callback;
        return *this;
    }

    Zeno &Zeno::onConfigDeleted(std::function<void(const String &)> callback)
    {
        _configDeletedCallback = callback;
        return *this;
    }

    bool Zeno::getConnectionConfig(const String &shortId, ConnectionConfig &outConfig)
    {
#if defined(ZENOPCB_DISABLE_STORAGE)
        (void)shortId; (void)outConfig;
        return false;
#else
        if (!_storageEnabled)
        {
            return false;
        }
        return ConfigMessageHandler::getInstance().getConfig(shortId, outConfig);
#endif
    }

    std::vector<String> Zeno::getAllConnectionConfigIds()
    {
#if defined(ZENOPCB_DISABLE_STORAGE)
        return std::vector<String>();
#else
        if (!_storageEnabled)
        {
            return std::vector<String>();
        }
        return ConfigMessageHandler::getInstance().getAllConfigIds();
#endif
    }

    // ============================================
    // Fluent Configuration - Data Monitor Storage
    // ============================================

    Zeno &Zeno::enableDataMonitorStorage()
    {
        _dataMonitorStorageEnabled = true;
        ZENO_LOG_CORE("Data Monitor Storage module enabled");
        return *this;
    }

    Zeno &Zeno::onDataMonitorCreated(std::function<void(const DataMonitorConfig &)> callback)
    {
        _dataMonitorCreatedCallback = callback;
        return *this;
    }

    Zeno &Zeno::onDataMonitorUpdated(std::function<void(const DataMonitorConfig &)> callback)
    {
        _dataMonitorUpdatedCallback = callback;
        return *this;
    }

    Zeno &Zeno::onDataMonitorDeleted(std::function<void(const String &)> callback)
    {
        _dataMonitorDeletedCallback = callback;
        return *this;
    }

    Zeno &Zeno::onDataMonitorToggled(std::function<void(const String &, bool)> callback)
    {
        _dataMonitorToggledCallback = callback;
        return *this;
    }

    bool Zeno::getDataMonitorConfig(const String &mqttKey, DataMonitorConfig &outConfig)
    {
#if defined(ZENOPCB_DISABLE_STORAGE)
        (void)mqttKey; (void)outConfig;
        return false;
#else
        if (!_dataMonitorStorageEnabled)
        {
            return false;
        }
        return DataMonitorMessageHandler::getInstance().getMonitor(mqttKey, outConfig);
#endif
    }

    std::vector<String> Zeno::getAllDataMonitorIds()
    {
#if defined(ZENOPCB_DISABLE_STORAGE)
        return std::vector<String>();
#else
        if (!_dataMonitorStorageEnabled)
        {
            return std::vector<String>();
        }
        return DataMonitorMessageHandler::getInstance().getAllMonitorIds();
#endif
    }

    std::vector<DataMonitorConfig> Zeno::getDataMonitorsByConnection(const String &connectionId)
    {
#if defined(ZENOPCB_DISABLE_STORAGE)
        (void)connectionId;
        return std::vector<DataMonitorConfig>();
#else
        if (!_dataMonitorStorageEnabled)
        {
            return std::vector<DataMonitorConfig>();
        }
        return DataMonitorMessageHandler::getInstance().getMonitorsByConnection(connectionId);
#endif
    }

    Zeno &Zeno::mqttCredentials(const char *clientId, const char *username, const char *password)
    {
        _mqttClientId = clientId;
        _mqttUsername = username;
        _mqttPassword = password;
        return *this;
    }

    Zeno &Zeno::onMqttConnected(std::function<void()> callback)
    {
        _mqttConnectedCallback = callback;
        return *this;
    }

    Zeno &Zeno::onMqttMessage(std::function<void(const String &topic, const String &payload)> callback)
    {
        _mqttMessageCallback = callback;
        return *this;
    }

    ZenoPCBMQTT &Zeno::getMQTT()
    {
        if (!_mqtt)
        {
            _mqtt = new ZenoPCBMQTT();
        }
        return *_mqtt;
    }

    // ============================================
    // Fluent Configuration - Callbacks
    // ============================================

    Zeno &Zeno::onConnected(std::function<void()> callback)
    {
        _connectedCallback = callback;
        return *this;
    }

    Zeno &Zeno::onDisconnected(std::function<void()> callback)
    {
        _disconnectedCallback = callback;
        return *this;
    }

    Zeno &Zeno::onConfigured(std::function<void(const DeviceConfig &)> callback)
    {
        _configuredCallback = callback;
        return *this;
    }

    Zeno &Zeno::onError(std::function<void(const String &)> callback)
    {
        _errorCallback = callback;
        return *this;
    }

    Zeno &Zeno::onStateChange(std::function<void(ZenoState)> callback)
    {
        _stateCallback = callback;
        return *this;
    }

    // ============================================
    // Lifecycle
    // ============================================

    bool Zeno::begin()
    {
        ZENO_LOG_CORE("Initializing...");

        // ── Wire the HAL into static-pointer storage consumers ────────────
        // (Plan 04-05) — replaces the nullptr default from plans 04-03/04-04.
        // Must run BEFORE any module that uses storage (LittleFSManager,
        // ScheduleStorage, IrrigationStorage). Order is irrelevant within
        // this block — they are independent statics.
#if !defined(ZENOPCB_DISABLE_SCHEDULE)
        ScheduleStorage::setHal(&_hal);
#endif
#if !defined(ZENOPCB_DISABLE_STORAGE)
        LittleFSManager::setHal(&_hal);
#endif
#if defined(ESP32)
        IrrigationStorage::setHal(&_hal);
#endif

        // Initialize WiFi Provisioning (cần chạy trước để load DeviceConfig từ NVS)
        _initProvisioning();

#if !defined(ZENOPCB_DISABLE_PROVISIONING)
        // ── Chọn connection mode dựa trên config đã lưu trong NVS ─────────────
        if (_wifiProvisioning && _wifiProvisioning->isConfigured())
        {
            DeviceConfig savedCfg = getConfig();
            ConnectionType savedConn = savedCfg.connectionType;

            if (savedConn == ConnectionType::WIFI && _networkProvider)
            {
                // User đã chọn WiFi qua app → disable external provider, dùng WiFi
                ZENO_LOG_CORE("📡 Saved connectionType=WIFI → Disabling %s provider, switching to WiFi",
                              _networkProvider->getName());
                _networkProvider = nullptr;
                _actualConnectionType = "WIFI"; // Track actual connection
                // Re-enable WiFi auto-connect (was skipped because provider was set)
                _wifiProvisioning->setSkipAutoWiFiConnect(false);
                _wifiProvisioning->connectToSavedWiFi();
            }
            else if (savedConn == ConnectionType::CELLULAR && _registeredNetworkProvider)
            {
                // User đã chọn Cellular → đảm bảo dùng network provider
                _networkProvider = _registeredNetworkProvider;
                _actualConnectionType = "4G"; // Track actual connection
                ZENO_LOG_CORE("📡 Saved connectionType=CELLULAR → Using %s provider",
                              _networkProvider->getName());
            }
            else if (savedConn == ConnectionType::ETHERNET && _registeredNetworkProvider)
            {
                // User đã chọn Ethernet → dùng network provider
                _networkProvider = _registeredNetworkProvider;
                _actualConnectionType = "ETHERNET"; // Track actual connection
                ZENO_LOG_CORE("📡 Saved connectionType=ETHERNET → Using %s provider",
                              _networkProvider->getName());
            }
            else
            {
                // Fallback: default to WiFi if no provider
                _actualConnectionType = _networkProvider ? "ETHERNET" : "WIFI";
                ZENO_LOG_CORE("📡 Saved connectionType=%d → Using default provider",
                              (int)savedConn);
            }
        }
#endif // !ZENOPCB_DISABLE_PROVISIONING

        // ── Khởi tạo Network Provider nếu được cấu hình ──────────────────────
        if (_networkProvider)
        {
#if !defined(ZENOPCB_DISABLE_PROVISIONING)
            // Ensure WiFi auto-connect is skipped when using external provider
            if (_wifiProvisioning)
            {
                _wifiProvisioning->setSkipAutoWiFiConnect(true);
            }
#endif
            DeviceConfig cfg = getConfig();
            bool provOK = _networkProvider->begin(cfg);
            if (!provOK)
            {
                ZENO_LOG_CORE("⚠️ NetworkProvider(%s)::begin() failed", _networkProvider->getName());
            }
        }

        // Initialize MQTT if configured
        _initMQTT();

        // Initialize Storage module if enabled (must be after MQTT)
        _initStorage();

        // Initialize Data Monitor Storage module if enabled
        _initDataMonitorStorage();

        // Initialize Schedule module if enabled
        _initSchedule();

        // Initialize Irrigation module if enabled
        _initIrrigation();

        // Initialize Diagnostics module if enabled
        _initDiagnostics();

        // Initialize Alarm module if enabled
        _initAlarm();

        // Initialize OTA module if enabled
        _initOTA();

#if defined(ESP32)
        // ⭐ Initialize Modbus system (ESP32-only per Plan 06-03 D-03)
        //
        // Gate 2026-06-06: Modbus is the gateway-class feature — only
        // useful when the device persists connection configs + register
        // configs (i.e. it actually has Modbus slaves to poll). Tie the
        // init to those storage flags so a generic ESP32 example sketch
        // that just publishes a sensor on Z0 does not pull in Modbus
        // subsystem init + serial noise. ZMG / ZF / ZenoRTU production
        // firmware already calls `.enableStorage()` and
        // `.enableDataMonitorStorage()`, so this gate is backward-
        // compatible for those entry points — no src/main.cpp change.
        if (_storageEnabled || _dataMonitorStorageEnabled)
        {
            ZENO_LOG_CORE("Initializing Modbus System...");
            if (initializeModbusSystem())
            {
                ZENO_LOG_CORE("✅ Modbus system initialized");

                // ⭐ Load saved configs from LittleFS
                size_t loaded = loadSavedModbusConfigs();
                if (loaded > 0)
                {
                    ZENO_LOG_CORE("✅ Loaded %d saved Modbus configs", loaded);
                }
                else
                {
                    ZENO_LOG_CORE("ℹ️ No saved Modbus configs found");
                }
            }
            else
            {
                ZENO_LOG_CORE("⚠️ Modbus system initialization failed");
            }
        }
#endif  // ESP32

        // If WiFi credentials provided directly, use them
        // Pattern H TU gate: WiFi.mode() + WIFI_STA enum are ESP-specific.
        // UNO R4 (CWifi) and STM32 (WiFiEspAT) expose neither; the body is
        // a no-op on those platforms — the network provider abstraction is
        // expected to drive connectivity via setNetworkProvider() instead.
#if defined(ESP32) || defined(ESP8266)
        if (_wifiConfigured && _wifiSSID.length() > 0)
        {
            // Library-standard always-on connect status (matches the
            // [MQTT] connected / not connected pattern). SSID is local
            // network identity, not a credential — safe to print.
            ZENOPCB_PRINTF("[WiFi] connecting to %s\n", _wifiSSID.c_str());
            WiFi.mode(WIFI_STA);
            WiFi.begin(_wifiSSID.c_str(), _wifiPassword.c_str());

            int attempts = 0;
            while (WiFi.status() != WL_CONNECTED && attempts < 40)
            {
                delay(500);
                ZENO_LOG_RAW(".");
                attempts++;
            }

            if (WiFi.status() == WL_CONNECTED)
            {
                // Always-on success log. Local IP is not sensitive.
                ZENOPCB_PRINTF("\n[WiFi] connected, ip=%s\n",
                               WiFi.localIP().toString().c_str());
                _setState(ZenoState::CONNECTED);
                if (_connectedCallback)
                {
                    _connectedCallback();
                }

                // Connect MQTT after WiFi connected
                if (_mqtt && _mqttEnabled)
                {
                    _mqtt->connect();
                }
            }
            else
            {
                // Always-on failure log — symmetric with [WiFi] connected.
                ZENOPCB_PRINTF("\n[WiFi] not connected\n");
                _setState(ZenoState::DISCONNECTED);
            }
        }
#if !defined(ZENOPCB_DISABLE_PROVISIONING)
        else if (_provisioningEnabled && _wifiProvisioning)
        {
            // Check if already configured via provisioning
            if (_wifiProvisioning->isConfigured())
            {
                ZENO_LOG_CORE("Using saved WiFi configuration");
            }
            else
            {
                ZENO_LOG_CORE("No WiFi configured - hold button to enter AP mode");
            }
        }
#endif
#else
        // UNO R4 / STM32: direct WiFi.mode()/begin() path is unavailable.
        // External network provider (setNetworkProvider) must be wired by
        // the application; library code stays portable.
        (void)_wifiConfigured;
        (void)_wifiSSID;
        (void)_wifiPassword;
#endif // ESP32 || ESP8266

        // ============================================
        // Setup UNIFIED MQTT onConnected Callback
        // ⚠️ MUST be LAST to avoid overwrite by individual modules
        // ============================================
        ZENO_LOG_VERBOSE("Checking MQTT: ptr=%s enabled=%s",
                         _mqtt ? "SET" : "NULL",
                         _mqttEnabled ? "true" : "false");

        if (_mqtt && _mqttEnabled)
        {
            ZENO_LOG_VERBOSE("Setting up unified MQTT callbacks...");
            _mqtt->onConnected([this]()
                               {

                if (!_mqtt || !_mqtt->isConnected()) {
                    ZENO_LOG_CORE("MQTT not connected, aborting subscriptions!");
                    return;
                }

                ZENO_LOG_VERBOSE("MQTT connected - starting subscriptions...");

                // ========================================
                // 1. Storage Module - connection-config
                // ========================================
                if (_storageEnabled && _mqttUsername.length() > 0) {
                    String configTopic = "v1/devices/" + _mqttUsername + "/connection-config";
                    ZENO_LOG_VERBOSE("[1] Subscribe: %s", maskTopic(configTopic).c_str());
                    bool subResult = _mqtt->subscribe(configTopic.c_str(), MQTTQoS::QOS_1);
                    ZENO_LOG_VERBOSE("[1] Result: %s", subResult ? "OK" : "FAILED");
                }

                // ========================================
                // 2. DataMonitor Module - data-monitors
                // ========================================
                if (_dataMonitorStorageEnabled && _mqttUsername.length() > 0) {
                    String dataMonitorTopic = "v1/devices/" + _mqttUsername + "/data-monitors";
                    ZENO_LOG_VERBOSE("[2] Subscribe: %s", maskTopic(dataMonitorTopic).c_str());
                    bool subResult = _mqtt->subscribe(dataMonitorTopic.c_str(), MQTTQoS::QOS_1);
                    ZENO_LOG_VERBOSE("[2] Result: %s", subResult ? "OK" : "FAILED");
                }

                // ========================================
                // 2.5. Control Topic - required by DataMonitor OR Z Keys
                // ========================================
                if ((_dataMonitorStorageEnabled || _zKeysEnabled) && _mqttUsername.length() > 0) {
                    String controlTopic = "v1/devices/" + _mqttUsername + "/control";
                    ZENO_LOG_VERBOSE("[2.5] Subscribe: %s", maskTopic(controlTopic).c_str());
                    bool ctrlResult = _mqtt->subscribe(controlTopic.c_str(), MQTTQoS::QOS_1);
                    ZENO_LOG_VERBOSE("[2.5] Result: %s", ctrlResult ? "OK" : "FAILED");
                }

                // ========================================
                // 3. Schedule Module - schedules
                // ========================================
                if (_scheduleEnabled && _mqttUsername.length() > 0) {
                    String scheduleTopic = "v1/devices/" + _mqttUsername + "/schedules";
                    ZENO_LOG_VERBOSE("[3] Subscribe: %s", maskTopic(scheduleTopic).c_str());
                    bool subResult = _mqtt->subscribe(scheduleTopic.c_str(), MQTTQoS::QOS_1);
                    ZENO_LOG_VERBOSE("[3] Result: %s", subResult ? "OK" : "FAILED");
                }

                // ========================================
                // 3.5. Irrigation Module (V3: shared /config topic)
                // ========================================
                if (_irrigationEnabled && _mqttUsername.length() > 0) {
                    String configTopic = "v1/devices/" + _mqttUsername + "/config";
                    ZENO_LOG_VERBOSE("[3.5] Subscribe: %s", maskTopic(configTopic).c_str());
                    bool subResult = _mqtt->subscribe(configTopic.c_str(), MQTTQoS::QOS_1);
                    ZENO_LOG_VERBOSE("[3.5] Result: %s", subResult ? "OK" : "FAILED");
                }

                // ========================================
                // 3.1. Time Sync — needed for schedules, diagnostics, timestamps
                // ========================================
                if (!TimeManager::isSynced()) {
                    bool synced = false;

                    // Try provider-specific time sync first (4G: reads AT+CCLK? / NITZ)
                    // configTime()/SNTP only works over WiFi/Ethernet — not 4G
                    if (_networkProvider) {
                        synced = _networkProvider->syncTime();
                    }

                    if (!synced) {
                        // Fallback: ESP32 SNTP (works for WiFi/Ethernet)
                        TimeManager::syncNTP();
                        ZENO_LOG_CORE("⏰ NTP sync initiated via SNTP (non-blocking, check in loop)");
                        _ntpSyncPending = true;
                        _ntpSyncStartMs = millis();
                    } else {
                        ZENO_LOG_CORE("⏰ Time synced via network provider");
                    }
                } else {
                    time_t now = TimeManager::getUTC();
                    ZENO_LOG_VERBOSE("[3.1] Time already synced! UTC: %ld", now);
                }

                // ========================================
                // 3.5. Diagnostics Module - diagnostics/request
                // ========================================
                if (_diagnosticsEnabled && _mqttUsername.length() > 0) {
                    String diagnosticsTopic = "v1/devices/" + _mqttUsername + "/diagnostics/request";
                    ZENO_LOG_VERBOSE("[3.5] Subscribe: %s", maskTopic(diagnosticsTopic).c_str());
                    bool subResult = _mqtt->subscribe(diagnosticsTopic.c_str(), MQTTQoS::QOS_1);
                    ZENO_LOG_VERBOSE("[3.5] Result: %s", subResult ? "OK" : "FAILED");
                }

                // ========================================
                // 3.6. Alarm Module - alarm/config
                // ========================================
                if (_alarmEnabled && _mqttUsername.length() > 0) {
                    String alarmTopic = "v1/devices/" + _mqttUsername + "/alarm/config";
                    ZENO_LOG_VERBOSE("[3.6] Subscribe: %s", maskTopic(alarmTopic).c_str());
                    bool subResult = _mqtt->subscribe(alarmTopic.c_str(), MQTTQoS::QOS_1);
                    ZENO_LOG_VERBOSE("[3.6] Result: %s", subResult ? "OK" : "FAILED");
                }

                // ========================================
                // 3.7. OTA Module - ota
                // ========================================
                if (_otaEnabled && _mqttUsername.length() > 0) {
                    String otaTopic = "v1/devices/" + _mqttUsername + "/ota";
                    ZENO_LOG_VERBOSE("[3.7] Subscribe: %s", maskTopic(otaTopic).c_str());
                    bool subResult = _mqtt->subscribe(otaTopic.c_str(), MQTTQoS::QOS_1);
                    ZENO_LOG_VERBOSE("[3.7] Result: %s", subResult ? "OK" : "FAILED");
                }

                // ========================================
                // 3.8. Flush pending OTA error (queued when MQTT was offline)
                // ========================================
                if (_otaEnabled && _pendingOTAErrorPayload.length() > 0) {
                    String respTopic = "v1/devices/" + _deviceToken + "/ota/response";
                    _mqtt->publish(respTopic.c_str(), _pendingOTAErrorPayload.c_str(), MQTTQoS::QOS_1, false);
                    ZENO_LOG_OTA("📤 Flushed pending OTA error response: %s", _pendingOTAErrorPayload.c_str());
                    _pendingOTAErrorPayload = "";
                }

                // ========================================
                // 3.9. Flush pending OTA completed (saved to NVS before reboot)
                // 4G blocking OTA: MQTT was closed during OTA → result saved to NVS
                // → After reboot + MQTT reconnect → publish here
                // ========================================
                if (_otaEnabled) {
                    // T-4-02: namespace "ota_result" + key "payload" preserved byte-for-byte.
                    if (_hal.nvs().begin("ota_result", true)) {
                        char payloadBuf[OTA_PAYLOAD_BUF_SIZE];
                        _hal.nvs().getString("payload", payloadBuf, sizeof(payloadBuf), "");
                        _hal.nvs().end();
                        String savedPayload(payloadBuf);
                        if (savedPayload.length() > 0) {
                            ZENO_LOG_OTA("📦 Found pending OTA result in NVS (%d bytes)", savedPayload.length());
                            String respTopic = "v1/devices/" + _deviceToken + "/ota/response";

                            // Retry publish up to 3 times — 4G TCP may not be ready immediately
                            bool published = false;
                            for (int attempt = 0; attempt < 3 && !published; attempt++) {
                                if (attempt > 0) {
                                    delay(500);
                                    _mqtt->loop(); // Process TCP buffer
                                }
                                published = _mqtt->publish(respTopic.c_str(), savedPayload.c_str(), MQTTQoS::QOS_1, false);
                                ZENO_LOG_OTA("📤 OTA result publish attempt %d: %s", attempt + 1, published ? "OK" : "FAILED");
                            }

                            if (published) {
                                // Clear NVS only on successful publish
                                // T-4-02: namespace "ota_result" preserved byte-for-byte.
                                _hal.nvs().begin("ota_result", false);
                                _hal.nvs().clear();
                                _hal.nvs().end();
                                ZENO_LOG_OTA("✅ OTA result published & NVS cleared");
                            } else {
                                ZENO_LOG_OTA("⚠️ OTA result publish failed — will retry next reconnect");
                            }
                        }
                    } else {
                        ZENO_LOG_OTA("NVS ota_result: no pending data");
                    }
                }

#if !defined(ZENOPCB_DISABLE_PROVISIONING)
                // ========================================
                // 3.10. Deferred claim — for 4G/Ethernet devices that had
                // no WiFi available during provisioning AP mode.
                // If isClaimed=false but userId is set, publish claim
                // via the already-connected MQTT session.
                // ========================================
                if (_wifiProvisioning)
                {
                    DeviceConfig cfg = _wifiProvisioning->getConfig();
                    if (!cfg.isClaimed && cfg.userId.length() > 0 && cfg.deviceId.length() > 0)
                    {
                        ZENO_LOG_CORE("🔑 [3.10] Deferred claim: isClaimed=false, attempting via MQTT...");
                        ZENO_LOG_CORE("   userId: %s...", cfg.userId.substring(0, 8).c_str());

                        // Subscribe to claim ack topic BEFORE publishing
                        String claimAckTopic = "v1/devices/" + String(_deviceToken) + "/claim/ack";
                        _mqtt->subscribe(claimAckTopic.c_str(), MQTTQoS::QOS_1);
                        ZENO_LOG_CORE("   Subscribed: %s", maskTopic(claimAckTopic).c_str());

                        // Build & publish claim payload
                        JsonDocument claimDoc;
                        claimDoc["userId"] = cfg.userId;
                        claimDoc["deviceId"] = cfg.deviceId;
                        claimDoc["token"] = _deviceToken;
                        String claimPayload;
                        serializeJson(claimDoc, claimPayload);

                        String claimTopic = "v1/devices/" + String(_deviceToken) + "/claim";
                        bool pubOK = _mqtt->publish(claimTopic.c_str(), claimPayload.c_str(), MQTTQoS::QOS_1, false);
                        ZENO_LOG_CORE("   Published claim: %s (ok=%d)", maskTopic(claimTopic).c_str(), pubOK);
                        // Ack is handled in onMessage routing (claim/ack topic)
                    }
                }
#endif // !ZENOPCB_DISABLE_PROVISIONING

                // ========================================
                // 4. User's custom onConnected callback
                // ========================================
                ZENO_LOG_VERBOSE("[4] Calling user's onConnected callback...");
                if (_mqttConnectedCallback) {
                    _mqttConnectedCallback();
                    ZENO_LOG_VERBOSE("[4] User callback executed");
                }

                // ========================================
                // 5. Reset ZKey publish timer — stabilization delay
                // On 4G, publishing immediately after connect causes TCP
                // buffer overflow (too many AT+CIPSEND in quick succession).
                // Resetting the timer gives ZenoPubSubClient 1-2 loop() cycles
                // to process SUBACK packets before we start publishing.
                // ========================================
                if (_zKeysEnabled) {
                    // Timer-only reset (keep dirty values queued so the first
                    // post-stabilization publish includes anything set during
                    // setup() / pre-connect).
                    ZKeyBuffer::getInstance().markPublishTimer();
                    ZENO_LOG_VERBOSE("[5] ZKey publish timer reset (stabilization delay)");
                }

                ZENO_LOG_VERBOSE("MQTT subscriptions completed"); });

            // ============================================
            // Setup UNIFIED MQTT onMessage Callback
            // ⚠️ MUST handle ALL message routing in ONE place
            // ============================================
            ZENO_LOG_VERBOSE("Setting up unified MQTT onMessage callback...");
            _mqtt->onMessage([this](const String &topic, const String &payload)
                             {

                // ========================================
                // Route to appropriate handler based on topic
                // ========================================
                bool handled = false;

                // 1. Connection Config Handler
                if (topic.endsWith("/connection-config")) {
                    _handleConnectionConfigMessage(topic, payload);
                    handled = true;
                }

#if !defined(ZENOPCB_DISABLE_PROVISIONING)
                // 1.5. Deferred claim ack handler
                else if (topic.endsWith("/claim/ack") && _wifiProvisioning)
                {
                    ZENO_LOG_CORE("📋 Deferred claim ack received: %s", payload.c_str());
                    bool claimSuccess = false;
                    JsonDocument ackDoc;
                    DeserializationError err = deserializeJson(ackDoc, payload);
                    if (!err && ackDoc.containsKey("success"))
                    {
                        claimSuccess = ackDoc["success"].as<bool>();
                    }
                    else
                    {
                        claimSuccess = true; // No 'success' field → treat as OK
                    }

                    if (claimSuccess)
                    {
                        ZENO_LOG_CORE("✅ Deferred claim succeeded — marking device as claimed");
                        _wifiProvisioning->markClaimed();
                    }
                    else
                    {
                        ZENO_LOG_CORE("❌ Deferred claim rejected by server");
                    }
                    // Unsubscribe from claim/ack — no longer needed
                    String claimAckTopic = "v1/devices/" + String(_deviceToken) + "/claim/ack";
                    _mqtt->unsubscribe(claimAckTopic.c_str());
                    handled = true;
                }
#endif // !ZENOPCB_DISABLE_PROVISIONING
                
                // 2. Data Monitor Handler
                else if (topic.endsWith("/data-monitors")) {
                    _handleDataMonitorMessage(topic, payload);
                    handled = true;
                }
                
                // 3. Schedule Handler
                else if (topic.endsWith("/schedules")) {
                    ZENO_LOG_VERBOSE("Routing to _handleScheduleMessage()");
                    _handleScheduleMessage(topic, payload);
                    handled = true;
                }
                
                // 3.5. (V3: routing moved after /alarm/config — see 4.7)
                
                // 4. Diagnostics Request Handler
                else if (topic.endsWith("/diagnostics/request")) {
                    ZENO_LOG_VERBOSE("Routing to _handleDiagnosticsRequest()");
                    _handleDiagnosticsRequest(topic, payload);
                    handled = true;
                }
                
                // 4.5. Alarm Config Handler
                else if (topic.endsWith("/alarm/config")) {
                    ZENO_LOG_VERBOSE("Routing to _handleAlarmConfigMessage()");
                    _handleAlarmConfigMessage(topic, payload);
                    handled = true;
                }
                
#if defined(ESP32)
                // 4.7. Irrigation Config Handler (V3: shared /config topic) — ESP32-only (D-03)
                // Must come AFTER /alarm/config check (which also endsWith /config)
                else if (_irrigationEnabled && topic.endsWith("/config")) {
                    // Shared /config topic — verify type field
                    JsonDocument typeDoc;
                    DeserializationError typeErr = deserializeJson(typeDoc, payload);
                    if (!typeErr) {
                        const char *msgType = typeDoc["t"];
                        if (msgType && strcmp(msgType, "irrigation") == 0) {
                            ZENO_LOG_VERBOSE("Routing to _handleIrrigationMessage()");
                            _handleIrrigationMessage(topic, payload);
                            handled = true;
                        }
                    }
                }
#endif  // ESP32
                
                // 4.6. OTA Handler
                else if (topic.endsWith("/ota")) {
                    ZENO_LOG_VERBOSE("Routing to _handleOTAMessage()");
                    _handleOTAMessage(topic, payload);
                    handled = true;
                }
                
                // 5. Control Handler — /control topic
                //
                // Platform split:
                //   - ZKey dispatch (Z0..Z254 → ZKeyBuffer → onZKeyChange callbacks)
                //     runs on EVERY platform. Earlier the entire `/control` block
                //     was wrapped in `#if defined(ESP32)` because it coupled with
                //     Modbus write and GET_ALL telemetry, which silently dropped
                //     every cloud-down ZKey command on ESP8266 / UNO R4 / STM32.
                //   - Modbus writes + GET_ALL polling stay ESP32-only (Plan 06-03
                //     D-03 — RegisterPollingEngine is ESP32-only).
                else if (topic.endsWith("/control")) {
                    ZENO_LOG_VERBOSE("Routing /control message");

#if defined(ESP32)
                    // Full handler on ESP32: ZKey + Modbus writes + GET_ALL
                    MQTTControlHandler &handler = MQTTControlHandler::getInstance();

                    handler.onGetAllTelemetry([this](const String &telemetryJson) {
                        if (_mqtt && _mqtt->isConnected()) {
                            String telemetryTopic = String("v1/devices/") + _deviceToken + "/telemetry";
                            ZENO_LOG_VERBOSE("Publishing get_all telemetry to: %s", maskTopic(telemetryTopic).c_str());
                            _mqtt->publish(telemetryTopic.c_str(), telemetryJson.c_str(), MQTTQoS::QOS_1, false);
                        }
                    });

                    ControlMessageResult result = handler.handleMessage(payload);

                    // Immediately publish updated Z Key values back so the app
                    // sees new state right away (instead of waiting up to 5s for
                    // the next periodic publish).
                    if (_zKeysEnabled) {
                        if (_zKeyReadCallback) _zKeyReadCallback();
                        _publishZKeyTelemetry();
                    }

                    _publishAck("control", "w", "", result.allSuccess(), 0,
                                result.allSuccess() ? "" : result.toJson());

                    if (_mqtt && _mqtt->isConnected()) {
                        String responseTopic = topic + "/response";
                        _mqtt->publish(responseTopic.c_str(), result.toJson().c_str(), MQTTQoS::QOS_1, false);
                    }
#else
                    // Non-ESP32: ZKey-only dispatch. No Modbus / GET_ALL here.
                    // Mirrors the ZKey path in MQTTControlHandler::handleMessage()
                    // (src/modbus/MQTTControlHandler.cpp) so ESP8266 / UNO R4 /
                    // STM32 users get the same ZENO_READ(Zx) → onZKeyChange
                    // dispatch behaviour as ESP32.
                    if (_zKeysEnabled) {
                        JsonDocument doc;
                        DeserializationError err = deserializeJson(doc, payload);
                        if (!err) {
                            uint8_t dispatched = 0;
                            for (JsonPair kv : doc.as<JsonObject>()) {
                                const char *mqttKey = kv.key().c_str();
                                if (isZKey(mqttKey)) {
                                    ZKey zk = stringToZKey(mqttKey);
                                    ZKeyBuffer::getInstance().setFromJson(zk, kv.value());
                                    dispatched++;
                                    ZENO_LOG_VERBOSE("[/control] ZKey %s dispatched", mqttKey);
                                }
                            }
                            if (dispatched > 0) {
                                if (_zKeyReadCallback) _zKeyReadCallback();
                                _publishZKeyTelemetry();
                            }
                        } else {
                            ZENO_LOG_VERBOSE("[/control] JSON parse error: %s", err.c_str());
                        }
                    }
#endif  // ESP32
                    handled = true;
                }

                if (!handled) {
                    ZENO_LOG_VERBOSE("No handler matched topic: %s", maskTopic(topic).c_str());
                }

                // ========================================
                // Call user's message callback (always)
                // ========================================
                if (_mqttMessageCallback) {
                    _mqttMessageCallback(topic, payload);
                } });

            ZENO_LOG_VERBOSE("Unified MQTT callbacks registered");
        }

        ZENO_LOG_CORE("Initialized");
        return true;
    }

    void Zeno::loop()
    {
        // ── AP mode guard: compute once per loop ─────────────────────────────
        // Without this, ZenoPCBDiagnostics::loop() and other subsystems call
        // isConnected() / publish() → TinyGsmClient AT commands → blocks 1-3s per
        // iteration → starves _wifiProvisioning->loop() (handleClient) → mobile scan fails.
#if defined(ZENOPCB_DISABLE_PROVISIONING)
        const bool apActive = false; // Provisioning subsystem disabled at compile time
#else
        const bool apActive = _wifiProvisioning && _wifiProvisioning->isAPMode();
#endif

        // ── 4G OTA Serial2 guard: block MQTT/Network AT commands during OTA on 4G ──
        // On 4G, Serial2 is shared: MQTT (AT+CIPSEND mux 0) + OTA (AT+CIPRXGET mux 1)
        // AT interleaving corrupts data or drops connections. WiFi/ETH are safe.
        const bool otaActive = isOTAInProgress();
        const bool otaBlockSerial2 = otaActive && _networkProvider &&
                                     strcmp(_networkProvider->getName(), "4G") == 0;

#if !defined(ZENOPCB_DISABLE_PROVISIONING)
        // ── Pause network provider when AP mode is active ─────────────────────
        // 4G/Ethernet reconnection blocks CPU for 15-30s, starving WebServer
        if (_networkProvider && _wifiProvisioning)
        {
            if (apActive != _networkProvider->isPaused())
            {
                _networkProvider->setPaused(apActive);
                ZENO_LOG_CORE("%s network provider (AP mode %s)",
                              apActive ? "⏸️ Pausing" : "▶️ Resuming",
                              apActive ? "active" : "ended");
            }
        }
#endif // !ZENOPCB_DISABLE_PROVISIONING

        // ── Network Provider maintenance (block on 4G during OTA — AT cmd conflicts) ──
        if (_networkProvider && !otaBlockSerial2)
        {
            _networkProvider->loop();

            static bool _provWasConnected = false;
            static Client *_provLastClient = nullptr;
            bool provNowConnected = _networkProvider->isConnected();
            Client *provCurrentClient = provNowConnected ? _networkProvider->getClient() : nullptr;

            // Case 1: Provider just connected
            if (provNowConnected && !_provWasConnected)
            {
                ZENO_LOG_CORE("🔗 %s connected — IP: %s",
                              _networkProvider->getName(), _networkProvider->getLocalIP().c_str());
                _setState(ZenoState::CONNECTED);
                if (_connectedCallback)
                    _connectedCallback();

                // ⚠️ FIX: Give modem TCP/DNS stack time to stabilize after GPRS attach
                // A7680C/SIM7600 modems need 2-5s after GPRS before TCP works reliably
                if (strcmp(_networkProvider->getName(), "4G") == 0)
                {
                    ZENO_LOG_CORE("⏳ Waiting 3s for modem TCP stack stability...");
                    delay(3000);
                }

                // Set/update MQTT client and connect
                if (_mqtt && _mqttEnabled)
                {
                    _mqtt->setClient(provCurrentClient);
                    _mqtt->setNetworkCheck([this]()
                                           { return _networkProvider && _networkProvider->isConnected() && !isAPMode(); });
                    // Only connect when DISCONNECTED (idle) — không call khi đang RECONNECTING/ERROR
                    if (_mqtt->needsManualConnect())
                        _mqtt->connect();
                }
            }
            // Case 2: Provider client switched (MultiConnect failover)
            else if (provNowConnected && provCurrentClient != _provLastClient && _provLastClient != nullptr)
            {
                ZENO_LOG_CORE("🔄 Provider failover — now: %s (IP: %s)",
                              _networkProvider->getName(), _networkProvider->getLocalIP().c_str());
                if (_mqtt && _mqttEnabled)
                {
                    _mqtt->setClient(provCurrentClient);
                    _mqtt->setNetworkCheck([this]()
                                           { return _networkProvider && _networkProvider->isConnected() && !isAPMode(); });
                    // Only connect when DISCONNECTED (idle) — không call khi đang RECONNECTING/ERROR
                    if (_mqtt->needsManualConnect())
                        _mqtt->connect();
                }
            }
            // Case 3: Provider disconnected
            else if (!provNowConnected && _provWasConnected)
            {
                ZENO_LOG_CORE("🔗 %s connection lost", _networkProvider->getName());
                _setState(ZenoState::DISCONNECTED);

                // ⭐ Force disconnect MQTT — network already dead, can't send packets
                // Broker will detect via keepalive timeout (1.5 × keepAlive)
                if (_mqtt && _mqttEnabled)
                {
                    ZENO_LOG_CORE("🔌 Force disconnecting MQTT (network lost)");
                    _mqtt->forceDisconnect();
                }

                if (_disconnectedCallback)
                    _disconnectedCallback();
            }

            _provWasConnected = provNowConnected;
            _provLastClient = provCurrentClient;
        }

        // Process WiFi provisioning (compile-stripped under ZENOPCB_DISABLE_PROVISIONING)
#if !defined(ZENOPCB_DISABLE_PROVISIONING)
        if (_wifiProvisioning)
        {
            _wifiProvisioning->loop();

            // Update state based on WiFi provisioning state
            // ⚠️ Only check WiFi state when NOT using external network provider (4G/Ethernet)
            // External providers manage their own connection state
            if (!_networkProvider)
            {
                if (_wifiProvisioning->isWiFiConnected())
                {
                    // State transition: set CONNECTED once
                    if (_state != ZenoState::CONNECTED)
                    {
                        ZENO_LOG_CORE("📶 WiFi connected, triggering state change...");
                        _setState(ZenoState::CONNECTED);
                        if (_connectedCallback)
                        {
                            _connectedCallback();
                        }
                    }

                    // Connect MQTT khi WiFi up và MQTT ở trạng thái DISCONNECTED (idle)
                    // KHÔNG gọi connect() khi đang RECONNECTING/ERROR (MQTTClient tự retry với backoff)
                    if (_mqtt && _mqttEnabled && _mqtt->needsManualConnect())
                    {
                        ZENO_LOG_CORE("🔌 Attempting MQTT connect...");
                        bool result = _mqtt->connect();
                        ZENO_LOG_CORE("🔌 MQTT connect result: %s", result ? "SUCCESS" : "FAILED");
                    }
                }
                else if (!_wifiProvisioning->isWiFiConnected() && _state == ZenoState::CONNECTED)
                {
                    _setState(ZenoState::DISCONNECTED);

                    // ⭐ Force disconnect MQTT — WiFi already dead, can't send packets
                    if (_mqtt && _mqttEnabled)
                    {
                        ZENO_LOG_CORE("🔌 Force disconnecting MQTT (WiFi lost)");
                        _mqtt->forceDisconnect();
                    }

                    if (_disconnectedCallback)
                    {
                        _disconnectedCallback();
                    }
                }
            }

            // ⭐ LED Status Indicator
            // AP mode         → 50ms  (fast blink: provisioning mode)
            // No MQTT         → 200ms  (fast blink: not connected)
            // MQTT connected  → 1000ms (slow blink: all OK)
            if (_provConfig.ledPin >= 0)
            {
                bool apMode = _wifiProvisioning->isAPMode();
                bool mqttOk = (_mqtt != nullptr && _mqtt->isConnected());

                // Debug: log LED state once when it changes
                static bool _lastMqttOk = false;
                static bool _lastApMode = false;
                if (mqttOk != _lastMqttOk || apMode != _lastApMode)
                {
                    ZENO_LOG_CORE("💡 LED: apMode=%s mqttOk=%s (_mqtt=%s) → %sms",
                                  apMode ? "Y" : "N", mqttOk ? "Y" : "N",
                                  _mqtt ? "exists" : "NULL",
                                  apMode ? "50" : mqttOk ? "1000"
                                                         : "200");
                    _lastMqttOk = mqttOk;
                    _lastApMode = apMode;
                }

                uint32_t blinkMs = apMode ? 50 : mqttOk ? 1000
                                                        : 200;
                _wifiProvisioning->setLEDBlink(blinkMs);
                _wifiProvisioning->updateLED();
            }
        }
        else
        {
            // Simple WiFi monitoring when not using provisioning.
            // Pattern H gate: STM32F4 default-Ethernet has no WiFi.h so
            // status polling is skipped — provider abstraction is mandatory.
#if defined(ESP32) || defined(ESP8266) || defined(ARDUINO_UNOR4_WIFI) || defined(STM32F1xx)
            static bool wasConnected = false;
            bool isNowConnected = WiFi.status() == WL_CONNECTED;

            if (isNowConnected && !wasConnected)
            {
                _setState(ZenoState::CONNECTED);
                if (_connectedCallback)
                {
                    _connectedCallback();
                }

                // Connect MQTT when WiFi becomes connected
                // Only connect when DISCONNECTED (idle) — không call khi đang RECONNECTING/ERROR
                if (_mqtt && _mqttEnabled && _mqtt->needsManualConnect())
                {
                    _mqtt->connect();
                }
            }
            else if (!isNowConnected && wasConnected)
            {
                _setState(ZenoState::DISCONNECTED);
                if (_disconnectedCallback)
                {
                    _disconnectedCallback();
                }
            }
            wasConnected = isNowConnected;
#endif // ESP32 || ESP8266 || ARDUINO_UNOR4_WIFI || STM32F1 — WiFi.status monitoring
        }
#endif // !ZENOPCB_DISABLE_PROVISIONING — entire `if (_wifiProvisioning)…else…` block

        // Process MQTT (block on 4G during OTA — AT+CIPSEND conflicts with AT+CIPRXGET)
        if (_mqtt && _mqttEnabled && !otaBlockSerial2)
        {
            _mqtt->loop();
        }

#if defined(ESP32)
        // ⭐ Process Modbus system + MQTTControlHandler (runs during OTA — RTU=Serial1, TCP=Ethernet)
        // ESP32-only per Plan 06-03 D-03 (Modbus subsystem stripped on ESP8266).
        if (!apActive)
        {
            loopModbusSystem();
            // processGetAll: MQTT publish sẽ tự fail nếu MQTT disconnected
            MQTTControlHandler::getInstance().processGetAll();
        }
#endif  // ESP32

        // ⭐ Non-blocking NTP sync check
        if (_ntpSyncPending)
        {
            if (TimeManager::isSynced())
            {
                _ntpSyncPending = false;
                time_t now = TimeManager::getUTC();
                struct tm timeinfo;
                gmtime_r(&now, &timeinfo);
                ZENO_LOG_CORE("✅ NTP synced! UTC: %04d-%02d-%02d %02d:%02d:%02d (took %lums)",
                              timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday,
                              timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec,
                              millis() - _ntpSyncStartMs);
            }
            else if (millis() - _ntpSyncStartMs > 60000)
            {
                // Give up after 60s — will retry on next MQTT reconnect
                _ntpSyncPending = false;
                ZENO_LOG_CORE("⚠️ NTP sync timeout after 60s (will retry on reconnect)");
            }
        }

#if !defined(ZENOPCB_DISABLE_SCHEDULE)
        // ⭐ Process Schedule Executor (runs during OTA — local timer + GPIO, no Serial2)
        // Outer throttle 50ms (just to avoid calling every single loop tick)
        // Inner intervalElapsed() handles the actual precise timing per schedule
        if (!apActive && _scheduleExecutor && _scheduleEnabled)
        {
            static unsigned long lastScheduleCheck = 0;
            if (millis() - lastScheduleCheck >= 50)
            {
                lastScheduleCheck = millis();
                _scheduleExecutor->loop();
            }
        }
#endif // !ZENOPCB_DISABLE_SCHEDULE

#if defined(ESP32)
        // ⭐ Process Irrigation module (scheduler time-checking + executor state machine)
        // ESP32-only per Plan 06-03 D-03.
        if (!apActive && _irrigationEnabled)
        {
            static unsigned long lastIrrigationCheck = 0;
            unsigned long now = millis();

            // Scheduler: check every 1 second (time-based triggers)
            if (now - lastIrrigationCheck >= 1000)
            {
                lastIrrigationCheck = now;
                IrrigationScheduler::getInstance().loop();
            }

            // Executor: check every loop tick (WAIT step timer needs precision)
            IrrigationExecutor::getInstance().loop();
        }
#endif  // ESP32

#if !defined(ZENOPCB_DISABLE_DIAGNOSTICS)
        // ⭐ Process Diagnostics (runs during OTA — uses cached connection state)
        if (!apActive && _diagnostics && _diagnosticsEnabled)
        {
            _diagnostics->loop();
        }
#endif // !ZENOPCB_DISABLE_DIAGNOSTICS

#if !defined(ZENOPCB_DISABLE_OTA)
        // ⭐ Process deferred OTA start (tách khỏi MQTT callback → tránh block keepalive)
        if (!apActive && _ota && _otaEnabled && _pendingOTAStart)
        {
            _pendingOTAStart = false;
            ZENO_LOG_CORE("🚀 Starting deferred OTA: %s", maskUrl(_pendingOTAUrl).c_str());

            // Refresh OTA client (có thể đã đổi sau failover)
            if (_networkProvider)
            {
                Client *provOTAClient = _networkProvider->getOTAClient();
                if (provOTAClient)
                    _ota->setClient(provOTAClient);
            }

            _publishOTAResponse("starting", 0, _pendingOTAVersion);

            // Record OTA start time
            _otaStartTimeMs = millis();

            // Non-blocking OTA cho TẤT CẢ networks (kể cả 4G)
            // Mọi subsystem vẫn chạy bình thường — thử nghiệm no-block mode
            bool started = _ota->beginUpdate(_pendingOTAUrl.c_str());
            if (!started)
            {
                ZENO_LOG_CORE("❌ OTA failed to start");
                _lastFailedOTAPayload = _pendingOTAPayload;
                _lastFailedOTATime = millis();
                _otaStartTimeMs = 0;
            }
            else
            {
                _lastFailedOTAPayload = "";
                ZENO_LOG_CORE("⏱️ OTA started — timer running");

                // ⭐ 4G: Enable MQTT queue mode — messages queued, flushed at yield points
                bool is4G = _networkProvider && strcmp(_networkProvider->getName(), "4G") == 0;
                if (is4G)
                {
                    _mqttQueueEnabled = true;
                    ZENO_LOG_OTA("📦 4G OTA: MQTT queue mode ENABLED — messages will be flushed at yield points");
                }
            }

            // Free memory
            _pendingOTAUrl = "";
            _pendingOTAVersion = "";
            _pendingOTAPayload = "";
        }

        // ⭐ Process OTA non-blocking mode (skip during AP)
        if (!apActive && _ota && _otaEnabled && _ota->isInProgress())
        {
            _ota->loop();

            // Publish OTA progress to backend every 5%
            // 4G: queue for yield callback instead of direct publish
            static int lastPublishedPct = -1;
            int pct = (int)_ota->getProgress();
            if (pct / 5 != lastPublishedPct && _mqtt && _mqtt->isConnected())
            {
                lastPublishedPct = pct / 5;
                if (_mqttQueueEnabled)
                {
                    // Build progress JSON and queue
                    JsonDocument doc;
                    doc["status"] = "downloading";
                    doc["progress"] = pct;
                    doc["version"] = _ota->getNewVersion();
                    doc["ts"] = (unsigned long)time(nullptr);
                    String payload;
                    serializeJson(doc, payload);
                    String topic = "v1/devices/" + _deviceToken + "/ota/response";
                    _enqueueMQTT(topic, payload, MQTTQoS::QOS_1, false);
                }
                else
                {
                    _publishOTAResponse("downloading", pct, _ota->getNewVersion());
                }
            }
        }
        // else // Tắt debug này - quá verbose
        // {
        //     static unsigned long lastDiagDebug = 0;
        //     if (millis() - lastDiagDebug > 60000) // Every 60 seconds
        //     {
        //         lastDiagDebug = millis();
        //         ZENO_LOG_CORE("[MAIN-LOOP] Diagnostics NOT running: ptr=%p, enabled=%d",
        //                       _diagnostics,
        //                       _diagnosticsEnabled);
        //     }
        // }
#endif // !ZENOPCB_DISABLE_OTA

#if defined(ESP32)
        // ⭐ Periodic Modbus telemetry publishing (runs during OTA — JSON build local, MQTT publish tự fail nếu disconnected)
        // ESP32-only per Plan 06-03 D-03 (Modbus subsystem stripped on ESP8266).
        static unsigned long lastTelemetryPrint = 0;
        auto &modbusBuffer = ModbusDataBuffer::getInstance();
        // Periodic publish: only when there is at least one publishable (VALID / fresh stale) value.
        // Instant publish (get_all, write callback) always goes through regardless.
        bool periodicDue = (millis() - lastTelemetryPrint > _modbusTelemetryInterval) && modbusBuffer.hasPublishableValues();
        if (!apActive && (periodicDue || modbusBuffer.isInstantPublishPending()))
        {
            lastTelemetryPrint = millis();
            modbusBuffer.clearInstantPublish(); // Clear instant-publish flag

            // Debug: Check active connections and registers (only log if active)
            int activeConn = ModbusConnectionManager::getInstance().getActiveConnectionCount();
            int regCount = ModbusDataBuffer::getInstance().getRegisterCount();
            if (activeConn > 0 || regCount > 0)
            {
                ZENO_LOG_CORE("Modbus Connections: %d, Monitors: %d", activeConn, regCount);
            }

            String telemetryJson = ModbusDataBuffer::getInstance().buildTelemetryJson();
            if (telemetryJson.length() > 2 && telemetryJson != "null") // skip empty "{}" or null
            {
                ZENO_LOG_CORE("📊 Modbus telemetry: %d bytes, %d monitors",
                              telemetryJson.length(), regCount);

                // ⭐ Chunk publish: split into max 50 keys per MQTT message
                // During 4G OTA: queue small payloads, skip chunking (queue can't hold multiple chunks)
                if (_mqtt && _mqtt->isConnected())
                {
                    const int CHUNK_SIZE = 50;

                    // ⭐ Phase 3 Plan 03-02 hot-path refactor: build the topic ONCE
                    // into a stack buffer (every branch below targets the same topic).
                    // THREAD-NOTE: _deviceToken is set once at provisioning and never
                    // mutated post-begin(); .c_str() is stable here. (RESEARCH.md Pitfall #3)
                    char chunkTopicBuf[128]; // matches MQTTClient::_brokerStable convention
                    int ctn = snprintf(chunkTopicBuf, sizeof(chunkTopicBuf),
                                       "v1/devices/%s/telemetry", _deviceToken.c_str());
                    if (ctn < 0 || (size_t)ctn >= sizeof(chunkTopicBuf))
                    {
                        ZENO_LOG_CORE("❌ Modbus chunk topic truncated");
                        // Fall through to the alarm check below — no publish attempted.
                    }
                    else
                    {

                    JsonDocument srcDoc;
                    DeserializationError err = deserializeJson(srcDoc, telemetryJson);

                    if (!err && srcDoc.is<JsonObject>())
                    {
                        JsonObject srcObj = srcDoc.as<JsonObject>();
                        int totalKeys = srcObj.size();

                        if (totalKeys <= CHUNK_SIZE)
                        {
                            // Small payload — send as-is or queue
                            if (_mqttQueueEnabled)
                            {
                                // 4G OTA active → queue for yield callback.
                                // Cold path: one-time String wrap at the queue boundary
                                // is acceptable per RESEARCH.md Risk R2 (QueuedMQTTMessage
                                // out of scope for Plan 03-02).
                                _enqueueMQTT(String(chunkTopicBuf), telemetryJson);
                                ZENO_LOG_CORE("📦 Modbus telemetry queued (%d keys) — will flush at OTA yield", totalKeys);
                            }
                            else
                            {
                                // Hot path — direct publish via char* overload from plan 03-01.
                                // `telemetryJson` itself is the unavoidable return of
                                // ModbusDataBuffer::buildTelemetryJson() (out-of-scope per plan);
                                // reuse its .c_str() — stable until next String mutation.
                                // QoS / retain mirror sendTelemetryJson → publishJson defaults
                                // (QOS_0, retain=false; verified mqtt/MQTTClient.cpp:447).
                                bool published = _mqtt->publish(chunkTopicBuf,
                                                                telemetryJson.c_str(),
                                                                MQTTQoS::QOS_0, false);
                                ZENO_LOG_CORE("%s Modbus telemetry published (%d keys)",
                                              published ? "✅" : "❌", totalKeys);
                            }
                        }
                        else if (!_mqttQueueEnabled)
                        {
                            // Large payload — split into chunks (only if not in queue mode)
                            int chunkCount = 0;
                            int keyIndex = 0;
                            int publishedChunks = 0;

                            JsonDocument chunkDoc;
                            for (JsonPair kv : srcObj)
                            {
                                chunkDoc[kv.key()] = kv.value();
                                keyIndex++;

                                if (keyIndex % CHUNK_SIZE == 0 || keyIndex == totalKeys)
                                {
                                    chunkCount++;

                                    // ⭐ Phase 3 Plan 03-02: serialize chunk into stack
                                    // buffer instead of String. RESEARCH.md A1: chunk JSON
                                    // is bounded by MQTT_MAX_PAYLOAD + CHUNK_SIZE=50 keys
                                    // (typical < 256 B); 512 = 2x headroom under CLAUDE.md.
                                    char chunkJsonBuf[512];
                                    size_t cn = serializeJson(chunkDoc, chunkJsonBuf,
                                                              sizeof(chunkJsonBuf));
                                    if (cn == 0 || cn >= sizeof(chunkJsonBuf))
                                    {
                                        ZENO_LOG_CORE("❌ Modbus chunk JSON truncated/empty (n=%u, cap=%u)",
                                                      (unsigned)cn, (unsigned)sizeof(chunkJsonBuf));
                                        chunkDoc.clear();
                                        continue; // skip this chunk; preserve loop progression
                                    }

                                    // Hot path — direct publish via char* overload.
                                    bool ok = _mqtt->publish(chunkTopicBuf, chunkJsonBuf,
                                                             MQTTQoS::QOS_0, false);
                                    if (ok)
                                        publishedChunks++;
                                    ZENO_LOG_CORE("%s Chunk %d/%d (%d keys, %u bytes)",
                                                  ok ? "✅" : "❌", chunkCount,
                                                  (totalKeys + CHUNK_SIZE - 1) / CHUNK_SIZE,
                                                  chunkDoc.size(), (unsigned)cn);

                                    chunkDoc.clear();
                                }
                            }
                            ZENO_LOG_CORE("📊 Telemetry: %d keys → %d chunks, %d published",
                                          totalKeys, chunkCount, publishedChunks);
                        }
                        else
                        {
                            // Large payload + queue mode → skip (can't queue multiple chunks)
                            ZENO_LOG_CORE("⏭️ Telemetry skipped (%d keys) — too large for 4G OTA queue", totalKeys);
                        }
                    }
                    else
                    {
                        // Fallback: parse failed
                        if (!_mqttQueueEnabled)
                        {
                            // Hot path — direct publish via char* overload.
                            bool published = _mqtt->publish(chunkTopicBuf,
                                                            telemetryJson.c_str(),
                                                            MQTTQoS::QOS_0, false);
                            ZENO_LOG_CORE("%s Modbus telemetry published (raw fallback)",
                                          published ? "✅" : "❌");
                        }
                    }

                    } // end topic-build success guard
                }

#if !defined(ZENOPCB_DISABLE_ALARM)
                // ⭐ Alarm: check rules against Modbus telemetry keys (mqtt_key numeric strings)
                if (_alarmEnabled && _alarmEngine && _alarmEngine->getRuleCount() > 0)
                {
                    _alarmEngine->checkAlarmsFromJson(telemetryJson);
                }
#endif // !ZENOPCB_DISABLE_ALARM
            }
        }
#endif  // ESP32 — Modbus periodic telemetry block

        // ⭐ Z Key telemetry publishing
        // When 4G OTA queue mode: ZKey publish goes through queue
        if (!apActive && _zKeysEnabled)
        {
            auto &zBuffer = ZKeyBuffer::getInstance();

            if (zBuffer.isPublishDue())
            {
                // Timer-only reset (NOT markPublished). Previously the call
                // was markPublished(), which also wiped every dirty flag —
                // meaning any `ZENO_WRITE(Zx, …)` the user did from `loop()`
                // between intervals got discarded before `_publishZKeyTelemetry()`
                // ran, so only values set INSIDE ZENO_READ_ALL ever reached
                // the cloud. markPublishTimer() preserves those values; the
                // dirty clear now happens in `_publishZKeyTelemetry()` after
                // a successful publish (it already calls clearDirtyFlags()).
                zBuffer.markPublishTimer();

                if (_zKeyReadCallback)
                    _zKeyReadCallback();

                bool shouldPublish = zBuffer.isInstantPublishPending() || zBuffer.hasDirtyKeys();

                if (shouldPublish)
                {
                    _publishZKeyTelemetry();
                }

#if !defined(ZENOPCB_DISABLE_ALARM)
                // Alarm check runs independently of dirty state:
                // condition may be met even if ZKey value hasn't changed.
                if (_alarmEnabled && _alarmEngine && _alarmEngine->getRuleCount() > 0)
                {
                    _checkAndPublishAlarms();
                }
#endif // !ZENOPCB_DISABLE_ALARM
            }
        }
    }

    // ============================================
    // Actions
    // ============================================

    void Zeno::startAPMode()
    {
#if !defined(ZENOPCB_DISABLE_PROVISIONING)
        if (_wifiProvisioning)
        {
            _wifiProvisioning->startAPMode();
            _setState(ZenoState::AP_MODE);
        }
#endif
    }

    void Zeno::stopAPMode()
    {
#if !defined(ZENOPCB_DISABLE_PROVISIONING)
        if (_wifiProvisioning)
        {
            _wifiProvisioning->stopAPMode();
        }
#endif
    }

    void Zeno::factoryReset()
    {
#if !defined(ZENOPCB_DISABLE_PROVISIONING)
        if (_wifiProvisioning)
        {
            _wifiProvisioning->factoryReset();
        }
#endif
    }

    void Zeno::reconnect()
    {
        // External network provider handles its own reconnection in loop()
        if (_networkProvider)
        {
            ZENO_LOG_CORE("Reconnect delegated to %s provider", _networkProvider->getName());
            return;
        }

        if (_wifiConfigured)
        {
            // Pattern H gate: WiFi.disconnect/begin unavailable on F4 Ethernet path.
#if defined(ESP32) || defined(ESP8266) || defined(ARDUINO_UNOR4_WIFI) || defined(STM32F1xx)
            WiFi.disconnect();
            delay(100);
            WiFi.begin(_wifiSSID.c_str(), _wifiPassword.c_str());
#else
            ZENO_LOG_CORE("⚠️ _reconnect: WiFi.h unavailable on this platform");
#endif
        }
    }

    // ============================================
    // Status
    // ============================================

    bool Zeno::isConnected() const
    {
        // Check external network provider first (4G, Ethernet, MultiConnect)
        if (_networkProvider)
        {
            return _networkProvider->isConnected();
        }
        // Pattern H gate: F4 default-Ethernet has no WiFi.h; in that branch a
        // provider is mandatory, so reaching here means not-connected.
#if defined(ESP32) || defined(ESP8266) || defined(ARDUINO_UNOR4_WIFI) || defined(STM32F1xx)
        return WiFi.status() == WL_CONNECTED;
#else
        return false;
#endif
    }

    bool Zeno::isConfigured() const
    {
#if !defined(ZENOPCB_DISABLE_PROVISIONING)
        if (_wifiProvisioning)
        {
            return _wifiProvisioning->isConfigured();
        }
#endif
        return _wifiConfigured;
    }

    bool Zeno::isAPMode() const
    {
#if !defined(ZENOPCB_DISABLE_PROVISIONING)
        if (_wifiProvisioning)
        {
            return _wifiProvisioning->isAPMode();
        }
#endif
        return false;
    }

    ZenoState Zeno::getState() const
    {
        return _state;
    }

    String Zeno::getActualConnectionType() const
    {
        // Return library-tracked actual connection type
        if (_actualConnectionType.length() > 0)
        {
            return _actualConnectionType;
        }
        // Fallback: determine from current state
        if (_networkProvider == nullptr)
        {
            return "WIFI";
        }
        return "ETHERNET"; // Default for external providers
    }

    String Zeno::getAPSSID() const
    {
#if !defined(ZENOPCB_DISABLE_PROVISIONING)
        if (_wifiProvisioning)
        {
            return _wifiProvisioning->getAPSSID();
        }
#endif
        return "";
    }

    String Zeno::getIP() const
    {
        // Pattern H gate: F4 default-Ethernet has no WiFi.h.
#if defined(ESP32) || defined(ESP8266) || defined(ARDUINO_UNOR4_WIFI) || defined(STM32F1xx)
        if (WiFi.status() == WL_CONNECTED)
        {
            return WiFi.localIP().toString();
        }
#endif
        return "";
    }

    DeviceConfig Zeno::getConfig() const
    {
#if !defined(ZENOPCB_DISABLE_PROVISIONING)
        if (_wifiProvisioning)
        {
            return _wifiProvisioning->getConfig();
        }
#endif
        DeviceConfig config;
        config.wifiSSID = _wifiSSID;
        config.deviceId = _deviceId;
        config.configured = _wifiConfigured;
        return config;
    }

    void Zeno::setDeviceCredentials(const String &deviceId, const String &token)
    {
        // Save credentials for MQTT topic building
        _deviceId = deviceId;
        _deviceToken = token;

        // ⭐ Auto-configure ZenoPCB MQTT defaults.
        // Users only need DEVICE_ID + DEVICE_TOKEN - no broker config needed.
        // Only apply defaults if broker not already explicitly set.
        //
        // Lookup precedence (Phase 5 D-14, layered):
        //   1. Runtime .mqtt(host, port) override — already populated _mqttBroker
        //      BEFORE this code runs (e.g. ZMG-01 / ZF-01 firmware).
        //   2. Compile-time -DZENOPCB_BROKER_HOST="..." build flag (advanced
        //      override; undocumented escape hatch per PROJECT.md).
        //   3. XOR-obfuscated getCloudBroker() default (defense-in-depth fallback).
        if (_mqttBroker.length() == 0)
        {
#ifdef ZENOPCB_BROKER_HOST
            _mqttBroker = ZENOPCB_BROKER_HOST;
#ifdef ZENOPCB_BROKER_PORT
            _mqttPort = ZENOPCB_BROKER_PORT;
#else
            _mqttPort = 1883;
#endif
#else
            _mqttBroker = getCloudBroker();
            _mqttPort = getCloudPort();
#endif
            _mqttTLS = false;
            _mqttEnabled = true;
        }
        // clientId = deviceId, username = token, password = "zenopcb"
        // (ZenoPCB platform default — do NOT change without confirmation
        // from the cloud-side auth owner; ZMG production firmware ships
        // with these exact defaults).
        if (_mqttClientId.length() == 0)
            _mqttClientId = deviceId;
        if (_mqttUsername.length() == 0)
            _mqttUsername = token;
        if (_mqttPassword.length() == 0)
            _mqttPassword = "zenopcb";

#if !defined(ZENOPCB_DISABLE_PROVISIONING)
        if (_wifiProvisioning)
        {
            _wifiProvisioning->setDeviceCredentials(deviceId, token);
        }
#endif
    }

    // ============================================
    // Internal Methods
    // ============================================

    void Zeno::_initProvisioning()
    {
#if defined(ZENOPCB_DISABLE_PROVISIONING)
        // Plan 07-06.6 MICRO_BASIC: WiFiProvisioning subsystem stripped at compile time.
        // _wifiProvisioning stays nullptr; all call sites guard on the same flag.
        _wifiProvisioning = nullptr;
        ZENO_LOG_CORE("[WARN] WiFiProvisioning subsystem disabled at compile time (ZENOPCB_DISABLE_PROVISIONING) — captive portal / NVS credentials not available on this build");
        return;
#else
        // Always create provisioning for button support — explicit HAL injection
        // (Plan 04-05 swaps the default-ctor bridge for `_hal` propagation).
        _wifiProvisioning = new WiFiProvisioning(_hal);

        // Forward device credentials that were set before begin()
        if (_deviceId.length() > 0 || _deviceToken.length() > 0)
        {
            _wifiProvisioning->setDeviceCredentials(_deviceId, _deviceToken);
        }

        // Skip WiFi auto-connect when using external network provider (4G, Ethernet)
        if (_networkProvider)
        {
            _wifiProvisioning->setSkipAutoWiFiConnect(true);
        }

        // ── MQTT connectivity test for /api/connect/wifi ──────────────────────
        // Called BEFORE saving config — WiFi is in WIFI_AP_STA and connected
        // Tests that the MQTT broker is reachable using device credentials.
        //
        // Pattern H gate (Plan 07-06.5): STM32F4 default-Ethernet build has no
        // WiFi.h equivalent, so the test/claim lambdas (which instantiate a
        // local WiFiClient) skip wiring on that platform — the provisioning
        // surface (web server, AP mode) is already a no-op stub there.
#if defined(ESP32) || defined(ESP8266) || defined(ARDUINO_UNOR4_WIFI) || defined(STM32F1xx)
        _wifiProvisioning->setMQTTTestCallback([this]() -> bool
                                               {
            // Phase 5 D-14 (layered): runtime override -> build-flag override -> XOR fallback.
#ifdef ZENOPCB_BROKER_HOST
            String broker = _mqttBroker.length() == 0 ? String(ZENOPCB_BROKER_HOST) : _mqttBroker;
#ifdef ZENOPCB_BROKER_PORT
            uint16_t port = (_mqttPort == 0) ? (uint16_t)ZENOPCB_BROKER_PORT : _mqttPort;
#else
            uint16_t port = (_mqttPort == 0) ? (uint16_t)1883 : _mqttPort;
#endif
#else
            String broker = _mqttBroker.length() == 0 ? getCloudBroker() : _mqttBroker;
            uint16_t port = (_mqttPort == 0) ? getCloudPort() : _mqttPort;
#endif
            String clientId = _mqttClientId.length() == 0 ? _deviceId : _mqttClientId;

            // No-leak rule (user feedback 2026-06-06): no broker host /
            // port / clientId fragment in serial — even an 8-char prefix
            // of the device ID is enough to correlate the device on cloud.
            ZENO_LOG_CORE("🔬 MQTT test starting");

            WiFiClient tempWiFiClient;
            ZenoPubSubClient tempMqtt(tempWiFiClient);
            tempMqtt.setServer(broker.c_str(), port);
            tempMqtt.setKeepAlive(10);
            tempMqtt.setSocketTimeout(8);

            bool ok = tempMqtt.connect(
                clientId.c_str(),
                _mqttUsername.c_str(),
                _mqttPassword.c_str()
            );

            if (ok)
            {
                ZENO_LOG_CORE("✅ MQTT test PASSED (state: %d)", tempMqtt.state());
                tempMqtt.disconnect();
            }
            else
            {
                ZENO_LOG_CORE("❌ MQTT test FAILED (state: %d)", tempMqtt.state());
            }
            tempWiFiClient.stop();
            return ok; });

        // ── Device claim callback ─────────────────────────────────────────────
        // Called after MQTT test passes. Publishes claim and waits for backend ack.
        // WiFi is still in AP_STA mode and connected when this is called.
        _wifiProvisioning->setClaimCallback([this](const String &userId, const String &deviceId, const String &token) -> bool
                                            {
            // Phase 5 D-14 (layered): runtime override -> build-flag override -> XOR fallback.
#ifdef ZENOPCB_BROKER_HOST
            String broker = _mqttBroker.length() == 0 ? String(ZENOPCB_BROKER_HOST) : _mqttBroker;
#ifdef ZENOPCB_BROKER_PORT
            uint16_t port = (_mqttPort == 0) ? (uint16_t)ZENOPCB_BROKER_PORT : _mqttPort;
#else
            uint16_t port = (_mqttPort == 0) ? (uint16_t)1883 : _mqttPort;
#endif
#else
            String broker = _mqttBroker.length() == 0 ? getCloudBroker() : _mqttBroker;
            uint16_t port = (_mqttPort == 0) ? getCloudPort() : _mqttPort;
#endif
            String clientId = _mqttClientId.length() == 0 ? _deviceId : _mqttClientId;
            // Add suffix to avoid clientId collision with the main MQTT client
            clientId += "_claim";

            ZENO_LOG_CORE("📋 Claim: connecting to %s:%d (clientId: %s)", broker.c_str(), port, clientId.c_str());

            WiFiClient claimWiFiClient;
            ZenoPubSubClient claimMqtt(claimWiFiClient);
            claimMqtt.setServer(broker.c_str(), port);
            claimMqtt.setKeepAlive(15);
            claimMqtt.setSocketTimeout(10);
            claimMqtt.setBufferSize(512);

            // Track ack received — file-scope static slot (s_claimAck*)
            // lets us pass a non-capturing lambda → plain function pointer.
            s_claimAckReceived = false;
            s_claimAckSuccess = false;

            // Set message callback BEFORE connect (non-capturing — required
            // for strict-conversion toolchains; Phase 7 Area E).
            claimMqtt.setCallback(+[](char* topic, byte* payload, unsigned int length) {
                (void)topic;
                String msg;
                msg.reserve(length);
                for (unsigned int i = 0; i < length; i++) {
                    msg += (char)payload[i];
                }
                ZENO_LOG_CORE("📋 Claim ack received: %s", msg.c_str());

                JsonDocument ackDoc;
                DeserializationError err = deserializeJson(ackDoc, msg);
                if (!err && ackDoc.containsKey("success")) {
                    s_claimAckSuccess = ackDoc["success"].as<bool>();
                } else {
                    // If can't parse, treat any ack as success
                    s_claimAckSuccess = true;
                }
                s_claimAckReceived = true;
            });

            // Connect
            bool connected = claimMqtt.connect(
                clientId.c_str(),
                _mqttUsername.c_str(),
                _mqttPassword.c_str()
            );

            if (!connected) {
                ZENO_LOG_CORE("❌ Claim: MQTT connect failed (state: %d)", claimMqtt.state());
                claimWiFiClient.stop();
                return false;
            }

            // Subscribe to ack topic
            String ackTopic = "v1/devices/" + String(_deviceToken) + "/claim/ack";
            bool subOK = claimMqtt.subscribe(ackTopic.c_str(), 1);
            ZENO_LOG_CORE("📋 Subscribed to ack topic: %s (ok: %d)", maskTopic(ackTopic).c_str(), subOK);

            // Build claim payload
            JsonDocument claimDoc;
            claimDoc["userId"] = userId;
            claimDoc["deviceId"] = deviceId;
            claimDoc["token"] = token;
            String claimPayload;
            serializeJson(claimDoc, claimPayload);

            ZENO_LOG_CORE("📋 Claim payload: (hidden - contains token)");

            // Publish claim
            String claimTopic = "v1/devices/" + String(_deviceToken) + "/claim";
            bool pubOK = claimMqtt.publish(claimTopic.c_str(), claimPayload.c_str(), false);
            ZENO_LOG_CORE("📋 Published claim to: %s (ok: %d)", maskTopic(claimTopic).c_str(), pubOK);

            if (!pubOK) {
                ZENO_LOG_CORE("❌ Claim: publish failed");
                claimMqtt.disconnect();
                claimWiFiClient.stop();
                return false;
            }

            // Wait for ack with retry — total 30s, retry publish every 8s
            unsigned long startWait = millis();
            unsigned long lastPublishTime = millis();
            const unsigned long CLAIM_TIMEOUT_MS = 30000;
            const unsigned long CLAIM_RETRY_INTERVAL_MS = 8000;
            int claimRetry = 0;
            const int CLAIM_MAX_RETRIES = 3;
            ZENO_LOG_CORE("⏳ Waiting for claim ack (timeout: %lums, max retries: %d)...", CLAIM_TIMEOUT_MS, CLAIM_MAX_RETRIES);

            while (!s_claimAckReceived && (millis() - startWait < CLAIM_TIMEOUT_MS)) {
                claimMqtt.loop();
                yield();
                // Retry publish if no ack after CLAIM_RETRY_INTERVAL_MS
                if (!s_claimAckReceived && claimRetry < CLAIM_MAX_RETRIES &&
                    (millis() - lastPublishTime >= CLAIM_RETRY_INTERVAL_MS)) {
                    claimRetry++;
                    ZENO_LOG_CORE("📋 Claim retry #%d (elapsed: %lums)...", claimRetry, millis() - startWait);
                    claimMqtt.publish(claimTopic.c_str(), claimPayload.c_str(), false);
                    lastPublishTime = millis();
                }
            }

            // Cleanup
            claimMqtt.unsubscribe(ackTopic.c_str());
            claimMqtt.disconnect();
            claimWiFiClient.stop();

            if (s_claimAckReceived) {
                ZENO_LOG_CORE("📋 Claim result: %s (took %lums)", s_claimAckSuccess ? "SUCCESS" : "REJECTED", millis() - startWait);
                return s_claimAckSuccess;
            } else {
                ZENO_LOG_CORE("❌ Claim: timeout after %lums — no ack received", CLAIM_TIMEOUT_MS);
                return false;
            } });
#endif // ESP32 || ESP8266 || ARDUINO_UNOR4_WIFI || STM32F1 — MQTT-test / claim callbacks

        _wifiProvisioning->begin(_provConfig);

        // Set device info for API responses
        _wifiProvisioning->setDeviceInfo(_deviceInfo);

        // Wire up callbacks
        _wifiProvisioning->onWiFiConnected([this]()
                                           {
            _setState(ZenoState::CONNECTED);
            if (_connectedCallback) {
                _connectedCallback();
            } });

        _wifiProvisioning->onConfigReceived([this](const DeviceConfig &config)
                                            {
            if (_configuredCallback) {
                _configuredCallback(config);
            } });

        _wifiProvisioning->onError([this](const String &error)
                                   {
            if (_errorCallback) {
                _errorCallback(error);
            } });

        _wifiProvisioning->onStateChange([this](ProvisioningState state)
                                         {
            // Map provisioning state to ZenoState
            switch (state) {
                case ProvisioningState::AP_MODE_ACTIVE:
                    _setState(ZenoState::AP_MODE);
                    break;
                case ProvisioningState::CONNECTING_WIFI:
                    _setState(ZenoState::CONNECTING);
                    break;
                case ProvisioningState::CONNECTED:
                    _setState(ZenoState::CONNECTED);
                    break;
                case ProvisioningState::FAILED:
                    _setState(ZenoState::ERROR);
                    break;
                default:
                    break;
            } });

        ZENO_LOG_CORE("AP SSID: %s\n", _wifiProvisioning->getAPSSID().c_str());
        ZENO_LOG_CORE("AP Password: %s\n", _provConfig.apPassword.c_str());
#endif // !ZENOPCB_DISABLE_PROVISIONING
    }

    void Zeno::_initMQTT()
    {
        // Use default cloud broker if user did not call .mqtt()
        // Phase 5 D-14 (layered): runtime override -> build-flag override -> XOR fallback.
        if (!_mqttEnabled || _mqttBroker.length() == 0)
        {
#ifdef ZENOPCB_BROKER_HOST
            _mqttBroker = ZENOPCB_BROKER_HOST;
#ifdef ZENOPCB_BROKER_PORT
            _mqttPort = ZENOPCB_BROKER_PORT;
#else
            _mqttPort = 1883;
#endif
#else
            _mqttBroker = getCloudBroker();
            _mqttPort = getCloudPort();
#endif
            _mqttEnabled = true;
        }

        ZENO_LOG_CORE("🔧 _initMQTT() called");

        _mqtt = new ZenoPCBMQTT();
        ZENO_LOG_CORE("✅ ZenoPCBMQTT object created");

        // Configure broker
        if (_mqttTLS)
        {
#ifdef ZENOPCB_ENABLE_TLS
            if (_tlsEnabled && _wifiClientSecure)
            {
                _mqtt->brokerTLS(_mqttBroker, _mqttPort);
                // Phase 7 D-27 — if user pinned a PEM root CA via setRootCA(),
                // use setCACert() on ESP32 for verified TLS. Otherwise fall
                // back to setInsecure() dev-mode behaviour. ESP8266 BearSSL
                // needs a parsed BearSSL::X509List (deferred — logs WARN
                // then falls back to setInsecure for now).
                if (_rootCA != nullptr)
                {
#if defined(ESP32)
                    _wifiClientSecure->setCACert(_rootCA);
                    ZENO_LOG_CORE("TLS root CA pinned via setCACert (ESP32)");
#elif defined(ESP8266)
                    ZENO_LOG_CORE("[WARN] ESP8266 BearSSL root CA pinning requires user-constructed BearSSL::X509List — falling back to setInsecure (D-27 note)");
                    _wifiClientSecure->setInsecure();
#else
                    ZENO_LOG_CORE("[WARN] Root CA pinning not implemented on this platform — setInsecure fallback");
                    _wifiClientSecure->setInsecure();
#endif
                }
                else
                {
                    _wifiClientSecure->setInsecure(); // Skip cert verification (dev mode)
                }
                _mqtt->setClient(_wifiClientSecure);
            }
            else
            {
                ZENO_LOG_CORE("ERROR: TLS not enabled. Call .enableTLS() before .mqttTLS()");
                return;
            }
#else
            ZENO_LOG_CORE("ERROR: TLS not available. Add -DZENOPCB_ENABLE_TLS to build_flags");
            return;
#endif
        }
        else
        {
            ZENO_LOG_CORE("📡 Setting MQTT broker (non-TLS)");
            _mqtt->broker(_mqttBroker, _mqttPort);

            // 4G needs longer timeouts: TCP handshake on cellular is much slower
            // ⚠️ FIX: keepAlive() and socketTimeout() now directly set ZenoPubSubClient params
            // (no longer call full setConfig), so order doesn't matter
            if (_networkProvider && strcmp(_networkProvider->getName(), "4G") == 0)
            {
                _mqtt->keepAlive(MQTT_CELLULAR_KEEPALIVE);
                _mqtt->socketTimeout(MQTT_CELLULAR_SOCKET_TIMEOUT);
                ZENO_LOG_CORE("⚡ 4G mode: keepAlive=%ds, socketTimeout=%ds",
                              MQTT_CELLULAR_KEEPALIVE, MQTT_CELLULAR_SOCKET_TIMEOUT);
            }
        }

        // Set network check callback for auto-reconnect
        // NOTE: AP mode guard prevents MQTT blocking TCP connect (28s timeout) while
        // provisioning web server needs CPU. Without this, button IO-0 becomes unresponsive.
        if (_networkProvider)
        {
            _mqtt->setNetworkCheck([this]()
                                   { return _networkProvider && _networkProvider->isConnected() && !isAPMode(); });
        }
        else
        {
            // Pattern H gate: F4 default-Ethernet has no WiFi.h, so the
            // network-check lambda there returns the AP-mode guard only
            // (which on F4 is permanently false since provisioning is a
            // no-op stub) — effectively disabling MQTT auto-reconnect
            // unless an external network provider is wired in.
#if defined(ESP32) || defined(ESP8266) || defined(ARDUINO_UNOR4_WIFI) || defined(STM32F1xx)
            _mqtt->setNetworkCheck([this]()
                                   { return WiFi.status() == WL_CONNECTED && !isAPMode(); });
#else
            _mqtt->setNetworkCheck([this]()
                                   { return !isAPMode(); });
#endif
        }
        ZENO_LOG_CORE("✅ MQTT initialized successfully");

        // Set credentials if provided
        if (_mqttUsername.length() > 0)
        {
            _mqtt->credentials(_mqttUsername, _mqttPassword);
        }

        // Set device token for topic generation (v1 protocol uses token, not device ID)
        // Topic format: v1/devices/{token}/telemetry
        if (_mqttUsername.length() > 0)
        {
            _mqtt->deviceId(_mqttUsername); // Use token as deviceId for topic building
        }

        // LWT is auto-configured in ZenoPCBMQTT::begin() using buildTopic("status")
        // → Same topic as sendStatus("online"/"offline") = v1/devices/{token}/status
        // → QoS 1 + retain=true ensures broker replaces retained "online" with "offline"

        // Set MQTT client ID if provided
        if (_mqttClientId.length() > 0)
        {
            _mqtt->clientId(_mqttClientId);
        }

        // ⚠️ CRITICAL: Initialize MQTT (calls setConfig/setServer) BEFORE setClient()
        _mqtt->begin();

        // ⚠️ NOW set network client AFTER setServer() to ensure ZenoPubSubClient uses it
        // ZenoPubSubClient may reset internal state when setServer() is called
        if (_mqttTLS)
        {
#ifdef ZENOPCB_ENABLE_TLS
            if (_tlsEnabled && _wifiClientSecure)
            {
                _mqtt->setClient(_wifiClientSecure);
                ZENO_LOG_CORE("✅ TLS Client set for MQTT (after setConfig)");
            }
#endif
        }
        else
        {
            // Use network provider client if available, otherwise WiFi.
            // Pattern H gate: STM32F4 default-Ethernet has no WiFi.h /
            // `WiFiClient`, so the fallback path is dropped and provider
            // is mandatory there (see ZenoPCB.h member declaration guard).
            if (_networkProvider)
            {
                Client *provClient = _networkProvider->getClient();
                if (provClient)
                {
                    _mqtt->setClient(provClient);
                    ZENO_LOG_CORE("✅ %s Client set for MQTT (after begin, final)", _networkProvider->getName());
                }
                else
                {
#if defined(ESP32) || defined(ESP8266) || defined(ARDUINO_UNOR4_WIFI) || defined(STM32F1xx)
                    _mqtt->setClient(&_wifiClient);
                    ZENO_LOG_CORE("⚠️ Provider client NULL, fallback to WiFiClient");
#else
                    ZENO_LOG_CORE("⚠️ Provider client NULL and no WiFiClient fallback on this platform");
#endif
                }
            }
            else
            {
#if defined(ESP32) || defined(ESP8266) || defined(ARDUINO_UNOR4_WIFI) || defined(STM32F1xx)
                _mqtt->setClient(&_wifiClient);
                ZENO_LOG_CORE("✅ WiFiClient set for MQTT");
#else
                ZENO_LOG_CORE("⚠️ No network provider AND no WiFiClient on this platform — MQTT will not start");
#endif
            }
        }

        ZENO_LOG_CORE("MQTT configured - Broker: %s:%d", _mqttBroker.c_str(), _mqttPort);
    }

    void Zeno::_initStorage()
    {
#if defined(ZENOPCB_DISABLE_STORAGE)
        if (_storageEnabled)
        {
            ZENO_LOG_CORE("[WARN] Storage subsystem disabled at compile time (ZENOPCB_DISABLE_STORAGE) — enableStorage() is a no-op on this build");
        }
        return;
#else
        ZENO_LOG_VERBOSE("_initStorage() called, enabled=%s", _storageEnabled ? "true" : "false");

        if (!_storageEnabled)
        {
            return;
        }

        ZENO_LOG_CORE("Initializing storage module...");

        // Initialize ConfigMessageHandler (singleton)
        ConfigMessageHandler &handler = ConfigMessageHandler::getInstance();

        if (!handler.begin())
        {
            ZENO_LOG_CORE("ERROR: Failed to initialize storage");
            return;
        }

        // Wire up storage callbacks
        if (_configCreatedCallback)
        {
            handler.onConfigCreated(_configCreatedCallback);
        }

        if (_configUpdatedCallback)
        {
            handler.onConfigUpdated(_configUpdatedCallback);
        }

        if (_configDeletedCallback)
        {
            handler.onConfigDeleted(_configDeletedCallback);
        }

        ZENO_LOG_CORE("Storage module initialized (subscriptions handled by unified callback)");
#endif // !ZENOPCB_DISABLE_STORAGE
    }

    void Zeno::_handleConnectionConfigMessage(const String &topic, const String &payload)
    {
#if defined(ZENOPCB_DISABLE_STORAGE)
        (void)topic; (void)payload;
        return;
#else
        ZENO_LOG_VERBOSE("Config message: %s (%d bytes)", maskTopic(topic).c_str(), payload.length());

        ZENO_LOG_CORE("Config message received on: %s", maskTopic(topic).c_str());

        ConfigMessageHandler &handler = ConfigMessageHandler::getInstance();
        HandleResult result = handler.handleMessage(topic, payload);

        // Centralized ACK
        char actionStr[2] = {static_cast<char>(result.action), '\0'};
        _publishAck("config", actionStr, result.shortId.c_str(), result.success,
                    result.processingMs, result.errorMessage);

        if (result.success)
        {
            ZENO_LOG_CORE("Config %c for %s - OK (%dms)",
                          static_cast<char>(result.action),
                          result.shortId.c_str(),
                          result.processingMs);
        }
        else
        {
            ZENO_LOG_CORE("Config error: %s", result.errorMessage.c_str());
            if (_errorCallback)
            {
                _errorCallback("Config: " + result.errorMessage);
            }
        }
#endif // !ZENOPCB_DISABLE_STORAGE
    }

    void Zeno::_initDataMonitorStorage()
    {
#if defined(ZENOPCB_DISABLE_STORAGE)
        return;
#else
        ZENO_LOG_VERBOSE("_initDataMonitorStorage() called, enabled=%s, mqtt=%s",
                         _dataMonitorStorageEnabled ? "true" : "false",
                         _mqtt ? "SET" : "NULL");

        if (!_dataMonitorStorageEnabled)
        {
            return;
        }

        ZENO_LOG_CORE("Initializing data monitor storage module...");

        // Initialize DataMonitorMessageHandler (singleton)
        DataMonitorMessageHandler &handler = DataMonitorMessageHandler::getInstance();

        if (!handler.begin())
        {
            ZENO_LOG_CORE("ERROR: Failed to initialize data monitor storage");
            return;
        }

        // Wire up storage callbacks
        if (_dataMonitorCreatedCallback)
        {
            handler.onMonitorCreated(_dataMonitorCreatedCallback);
        }

        if (_dataMonitorUpdatedCallback)
        {
            handler.onMonitorUpdated(_dataMonitorUpdatedCallback);
        }

        if (_dataMonitorDeletedCallback)
        {
            handler.onMonitorDeleted(_dataMonitorDeletedCallback);
        }

        if (_dataMonitorToggledCallback)
        {
            handler.onMonitorToggled(_dataMonitorToggledCallback);
        }

        ZENO_LOG_CORE("Data monitor storage module initialized (subscriptions handled by unified callback)");
#endif // !ZENOPCB_DISABLE_STORAGE
    }

    void Zeno::_handleDataMonitorMessage(const String &topic, const String &payload)
    {
#if defined(ZENOPCB_DISABLE_STORAGE)
        (void)topic; (void)payload;
        return;
#else
        ZENO_LOG_VERBOSE("DataMonitor message: %s (%d bytes)", maskTopic(topic).c_str(), payload.length());

        ZENO_LOG_CORE("Data monitor message received on: %s", maskTopic(topic).c_str());

        DataMonitorMessageHandler &handler = DataMonitorMessageHandler::getInstance();
        DataMonitorHandleResult result = handler.handleMessage(topic, payload);

        // Centralized ACK
        char actionStr[2] = {static_cast<char>(result.action), '\0'};
        _publishAck("monitor", actionStr, result.mqttKey.c_str(), result.success,
                    result.processingMs, result.errorMessage);

        if (result.success)
        {
            ZENO_LOG_CORE("Data monitor %c for %s - OK (%dms)",
                          static_cast<char>(result.action),
                          result.mqttKey.c_str(),
                          result.processingMs);
        }
        else
        {
            ZENO_LOG_CORE("Data monitor error: %s", result.errorMessage.c_str());
            if (_errorCallback)
            {
                _errorCallback("DataMonitor: " + result.errorMessage);
            }
        }
#endif // !ZENOPCB_DISABLE_STORAGE
    }

    // ============================================
    // Diagnostics Management
    // ============================================

    Zeno &Zeno::enableDiagnostics(uint32_t intervalMs)
    {
        _diagnosticsEnabled = true;
        _diagnosticsIntervalMs = intervalMs;

#if !defined(ZENOPCB_DISABLE_DIAGNOSTICS)
        if (_diagnostics != nullptr)
        {
            _diagnostics->setInterval(intervalMs);
        }
#endif

        ZENO_LOG_CORE("[DIAG] Diagnostics enabled with interval: %lu ms", intervalMs);
        return *this;
    }

    Zeno &Zeno::setConnectionType(const char *type)
    {
#if !defined(ZENOPCB_DISABLE_DIAGNOSTICS)
        if (_diagnostics != nullptr)
        {
            // Use library-tracked actual connection type if available
            String actualType = String(type);
            if (_actualConnectionType.length() > 0)
            {
                // Library đã track actual connection (from provisioning switch)
                actualType = _actualConnectionType;
            }
            else if (_networkProvider == nullptr)
            {
                // No provider and no tracked type → default WiFi
                actualType = "WIFI";
            }
            _diagnostics->setConnectionType(actualType);
        }
#else
        (void)type;
#endif
        return *this;
    }

    Zeno &Zeno::setDiagnosticsInterval(uint32_t intervalMs)
    {
#if !defined(ZENOPCB_DISABLE_DIAGNOSTICS)
        if (_diagnostics != nullptr)
        {
            _diagnostics->setInterval(intervalMs);
        }
#else
        (void)intervalMs;
#endif
        return *this;
    }

    Zeno &Zeno::enablePassiveDiagnostics(bool enable)
    {
#if !defined(ZENOPCB_DISABLE_DIAGNOSTICS)
        if (_diagnostics != nullptr)
        {
            _diagnostics->enablePassive(enable);
        }
#else
        (void)enable;
#endif
        return *this;
    }

    Zeno &Zeno::setDiagnosticsMaxRetries(uint8_t maxRetries)
    {
#if !defined(ZENOPCB_DISABLE_DIAGNOSTICS)
        if (_diagnostics != nullptr)
        {
            _diagnostics->setMaxRetries(maxRetries);
        }
#else
        (void)maxRetries;
#endif
        return *this;
    }

    Zeno &Zeno::onDiagnosticsRequest(std::function<void(const String &requestId)> callback)
    {
        _diagnosticsRequestCallback = callback;
#if !defined(ZENOPCB_DISABLE_DIAGNOSTICS)
        if (_diagnostics != nullptr)
        {
            _diagnostics->onRequest(callback);
        }
#endif
        return *this;
    }

    bool Zeno::sendDiagnosticsNow()
    {
#if !defined(ZENOPCB_DISABLE_DIAGNOSTICS)
        if (_diagnostics != nullptr)
        {
            return _diagnostics->sendNow();
        }
#endif
        return false;
    }

    // ============================================
    // Z Key System Implementation
    // ============================================

    Zeno &Zeno::enableZKeys()
    {
        _zKeysEnabled = true;
        ZENO_LOG_CORE("Z Key system enabled");
        return *this;
    }

    Zeno &Zeno::set(ZKey key, int32_t value)
    {
        if (_zKeysEnabled)
        {
            ZKeyBuffer::getInstance().set(key, value);
        }
        return *this;
    }

    Zeno &Zeno::set(ZKey key, float value)
    {
        if (_zKeysEnabled)
        {
            ZKeyBuffer::getInstance().set(key, value);
        }
        return *this;
    }

    Zeno &Zeno::set(ZKey key, const String &value)
    {
        if (_zKeysEnabled)
        {
            ZKeyBuffer::getInstance().set(key, value);
        }
        return *this;
    }

    Zeno &Zeno::set(ZKey key, const char *value)
    {
        if (_zKeysEnabled)
        {
            ZKeyBuffer::getInstance().set(key, value);
        }
        return *this;
    }

    Zeno &Zeno::set(ZKey key, bool value)
    {
        if (_zKeysEnabled)
        {
            ZKeyBuffer::getInstance().set(key, value);
        }
        return *this;
    }

    ZValue Zeno::get(ZKey key) const
    {
        return ZKeyBuffer::getInstance().get(key);
    }

    int32_t Zeno::getInt(ZKey key, int32_t defaultVal) const
    {
        return ZKeyBuffer::getInstance().getInt(key, defaultVal);
    }

    float Zeno::getFloat(ZKey key, float defaultVal) const
    {
        return ZKeyBuffer::getInstance().getFloat(key, defaultVal);
    }

    String Zeno::getString(ZKey key, const String &defaultVal) const
    {
        return ZKeyBuffer::getInstance().getString(key, defaultVal);
    }

    bool Zeno::getBool(ZKey key, bool defaultVal) const
    {
        return ZKeyBuffer::getInstance().getBool(key, defaultVal);
    }

    Zeno &Zeno::onZKeyChange(ZKey key, ZKeyChangeCallback callback)
    {
        ZKeyBuffer::getInstance().onChange(key, callback);
        return *this;
    }

    Zeno &Zeno::onAnyZKeyChange(ZKeyChangeCallback callback)
    {
        ZKeyBuffer::getInstance().onAnyChange(callback);
        return *this;
    }

    Zeno &Zeno::onZKeyRead(std::function<void()> callback)
    {
        _zKeyReadCallback = callback;
        return *this;
    }

    Zeno &Zeno::setModbusTelemetryInterval(uint32_t intervalMs)
    {
        _modbusTelemetryInterval = max(intervalMs, (uint32_t)1000);
        return *this;
    }

    Zeno &Zeno::setZPublishInterval(uint32_t intervalMs)
    {
        ZKeyBuffer::getInstance().setPublishInterval(intervalMs);
        return *this;
    }

    Zeno &Zeno::setZInstantPublish(bool enable)
    {
        ZKeyBuffer::getInstance().setInstantPublish(enable);
        return *this;
    }

    void Zeno::_publishZKeyTelemetry()
    {
        if (!_zKeysEnabled || !_mqtt || !_mqtt->isConnected())
            return;

        auto &buffer = ZKeyBuffer::getInstance();

        if (!buffer.hasDirtyKeys())
            return;

        // ⭐ Phase 3 Plan 03-02 hot-path refactor: build the Z-Key payload + topic
        // into stack buffers instead of three heap-allocated Strings per publish.
        // Pattern from .planning/phases/03-internal-tech-debt-cleanup/03-RESEARCH.md
        // Code Example 2; depends on the const char* publish overloads added in 03-01.

        // 1. Build JSON into a stack buffer via ArduinoJson serializeJson(doc, char*, size).
        //    THREAD-NOTE: ZKeyBuffer instance + JsonDocument are stack-scoped here;
        //    no cross-task aliasing.
        JsonDocument doc;
        buffer.mergeIntoJson(doc);
        char zJsonBuf[512]; // RESEARCH.md A1: typical < 256 B; 512 = 2x headroom; well under CLAUDE.md 1 KB stack limit
        size_t n = serializeJson(doc, zJsonBuf, sizeof(zJsonBuf));
        if (n == 0 || n >= sizeof(zJsonBuf))
        {
            ZENO_LOG_CORE("❌ ZKey JSON truncated/empty (n=%u, cap=%u)",
                          (unsigned)n, (unsigned)sizeof(zJsonBuf));
            return;
        }
        if (n <= 2) // "{}" — nothing dirty after merge; preserve existing length > 2 guard
            return;

        // 2. Build topic into stack buffer.
        //    THREAD-NOTE: _deviceToken is set once at provisioning (Zeno::setDeviceCredentials
        //    / NVS first-boot) and never mutated post-begin(). .c_str() is stable for the
        //    lifetime of this call. (RESEARCH.md Pitfall #3)
        char topicBuf[128]; // longest topic ~53 B; 128 matches MQTTClient::_brokerStable convention
        int tn = snprintf(topicBuf, sizeof(topicBuf), "v1/devices/%s/telemetry",
                          _deviceToken.c_str());
        if (tn < 0 || (size_t)tn >= sizeof(topicBuf))
        {
            ZENO_LOG_CORE("❌ ZKey topic truncated");
            return;
        }

        // ⭐ 4G OTA: queue instead of direct publish.
        // Cold path (OTA active). One-time String alloc at the queue boundary is
        // acceptable per RESEARCH.md Risk R2 — the QueuedMQTTMessage struct still
        // takes const String& and is explicitly out of scope for Plan 03-02.
        if (_mqttQueueEnabled)
        {
            _enqueueMQTT(String(topicBuf), String(zJsonBuf), MQTTQoS::QOS_1, false);
            ZENO_LOG_CORE("📦 ZKey queued: %s — will flush at OTA yield", zJsonBuf);
            buffer.clearDirtyFlags();
            buffer.markPublished();
            return;
        }

        // Hot path — direct publish via new char* overload from plan 03-01
        // (Pitfall #2: must NOT use .c_str() round-trip into the String overload).
        bool published = _mqtt->publish(topicBuf, zJsonBuf, MQTTQoS::QOS_1, false);

        if (published)
        {
            ZENO_LOG_CORE("📤 ZKey: %s", zJsonBuf);
            buffer.clearDirtyFlags();
            buffer.markPublished();
        }
        else
        {
            ZENO_LOG_CORE("❌ ZKey publish FAILED");
        }
    }

    void Zeno::_initDiagnostics()
    {
#if defined(ZENOPCB_DISABLE_DIAGNOSTICS)
        // Plan 07-06.6 MICRO_BASIC: Diagnostics subsystem stripped at compile time.
        if (_diagnosticsEnabled)
        {
            ZENO_LOG_CORE("[WARN] Diagnostics subsystem disabled at compile time (ZENOPCB_DISABLE_DIAGNOSTICS) — enableDiagnostics() is a no-op on this build");
        }
        return;
#else
        ZENO_LOG_CORE("[DIAG-INIT] Checking: enabled=%d, mqtt=%p",
                      _diagnosticsEnabled,
                      _mqtt);

        if (!_diagnosticsEnabled || _mqtt == nullptr)
        {
            ZENO_LOG_CORE("[DIAG-INIT] SKIP: Diagnostics not enabled or MQTT null");
            return;
        }

        ZENO_LOG_CORE("[DIAG] Initializing diagnostics module");

        // Create diagnostics instance
        _diagnostics = new ZenoPCBDiagnostics(&_deviceInfo, _mqtt);

        if (_diagnostics == nullptr)
        {
            ZENO_LOG_CORE("[DIAG] ERROR: Failed to create diagnostics instance");
            return;
        }

        // Begin with config derived from enableDiagnostics() call
        DiagnosticsConfig config;
        config.interval = (_diagnosticsIntervalMs > 0) ? _diagnosticsIntervalMs : 600000;
        config.passiveEnabled = true;
        config.maxRetries = 2;

        // Use actual connection type if library tracked it, otherwise derive from deviceInfo
        if (_actualConnectionType.length() > 0)
        {
            if (_actualConnectionType == "WIFI")
                config.connectionType = DiagnosticsConnectionType::WIFI;
            else if (_actualConnectionType == "ETHERNET")
                config.connectionType = DiagnosticsConnectionType::ETHERNET;
            else if (_actualConnectionType == "4G")
                config.connectionType = DiagnosticsConnectionType::CELLULAR_4G;
            ZENO_LOG_CORE("[DIAG] Using tracked connectionType: %s", _actualConnectionType.c_str());
        }
        else
        {
            // Fallback: derive from deviceInfo.supportedConnections bitmask
            if (_deviceInfo.supportsWiFi())
                config.connectionType = DiagnosticsConnectionType::WIFI;
            else if (_deviceInfo.supportsEthernet())
                config.connectionType = DiagnosticsConnectionType::ETHERNET;
            else if (_deviceInfo.supportsCellular())
                config.connectionType = DiagnosticsConnectionType::CELLULAR_4G;
        }

        _diagnostics->begin(config);

        // Pass network provider for cellular diagnostics (IP, IMEI, signal, operator)
        if (_networkProvider != nullptr)
        {
            _diagnostics->setNetworkProvider(_networkProvider);
        }

        // Set callback if registered
        if (_diagnosticsRequestCallback)
        {
            _diagnostics->onRequest(_diagnosticsRequestCallback);
        }

        ZENO_LOG_CORE("[DIAG] Diagnostics module initialized (subscription handled by unified callback)");
#endif // !ZENOPCB_DISABLE_DIAGNOSTICS
    }

    void Zeno::_handleDiagnosticsRequest(const String &topic, const String &payload)
    {
#if defined(ZENOPCB_DISABLE_DIAGNOSTICS)
        (void)topic; (void)payload;
        return;
#else
        ZENO_LOG_CORE("[DIAG] Received diagnostics request: %s", payload.c_str());

        if (_diagnostics != nullptr)
        {
            _diagnostics->handleRequest(payload);
        }
#endif // !ZENOPCB_DISABLE_DIAGNOSTICS
    }

    // ============================================
    // Schedule Management
    // ============================================

    Zeno &Zeno::enableSchedule()
    {
        _scheduleEnabled = true;
        ZENO_LOG_CORE("Schedule module enabled");

        // Initialize in begin() after WiFi connected
        return *this;
    }

    void Zeno::_initSchedule()
    {
        // Phase 7 Plan 07-06.6 — ZENOPCB_MICRO_BASIC profile compile-strip.
        // When `-DZENOPCB_DISABLE_SCHEDULE` is set, the entire Schedule subsystem
        // is gone (TU guards in schedule/*.cpp produce no link symbols). The body
        // below references those symbols, so it must be guarded too. The builder
        // method `enableSchedule()` is kept (Pattern F: fluent-chain preserved)
        // but resolves to a silent no-op + one-time warn here.
#if defined(ZENOPCB_DISABLE_SCHEDULE)
        if (_scheduleEnabled)
        {
            ZENO_LOG_CORE("[WARN] Schedule subsystem disabled at compile time (ZENOPCB_DISABLE_SCHEDULE) — enableSchedule() is a no-op on this build");
        }
        return;
#else
        if (!_scheduleEnabled)
        {
            return;
        }

        ZENO_LOG_CORE("Initializing Schedule module...");

        // Ensure LittleFS is mounted before ScheduleStorage uses it
        // (may already be initialized by _initStorage, safe to call again)
        if (!LittleFSManager::initialize())
        {
            ZENO_LOG_CORE("⚠️ LittleFS init failed — schedules will not persist");
        }

        // Create schedule executor
        if (!_scheduleExecutor)
        {
            _scheduleExecutor = new ScheduleExecutor();
        }

        // Initialize executor (loads schedules from LittleFS)
        if (!_scheduleExecutor->begin())
        {
            ZENO_LOG_CORE("❌ Failed to initialize Schedule executor");
            return;
        }

        // Register callbacks from message handler to executor
        ScheduleMessageHandler &msgHandler = ScheduleMessageHandler::getInstance();

        msgHandler.onScheduleCreated([this](const ScheduleConfig &config)
                                     {
            ZENO_LOG_CORE("Schedule created: %s", config.id);
            if (_scheduleExecutor) {
                _scheduleExecutor->addOrUpdateSchedule(config);
            }
            if (_scheduleCreatedCallback) {
                _scheduleCreatedCallback(config);
            } });

        msgHandler.onScheduleUpdated([this](const ScheduleConfig &config)
                                     {
            ZENO_LOG_CORE("Schedule updated: %s", config.id);
            if (_scheduleExecutor) {
                _scheduleExecutor->addOrUpdateSchedule(config);
            }
            if (_scheduleUpdatedCallback) {
                _scheduleUpdatedCallback(config);
            } });

        msgHandler.onScheduleDeleted([this](const String &scheduleId)
                                     {
            ZENO_LOG_CORE("Schedule deleted: %s", scheduleId.c_str());
            if (_scheduleExecutor) {
                _scheduleExecutor->removeSchedule(scheduleId);
            }
            if (_scheduleDeletedCallback) {
                _scheduleDeletedCallback(scheduleId);
            } });

        msgHandler.onScheduleSynced([this](uint8_t count)
                                    {
            ZENO_LOG_CORE("Schedules synced: %d schedules", count);
            if (_scheduleExecutor) {
                _scheduleExecutor->reloadSchedules();
            }
            if (_scheduleSyncedCallback) {
                _scheduleSyncedCallback(count);
            } });

        // Register executor callbacks
        if (_scheduleExecutor)
        {
            _scheduleExecutor->onScheduleExecuted([this](const String &id, ExecutionStatus status,
                                                         int64_t value, const String &error)
                                                  {
                ZENO_LOG_CORE("Schedule %s executed: %s (value=%lld)", 
                             id.c_str(), executionStatusToString(status), value);
                
                // Publish execution report to MQTT
                // Skip for INTERVAL schedules (fires too frequently — cloud doesn't need every tick)
                ScheduleType sType = _scheduleExecutor ? _scheduleExecutor->getScheduleType(id) : ScheduleType::ONCE;
                if (_mqtt && _mqtt->isConnected() && sType != ScheduleType::INTERVAL) {
                    String execTopic = "v1/devices/" + _deviceToken + "/schedules/executed";
                    
                    // Build JSON payload
                    JsonDocument doc;
                    doc["id"] = id;
                    doc["ts"] = (unsigned long)time(nullptr);  // Current UTC timestamp
                    doc["status"] = (status == ExecutionStatus::SUCCESS) ? "success" : "failed";
                    if (status == ExecutionStatus::SUCCESS) {
                        // Use snprintf into a stack buffer to avoid the ambiguous
                        // String((long long)) ctor — STM32duino + Renesas WString
                        // expose only `String(long)` / `String(unsigned long)`, so
                        // the implicit long-long path becomes ambiguous on those
                        // toolchains. ESP32/ESP8266 baseline byte-identical.
                        char i64buf[24];
                        snprintf(i64buf, sizeof(i64buf), "%lld", (long long)value);
                        doc["value"] = String(i64buf);
                    }
                    if (error.length() > 0) {
                        doc["error"] = error;
                    }
                    
                    String payload;
                    serializeJson(doc, payload);
                    
                    _mqtt->publish(execTopic.c_str(), payload.c_str(), MQTTQoS::QOS_1, false);
                    ZENO_LOG_CORE("Published execution report to: %s", maskTopic(execTopic).c_str());
                }
                
                if (_scheduleExecutedCallback) {
                    _scheduleExecutedCallback(id, status, value, error);
                } });

            _scheduleExecutor->onScheduleError([this](const String &id, const String &error)
                                               {
                ZENO_LOG_CORE("Schedule %s error: %s", id.c_str(), error.c_str());
                if (_scheduleErrorCallback) {
                    _scheduleErrorCallback(id, error);
                } });
        }

        ZENO_LOG_CORE("✅ Schedule module initialized with %d schedules (subscription handled by unified callback)",
                      _scheduleExecutor ? _scheduleExecutor->getScheduleCount() : 0);
#endif // !ZENOPCB_DISABLE_SCHEDULE
    }

    void Zeno::_handleScheduleMessage(const String &topic, const String &payload)
    {
#if defined(ZENOPCB_DISABLE_SCHEDULE)
        (void)topic; (void)payload;
        // Subsystem stripped at compile time — message is silently ignored
        return;
#else
        ZENO_LOG_CORE("📥 Schedule message received on: %s", maskTopic(topic).c_str());

        ScheduleMessageHandler &handler = ScheduleMessageHandler::getInstance();
        ScheduleHandleResult result = handler.handleMessage(topic, payload);

        // Centralized ACK
        char actionStr[2] = {(char)result.action, '\0'};
        _publishAck("schedule", actionStr, result.scheduleId.c_str(), result.success,
                    result.processingMs, result.errorMessage);

        if (result.success)
        {
            ZENO_LOG_CORE("✅ Schedule %c for %s - OK (%dms)",
                          (char)result.action,
                          result.scheduleId.c_str(),
                          result.processingMs);
        }
        else
        {
            ZENO_LOG_CORE("❌ Schedule error: %s", result.errorMessage.c_str());
            if (_errorCallback)
            {
                _errorCallback("Schedule: " + result.errorMessage);
            }
        }
#endif // !ZENOPCB_DISABLE_SCHEDULE
    }

    Zeno &Zeno::onScheduleExecuted(ScheduleExecutedCallback callback)
    {
        _scheduleExecutedCallback = callback;
        return *this;
    }

    Zeno &Zeno::onScheduleError(ScheduleErrorCallback callback)
    {
        _scheduleErrorCallback = callback;
        return *this;
    }

    Zeno &Zeno::onScheduleCreated(ScheduleCreatedCallback callback)
    {
        _scheduleCreatedCallback = callback;
        return *this;
    }

    Zeno &Zeno::onScheduleUpdated(ScheduleUpdatedCallback callback)
    {
        _scheduleUpdatedCallback = callback;
        return *this;
    }

    Zeno &Zeno::onScheduleDeleted(ScheduleDeletedCallback callback)
    {
        _scheduleDeletedCallback = callback;
        return *this;
    }

    Zeno &Zeno::onScheduleSynced(ScheduleSyncedCallback callback)
    {
        _scheduleSyncedCallback = callback;
        return *this;
    }

    uint8_t Zeno::getScheduleCount() const
    {
        return _scheduleExecutor ? _scheduleExecutor->getScheduleCount() : 0;
    }

    // Plan 06-03 — Modbus getters preserve their public signature on every
    // platform (so a publish-all sketch compiles), but the body is
    // ESP32-only. ESP8266 returns 0 because the Modbus subsystem is not
    // available (capability gap; surfaced via _hal.capabilities() if the
    // caller wants to gate UI).
    int Zeno::getConnectionCount() const
    {
#if defined(ESP32)
        return (int)ModbusConnectionManager::getInstance().getActiveConnectionCount();
#else
        return 0;
#endif
    }

    int Zeno::getDataMonitorCount() const
    {
#if defined(ESP32)
        return (int)ModbusDataBuffer::getInstance().getRegisterCount();
#else
        return 0;
#endif
    }

    bool Zeno::isMaxSchedulesReached() const
    {
        return getScheduleCount() >= MAX_SCHEDULES;
    }

    // ============================================
    // Irrigation Management
    // ============================================

    // Plan 06-03 Pattern F (OQ-1 RESOLVED) — enableIrrigation() stays
    // available on every platform so a publish-all sketch
    // (`zeno.enableIrrigation();`) compiles on ESP8266; on non-ESP32 the
    // body logs a runtime warning and the fluent API still returns *this.
    Zeno &Zeno::enableIrrigation()
    {
#if defined(ESP32)
        _irrigationEnabled = true;
        ZENO_LOG_CORE("Irrigation module enabled");
#else
        ZENO_LOG_CORE("⚠️ Irrigation not available on this platform — ignoring (capability not in _hal.capabilities())");
#endif
        return *this;
    }

#if defined(ESP32)
    // Typed callback setters reference IrrigationWriteCallback/etc. which
    // live in irrigation/IrrigationTypes.h — guarded by D-03 module strip.
    Zeno &Zeno::setIrrigationWriteFunction(IrrigationWriteCallback callback)
    {
        _irrigationWriteCallback = callback;
        return *this;
    }

    Zeno &Zeno::onIrrigationStepProgress(IrrigationStepCallback callback)
    {
        _irrigationStepCallback = callback;
        return *this;
    }

    Zeno &Zeno::onIrrigationCompleted(IrrigationCompletedCallback callback)
    {
        _irrigationCompletedCallback = callback;
        return *this;
    }

    Zeno &Zeno::onIrrigationError(IrrigationErrorCallback callback)
    {
        _irrigationErrorCallback = callback;
        return *this;
    }
#endif  // ESP32 — typed irrigation callback setters

    // Plan 06-03 Pattern F — _initIrrigation() body is ESP32-only (touches
    // IrrigationExecutor/Scheduler/MessageHandler/Storage types). On
    // ESP8266 it is a silent no-op; the user-facing warning was already
    // logged by enableIrrigation() so we do not re-emit.
    void Zeno::_initIrrigation()
    {
#if defined(ESP32)
        if (!_irrigationEnabled)
        {
            return;
        }

        ZENO_LOG_CORE("Initializing Irrigation module...");

        // Ensure LittleFS is mounted
        if (!LittleFSManager::initialize())
        {
            ZENO_LOG_CORE("⚠️ LittleFS init failed — irrigation scenarios will not persist");
        }

        IrrigationExecutor &executor = IrrigationExecutor::getInstance();
        IrrigationScheduler &scheduler = IrrigationScheduler::getInstance();
        IrrigationMessageHandler &msgHandler = IrrigationMessageHandler::getInstance();

        // Wire write function
        if (_irrigationWriteCallback)
        {
            executor.setWriteFunction(_irrigationWriteCallback);
        }

        // Wire executor callbacks → publish status to MQTT
        executor.onStepProgress([this](const char *sid, const char *eid,
                                       uint8_t step, uint8_t total,
                                       IrrigationAction action,
                                       const IrrigationStep &stepData)
                                {
            if (isWaitAction(action))
            {
                _publishIrrigationStatus("running", step, total,
                                         irrigationActionToCode(action),
                                         nullptr, 0, stepData.waitDuration);
            }
            else
            {
                _publishIrrigationStatus("running", step, total,
                                         irrigationActionToCode(action),
                                         stepData.mqttKeys, stepData.keyCount);
            }
            if (_irrigationStepCallback)
                _irrigationStepCallback(sid, eid, step, total, action, stepData); });

        executor.onCompleted([this](const char *sid, const char *eid,
                                    uint32_t totalDurationSec)
                             {
            IrrigationExecutor &ex = IrrigationExecutor::getInstance();
            const IrrigationExecution &execution = ex.getExecution();
            _publishIrrigationStatus("completed", execution.stepCount, execution.stepCount,
                                     nullptr, nullptr, 0, totalDurationSec);
            if (_irrigationCompletedCallback)
                _irrigationCompletedCallback(sid, eid, totalDurationSec); });

        executor.onError([this](const char *sid, const char *eid,
                                uint8_t step, uint8_t total,
                                const char *error)
                         {
            _publishIrrigationStatus("error", step, total,
                                     nullptr, nullptr, 0, 0, error);
            if (_irrigationErrorCallback)
                _irrigationErrorCallback(sid, eid, step, total, error); });

        // Initialize executor
        executor.begin();

        // Wire scheduler trigger → start executor from storage (V3: scheduleId + scenarioId)
        scheduler.setTriggerCallback([this](const char *scheduleId, const char *scenarioId) -> bool
                                     {
            ZENO_LOG_CORE("⏰ Irrigation schedule %s → scenario %s", scheduleId, scenarioId);
            IrrigationExecutor &exec = IrrigationExecutor::getInstance();
            if (exec.isRunning())
            {
                ZENO_LOG_CORE("⚠️ Executor busy — skip scheduled trigger");
                return false;
            }
            // Generate eid from schedule ID
            char eid[IRRIGATION_EID_LEN];
            snprintf(eid, sizeof(eid), "sch_%s", scheduleId);
            return exec.startFromStorage(scenarioId, eid); });

        // Initialize scheduler (loads schedules from LittleFS)
        scheduler.begin();

        // Wire message handler error callback
        msgHandler.onError([this](const String &error, const String &payload)
                           {
            ZENO_LOG_CORE("❌ Irrigation message error: %s", error.c_str());
            if (_errorCallback) _errorCallback("Irrigation: " + error); });

        // On MQTT disconnect → stop execution for safety
        // (handled externally by user's disconnect callback calling stopExecution)

        ZENO_LOG_CORE("✅ Irrigation module initialized (%d scenarios, %d schedules)",
                      IrrigationStorage::getScenarioCount(),
                      scheduler.getScheduleCount());
#endif  // ESP32 — _initIrrigation() body
    }

#if defined(ESP32)
    // Irrigation message + ACK helpers — ESP32-only (declared inside an
    // #if defined(ESP32) block in ZenoPCB.h and reference types from the
    // guarded IrrigationTypes.h header).
    void Zeno::_handleIrrigationMessage(const String &topic, const String &payload)
    {
        if (!_irrigationEnabled)
        {
            return;
        }

        ZENO_LOG_CORE("📥 Irrigation message received on: %s", maskTopic(topic).c_str());

        IrrigationMessageHandler &handler = IrrigationMessageHandler::getInstance();
        IrrigationHandleResult result = handler.handleMessage(topic, payload);

        // Parse ts + eid from payload for ACK
        JsonDocument doc;
        deserializeJson(doc, payload);
        uint32_t ts = doc["d"]["ts"] | 0;
        const char *eid = doc["d"]["eid"] | "";

        // V3: Use result.action (set by handler) and result.scheduleId for sc/dc
        const char *ackId = result.scenarioId;
        if (strcmp(result.action, "sc") == 0 || strcmp(result.action, "dc") == 0)
        {
            ackId = result.scheduleId;
        }

        // Custom ACK with eid (irrigation-specific)
        _publishIrrigationAck(result.action, ackId, eid,
                              result.success, result.processingMs, ts,
                              result.success ? nullptr : result.errorMessage,
                              result.scenarioCount, result.scheduleCount);

        if (result.success)
        {
            ZENO_LOG_CORE("✅ Irrigation %s for %s - OK (%dms)",
                          result.action, ackId, result.processingMs);
        }
        else
        {
            ZENO_LOG_CORE("❌ Irrigation error: %s", result.errorMessage);
            if (_errorCallback)
            {
                _errorCallback(String("Irrigation: ") + result.errorMessage);
            }
        }
    }

    void Zeno::_publishIrrigationStatus(const char *status, uint8_t step, uint8_t total,
                                        const char *action,
                                        const char (*keys)[IRRIGATION_KEY_LEN],
                                        uint8_t keyCount,
                                        uint32_t dur,
                                        const char *error)
    {
        if (!_mqtt || !_mqtt->isConnected())
            return;

        IrrigationExecutor &exec = IrrigationExecutor::getInstance();
        const IrrigationExecution &execution = exec.getExecution();

        String statusTopic = String("v1/devices/") + _deviceToken + "/irrigation/status";

        JsonDocument doc;
        doc["sid"] = execution.scenarioId;
        doc["eid"] = execution.executionId;
        doc["st"] = status;
        doc["step"] = step;
        doc["total"] = total;
        doc["ts"] = (uint32_t)time(nullptr);

        if (action)
        {
            doc["sa"] = action;
        }

        if (keys && keyCount > 0)
        {
            JsonArray sk = doc["sk"].to<JsonArray>();
            for (uint8_t i = 0; i < keyCount; i++)
            {
                sk.add(keys[i]);
            }
        }

        if (dur > 0)
        {
            doc["dur"] = dur;
        }

        if (error && error[0] != '\0')
        {
            doc["err"] = error;
        }

        String payload;
        serializeJson(doc, payload);
        _mqtt->publish(statusTopic.c_str(), payload.c_str(), MQTTQoS::QOS_1, false);
        ZENO_LOG_CORE("📨 Irrigation status → %s: %s", status, payload.c_str());
    }

    void Zeno::_publishIrrigationAck(const char *action, const char *id, const char *eid,
                                     bool success, uint32_t ms, uint32_t ts,
                                     const char *error,
                                     uint8_t scenarioCount, uint8_t scheduleCount)
    {
        if (!_mqtt || !_mqtt->isConnected())
            return;

        String ackTopic = String("v1/devices/") + _deviceToken + "/ack";

        JsonDocument doc;
        doc["mod"] = "irrigation";
        doc["a"] = action;
        if (id && id[0] != '\0')
            doc["id"] = id;
        if (eid && eid[0] != '\0')
            doc["eid"] = eid;
        doc["ok"] = success;
        doc["ms"] = ms;
        doc["ts"] = ts > 0 ? ts : (uint32_t)time(nullptr);

        // V3: fa action includes scenario/schedule counts
        if (strcmp(action, "fa") == 0)
        {
            doc["sc"] = scenarioCount;
            doc["sch"] = scheduleCount;
        }

        if (!success && error && error[0] != '\0')
        {
            doc["err"] = error;
        }

        String payload;
        serializeJson(doc, payload);
        _mqtt->publish(ackTopic.c_str(), payload.c_str(), MQTTQoS::QOS_1, false);
        ZENO_LOG_CORE("📨 Irrigation ACK → %s: %s", action, payload.c_str());
    }
#endif  // ESP32 — irrigation message + ACK helpers

    // ============================================
    // Edge Alarm Management
    // ============================================

    Zeno &Zeno::enableAlarm()
    {
        _alarmEnabled = true;
        ZENO_LOG_CORE("Edge Alarm module enabled");
        return *this;
    }

    void Zeno::_initAlarm()
    {
#if defined(ZENOPCB_DISABLE_ALARM)
        // Plan 07-06.6 MICRO_BASIC: Alarm subsystem stripped at compile time.
        if (_alarmEnabled)
        {
            ZENO_LOG_CORE("[WARN] Alarm subsystem disabled at compile time (ZENOPCB_DISABLE_ALARM) — enableAlarm() is a no-op on this build");
        }
        return;
#else
        if (!_alarmEnabled)
            return;

        ZENO_LOG_CORE("Initializing Edge Alarm module...");

        if (!_alarmEngine)
        {
            _alarmEngine = new AlarmEngine();
        }

        // Wire internal publish callback: alarm event → MQTT
        // Returns true if successfully published (starts cooldown), false = retry next cycle.
        _alarmEngine->onAlarmTriggered([this](const AlarmEvent &event) -> bool
                                       {
            if (_mqtt && _mqtt->isConnected())
            {
                String alarmTopic = "v1/devices/" + _deviceToken + "/alarm";

                JsonDocument doc;
                doc["id"] = (const char *)event.ruleId;
                doc["k"]  = (const char *)event.key;
                doc["cv"] = event.currentValue;
                doc["ts"] = event.timestamp;

                String payload;
                serializeJson(doc, payload);

                bool ok = _mqtt->publish(alarmTopic.c_str(), payload.c_str(), MQTTQoS::QOS_1, false);
                ZENO_LOG_CORE("%s Alarm event: rule=%s key=%s val=%.4f ts=%lu (Unix epoch)",
                              ok ? "\U0001f514" : "⚠️",
                              event.ruleId, event.key, event.currentValue,
                              (unsigned long)event.timestamp);
                if (!ok)
                {
                    ZENO_LOG_CORE("  ↳ Publish failed — cooldown NOT started, will retry next cycle");
                }
                return ok;
            }
            ZENO_LOG_CORE("⚠️ Alarm skipped — MQTT not connected (will retry)");
            return false; });

        // Wire user-facing notification callback
        if (_alarmTriggeredCallback)
        {
            _alarmEngine->onAlarmNotify(_alarmTriggeredCallback);
        }

        ZENO_LOG_CORE("✅ Edge Alarm module initialized (subscription handled by unified callback)");
#endif // !ZENOPCB_DISABLE_ALARM
    }

    void Zeno::_handleAlarmConfigMessage(const String &topic, const String &payload)
    {
#if defined(ZENOPCB_DISABLE_ALARM)
        (void)topic; (void)payload;
        return;
#else
        ZENO_LOG_CORE("📥 Alarm config received: %s", payload.c_str());
        uint32_t startMs = millis();

        if (!_alarmEngine)
        {
            ZENO_LOG_CORE("⚠️ Alarm engine not initialized");
            _publishAck("alarm", "?", "", false, millis() - startMs, "Alarm engine not initialized");
            return;
        }

        JsonDocument doc;
        DeserializationError err = deserializeJson(doc, payload);
        if (err)
        {
            ZENO_LOG_CORE("❌ Alarm config parse error: %s", err.c_str());
            _publishAck("alarm", "?", "", false, millis() - startMs, String("Parse error: ") + err.c_str());
            return;
        }

        // Parse request ID for ACK tracking (explicit null check)
        const char *rid = doc["rid"].as<const char *>();

        const char *action = doc["a"];
        if (!action)
        {
            ZENO_LOG_CORE("❌ Alarm config missing 'a' field");
            _publishAck("alarm", "?", rid ? rid : "", false, millis() - startMs, "Missing 'a' field");
            return;
        }

        if (strcmp(action, "set") == 0)
        {
            // Full sync — replace all rules
            JsonArray rules = doc["r"].as<JsonArray>();
            int count = _alarmEngine->syncRules(rules);
            ZENO_LOG_CORE("✅ Alarm rules synced: %d rules", count);

            // ACK: id = rid (request tracking ID)
            _publishAck("alarm", "set", rid ? rid : "", true, millis() - startMs);

            if (_alarmConfigReceivedCallback)
            {
                _alarmConfigReceivedCallback(count);
            }
        }
        else if (strcmp(action, "del") == 0)
        {
            // Delete single rule
            const char *ruleId = doc["id"];
            if (ruleId)
            {
                _alarmEngine->deleteRule(ruleId);
                ZENO_LOG_CORE("✅ Alarm rule deleted: %s", ruleId);

                // ACK: id = rule ID being deleted
                _publishAck("alarm", "del", ruleId, true, millis() - startMs);

                if (_alarmConfigReceivedCallback)
                {
                    _alarmConfigReceivedCallback(_alarmEngine->getRuleCount());
                }
            }
            else
            {
                ZENO_LOG_CORE("❌ Alarm del missing 'id' field");
                _publishAck("alarm", "del", rid ? rid : "", false, millis() - startMs, "Missing 'id' field");
            }
        }
        else
        {
            ZENO_LOG_CORE("⚠️ Unknown alarm action: %s", action);
            _publishAck("alarm", action, rid ? rid : "", false, millis() - startMs, String("Unknown action: ") + action);
        }
#endif // !ZENOPCB_DISABLE_ALARM
    }

    void Zeno::_checkAndPublishAlarms()
    {
#if defined(ZENOPCB_DISABLE_ALARM)
        return;
#else
        if (!_alarmEnabled || !_alarmEngine || _alarmEngine->getRuleCount() == 0)
            return;

        ZENO_LOG_VERBOSE("[Alarm] checking ZKey values against %d rules", _alarmEngine->getRuleCount());

        auto &zBuffer = ZKeyBuffer::getInstance();

        // Iterate all Z keys and check alarm conditions
        for (int i = 0; i < 255; i++)
        {
            ZKey key = static_cast<ZKey>(i);
            const ZValue &val = zBuffer.get(key);

            // Only check keys that have been set (not NONE type)
            if (val.type == ZValueType::NONE)
                continue;

            // Convert to double for alarm evaluation
            double numericValue = 0.0;
            switch (val.type)
            {
            case ZValueType::INT:
                numericValue = (double)val.toInt();
                break;
            case ZValueType::FLOAT:
                numericValue = (double)val.toFloat();
                break;
            case ZValueType::BOOL:
                numericValue = val.toBool() ? 1.0 : 0.0;
                break;
            default:
                continue; // Skip string types — not numeric
            }

            // Build key name "Z0", "Z1", etc.
            String keyName = "Z" + String(i);
            _alarmEngine->checkAlarms(keyName.c_str(), numericValue);
        }
#endif // !ZENOPCB_DISABLE_ALARM
    }

    Zeno &Zeno::onAlarmTriggered(AlarmTriggeredCallback callback)
    {
        _alarmTriggeredCallback = callback;
#if !defined(ZENOPCB_DISABLE_ALARM)
        // If engine already created, update it
        if (_alarmEngine)
        {
            _alarmEngine->onAlarmNotify(callback);
        }
#endif
        return *this;
    }

    Zeno &Zeno::onAlarmConfigReceived(AlarmConfigCallback callback)
    {
        _alarmConfigReceivedCallback = callback;
        return *this;
    }

    int Zeno::getAlarmRuleCount() const
    {
#if defined(ZENOPCB_DISABLE_ALARM)
        return 0;
#else
        return _alarmEngine ? _alarmEngine->getRuleCount() : 0;
#endif
    }

    void Zeno::_setState(ZenoState newState)
    {
        if (_state != newState)
        {
            _state = newState;
            if (_stateCallback)
            {
                _stateCallback(newState);
            }
        }
    }

    // ============================================
    // Centralized ACK System
    // ============================================

    void Zeno::_publishAck(const char *module, const char *action, const char *id,
                           bool success, uint32_t ms, const String &error)
    {
        if (!_mqtt || !_mqtt->isConnected())
        {
            return;
        }

        // Build unified ACK topic: v1/devices/{token}/ack
        String ackTopic = String("v1/devices/") + _deviceToken + "/ack";

        // Build ACK payload
        JsonDocument doc;
        doc["mod"] = module; // "config", "monitor", "schedule", "alarm", "control"
        doc["a"] = action;   // action char/string
        if (id && id[0] != '\0')
        {
            doc["id"] = id; // For alarm: rid (set) or ruleId (del)
        }
        doc["ok"] = success;
        doc["ms"] = ms;
        doc["ts"] = (uint32_t)time(nullptr); // UTC timestamp

        if (!success && error.length() > 0)
        {
            doc["err"] = error;
        }

        String ackPayload;
        serializeJson(doc, ackPayload);

        _mqtt->publish(ackTopic.c_str(), ackPayload.c_str(), MQTTQoS::QOS_1, false);
        ZENO_LOG_CORE("📨 ACK → %s: %s", module, ackPayload.c_str());
    }

    // ============================================
    // OTA Module Implementation
    // ============================================

    Zeno &Zeno::enableOTA()
    {
        _otaEnabled = true;
        ZENO_LOG_CORE("OTA update module enabled");
        return *this;
    }

    // Pattern G (Phase 7 D-06) — fallible OTA trigger. Distinct overload from the
    // enableOTA() builder above. T-4-03 safe: ESP32 production firmware does NOT
    // call zeno.ota(url) directly (verified via RESEARCH §Pitfall 4 + A8), so this
    // additive method does not affect any existing call site.
    ZenoCapability Zeno::ota(const char *url)
    {
#if defined(ZENOPCB_DISABLE_OTA)
        (void)url;
        ZENO_LOG_CORE("[WARN] ota(url) — OTA subsystem disabled at compile time (ZENOPCB_DISABLE_OTA)");
        return ZenoCapability::Unavailable;
#else
        if (!(_hal.capabilities() & IZenoHal::CAP_OTA))
        {
            ZENO_LOG_CORE("[WARN] OTA not available on this platform — capabilities() & CAP_OTA == 0");
            return ZenoCapability::Unavailable;
        }

        // Delegate to the existing ZenoPCBOTA instance — the same code path the
        // internal MQTT /ota command handler uses. If the user has not yet called
        // enableOTA() + begin(), _ota is null; return Error rather than crashing
        // on a null deref. Plan 07-06 may refine this to a stricter Pending /
        // Error split for non-ESP32 platforms.
        if (_ota == nullptr)
        {
            ZENO_LOG_CORE("[WARN] ota(url) called before enableOTA() + begin() — _ota null");
            return ZenoCapability::Error;
        }
        return _ota->beginUpdate(url) ? ZenoCapability::OK : ZenoCapability::Error;
#endif
    }

    Zeno &Zeno::onOTAProgress(OTAProgressCallback callback)
    {
        _otaProgressCallback = callback;
        return *this;
    }

    Zeno &Zeno::onOTAComplete(OTACompleteCallback callback)
    {
        _otaCompleteCallback = callback;
        return *this;
    }

    Zeno &Zeno::onOTAError(OTAErrorCallback callback)
    {
        _otaErrorCallback = callback;
        return *this;
    }

    bool Zeno::canOTARollBack() const
    {
#if defined(ZENOPCB_DISABLE_OTA)
        return false;
#else
        return ZenoPCBOTA::canRollBack();
#endif
    }

    bool Zeno::isOTAInProgress() const
    {
#if defined(ZENOPCB_DISABLE_OTA)
        return false;
#else
        return _ota && _ota->isInProgress();
#endif
    }

    bool Zeno::isTimeSynced() const
    {
        return TimeManager::isSynced();
    }

    time_t Zeno::getUTC() const
    {
        return TimeManager::isSynced() ? TimeManager::getUTC() : 0;
    }

    void Zeno::_initOTA()
    {
#if defined(ZENOPCB_DISABLE_OTA)
        if (_otaEnabled)
        {
            ZENO_LOG_CORE("[WARN] OTA subsystem disabled at compile time (ZENOPCB_DISABLE_OTA) — enableOTA() is a no-op on this build");
        }
        return;
#else
        if (!_otaEnabled)
            return;

        ZENO_LOG_CORE("Initializing OTA module...");

        if (!_ota)
        {
            // Explicit HAL injection (Plan 04-05) — replaces the Plan 04-04 bridge.
            _ota = new ZenoPCBOTA(_hal);
        }

        // Wire callbacks
        if (_otaProgressCallback)
        {
            _ota->onProgress([this](float pct)
                             {
                                 _otaProgressCallback(pct);
                                 // ⚠️ KHÔNG publish MQTT progress trong callback!
                                 // Callback chạy BÊN TRONG _ota->loop() → AT+CIPRXGET đang active
                                 // Nếu gửi AT+CIPSEND (MQTT publish) → response bị lẫn → firmware corrupt
                                 // Progress MQTT sẽ được publish SAU _ota->loop() return (xem Zeno::loop)
                             });
        }

        _ota->onComplete([this](const String &version)
                         {
            unsigned long elapsed = (_otaStartTimeMs > 0) ? (millis() - _otaStartTimeMs) / 1000 : 0;
            ZENO_LOG_CORE("⏱️ OTA completed in %lus — Version: %s", elapsed, version.c_str());
            _otaStartTimeMs = 0;

            // ⭐ Disable queue mode and flush remaining messages
            if (_mqttQueueEnabled)
            {
                _mqttQueueEnabled = false;
                _flushMQTTQueue();
                ZENO_LOG_OTA("📦 4G OTA complete: MQTT queue mode DISABLED");
            }

            // 🔄 Force fresh MQTT connection before publishing OTA result
            // Old connection may be stale (isConnected=true but publish fails)
            bool is4G = _networkProvider && strcmp(_networkProvider->getName(), "4G") == 0;
            
            ZENO_LOG_OTA("📡 Reconnecting MQTT to publish OTA result...");
            
            // Step 1: Force disconnect old (possibly stale) connection
            if (_mqtt)
            {
                _mqtt->forceDisconnect();
                delay(200);
            }
            
            // Step 2: Give network time to stabilize after OTA HTTP connection closed
            if (is4G)
            {
                delay(1000);  // 4G needs more time
                if (_networkProvider) _networkProvider->loop();
            }
            else
            {
                delay(300);
            }
            
            // Step 3: Reconnect MQTT (15s timeout)
            unsigned long startMs = millis();
            bool connected = false;
            while (!connected && (millis() - startMs) < 15000)
            {
                if (is4G && _networkProvider)
                    _networkProvider->loop();  // Maintain 4G modem
                    
                _mqtt->connect();
                _mqtt->loop();
                connected = _mqtt->isConnected();
                
                if (!connected) 
                    delay(200);
            }
            
            ZENO_LOG_OTA("📡 MQTT reconnect: %s (took %lums)", 
                         connected ? "OK" : "FAILED", millis() - startMs);

            // Step 4: Publish OTA completed with retry
            bool published = false;
            if (connected)
            {
                String respTopic = "v1/devices/" + _deviceToken + "/ota/response";
                JsonDocument doc;
                doc["status"] = "completed";
                doc["progress"] = 100;
                doc["version"] = version;
                doc["ts"] = (unsigned long)time(nullptr);
                String payload;
                serializeJson(doc, payload);

                for (int attempt = 0; attempt < 3 && !published; attempt++)
                {
                    if (attempt > 0)
                    {
                        delay(500);
                        _mqtt->loop();
                    }
                    published = _mqtt->publish(respTopic.c_str(), payload.c_str(), MQTTQoS::QOS_1, false);
                    ZENO_LOG_OTA("📤 OTA result publish attempt %d: %s", attempt + 1, published ? "OK" : "FAILED");
                }

                if (published)
                {
                    // Wait for TCP buffer to flush before restart
                    delay(500);
                    _mqtt->loop();
                    ZENO_LOG_OTA("✅ OTA completed notification sent to backend");
                }
            }

            // Step 5: If publish failed, save to NVS for post-reboot delivery
            if (!published)
            {
                JsonDocument doc;
                doc["status"] = "completed";
                doc["progress"] = 100;
                doc["version"] = version;
                doc["ts"] = (unsigned long)time(nullptr);
                String payload;
                serializeJson(doc, payload);

                // T-4-02: namespace "ota_result" + key "payload" preserved byte-for-byte.
                _hal.nvs().begin("ota_result", false);
                _hal.nvs().putString("payload", payload.c_str());
                _hal.nvs().end();
                ZENO_LOG_OTA("💾 Publish failed — saved to NVS (will send after reboot)");
            }
            
            if (_otaCompleteCallback) _otaCompleteCallback(version); });

        _ota->onError([this](OTAError error, const String &message)
                      {
            // ⭐ Disable queue mode and flush remaining messages
            if (_mqttQueueEnabled)
            {
                _mqttQueueEnabled = false;
                _flushMQTTQueue();
                ZENO_LOG_OTA("📦 4G OTA error: MQTT queue mode DISABLED");
            }

            _publishOTAResponse("error", -1, "", message);
            if (_otaErrorCallback) _otaErrorCallback(error, message); });

        // ⭐ Yield callback — gọi tại safe points khi OTA đang đợi data
        // Tại đây AT command của OTA đã complete → an toàn cho MQTT AT commands
        _ota->onYield([this]()
                      {
            if (_mqttQueueEnabled && _mqtt)
            {
                // 1. Run MQTT loop for keepalive (safe — OTA AT command complete)
                _mqtt->loop();

                // 2. Flush queued MQTT messages
                if (_mqtt->isConnected())
                {
                    int flushed = 0;
                    while (_mqttQueueCount() > 0)
                    {
                        int idx = _mqttQueueHead;
                        bool ok = _mqtt->publish(
                            _mqttQueue[idx].topic.c_str(),
                            _mqttQueue[idx].payload.c_str(),
                            _mqttQueue[idx].qos,
                            _mqttQueue[idx].retain);
                        _mqttQueueHead = (_mqttQueueHead + 1) % MAX_MQTT_QUEUE;
                        if (ok) flushed++;
                        
                        // Limit to 2 messages per yield to avoid blocking OTA too long
                        if (flushed >= 2) break;
                    }
                    if (flushed > 0)
                    {
                        ZENO_LOG_OTA("📤 Flushed %d queued MQTT messages at OTA yield point", flushed);
                    }
                }
            } });

        // Set network client — OTA dùng client RIÊNG BIỆT với MQTT.
        // Pattern H gate: STM32F4 default-Ethernet has no WiFi.h / `WiFiClient`
        // member declared, so the fallback branches drop on that platform.
        if (_networkProvider)
        {
            Client *provOTAClient = _networkProvider->getOTAClient();
            if (provOTAClient)
            {
                _ota->setClient(provOTAClient);
                ZENO_LOG_CORE("✅ OTA using %s dedicated OTA client", _networkProvider->getName());
            }
            else
            {
#if defined(ESP32) || defined(ESP8266) || defined(ARDUINO_UNOR4_WIFI) || defined(STM32F1xx)
                _ota->setClient(&_otaWifiClient);
                ZENO_LOG_CORE("⚠️ OTA fallback to dedicated OTA WiFiClient (provider OTA client NULL)");
#else
                ZENO_LOG_CORE("⚠️ Provider OTA client NULL and no WiFiClient fallback on this platform");
#endif
            }
        }
        else
        {
#if defined(ESP32) || defined(ESP8266) || defined(ARDUINO_UNOR4_WIFI) || defined(STM32F1xx)
            _ota->setClient(&_otaWifiClient);
            ZENO_LOG_CORE("✅ OTA using dedicated OTA WiFiClient (separate from MQTT)");
#else
            ZENO_LOG_CORE("⚠️ No network provider AND no WiFiClient on this platform — OTA cannot connect");
#endif
        }

        ZENO_LOG_CORE("✅ OTA module initialized (subscription handled by unified callback)");
#endif // !ZENOPCB_DISABLE_OTA
    }

    void Zeno::_handleOTAMessage(const String &topic, const String &payload)
    {
#if defined(ZENOPCB_DISABLE_OTA)
        (void)topic; (void)payload;
        return;
#else
        ZENO_LOG_CORE("📥 OTA command received: %s", maskPayload(payload).c_str());

        if (!_ota)
        {
            ZENO_LOG_CORE("⚠️ OTA module not initialized");
            return;
        }

        // === Guard: prevent infinite retry loop ===
        // If reconnect delivers the same retained OTA message that already failed,
        // skip it to avoid endless OTA-fail → disconnect → reconnect → OTA-fail cycle.
        const unsigned long OTA_DEDUP_WINDOW_MS = 60000; // 60s cooldown

        if (_lastFailedOTAPayload.length() > 0 &&
            _lastFailedOTAPayload == payload &&
            (millis() - _lastFailedOTATime) < OTA_DEDUP_WINDOW_MS)
        {
            ZENO_LOG_CORE("⏭️ OTA skipped: same command failed %lus ago (cooldown 60s)",
                          (millis() - _lastFailedOTATime) / 1000);
            return;
        }

        JsonDocument doc;
        DeserializationError err = deserializeJson(doc, payload);
        if (err)
        {
            ZENO_LOG_CORE("❌ OTA parse error: %s", err.c_str());
            _publishOTAResponse("error", -1, "", String("JSON parse error: ") + err.c_str());
            return;
        }

        const char *action = doc["action"] | doc["a"].as<const char *>();

        if (!action)
        {
            ZENO_LOG_CORE("❌ OTA missing 'action' field");
            _publishOTAResponse("error", -1, "", "Missing 'action' field");
            return;
        }

        if (strcmp(action, "update") == 0)
        {
            const char *url = doc["url"];
            if (!url || strlen(url) == 0)
            {
                _publishOTAResponse("error", -1, "", "Missing 'url' field");
                return;
            }

            // Optional: version string
            const char *version = doc["version"] | doc["v"].as<const char *>();

            // Version check: skip if OTA version == current firmware version
            if (version && _deviceInfo.version.length() > 0 &&
                _deviceInfo.version == version)
            {
                ZENO_LOG_CORE("⏭️ OTA skipped: version %s already running", version);
                _publishOTAResponse("skipped", 0, version, "Already running this version");
                _lastFailedOTAPayload = "";
                return;
            }

            if (version)
            {
                _ota->setNewVersion(version);
            }

            // ⭐ DEFERRED: Không gọi beginUpdate() ở đây (blocking TCP connect)
            // Lưu thông tin → xử lý trong Zeno::loop() → tránh block MQTT keepalive
            _pendingOTAUrl = String(url);
            _pendingOTAVersion = version ? String(version) : "";
            _pendingOTAPayload = payload;
            _pendingOTAStart = true;

            ZENO_LOG_CORE("📋 OTA queued for next loop cycle: %s", maskUrl(String(url)).c_str());
        }
        else if (strcmp(action, "cancel") == 0)
        {
            if (_ota->isInProgress())
            {
                ZENO_LOG_CORE("⛔ OTA cancel command from backend");
                _ota->cancelUpdate("Cancelled by backend");
                // Error callback already fires _publishOTAResponse("error")
            }
            else if (_pendingOTAStart)
            {
                // Cancel pending (not yet started)
                _pendingOTAStart = false;
                _pendingOTAUrl = "";
                _pendingOTAVersion = "";
                _pendingOTAPayload = "";
                ZENO_LOG_CORE("⛔ Pending OTA cancelled by backend");
                _publishOTAResponse("cancelled", 0, "", "Cancelled by backend before start");
            }
            else
            {
                ZENO_LOG_CORE("⚠️ Cancel received but no OTA in progress");
                _publishOTAResponse("cancelled", 0, "", "No OTA in progress");
            }
        }
        else if (strcmp(action, "rollback") == 0)
        {
            if (ZenoPCBOTA::canRollBack())
            {
                _publishOTAResponse("rolling_back");
                ZENO_LOG_CORE("🔄 OTA rollback initiated");
                delay(500); // Let MQTT publish complete
                ZenoPCBOTA::rollBack();
            }
            else
            {
                _publishOTAResponse("error", -1, "", "No previous firmware available for rollback");
            }
        }
        else if (strcmp(action, "getversion") == 0)
        {
            // Report current firmware version
            String ver = _deviceInfo.version.length() > 0 ? _deviceInfo.version : String("unknown");
            bool canRB = ZenoPCBOTA::canRollBack();

            JsonDocument resp;
            resp["status"] = "info";
            resp["version"] = ver;
            resp["canRollback"] = canRB;

            String respPayload;
            serializeJson(resp, respPayload);

            if (_mqtt && _mqtt->isConnected())
            {
                String respTopic = "v1/devices/" + _deviceToken + "/ota/response";
                _mqtt->publish(respTopic.c_str(), respPayload.c_str(), MQTTQoS::QOS_1, false);
            }
        }
        else
        {
            ZENO_LOG_CORE("❌ Unknown OTA action: %s", action);
            _publishOTAResponse("error", -1, "", String("Unknown action: ") + action);
        }
#endif // !ZENOPCB_DISABLE_OTA
    }

    void Zeno::_publishOTAResponse(const char *status, float progress, const String &version, const String &error)
    {
        JsonDocument doc;
        doc["status"] = status;

        if (progress >= 0)
            doc["progress"] = (int)progress;

        if (version.length() > 0)
            doc["version"] = version;

        if (error.length() > 0)
            doc["error"] = error;

        doc["ts"] = (unsigned long)time(nullptr);

        String payload;
        serializeJson(doc, payload);

        if (!_mqtt || !_mqtt->isConnected())
        {
            // MQTT mất kết nối — queue cho sau reconnect
            if (strcmp(status, "error") == 0 && _pendingOTAErrorPayload.length() == 0)
            {
                _pendingOTAErrorPayload = payload;
                ZENO_LOG_OTA("⏳ MQTT offline — queued OTA error response for next reconnect");
            }
            else if (strcmp(status, "completed") == 0)
            {
                // ⚠️ 4G blocking OTA: MQTT socket đã đóng trước khi OTA
                // Lưu vào NVS → sẽ publish sau reboot khi MQTT reconnect
                // T-4-02: namespace "ota_result" + key "payload" preserved byte-for-byte.
                _hal.nvs().begin("ota_result", false);
                _hal.nvs().putString("payload", payload.c_str());
                _hal.nvs().end();
                ZENO_LOG_OTA("💾 MQTT offline — saved OTA completed to NVS (publish after reboot)");
            }
            return;
        }

        String respTopic = "v1/devices/" + _deviceToken + "/ota/response";
        bool published = _mqtt->publish(respTopic.c_str(), payload.c_str(), MQTTQoS::QOS_1, false);

        if (published)
        {
            ZENO_LOG_OTA("📤 Response → %s: %s", status, payload.c_str());
        }
        else
        {
            ZENO_LOG_OTA("❌ Publish failed → %s", status);

            // ⭐ Publish failed but isConnected() was true (stale connection)
            // Save to NVS so it can be published after reboot
            if (strcmp(status, "completed") == 0)
            {
                // T-4-02: namespace "ota_result" + key "payload" preserved byte-for-byte.
                _hal.nvs().begin("ota_result", false);
                _hal.nvs().putString("payload", payload.c_str());
                _hal.nvs().end();
                ZENO_LOG_OTA("💾 Publish failed — saved OTA completed to NVS (publish after reboot)");
            }
            else if (strcmp(status, "error") == 0 && _pendingOTAErrorPayload.length() == 0)
            {
                _pendingOTAErrorPayload = payload;
                ZENO_LOG_OTA("⏳ Publish failed — queued OTA error for next reconnect");
            }
        }
    }

    // ========================================
    // MQTT Queue Methods — for 4G OTA time-slicing
    // Messages queued during OTA, flushed at yield points
    // ========================================

    int Zeno::_mqttQueueCount() const
    {
        return (_mqttQueueTail - _mqttQueueHead + MAX_MQTT_QUEUE) % MAX_MQTT_QUEUE;
    }

    void Zeno::_enqueueMQTT(const String &topic, const String &payload, MQTTQoS qos, bool retain)
    {
        int count = _mqttQueueCount();
        if (count >= MAX_MQTT_QUEUE - 1)
        {
            ZENO_LOG_OTA("⚠️ MQTT queue full (%d), dropping oldest message", count);
            // Drop oldest (advance head)
            _mqttQueueHead = (_mqttQueueHead + 1) % MAX_MQTT_QUEUE;
        }

        _mqttQueue[_mqttQueueTail].topic = topic;
        _mqttQueue[_mqttQueueTail].payload = payload;
        _mqttQueue[_mqttQueueTail].qos = qos;
        _mqttQueue[_mqttQueueTail].retain = retain;
        _mqttQueueTail = (_mqttQueueTail + 1) % MAX_MQTT_QUEUE;

        ZENO_LOG_OTA("📥 Queued MQTT message (%d in queue): %s", _mqttQueueCount(), maskTopic(topic).c_str());
    }

    void Zeno::_flushMQTTQueue()
    {
        if (!_mqtt || !_mqtt->isConnected())
            return;

        int flushed = 0;
        while (_mqttQueueCount() > 0)
        {
            int idx = _mqttQueueHead;
            bool ok = _mqtt->publish(
                _mqttQueue[idx].topic.c_str(),
                _mqttQueue[idx].payload.c_str(),
                _mqttQueue[idx].qos,
                _mqttQueue[idx].retain);
            _mqttQueueHead = (_mqttQueueHead + 1) % MAX_MQTT_QUEUE;
            if (ok)
                flushed++;
        }
        if (flushed > 0)
        {
            ZENO_LOG_OTA("📤 Flushed %d remaining MQTT messages", flushed);
        }
    }

} // namespace ZenoPCB
