#include "WiFiProvisioning.h"
#include "../core/ZenoPCBDebug.h"
// platform HAL bridge for backward-compat default ctor
// (wires explicit DI from ZenoPCB.cpp callers). WDT-feed
// audit + ESP8266-specific yield()/feedWatchdog() insertions are Plan
// 06-04 territory; this plan only flips the include + ctor selector.
// (follow-up) extend HAL include switch to
// cover UNO R4 + STM32 so the default WiFiProvisioning() ctor can wire a
// real reference member instead of `*reinterpret_cast<IZenoHal*>(nullptr)`
// (the latter trips Renesas + STM32 g++ as an invalid nullptr cast under
// strict-conversion mode).
#if defined(ESP32)
  #include "../hal/esp32/Esp32Hal.h"
#elif defined(ESP8266)
  #include "../hal/esp8266/Esp8266Hal.h"
#elif defined(ARDUINO_UNOR4_WIFI)
  #include "../hal/unor4/UnoR4Hal.h"
#elif defined(STM32F1xx) || defined(STM32F4xx)
  #include "../hal/stm32/Stm32Hal.h"
#endif
#include "../ZenoJson.h"  // ArduinoJson API from vendored copy (namespace ZenoJson; see vendor/ArduinoJson/LICENSE.md)
#include <map>
#include <algorithm>

// ESP8266 compatibility shim for the ESP32 `wifi_mode_t`
// typedef used in _handleVerifyWiFi(). ESP8266 Arduino Core 3.x exposes
// the same concept under the name `WiFiMode_t`. The encryption-type
// enum names (WIFI_AUTH_*) used in _encryptionTypeToString() do NOT map
// 1:1 onto ESP8266 ENC_TYPE_* values (some collide), so that function
// is guarded directly at its body rather than via macro aliasing here.
// may rework these sites to use a HAL-mediated WiFi
// abstraction instead.
#if defined(ESP8266)
  using wifi_mode_t = WiFiMode_t;
#endif

// Bounded buffers for HAL getString reads. Sizes per 1.1
// chosen to cover the existing IEEE 802.11 / WPA2 / dotted-quad maxima
// plus padding. Stack arrays only on _loadConfig() which runs once at boot
// (CLAUDE.md "Stack arrays must be < 1 KB" total per-frame here ~512 B).
namespace ZenoPCB
{
    namespace
    {
        constexpr size_t BUF_USERID = 64;
        constexpr size_t BUF_DEVICEID = 64;
        constexpr size_t BUF_SSID = 64;     // IEEE 802.11: max 32 chars
        constexpr size_t BUF_WIFIPASS = 96; // WPA2-PSK max 63 chars
        constexpr size_t BUF_IP = 16;       // dotted-quad IPv4
        constexpr size_t BUF_APN = 64;
        constexpr size_t BUF_CELLUSER = 32;
        constexpr size_t BUF_CELLPASS = 32;
    } // namespace
} // namespace ZenoPCB

// whole-class capability gate. ESP32 + ESP8266
// retain their existing class body verbatim; UNO R4 + STM32 get stub bodies
// (returns failure + one-time warn log) at the bottom of the file. This TU
// guard wraps the ISR globals, the `using wifi_mode_t = ...` alias above,
// and every method body in this translation unit all reference APIs
// (WiFi.*, WebServer*, IRAM_ATTR, scanNetworks, etc.) that do not exist on
// UNO R4 / STM32 builds.
#if defined(ESP32) || defined(ESP8266)

// ISR globals for button (must be at file scope for IRAM placement) 
// Modified from interrupt context captures button press even when loop() is blocked
// (e.g., during MQTT TCP connect timeout of 15s+)
static volatile bool g_wifiProv_btnFlag = false;
static volatile unsigned long g_wifiProv_btnTime = 0;

static void IRAM_ATTR wifiProv_buttonISR()
{
    // Record first press only don't overwrite timestamp while still processing
    if (!g_wifiProv_btnFlag)
    {
        g_wifiProv_btnFlag = true;
        g_wifiProv_btnTime = millis();
    }
}

namespace ZenoPCB
{

    // ============================================
    // Constructor / Destructor
    // ============================================

    WiFiProvisioning::WiFiProvisioning(IZenoHal &hal)
        : _hal(hal), _webServer(nullptr), _state(ProvisioningState::IDLE), _buttonPressTime(0), _apStartTime(0), _buttonPressed(false), _lastLedToggle(0), _ledState(false), _lastReconnectAttempt(0), _reconnectInterval(5000), _reconnectAttempts(0), _maxReconnectAttempts(10), _wasConnected(false), _pendingAPShutdownTime(0), _pendingConnectWiFi(false), _skipAutoWiFiConnect(false), _mqttTestCallback(nullptr), _claimCallback(nullptr)
    {
    }

    // Backward-compat default ctor uses the canonical platform HAL
    // singleton (Esp32Hal on ESP32, Esp8266Hal on ESP8266 per Plan
    // 06-03). will switch ZenoPCB.cpp callers to pass `_hal`
    // directly.
    WiFiProvisioning::WiFiProvisioning()
#if defined(ESP32)
        : WiFiProvisioning(getEsp32Hal())
#elif defined(ESP8266)
        : WiFiProvisioning(getEsp8266Hal())
#endif
    {
    }

    WiFiProvisioning::~WiFiProvisioning()
    {
        if (_webServer)
        {
            delete _webServer;
            _webServer = nullptr;
        }
    }

    // ============================================
    // Configuration
    // ============================================

    bool WiFiProvisioning::begin(const ProvisioningConfig &config)
    {
        _config = config;
        _apSSID = _config.apSSIDPrefix + _getChipID();

        ZENO_LOG_PROV("Initializing...");
        ZENO_LOG_PROV("AP SSID: %s", _apSSID.c_str()); // sensitive verbose only
        ZENO_LOG_PROV("Button Pin: %d", _config.buttonPin);

        pinMode(_config.buttonPin, INPUT_PULLUP);

        // Initialize LED if configured
        _initLED();

        // Check if button is held at startup force AP mode immediately
        // This overrides WiFi auto-connect so user can enter config mode
        // even if device has saved credentials
        delay(50); // Debounce
        if (digitalRead(_config.buttonPin) == LOW)
        {
            ZENO_LOG_PROV("Button held at startup (GPIO %d) Entering AP mode...",
                          _config.buttonPin);
            _loadConfig(); // Load config first (for device info etc), but skip WiFi connect
            _startAccessPoint();
            return true;
        }

        if (_loadConfig())
        {
            ZENO_LOG_PROV("Configuration loaded from NVS");
            if (_deviceConfig.configured && !_skipAutoWiFiConnect)
            {
                ZENO_LOG_PROV("Device already configured");
                _connectToWiFi();
            }
            else if (_deviceConfig.configured && _skipAutoWiFiConnect)
            {
                ZENO_LOG_PROV("Device configured WiFi connect skipped (external network provider)");
            }
        }
        else
        {
            ZENO_LOG_PROV("No saved configuration");
        }

        return true;
    }

    void WiFiProvisioning::setAPSSIDPrefix(const String &prefix)
    {
        _config.apSSIDPrefix = prefix;
        _apSSID = prefix + _getChipID();
    }

    void WiFiProvisioning::setAPPassword(const String &password)
    {
        _config.apPassword = password;
    }

    void WiFiProvisioning::setButtonPin(uint8_t pin)
    {
        _config.buttonPin = pin;
        pinMode(pin, INPUT_PULLUP);
    }

    void WiFiProvisioning::setSkipAutoWiFiConnect(bool skip)
    {
        _skipAutoWiFiConnect = skip;
    }

    void WiFiProvisioning::setMQTTTestCallback(std::function<bool()> callback)
    {
        _mqttTestCallback = callback;
    }

    void WiFiProvisioning::setClaimCallback(std::function<bool(const String &userId, const String &deviceId, const String &token)> callback)
    {
        _claimCallback = callback;
    }

    void WiFiProvisioning::markClaimed()
    {
        if (_deviceConfig.isClaimed)
            return; // Already claimed no-op
        _deviceConfig.isClaimed = true;
        // Persist only the claim flag lightweight NVS write
        if (_hal.nvs().begin("zenopcb", false))
        {
            _hal.nvs().putBool("isClaimed", true);
            _hal.nvs().end();
            ZENO_LOG_PROV("Device marked as claimed (deferred claim succeeded)");
        }
        else
        {
            ZENO_LOG_PROV("markClaimed: failed to open NVS");
        }
    }

    void WiFiProvisioning::connectToSavedWiFi()
    {
        if (_deviceConfig.configured && _deviceConfig.wifiSSID.length() > 0)
        {
            ZENO_LOG_PROV("Connecting to saved WiFi: %s", _deviceConfig.wifiSSID.c_str()); // sensitive verbose only
            _connectToWiFi();
        }
        else
        {
            ZENO_LOG_PROV("No saved WiFi credentials to connect");
        }
    }

    void WiFiProvisioning::setButtonHoldTime(uint32_t ms)
    {
        _config.buttonHoldTimeMs = ms;
    }

    void WiFiProvisioning::setAPTimeout(uint32_t ms)
    {
        _config.apTimeoutMs = ms;
    }

    void WiFiProvisioning::setDeviceInfo(const DeviceInfo &info)
    {
        _deviceInfo = info;
    }

    // ============================================
    // Lifecycle
    // ============================================

    void WiFiProvisioning::loop()
    {
        _checkButton();

        // PITFALL 3 ESP8266 SW-WDT fires at ~1.5s. Feed every loop iteration.
        // No-op-ish on ESP32 (esp_task_wdt_reset() is cheap, ~tens of cycles).
        _hal.system().feedWatchdog();

        // Non-blocking AP shutdown after config received
        if (_pendingAPShutdownTime > 0 && millis() >= _pendingAPShutdownTime)
        {
            ZENO_LOG_PROV("Pending AP shutdown triggered");
            _pendingAPShutdownTime = 0;
            _stopAccessPoint();

            if (_pendingConnectWiFi)
            {
                // WiFi connection requested
                _pendingConnectWiFi = false;
                _connectToWiFi();
            }
            else
            {
                // Ethernet or Cellular - no WiFi connection needed
                _state = ProvisioningState::CONNECTED;
                ZENO_LOG_PROV("Config saved, state -> CONNECTED");
                if (_configCallback)
                {
                    _configCallback(_deviceConfig);
                }
            }
        }

        if (_state == ProvisioningState::AP_MODE_ACTIVE)
        {
            _checkAPTimeout();
            // LED managed externally by ZenoPCB::loop() via updateLED()
            // Khng chy reconnect khi ang AP mode
        }
        else if (_deviceConfig.configured && _deviceConfig.wifiSSID.length() > 0 && !_skipAutoWiFiConnect)
        {
            // Non-blocking WiFi connection check and reconnect
            // Ch chy khi khng AP mode, c config, v khng dng external network provider
            _checkWiFiConnection();
        }

        if (_webServer)
        {
            _webServer->handleClient();
        }
    }

    void WiFiProvisioning::startAPMode()
    {
        ZENO_LOG_PROV("Starting AP mode...");
        _startAccessPoint();
    }

    void WiFiProvisioning::stopAPMode()
    {
        ZENO_LOG_PROV("Stopping AP mode...");
        _stopAccessPoint();
    }

    void WiFiProvisioning::factoryReset()
    {
        ZENO_LOG_PROV("Factory reset...");
        _clearConfig();
        _hal.system().restart();
    }

    // ============================================
    // Status
    // ============================================

    bool WiFiProvisioning::isConfigured() const
    {
        return _deviceConfig.configured;
    }

    bool WiFiProvisioning::isWiFiConnected() const
    {
        return WiFi.status() == WL_CONNECTED;
    }

    bool WiFiProvisioning::isAPMode() const
    {
        return _state == ProvisioningState::AP_MODE_ACTIVE;
    }

    ProvisioningState WiFiProvisioning::getState() const
    {
        return _state;
    }

    DeviceConfig WiFiProvisioning::getConfig() const
    {
        return _deviceConfig;
    }

    String WiFiProvisioning::getAPSSID() const
    {
        return _apSSID;
    }

    String WiFiProvisioning::getAPIP() const
    {
        if (WiFi.getMode() == WIFI_AP || WiFi.getMode() == WIFI_AP_STA)
        {
            return WiFi.softAPIP().toString();
        }
        return "";
    }

    // ============================================
    // Callbacks
    // ============================================

    void WiFiProvisioning::onStateChange(ProvisioningStateCallback callback)
    {
        _stateCallback = callback;
    }

    void WiFiProvisioning::onConfigReceived(ConfigReceivedCallback callback)
    {
        _configCallback = callback;
    }

    void WiFiProvisioning::onWiFiConnected(WiFiConnectedCallback callback)
    {
        _wifiCallback = callback;
    }

    void WiFiProvisioning::onError(ErrorCallback callback)
    {
        _errorCallback = callback;
    }

    void WiFiProvisioning::setDeviceCredentials(const String &deviceId, const String &token)
    {
        _provisionedDeviceId = deviceId;
        _provisionedToken = token;
        ZENO_LOG_PROV("Device credentials set - ID: %s", deviceId.c_str());
    }

    // ============================================
    // Button Handling
    // ============================================

    void WiFiProvisioning::_checkButton()
    {
        // Sync interrupt flag 
        // Button may have been pressed while loop() was blocked (e.g., MQTT TCP connect).
        // ISR recorded the press time; sync it here so hold calculation is correct.
        if (g_wifiProv_btnFlag)
        {
            if (!_buttonPressed)
            {
                _buttonPressed = true;
                _buttonPressTime = g_wifiProv_btnTime;
                _setState(ProvisioningState::BUTTON_PRESSED);
                ZENO_LOG_PROV("Button detected via interrupt (GPIO %d) - hold %lums for AP mode",
                              _config.buttonPin, (unsigned long)_config.buttonHoldTimeMs);
            }
            g_wifiProv_btnFlag = false; // Clear already tracking this press
        }

        bool buttonState = digitalRead(_config.buttonPin) == LOW;

        if (buttonState && !_buttonPressed)
        {
            _buttonPressed = true;
            _buttonPressTime = millis();
            _setState(ProvisioningState::BUTTON_PRESSED);
            ZENO_LOG_PROV("Button pressed (GPIO %d) - hold %lums for AP mode",
                          _config.buttonPin, (unsigned long)_config.buttonHoldTimeMs);
        }
        else if (buttonState && _buttonPressed)
        {
            unsigned long holdTime = millis() - _buttonPressTime;

            // Show countdown every 500ms
            static unsigned long lastCountdown = 0;
            if (holdTime > 0 && millis() - lastCountdown >= 500)
            {
                lastCountdown = millis();
                uint32_t remaining = holdTime < _config.buttonHoldTimeMs
                                         ? (_config.buttonHoldTimeMs - holdTime) / 1000 + 1
                                         : 0;
                if (remaining > 0)
                    ZENO_LOG_PROV("Holding... %lus remaining", (unsigned long)remaining);
            }

            if (holdTime >= _config.buttonHoldTimeMs && _state != ProvisioningState::AP_MODE_ACTIVE)
            {
                ZENO_LOG_PROV("Button held %lums Starting AP mode!", holdTime);
                _handleButtonPress();
            }
        }
        else if (!buttonState && _buttonPressed)
        {
            unsigned long holdTime = millis() - _buttonPressTime;
            _buttonPressed = false;
            ZENO_LOG_PROV("Button released after %lums", holdTime);
        }
    }

    void WiFiProvisioning::_handleButtonPress()
    {
        startAPMode();
    }

    void WiFiProvisioning::_handleButtonRelease()
    {
        // Reserved for future use
    }

    // ============================================
    // AP Mode
    // ============================================

    void WiFiProvisioning::_startAccessPoint()
    {
        _setState(ProvisioningState::AP_MODE_STARTING);

        WiFi.disconnect();
        delay(100);

        WiFi.mode(WIFI_AP);

        bool success;
        if (_config.apPassword.length() == 0)
        {
            success = WiFi.softAP(_apSSID.c_str());
            ZENO_LOG_PROV("AP Mode: Open network");
        }
        else
        {
            success = WiFi.softAP(_apSSID.c_str(), _config.apPassword.c_str());
            ZENO_LOG_PROV("AP Mode: Password protected");
        }

        if (!success)
        {
            _triggerError("Failed to start AP");
            return;
        }

        ZENO_LOG_PROV("AP Started: %s IP: %s", // sensitive verbose only
                      _apSSID.c_str(), WiFi.softAPIP().toString().c_str());

        _setupWebServer();

        _apStartTime = millis();
        _setState(ProvisioningState::AP_MODE_ACTIVE);

        // Flash LED 3x to confirm AP mode started (visual feedback)
        if (_config.ledPin >= 0)
        {
            ZENO_LOG_PROV("Flashing LED GPIO %d to confirm AP mode...", _config.ledPin);
            for (int i = 0; i < 3; i++)
            {
                _setLED(true);
                delay(150);
                _setLED(false);
                delay(150);
            }
            ZENO_LOG_PROV("LED flash done - continuous blink starting in loop()");
        }
        else
        {
            ZENO_LOG_PROV("WARNING: LED not configured (ledPin=-1) - no blink indicator!");
        }
    }

    void WiFiProvisioning::_stopAccessPoint()
    {
        // Turn off LED when exiting AP mode
        _setLED(false);

        if (_webServer)
        {
            _webServer->stop();
            delete _webServer;
            _webServer = nullptr;
        }

        WiFi.softAPdisconnect(true);
        WiFi.mode(WIFI_STA);

        _setState(ProvisioningState::IDLE);
    }

    void WiFiProvisioning::_checkAPTimeout()
    {
        if (_config.apTimeoutMs > 0)
        {
            unsigned long elapsed = millis() - _apStartTime;
            if (elapsed >= _config.apTimeoutMs)
            {
                ZENO_LOG_PROV("AP mode timeout");
                _setState(ProvisioningState::TIMEOUT);
                _stopAccessPoint();
            }
        }
    }

    // ============================================
    // Web Server (RESTful API Only - No HTML)
    // ============================================

    void WiFiProvisioning::_setupWebServer()
    {
        if (_webServer)
        {
            delete _webServer;
        }

        _webServer = new WebServer(_config.webServerPort);

        // API Endpoints only - No HTML interface
        _webServer->on("/", HTTP_GET, [this]()
                       { _handleRoot(); });
        _webServer->on("/api/info", HTTP_GET, [this]()
                       { _handleGetInfo(); });
        _webServer->on("/api/info", HTTP_OPTIONS, [this]()
                       { _sendCORS(); });
        _webServer->on("/api/networks", HTTP_GET, [this]()
                       { _handleGetNetworks(); });
        _webServer->on("/api/networks", HTTP_OPTIONS, [this]()
                       { _sendCORS(); });
        _webServer->on("/api/config", HTTP_GET, [this]()
                       { _handleGetConfig(); });
        _webServer->on("/api/config", HTTP_POST, [this]()
                       { _handlePostConfig(); });
        _webServer->on("/api/config", HTTP_OPTIONS, [this]()
                       { _sendCORS(); });
        _webServer->on("/api/reset", HTTP_POST, [this]()
                       { _handleReset(); });
        _webServer->on("/api/reset", HTTP_OPTIONS, [this]()
                       { _sendCORS(); });
        // Connect endpoints only register what this firmware supports
        _webServer->on("/api/connect/wifi", HTTP_POST, [this]()
                       { _handleConnectWiFi(); });
        _webServer->on("/api/connect/wifi", HTTP_OPTIONS, [this]()
                       { _sendCORS(); });
        if (_deviceInfo.supportsEthernet())
        {
            _webServer->on("/api/connect/ethernet", HTTP_POST, [this]()
                           { _handleConnectEthernet(); });
            _webServer->on("/api/connect/ethernet", HTTP_OPTIONS, [this]()
                           { _sendCORS(); });
        }
        if (_deviceInfo.supportsCellular())
        {
            _webServer->on("/api/connect/cellular", HTTP_POST, [this]()
                           { _handleConnectCellular(); });
            _webServer->on("/api/connect/cellular", HTTP_OPTIONS, [this]()
                           { _sendCORS(); });
        }
        _webServer->on("/api/verify", HTTP_POST, [this]()
                       { _handleVerifyWiFi(); });
        _webServer->on("/api/verify", HTTP_OPTIONS, [this]()
                       { _sendCORS(); });
        _webServer->onNotFound([this]()
                               { _handleNotFound(); });

        _webServer->begin();
        ZENO_LOG_PROV("API Server started on port %d", _config.webServerPort);
    }

    void WiFiProvisioning::_handleRoot()
    {
        _sendCORS();
        // Return API documentation as JSON
        String json = "{";
        json += "\"name\":\"ZenoPCB WiFi Provisioning API\",";
        json += "\"version\":\"1.3.0\",";
        json += "\"endpoints\":{";
        json += "\"GET /api/info\":\"Device information (includes supportedConnections)\",";
        json += "\"GET /api/networks\":\"Scan WiFi networks (max 20, sorted by signal)\",";
        json += "\"GET /api/config\":\"Get current configuration\",";
        json += "\"POST /api/config\":\"Save configuration\",";
        json += "\"POST /api/verify\":\"Verify WiFi credentials before saving\",";
        json += "\"POST /api/connect/wifi\":\"Connect via WiFi (supports DHCP/Static IP)\"";
        if (_deviceInfo.supportsEthernet())
            json += ",\"POST /api/connect/ethernet\":\"Connect via Ethernet (supports DHCP/Static IP)\"";
        if (_deviceInfo.supportsCellular())
            json += ",\"POST /api/connect/cellular\":\"Connect via 4G/Cellular (APN config)\"";
        json += ",\"POST /api/reset\":\"Factory reset\"";
        json += "}}";
        _webServer->send(200, "application/json", json);
    }

    void WiFiProvisioning::_handleGetInfo()
    {
        _sendCORS();
        String json = _generateConfigJSON();
        _webServer->send(200, "application/json", json);
    }

    void WiFiProvisioning::_handleGetConfig()
    {
        _sendCORS();
        JsonDocument doc;

        doc["configured"] = _deviceConfig.configured;
        doc["isClaimed"] = _deviceConfig.isClaimed;
        doc["connectionType"] = _connectionTypeToString(_deviceConfig.connectionType);
        doc["userId"] = _deviceConfig.userId;
        doc["deviceId"] = _deviceConfig.deviceId;

        // Device info
        doc["deviceType"] = _deviceInfo.typeName;
        doc["deviceModel"] = _deviceInfo.name;
        doc["firmwareVersion"] = _deviceInfo.version;
        doc["manufacturer"] = _deviceInfo.manufacturer;

        // Supported connections (array) same as /api/info
        JsonArray supportedArr = doc["supportedConnections"].to<JsonArray>();
        if (_deviceInfo.supportsWiFi())
            supportedArr.add("wifi");
        if (_deviceInfo.supportsEthernet())
            supportedArr.add("ethernet");
        if (_deviceInfo.supportsCellular())
            supportedArr.add("4g");

        // WiFi settings
        doc["wifiSSID"] = _deviceConfig.wifiSSID;
        // Note: password not returned for security

        // Ethernet settings
        doc["ethernetDHCP"] = _deviceConfig.ethernetDHCP;
        if (!_deviceConfig.ethernetDHCP)
        {
            doc["ethernetIP"] = _deviceConfig.ethernetIP;
            doc["ethernetGateway"] = _deviceConfig.ethernetGateway;
            doc["ethernetSubnet"] = _deviceConfig.ethernetSubnet;
            doc["ethernetDNS"] = _deviceConfig.ethernetDNS;
        }

        // Cellular settings
        doc["cellularAPN"] = _deviceConfig.cellularAPN;

        String output;
        serializeJson(doc, output);
        _webServer->send(200, "application/json", output);
    }

    void WiFiProvisioning::_handleGetNetworks()
    {
        _sendCORS();
        ZENO_LOG_PROV("Scanning WiFi networks...");

        std::vector<WiFiNetwork> networks;
        if (_scanNetworks(networks))
        {
            String json = _generateNetworksJSON(networks);
            _webServer->send(200, "application/json", json);
        }
        else
        {
            _webServer->send(500, "application/json", "{\"success\":false,\"error\":\"Failed to scan networks\"}");
        }
    }

    void WiFiProvisioning::_handlePostConfig()
    {
        _sendCORS();

        if (!_webServer->hasArg("plain"))
        {
            _webServer->send(400, "application/json", "{\"success\":false,\"error\":\"No data received\"}");
            return;
        }

        String body = _webServer->arg("plain");
        ZENO_LOG_PROV("Received config (%d bytes)", body.length());

        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, body);

        if (error)
        {
            ZENO_LOG_PROV("JSON parse error: %s\n", error.c_str());
            _webServer->send(400, "application/json", "{\"success\":false,\"error\":\"Invalid JSON\"}");
            return;
        }

        // Update only provided fields

        // Connection type
        if (!doc["connectionType"].isNull())
        {
            String connType = doc["connectionType"].as<String>();
            _deviceConfig.connectionType = _stringToConnectionType(connType);
        }

        // Device info
        if (!doc["userId"].isNull())
        {
            _deviceConfig.userId = doc["userId"].as<String>();
        }
        if (!doc["deviceId"].isNull())
        {
            _deviceConfig.deviceId = doc["deviceId"].as<String>();
        }

        // WiFi settings
        if (!doc["wifiSSID"].isNull())
        {
            _deviceConfig.wifiSSID = doc["wifiSSID"].as<String>();
        }
        if (!doc["wifiPassword"].isNull())
        {
            _deviceConfig.wifiPassword = doc["wifiPassword"].as<String>();
        }

        // Ethernet settings
        if (!doc["ethernetDHCP"].isNull())
        {
            _deviceConfig.ethernetDHCP = doc["ethernetDHCP"].as<bool>();
        }
        if (!doc["ethernetIP"].isNull())
        {
            _deviceConfig.ethernetIP = doc["ethernetIP"].as<String>();
        }
        if (!doc["ethernetGateway"].isNull())
        {
            _deviceConfig.ethernetGateway = doc["ethernetGateway"].as<String>();
        }
        if (!doc["ethernetSubnet"].isNull())
        {
            _deviceConfig.ethernetSubnet = doc["ethernetSubnet"].as<String>();
        }
        if (!doc["ethernetDNS"].isNull())
        {
            _deviceConfig.ethernetDNS = doc["ethernetDNS"].as<String>();
        }

        // Cellular settings
        if (!doc["cellularAPN"].isNull())
        {
            _deviceConfig.cellularAPN = doc["cellularAPN"].as<String>();
        }
        if (!doc["cellularUser"].isNull())
        {
            _deviceConfig.cellularUser = doc["cellularUser"].as<String>();
        }
        if (!doc["cellularPass"].isNull())
        {
            _deviceConfig.cellularPass = doc["cellularPass"].as<String>();
        }

        // Check if configured based on connection type
        switch (_deviceConfig.connectionType)
        {
        case ConnectionType::WIFI:
            _deviceConfig.configured = _deviceConfig.wifiSSID.length() > 0;
            break;
        case ConnectionType::ETHERNET:
            _deviceConfig.configured = true; // Ethernet always ready if selected
            break;
        case ConnectionType::CELLULAR:
            _deviceConfig.configured = _deviceConfig.cellularAPN.length() > 0;
            break;
        default:
            _deviceConfig.configured = false;
        }

        if (_saveConfig())
        {
            ZENO_LOG_PROV("Configuration saved successfully");

            if (_configCallback)
            {
                _configCallback(_deviceConfig);
            }

            JsonDocument response;
            response["success"] = true;
            response["message"] = "Configuration saved";
            response["configured"] = _deviceConfig.configured;

            String output;
            serializeJson(response, output);
            _webServer->send(200, "application/json", output);
        }
        else
        {
            ZENO_LOG_PROV("Failed to save configuration");
            _webServer->send(500, "application/json", "{\"success\":false,\"error\":\"Failed to save configuration\"}");
        }
    }

    // ============================================
    // Connect WiFi Handler
    // POST /api/connect/wifi
    // ============================================
    void WiFiProvisioning::_handleConnectWiFi()
    {
        _sendCORS();
        ZENO_LOG_PROV("=== POST /api/connect/wifi ===");

        if (!_webServer->hasArg("plain"))
        {
            ZENO_LOG_PROV("No request body, using saved config");
            if (_deviceConfig.wifiSSID.length() == 0)
            {
                _webServer->send(400, "application/json",
                                 "{\"success\":false,\"reason\":\"MISSING_SSID\",\"error\":\"No WiFi SSID configured. Send wifiSSID in request body\"}");
                return;
            }
        }
        else
        {
            String body = _webServer->arg("plain");
            ZENO_LOG_PROV("WiFi connect request (%d bytes)", body.length());
            // Raw body NOT logged may contain plaintext WiFi password

            JsonDocument doc;
            DeserializationError error = deserializeJson(doc, body);
            if (!error)
            {
                if (doc.containsKey("userId"))
                {
                    _deviceConfig.userId = doc["userId"].as<String>();
                    ZENO_LOG_PROV("userId: %s...", _deviceConfig.userId.substring(0, 8).c_str());
                }
                if (doc.containsKey("deviceId"))
                {
                    _deviceConfig.deviceId = doc["deviceId"].as<String>();
                    ZENO_LOG_PROV("deviceId: %s...", _deviceConfig.deviceId.substring(0, 8).c_str());
                }
                if (doc.containsKey("wifiSSID"))
                {
                    _deviceConfig.wifiSSID = doc["wifiSSID"].as<String>();
                    ZENO_LOG_PROV("wifiSSID received"); // SSID not logged
                }
                if (doc.containsKey("wifiPassword"))
                {
                    _deviceConfig.wifiPassword = doc["wifiPassword"].as<String>();
                    // password not logged
                }
                if (doc.containsKey("dhcp"))
                {
                    _deviceConfig.wifiDHCP = doc["dhcp"].as<bool>();
                    ZENO_LOG_PROV("dhcp: %s", _deviceConfig.wifiDHCP ? "true" : "false");
                }
                if (doc.containsKey("ip"))
                    _deviceConfig.wifiIP = doc["ip"].as<String>();
                if (doc.containsKey("gateway"))
                    _deviceConfig.wifiGateway = doc["gateway"].as<String>();
                if (doc.containsKey("subnet"))
                    _deviceConfig.wifiSubnet = doc["subnet"].as<String>();
                if (doc.containsKey("dns"))
                    _deviceConfig.wifiDNS = doc["dns"].as<String>();
            }
            else
            {
                ZENO_LOG_PROV("JSON parse error: %s", error.c_str());
            }
        }

        if (_deviceConfig.wifiSSID.length() == 0)
        {
            _webServer->send(400, "application/json", "{\"success\":false,\"reason\":\"MISSING_SSID\",\"error\":\"wifiSSID is required\"}");
            return;
        }

        // STEP 1: Test WiFi connection 
        ZENO_LOG_PROV("Step 1/3: Testing WiFi connection..."); // SSID not logged
        WiFi.mode(WIFI_AP_STA);
        WiFi.begin(_deviceConfig.wifiSSID.c_str(), _deviceConfig.wifiPassword.c_str());

        int attempts = 0;
        while (WiFi.status() != WL_CONNECTED && attempts < 30) // 15s
        {
            delay(500);
            yield();                          // PITFALL 3 explicit cooperative yield
            _hal.system().feedWatchdog();
            ZENO_LOG_PROV_RAW(".");
            attempts++;
        }
        ZENO_LOG_PROV_RAW("\n");

        if (WiFi.status() != WL_CONNECTED)
        {
            ZENO_LOG_PROV("WiFi connection failed");
            int wifiStatus = WiFi.status();
            WiFi.disconnect(true);
            delay(100);
            WiFi.mode(WIFI_AP);
            WiFi.softAP(_apSSID.c_str(), _config.apPassword.c_str());

            JsonDocument errDoc;
            errDoc["success"] = false;
            errDoc["step"] = "wifi";
            errDoc["reason"] = _getWiFiReasonCode(wifiStatus);
            errDoc["error"] = "WiFi connection failed. Check SSID and password.";
            errDoc["wifiStatus"] = _getWiFiStatusString(wifiStatus);
            String out;
            serializeJson(errDoc, out);
            _webServer->send(401, "application/json", out);
            return;
        }

        String wifiIP = WiFi.localIP().toString();
        int wifiRSSI = WiFi.RSSI();
        ZENO_LOG_PROV("WiFi OK! IP: %s, RSSI: %d", wifiIP.c_str(), wifiRSSI);

        // STEP 2: Test MQTT connection 
        bool mqttOK = true;
        if (_mqttTestCallback)
        {
            ZENO_LOG_PROV("Step 2/3: Testing MQTT broker connection...");
            mqttOK = _mqttTestCallback();

            if (!mqttOK)
            {
                ZENO_LOG_PROV("MQTT connection failed");
                WiFi.disconnect(true);
                delay(100);
                WiFi.mode(WIFI_AP);
                WiFi.softAP(_apSSID.c_str(), _config.apPassword.c_str());

                JsonDocument errDoc;
                errDoc["success"] = false;
                errDoc["step"] = "mqtt";
                errDoc["reason"] = "MQTT_UNREACHABLE";
                errDoc["error"] = "MQTT broker unreachable. WiFi OK but cannot connect to cloud.";
                errDoc["wifiIP"] = wifiIP;
                errDoc["wifiRSSI"] = wifiRSSI;
                String out;
                serializeJson(errDoc, out);
                _webServer->send(503, "application/json", out);
                return;
            }
            ZENO_LOG_PROV("MQTT OK!");
        }
        else
        {
            ZENO_LOG_PROV("No MQTT test callback skipping MQTT check");
        }

        // STEP 3: Claim device via MQTT 
        bool claimOK = true;
        if (_claimCallback && mqttOK)
        {
            ZENO_LOG_PROV("Step 3/3: Claiming device via MQTT...");
            ZENO_LOG_PROV("Claim params: userId=%s deviceId=%s token=%s",
                          _deviceConfig.userId.c_str(),
                          _deviceConfig.deviceId.c_str(),
                          maskToken(_provisionedToken).c_str());
            claimOK = _claimCallback(_deviceConfig.userId, _deviceConfig.deviceId, _provisionedToken);

            if (!claimOK)
            {
                ZENO_LOG_PROV("Device claim failed or timed out");
                WiFi.disconnect(true);
                delay(100);
                WiFi.mode(WIFI_AP);
                WiFi.softAP(_apSSID.c_str(), _config.apPassword.c_str());

                JsonDocument errDoc;
                errDoc["success"] = false;
                errDoc["step"] = "claim";
                errDoc["reason"] = "CLAIM_FAILED";
                errDoc["error"] = "Device claim failed. Server did not acknowledge within timeout.";
                errDoc["wifiIP"] = wifiIP;
                errDoc["wifiRSSI"] = wifiRSSI;
                errDoc["mqttConnected"] = true;
                String out;
                serializeJson(errDoc, out);
                _webServer->send(504, "application/json", out);
                return;
            }
            ZENO_LOG_PROV("Device claimed successfully!");
        }
        else if (!_claimCallback)
        {
            ZENO_LOG_PROV("No claim callback skipping device claim");
        }

        // All steps passed restore AP, save config 
        WiFi.disconnect(true);
        delay(100);
        WiFi.mode(WIFI_AP);
        WiFi.softAP(_apSSID.c_str(), _config.apPassword.c_str());

        _deviceConfig.connectionType = ConnectionType::WIFI;
        _deviceConfig.configured = true;
        _deviceConfig.isClaimed = claimOK;
        _saveConfig();
        ZENO_LOG_PROV("Configuration saved to NVS (claimed: %s)", claimOK ? "true" : "false");

        // Send success response 
        JsonDocument response;
        response["success"] = true;
        response["message"] = claimOK ? "WiFi, MQTT and claim verified. Restarting..." : "WiFi verified. Restarting...";
        response["connectionType"] = "wifi";
        response["ssid"] = _deviceConfig.wifiSSID;
        response["wifiIP"] = wifiIP;
        response["wifiRSSI"] = wifiRSSI;
        response["mqttConnected"] = mqttOK;
        response["claimed"] = claimOK;
        response["dhcp"] = _deviceConfig.wifiDHCP;

        String output;
        serializeJson(response, output);
        ZENO_LOG_PROV("Response: %s", output.c_str());
        _webServer->send(200, "application/json", output);
        ZENO_LOG_PROV("Response sent successfully");

        ZENO_LOG_PROV("Waiting 500ms for response to complete...");
        delay(500);
        ZENO_LOG_PROV("Restarting ESP to apply WiFi configuration...");
        _hal.system().restart();
    }

    // ============================================
    // Connect Ethernet Handler
    // POST /api/connect/ethernet
    // ============================================
    void WiFiProvisioning::_handleConnectEthernet()
    {
        _sendCORS();
        ZENO_LOG_PROV("=== POST /api/connect/ethernet ===");

        if (_webServer->hasArg("plain"))
        {
            String body = _webServer->arg("plain");
            ZENO_LOG_PROV("Ethernet connect request (%d bytes)", body.length());

            JsonDocument doc;
            DeserializationError error = deserializeJson(doc, body);

            if (!error)
            {
                // Device info
                if (doc.containsKey("userId"))
                {
                    _deviceConfig.userId = doc["userId"].as<String>();
                    ZENO_LOG_PROV("userId: %s...", _deviceConfig.userId.substring(0, 8).c_str());
                }
                if (doc.containsKey("deviceId"))
                {
                    _deviceConfig.deviceId = doc["deviceId"].as<String>();
                    ZENO_LOG_PROV("deviceId: %s...", _deviceConfig.deviceId.substring(0, 8).c_str());
                }
                // DHCP setting (default: true)
                if (doc.containsKey("dhcp"))
                {
                    _deviceConfig.ethernetDHCP = doc["dhcp"].as<bool>();
                    ZENO_LOG_PROV("dhcp: %s", _deviceConfig.ethernetDHCP ? "true" : "false");
                }
                // Static IP settings (only used if dhcp=false)
                if (doc.containsKey("ip"))
                {
                    _deviceConfig.ethernetIP = doc["ip"].as<String>();
                    ZENO_LOG_PROV("ip: %s", _deviceConfig.ethernetIP.c_str());
                }
                if (doc.containsKey("gateway"))
                {
                    _deviceConfig.ethernetGateway = doc["gateway"].as<String>();
                    ZENO_LOG_PROV("gateway: %s", _deviceConfig.ethernetGateway.c_str());
                }
                if (doc.containsKey("subnet"))
                {
                    _deviceConfig.ethernetSubnet = doc["subnet"].as<String>();
                    ZENO_LOG_PROV("subnet: %s", _deviceConfig.ethernetSubnet.c_str());
                }
                if (doc.containsKey("dns"))
                {
                    _deviceConfig.ethernetDNS = doc["dns"].as<String>();
                    ZENO_LOG_PROV("dns: %s", _deviceConfig.ethernetDNS.c_str());
                }
            }
            else
            {
                ZENO_LOG_PROV("JSON parse error: %s", error.c_str());
            }
        }
        else
        {
            ZENO_LOG_PROV("No request body, using defaults (DHCP)");
        }

        // Validate static IP if not using DHCP
        if (!_deviceConfig.ethernetDHCP && _deviceConfig.ethernetIP.length() == 0)
        {
            ZENO_LOG_PROV("Error: Static IP is required when dhcp=false");
            _webServer->send(400, "application/json",
                             "{\"success\":false,\"reason\":\"MISSING_STATIC_IP\",\"error\":\"Static IP is required when dhcp=false\"}");
            return;
        }

        // Establish temporary WiFi for MQTT test & Claim 
        // Ethernet isn't active in AP mode, so we use WiFi AP_STA temporarily
        bool wifiTempConnected = false;
        if (_deviceConfig.wifiSSID.length() > 0)
        {
            ZENO_LOG_PROV("Connecting WiFi temporarily for MQTT test & claim...");
            WiFi.mode(WIFI_AP_STA);
            WiFi.begin(_deviceConfig.wifiSSID.c_str(), _deviceConfig.wifiPassword.c_str());
            int attempts = 0;
            while (WiFi.status() != WL_CONNECTED && attempts < 20) // 10s
            {
                delay(500);
                ZENO_LOG_PROV_RAW(".");
                attempts++;
            }
            ZENO_LOG_PROV_RAW("\n");
            wifiTempConnected = (WiFi.status() == WL_CONNECTED);
            if (wifiTempConnected)
            {
                ZENO_LOG_PROV("Temp WiFi OK! IP: %s", WiFi.localIP().toString().c_str());
            }
            else
            {
                ZENO_LOG_PROV("Temp WiFi failed will skip MQTT test & claim");
            }
        }
        else
        {
            ZENO_LOG_PROV("No WiFi credentials will skip MQTT test & claim");
        }

        // STEP 1: Test MQTT connection via WiFiClient 
        bool mqttOK = true;
        if (wifiTempConnected && _mqttTestCallback)
        {
            ZENO_LOG_PROV("Step 1/2: Testing MQTT broker connection...");
            mqttOK = _mqttTestCallback();

            if (!mqttOK)
            {
                ZENO_LOG_PROV("MQTT connection failed");
                WiFi.disconnect(true);
                delay(100);
                WiFi.mode(WIFI_AP);
                WiFi.softAP(_apSSID.c_str(), _config.apPassword.c_str());

                JsonDocument errDoc;
                errDoc["success"] = false;
                errDoc["step"] = "mqtt";
                errDoc["reason"] = "MQTT_UNREACHABLE";
                errDoc["error"] = "MQTT broker unreachable. Cannot connect to cloud.";
                String out;
                serializeJson(errDoc, out);
                _webServer->send(503, "application/json", out);
                return;
            }
            ZENO_LOG_PROV("MQTT OK!");
        }

        // STEP 2: Claim device via MQTT 
        bool claimOK = false;
        if (wifiTempConnected && mqttOK && _claimCallback)
        {
            ZENO_LOG_PROV("Step 2/2: Claiming device via MQTT...");
            claimOK = _claimCallback(_deviceConfig.userId, _deviceConfig.deviceId, _provisionedToken);

            if (!claimOK)
            {
                ZENO_LOG_PROV("Device claim failed for Ethernet config");
                WiFi.disconnect(true);
                delay(100);
                WiFi.mode(WIFI_AP);
                WiFi.softAP(_apSSID.c_str(), _config.apPassword.c_str());

                JsonDocument errDoc;
                errDoc["success"] = false;
                errDoc["step"] = "claim";
                errDoc["reason"] = "CLAIM_FAILED";
                errDoc["error"] = "Device claim failed. Server did not acknowledge within timeout.";
                String out;
                serializeJson(errDoc, out);
                _webServer->send(504, "application/json", out);
                return;
            }
            ZENO_LOG_PROV("Device claimed successfully!");
        }
        else if (!wifiTempConnected)
        {
            ZENO_LOG_PROV("No network for MQTT/claim will complete on first Ethernet boot");
        }

        // Cleanup temporary WiFi
        if (wifiTempConnected)
        {
            WiFi.disconnect(true);
            delay(100);
            WiFi.mode(WIFI_AP);
            WiFi.softAP(_apSSID.c_str(), _config.apPassword.c_str());
        }

        // Update connection type
        _deviceConfig.connectionType = ConnectionType::ETHERNET;
        _deviceConfig.configured = true;
        _deviceConfig.isClaimed = claimOK;
        _saveConfig();
        ZENO_LOG_PROV("Ethernet config saved. DHCP: %s, claimed: %s",
                      _deviceConfig.ethernetDHCP ? "true" : "false",
                      claimOK ? "true" : "false");

        // Build response
        JsonDocument response;
        response["success"] = true;
        response["message"] = "Ethernet mode configured. Exiting AP mode...";
        response["connectionType"] = "ethernet";
        response["dhcp"] = _deviceConfig.ethernetDHCP;
        response["claimed"] = claimOK;
        if (!_deviceConfig.ethernetDHCP)
        {
            response["ip"] = _deviceConfig.ethernetIP;
            response["gateway"] = _deviceConfig.ethernetGateway;
            response["subnet"] = _deviceConfig.ethernetSubnet;
            response["dns"] = _deviceConfig.ethernetDNS;
        }

        String output;
        serializeJson(response, output);
        ZENO_LOG_PROV("Response: %s", output.c_str());
        _webServer->send(200, "application/json", output);
        ZENO_LOG_PROV("Response sent successfully");

        // Non-blocking: Set timer to stop AP after 500ms
        _pendingAPShutdownTime = millis() + 500;
        _pendingConnectWiFi = false; // Ethernet doesn't need WiFi connection
        ZENO_LOG_PROV("AP shutdown scheduled in 500ms");
    }

    // ============================================
    // Connect Cellular Handler
    // POST /api/connect/cellular
    // ============================================
    void WiFiProvisioning::_handleConnectCellular()
    {
        _sendCORS();
        ZENO_LOG_PROV("=== POST /api/connect/cellular ===");

        if (_webServer->hasArg("plain"))
        {
            String body = _webServer->arg("plain");
            ZENO_LOG_PROV("Cellular connect request (%d bytes)", body.length());

            JsonDocument doc;
            DeserializationError error = deserializeJson(doc, body);

            if (!error)
            {
                // Device info
                if (doc.containsKey("userId"))
                {
                    _deviceConfig.userId = doc["userId"].as<String>();
                    ZENO_LOG_PROV("userId: %s...", _deviceConfig.userId.substring(0, 8).c_str());
                }
                if (doc.containsKey("deviceId"))
                {
                    _deviceConfig.deviceId = doc["deviceId"].as<String>();
                    ZENO_LOG_PROV("deviceId: %s...", _deviceConfig.deviceId.substring(0, 8).c_str());
                }
                // APN settings
                if (doc.containsKey("apn"))
                {
                    _deviceConfig.cellularAPN = doc["apn"].as<String>();
                    ZENO_LOG_PROV("apn: %s", _deviceConfig.cellularAPN.c_str());
                }
                if (doc.containsKey("user"))
                {
                    _deviceConfig.cellularUser = doc["user"].as<String>();
                    ZENO_LOG_PROV("user: %s", _deviceConfig.cellularUser.c_str());
                }
                if (doc.containsKey("pass"))
                {
                    _deviceConfig.cellularPass = doc["pass"].as<String>();
                    ZENO_LOG_PROV("pass: ****");
                }
            }
            else
            {
                ZENO_LOG_PROV("JSON parse error: %s", error.c_str());
            }
        }
        else
        {
            ZENO_LOG_PROV("No request body");
        }

        // APN is usually required
        if (_deviceConfig.cellularAPN.length() == 0)
        {
            ZENO_LOG_PROV("Error: APN is required");
            _webServer->send(400, "application/json",
                             "{\"success\":false,\"reason\":\"MISSING_APN\",\"error\":\"APN is required for cellular connection\"}");
            return;
        }

        // Establish temporary WiFi for MQTT test & Claim 
        // 4G modem isn't used from WiFiProvisioning, so we use WiFi AP_STA temporarily
        bool wifiTempConnected = false;
        if (_deviceConfig.wifiSSID.length() > 0)
        {
            ZENO_LOG_PROV("Connecting WiFi temporarily for MQTT test & claim...");
            WiFi.mode(WIFI_AP_STA);
            WiFi.begin(_deviceConfig.wifiSSID.c_str(), _deviceConfig.wifiPassword.c_str());
            int attempts = 0;
            while (WiFi.status() != WL_CONNECTED && attempts < 20) // 10s
            {
                delay(500);
                ZENO_LOG_PROV_RAW(".");
                attempts++;
            }
            ZENO_LOG_PROV_RAW("\n");
            wifiTempConnected = (WiFi.status() == WL_CONNECTED);
            if (wifiTempConnected)
            {
                ZENO_LOG_PROV("Temp WiFi OK! IP: %s", WiFi.localIP().toString().c_str());
            }
            else
            {
                ZENO_LOG_PROV("Temp WiFi failed will skip MQTT test & claim");
            }
        }
        else
        {
            ZENO_LOG_PROV("No WiFi credentials will skip MQTT test & claim");
        }

        // STEP 1: Test MQTT connection via WiFiClient 
        bool mqttOK = true;
        if (wifiTempConnected && _mqttTestCallback)
        {
            ZENO_LOG_PROV("Step 1/2: Testing MQTT broker connection...");
            mqttOK = _mqttTestCallback();

            if (!mqttOK)
            {
                ZENO_LOG_PROV("MQTT connection failed");
                WiFi.disconnect(true);
                delay(100);
                WiFi.mode(WIFI_AP);
                WiFi.softAP(_apSSID.c_str(), _config.apPassword.c_str());

                JsonDocument errDoc;
                errDoc["success"] = false;
                errDoc["step"] = "mqtt";
                errDoc["reason"] = "MQTT_UNREACHABLE";
                errDoc["error"] = "MQTT broker unreachable. Cannot connect to cloud.";
                String out;
                serializeJson(errDoc, out);
                _webServer->send(503, "application/json", out);
                return;
            }
            ZENO_LOG_PROV("MQTT OK!");
        }

        // STEP 2: Claim device via MQTT 
        bool claimOK = false;
        if (wifiTempConnected && mqttOK && _claimCallback)
        {
            ZENO_LOG_PROV("Step 2/2: Claiming device via MQTT...");
            claimOK = _claimCallback(_deviceConfig.userId, _deviceConfig.deviceId, _provisionedToken);

            if (!claimOK)
            {
                ZENO_LOG_PROV("Device claim failed for Cellular config");
                WiFi.disconnect(true);
                delay(100);
                WiFi.mode(WIFI_AP);
                WiFi.softAP(_apSSID.c_str(), _config.apPassword.c_str());

                JsonDocument errDoc;
                errDoc["success"] = false;
                errDoc["step"] = "claim";
                errDoc["reason"] = "CLAIM_FAILED";
                errDoc["error"] = "Device claim failed. Server did not acknowledge within timeout.";
                String out;
                serializeJson(errDoc, out);
                _webServer->send(504, "application/json", out);
                return;
            }
            ZENO_LOG_PROV("Device claimed successfully!");
        }
        else if (!wifiTempConnected)
        {
            ZENO_LOG_PROV("No network for MQTT/claim will complete on first Cellular boot");
        }

        // Cleanup temporary WiFi
        if (wifiTempConnected)
        {
            WiFi.disconnect(true);
            delay(100);
            WiFi.mode(WIFI_AP);
            WiFi.softAP(_apSSID.c_str(), _config.apPassword.c_str());
        }

        // Update connection type
        _deviceConfig.connectionType = ConnectionType::CELLULAR;
        _deviceConfig.configured = true;
        _deviceConfig.isClaimed = claimOK;
        _saveConfig();
        ZENO_LOG_PROV("Cellular config saved. APN: %s, claimed: %s",
                      _deviceConfig.cellularAPN.c_str(), claimOK ? "true" : "false");

        // Build response
        JsonDocument response;
        response["success"] = true;
        response["message"] = "Cellular mode configured. Exiting AP mode...";
        response["connectionType"] = "cellular";
        response["apn"] = _deviceConfig.cellularAPN;
        response["claimed"] = claimOK;

        String output;
        serializeJson(response, output);
        ZENO_LOG_PROV("Response: %s", output.c_str());
        _webServer->send(200, "application/json", output);
        ZENO_LOG_PROV("Response sent successfully");

        // Non-blocking: Set timer to stop AP after 500ms
        _pendingAPShutdownTime = millis() + 500;
        _pendingConnectWiFi = false; // Cellular doesn't need WiFi connection
        ZENO_LOG_PROV("AP shutdown scheduled in 500ms");
    }

    void WiFiProvisioning::_handleVerifyWiFi()
    {
        _sendCORS();

        if (!_webServer->hasArg("plain"))
        {
            _webServer->send(400, "application/json", "{\"success\":false,\"error\":\"No data received\"}");
            return;
        }

        String body = _webServer->arg("plain");
        ZENO_LOG_PROV("Verifying WiFi credentials...");

        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, body);

        if (error)
        {
            _webServer->send(400, "application/json", "{\"success\":false,\"error\":\"Invalid JSON\"}");
            return;
        }

        String ssid = doc["wifiSSID"].as<String>();
        String password = doc["wifiPassword"].as<String>();

        if (ssid.length() == 0)
        {
            _webServer->send(400, "application/json", "{\"success\":false,\"error\":\"wifiSSID is required\"}");
            return;
        }

        ZENO_LOG_PROV("Testing connection to: %s", ssid.c_str());

        // Lu mode hin ti
        wifi_mode_t currentMode = WiFi.getMode();

        // Chuyn sang AP_STA c th test kt ni m vn gi AP
        WiFi.mode(WIFI_AP_STA);

        // Th kt ni
        WiFi.begin(ssid.c_str(), password.c_str());

        // i kt ni vi timeout 15 giy
        int attempts = 0;
        const int maxAttempts = 30; // 30 * 500ms = 15s
        while (WiFi.status() != WL_CONNECTED && attempts < maxAttempts)
        {
            delay(500);
            ZENO_LOG_PROV_RAW(".");
            attempts++;
        }
        ZENO_LOG_PROV_RAW("\n");

        bool connected = (WiFi.status() == WL_CONNECTED);
        String ip = connected ? WiFi.localIP().toString() : "";
        int rssi = connected ? WiFi.RSSI() : 0;

        // Ngt kt ni test
        WiFi.disconnect(true);
        delay(100);

        // Khi phc mode AP
        WiFi.mode(WIFI_AP);
        WiFi.softAP(_apSSID.c_str(), _config.apPassword.c_str());

        // To response
        JsonDocument response;
        response["success"] = connected;

        if (connected)
        {
            response["message"] = "WiFi credentials are valid";
            response["ip"] = ip;
            response["rssi"] = rssi;
            ZENO_LOG_PROV("WiFi verified OK! IP: %s, RSSI: %d", ip.c_str(), rssi);
        }
        else
        {
            response["message"] = "WiFi connection failed. Check SSID and password.";
            response["error"] = _getWiFiStatusString(WiFi.status());
            ZENO_LOG_PROV("WiFi verification FAILED");
        }

        String output;
        serializeJson(response, output);
        _webServer->send(connected ? 200 : 401, "application/json", output);
    }

    void WiFiProvisioning::_handleReset()
    {
        _sendCORS();
        _webServer->send(200, "application/json", "{\"success\":true,\"message\":\"Factory reset in progress...\"}");
        delay(500);
        factoryReset();
    }

    void WiFiProvisioning::_handleNotFound()
    {
        _sendCORS();
        _webServer->send(404, "application/json", "{\"error\":\"Not Found\",\"message\":\"Use GET / for API documentation\"}");
    }

    void WiFiProvisioning::_sendCORS()
    {
        _webServer->sendHeader("Access-Control-Allow-Origin", "*");
        _webServer->sendHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
        _webServer->sendHeader("Access-Control-Allow-Headers", "Content-Type, Authorization");
        if (_webServer->method() == HTTP_OPTIONS)
        {
            _webServer->send(200);
        }
    }

    // ============================================
    // WiFi
    // ============================================

    void WiFiProvisioning::_connectToWiFi()
    {
        if (_deviceConfig.wifiSSID.length() == 0)
        {
            ZENO_LOG_PROV("No WiFi SSID configured");
            return;
        }

        ZENO_LOG_PROV("Connecting to WiFi: %s\n", _deviceConfig.wifiSSID.c_str());
        ZENO_LOG_PROV("Hostname: %s\n", _apSSID.c_str());
        _setState(ProvisioningState::CONNECTING_WIFI);

        // Set hostname - order is critical for ESP32
        WiFi.disconnect(true); // Disconnect and clear stored credentials
        WiFi.mode(WIFI_OFF);   // Turn off WiFi completely
        delay(100);
        WiFi.mode(WIFI_STA);
        WiFi.setHostname(_apSSID.c_str());
        WiFi.begin(_deviceConfig.wifiSSID.c_str(), _deviceConfig.wifiPassword.c_str());

        int attempts = 0;
        while (WiFi.status() != WL_CONNECTED && attempts < 40)
        {
            delay(500);
            ZENO_LOG_PROV_RAW(".");
            attempts++;
        }

        if (WiFi.status() == WL_CONNECTED)
        {
            ZENO_LOG_PROV("Connected! IP: %s", WiFi.localIP().toString().c_str());
            _setState(ProvisioningState::CONNECTED);

            // Reset reconnect tracking
            _wasConnected = true;
            _reconnectAttempts = 0;
            _reconnectInterval = 5000; // Reset to initial interval

            if (_wifiCallback)
            {
                _wifiCallback();
            }
        }
        else
        {
            ZENO_LOG_PROV("Connection failed");
            _setState(ProvisioningState::FAILED);
            _triggerError("Failed to connect to WiFi");
        }
    }

    void WiFiProvisioning::_checkWiFiConnection()
    {
        // Khng chy reconnect khi ang AP mode
        if (_state == ProvisioningState::AP_MODE_ACTIVE ||
            _state == ProvisioningState::AP_MODE_STARTING)
        {
            return;
        }

        // Only check if we have a configured WiFi
        if (!_deviceConfig.configured || _deviceConfig.wifiSSID.length() == 0)
        {
            return;
        }

        bool currentlyConnected = (WiFi.status() == WL_CONNECTED);

        // WiFi just disconnected
        if (_wasConnected && !currentlyConnected)
        {
            ZENO_LOG_PROV("WiFi disconnected! Starting reconnect...");
            _wasConnected = false;
            _setState(ProvisioningState::DISCONNECTED);
            _lastReconnectAttempt = 0; // Force immediate reconnect attempt
            _reconnectAttempts = 0;
            _reconnectInterval = 5000; // Reset interval
        }

        // WiFi just connected (from reconnect)
        if (!_wasConnected && currentlyConnected)
        {
            ZENO_LOG_PROV("WiFi reconnected! IP: %s", WiFi.localIP().toString().c_str());
            _wasConnected = true;
            _reconnectAttempts = 0;
            _reconnectInterval = 5000;
            _setState(ProvisioningState::CONNECTED);

            if (_wifiCallback)
            {
                _wifiCallback();
            }
            return;
        }

        // Attempt reconnect if disconnected
        if (!currentlyConnected && _state != ProvisioningState::AP_MODE_ACTIVE)
        {
            _attemptReconnect();
        }
    }

    void WiFiProvisioning::_attemptReconnect()
    {
        unsigned long currentTime = millis();

        // Check if enough time has passed since last attempt
        if (currentTime - _lastReconnectAttempt < _reconnectInterval)
        {
            return;
        }

        _lastReconnectAttempt = currentTime;
        _reconnectAttempts++;

        ZENO_LOG_PROV("Reconnect attempt %d (interval: %dms)",
                      _reconnectAttempts, _reconnectInterval);

        // Non-blocking reconnect - just start the connection
        WiFi.disconnect(true);
        WiFi.mode(WIFI_STA);
        WiFi.setHostname(_apSSID.c_str()); // Keep hostname as ZENO-{ChipID}
        WiFi.begin(_deviceConfig.wifiSSID.c_str(), _deviceConfig.wifiPassword.c_str());

        // Exponential backoff: 5s -> 10s -> 20s -> 40s -> 60s (max 60s)
        _reconnectInterval = min(_reconnectInterval * 2, (uint32_t)60000);
    }

    bool WiFiProvisioning::_scanNetworks(std::vector<WiFiNetwork> &networks)
    {
        // PITFALL 3 synchronous WiFi.scanNetworks blocks 2-5s on ESP8266
        // single-threaded cooperative loop. Feed WDT before + after to bracket the
        // blocking window.
        _hal.system().feedWatchdog();
        int n = WiFi.scanNetworks();
        _hal.system().feedWatchdog();

        if (n == -1)
        {
            ZENO_LOG_PROV("WiFi scan failed");
            return false;
        }

        ZENO_LOG_PROV("Found %d networks, filtering...\n", n);

        // Temporary map to store best signal for each SSID (filter duplicates)
        std::map<String, WiFiNetwork> uniqueNetworks;

        for (int i = 0; i < n; i++)
        {
            String ssid = WiFi.SSID(i);
            int32_t rssi = WiFi.RSSI(i);
            uint8_t encType = WiFi.encryptionType(i);

            // Skip empty SSIDs
            if (ssid.length() == 0)
            {
                continue;
            }

            // Keep only the strongest signal for duplicate SSIDs
            auto it = uniqueNetworks.find(ssid);
            if (it == uniqueNetworks.end() || rssi > it->second.rssi)
            {
                uniqueNetworks[ssid] = WiFiNetwork(ssid, rssi, encType);
            }
        }

        // Convert map to vector
        networks.clear();
        for (auto &pair : uniqueNetworks)
        {
            networks.push_back(pair.second);
        }

        // Sort by signal strength (strongest first)
        std::sort(networks.begin(), networks.end(), [](const WiFiNetwork &a, const WiFiNetwork &b)
                  { return a.rssi > b.rssi; });

        // Limit to top 20 networks
        const size_t MAX_NETWORKS = 20;
        if (networks.size() > MAX_NETWORKS)
        {
            networks.resize(MAX_NETWORKS);
        }

        ZENO_LOG_PROV("Returning %d unique networks (max 20)\n", networks.size());

        WiFi.scanDelete();
        return true;
    }

    // ============================================
    // Storage (NVS)
    // ============================================

    bool WiFiProvisioning::_loadConfig()
    {
        // route through IZenoNVS. Namespace literal "zenopcb"
        // and every key name preserved byte-for-byte per
        // 1.1 (mitigation devices keep saved Wi-Fi config across
        // the refactor).

        // First try readonly mode
        if (!_hal.nvs().begin("zenopcb", true))
        {
            // Namespace doesn't exist yet create it by opening in write mode
            ZENO_LOG_PROV("NVS namespace not found, creating...");
            if (_hal.nvs().begin("zenopcb", false))
            {
                _hal.nvs().end();
                ZENO_LOG_PROV("NVS namespace created");
                // Re-open in readonly mode
                if (!_hal.nvs().begin("zenopcb", true))
                {
                    ZENO_LOG_PROV("Failed to open NVS after creation");
                    return false;
                }
            }
            else
            {
                ZENO_LOG_PROV("Failed to create NVS namespace");
                return false;
            }
        }

        _deviceConfig.configured = _hal.nvs().getBool("configured", false);

        if (_deviceConfig.configured)
        {
            // Connection type
            _deviceConfig.connectionType = static_cast<ConnectionType>(_hal.nvs().getUChar("connType", 1)); // Default: WIFI

            // Device info (bounded buffers per AUDIT 1.1 sizing)
            char userIdBuf[BUF_USERID];
            char deviceIdBuf[BUF_DEVICEID];
            _hal.nvs().getString("userId", userIdBuf, sizeof(userIdBuf), "");
            _hal.nvs().getString("deviceId", deviceIdBuf, sizeof(deviceIdBuf), "");
            _deviceConfig.userId = String(userIdBuf);
            _deviceConfig.deviceId = String(deviceIdBuf);

            // WiFi settings
            char ssidBuf[BUF_SSID];
            char wifiPassBuf[BUF_WIFIPASS];
            _hal.nvs().getString("wifiSSID", ssidBuf, sizeof(ssidBuf), "");
            _hal.nvs().getString("wifiPass", wifiPassBuf, sizeof(wifiPassBuf), "");
            _deviceConfig.wifiSSID = String(ssidBuf);
            _deviceConfig.wifiPassword = String(wifiPassBuf);

            // Ethernet settings
            _deviceConfig.ethernetDHCP = _hal.nvs().getBool("ethDHCP", true);
            char ethIPBuf[BUF_IP];
            char ethGWBuf[BUF_IP];
            char ethSNBuf[BUF_IP];
            char ethDNSBuf[BUF_IP];
            _hal.nvs().getString("ethIP", ethIPBuf, sizeof(ethIPBuf), "");
            _hal.nvs().getString("ethGW", ethGWBuf, sizeof(ethGWBuf), "");
            _hal.nvs().getString("ethSN", ethSNBuf, sizeof(ethSNBuf), "");
            _hal.nvs().getString("ethDNS", ethDNSBuf, sizeof(ethDNSBuf), "");
            _deviceConfig.ethernetIP = String(ethIPBuf);
            _deviceConfig.ethernetGateway = String(ethGWBuf);
            _deviceConfig.ethernetSubnet = String(ethSNBuf);
            _deviceConfig.ethernetDNS = String(ethDNSBuf);

            // Cellular settings
            char apnBuf[BUF_APN];
            char cellUserBuf[BUF_CELLUSER];
            char cellPassBuf[BUF_CELLPASS];
            _hal.nvs().getString("cellAPN", apnBuf, sizeof(apnBuf), "");
            _hal.nvs().getString("cellUser", cellUserBuf, sizeof(cellUserBuf), "");
            _hal.nvs().getString("cellPass", cellPassBuf, sizeof(cellPassBuf), "");
            _deviceConfig.cellularAPN = String(apnBuf);
            _deviceConfig.cellularUser = String(cellUserBuf);
            _deviceConfig.cellularPass = String(cellPassBuf);

            // Claim status
            _deviceConfig.isClaimed = _hal.nvs().getBool("isClaimed", false);
        }

        _hal.nvs().end();
        return _deviceConfig.configured;
    }

    bool WiFiProvisioning::_saveConfig()
    {
        if (!_hal.nvs().begin("zenopcb", false))
        {
            ZENO_LOG_PROV("Failed to open NVS for writing");
            return false;
        }

        _hal.nvs().putBool("configured", _deviceConfig.configured);

        // Connection type
        _hal.nvs().putUChar("connType", static_cast<uint8_t>(_deviceConfig.connectionType));

        // Device info
        _hal.nvs().putString("userId", _deviceConfig.userId.c_str());
        _hal.nvs().putString("deviceId", _deviceConfig.deviceId.c_str());

        // WiFi settings
        _hal.nvs().putString("wifiSSID", _deviceConfig.wifiSSID.c_str());
        _hal.nvs().putString("wifiPass", _deviceConfig.wifiPassword.c_str());

        // Ethernet settings
        _hal.nvs().putBool("ethDHCP", _deviceConfig.ethernetDHCP);
        _hal.nvs().putString("ethIP", _deviceConfig.ethernetIP.c_str());
        _hal.nvs().putString("ethGW", _deviceConfig.ethernetGateway.c_str());
        _hal.nvs().putString("ethSN", _deviceConfig.ethernetSubnet.c_str());
        _hal.nvs().putString("ethDNS", _deviceConfig.ethernetDNS.c_str());

        // Cellular settings
        _hal.nvs().putString("cellAPN", _deviceConfig.cellularAPN.c_str());
        _hal.nvs().putString("cellUser", _deviceConfig.cellularUser.c_str());
        _hal.nvs().putString("cellPass", _deviceConfig.cellularPass.c_str());

        // Claim status
        _hal.nvs().putBool("isClaimed", _deviceConfig.isClaimed);

        _hal.nvs().end();

        ZENO_LOG_PROV("Configuration saved to NVS");
        return true;
    }

    void WiFiProvisioning::_clearConfig()
    {
        if (!_hal.nvs().begin("zenopcb", false))
        {
            ZENO_LOG_PROV("Failed to open NVS for clearing");
            return;
        }

        _hal.nvs().clear();
        _hal.nvs().end();

        _deviceConfig = DeviceConfig();
        ZENO_LOG_PROV("Configuration cleared");
    }

    // ============================================
    // State Management
    // ============================================

    void WiFiProvisioning::_setState(ProvisioningState newState)
    {
        if (_state != newState)
        {
            _state = newState;
            ZENO_LOG_PROV("State changed: %d\n", (int)newState);

            if (_stateCallback)
            {
                _stateCallback(newState);
            }
        }
    }

    void WiFiProvisioning::_triggerError(const String &error)
    {
        ZENO_LOG_PROV("Error: %s\n", error.c_str());

        if (_errorCallback)
        {
            _errorCallback(error);
        }
    }

    // ============================================
    // Utility
    // ============================================

    String WiFiProvisioning::_getChipID()
    {
        // HAL impl preserves the exact "%08X" format on the
        // upper 32 bits of ESP.getEfuseMac() byte-for-byte identical to
        // the previous SSID suffix.
        char chipIdStr[13];
        _hal.system().getUniqueId(chipIdStr, sizeof(chipIdStr));
        return String(chipIdStr);
    }

    String WiFiProvisioning::_encryptionTypeToString(uint8_t type)
    {
#if defined(ESP32)
        switch (type)
        {
        case WIFI_AUTH_OPEN:
            return "Open";
        case WIFI_AUTH_WEP:
            return "WEP";
        case WIFI_AUTH_WPA_PSK:
            return "WPA";
        case WIFI_AUTH_WPA2_PSK:
            return "WPA2";
        case WIFI_AUTH_WPA_WPA2_PSK:
            return "WPA/WPA2";
        case WIFI_AUTH_WPA2_ENTERPRISE:
            return "WPA2-Enterprise";
        default:
            return "Unknown";
        }
#elif defined(ESP8266)
        // ESP8266 uses ENC_TYPE_* see ESP8266WiFiType.h. The value
        // space is smaller than ESP32's WIFI_AUTH_* (no Enterprise
        // distinction); we map what is available.
        switch (type)
        {
        case ENC_TYPE_NONE:
            return "Open";
        case ENC_TYPE_WEP:
            return "WEP";
        case ENC_TYPE_TKIP:
            return "WPA";
        case ENC_TYPE_CCMP:
            return "WPA2";
        case ENC_TYPE_AUTO:
            return "WPA/WPA2";
        default:
            return "Unknown";
        }
#endif
    }

    String WiFiProvisioning::_connectionTypeToString(ConnectionType type)
    {
        switch (type)
        {
        case ConnectionType::WIFI:
            return "wifi";
        case ConnectionType::ETHERNET:
            return "ethernet";
        case ConnectionType::CELLULAR:
            return "4g";
        case ConnectionType::NONE:
        default:
            return "none";
        }
    }

    ConnectionType WiFiProvisioning::_stringToConnectionType(const String &str)
    {
        if (str == "wifi")
            return ConnectionType::WIFI;
        if (str == "ethernet")
            return ConnectionType::ETHERNET;
        if (str == "4g" || str == "cellular")
            return ConnectionType::CELLULAR;
        return ConnectionType::NONE;
    }

    String WiFiProvisioning::_generateConfigJSON()
    {
        JsonDocument doc;

        // Device identification
        doc["chipId"] = _getChipID();
        doc["apSSID"] = _apSSID;

        // Device info (set at compile time)
        doc["deviceType"] = _deviceInfo.typeName;
        doc["deviceModel"] = _deviceInfo.name;
        doc["firmwareVersion"] = _deviceInfo.version;
        doc["manufacturer"] = _deviceInfo.manufacturer;

        // Provisioned device credentials (needed by mobile app for cloud pairing)
        doc["provisionedDeviceId"] = _provisionedDeviceId;
        doc["provisionedToken"] = _provisionedToken;

        // Supported connections (array)
        JsonArray supportedArr = doc["supportedConnections"].to<JsonArray>();
        if (_deviceInfo.supportsWiFi())
            supportedArr.add("wifi");
        if (_deviceInfo.supportsEthernet())
            supportedArr.add("ethernet");
        if (_deviceInfo.supportsCellular())
            supportedArr.add("4g");

        // User configuration
        doc["configured"] = _deviceConfig.configured;
        doc["connectionType"] = _connectionTypeToString(_deviceConfig.connectionType);
        doc["userId"] = _deviceConfig.userId;
        doc["deviceId"] = _deviceConfig.deviceId;
        doc["wifiSSID"] = _deviceConfig.wifiSSID;
        doc["isClaimed"] = _deviceConfig.isClaimed;
        doc["wifiConnected"] = isWiFiConnected();

        if (isWiFiConnected())
        {
            doc["wifiIP"] = WiFi.localIP().toString();
            doc["wifiRSSI"] = WiFi.RSSI();
        }

        String output;
        serializeJson(doc, output);
        return output;
    }

    String WiFiProvisioning::_generateNetworksJSON(const std::vector<WiFiNetwork> &networks)
    {
        JsonDocument doc;
        JsonArray arr = doc["networks"].to<JsonArray>();

        for (const auto &net : networks)
        {
            JsonObject obj = arr.add<JsonObject>();
            obj["ssid"] = net.ssid;
            obj["rssi"] = net.rssi;
            obj["encryption"] = _encryptionTypeToString(net.encryptionType);
        }

        String output;
        serializeJson(doc, output);
        return output;
    }

    String WiFiProvisioning::_getWiFiStatusString(int status)
    {
        switch (status)
        {
        case WL_IDLE_STATUS:
            return "Idle";
        case WL_NO_SSID_AVAIL:
            return "SSID not found";
        case WL_SCAN_COMPLETED:
            return "Scan completed";
        case WL_CONNECTED:
            return "Connected";
        case WL_CONNECT_FAILED:
            return "Connection failed";
        case WL_CONNECTION_LOST:
            return "Connection lost";
        case WL_DISCONNECTED:
            return "Disconnected";
        case WL_NO_SHIELD:
            return "No WiFi shield";
        default:
            return "Wrong password or unknown error";
        }
    }

    String WiFiProvisioning::_getWiFiReasonCode(int status)
    {
        switch (status)
        {
        case WL_NO_SSID_AVAIL:
            return "NETWORK_NOT_FOUND";
        case WL_CONNECT_FAILED:
            return "WRONG_PASSWORD";
        case WL_CONNECTION_LOST:
            return "CONNECTION_LOST";
        case WL_DISCONNECTED:
        case WL_IDLE_STATUS:
            return "CONNECTION_TIMEOUT";
        default:
            return "WRONG_PASSWORD";
        }
    }

    // ============================================
    // LED Indicator - Public API
    // ============================================

    void WiFiProvisioning::setLEDBlink(uint32_t intervalMs)
    {
        if (intervalMs > 0)
        {
            _config.ledBlinkInterval = intervalMs;
        }
    }

    void WiFiProvisioning::updateLED()
    {
        _updateLED();
    }

    // ============================================
    // LED Indicator
    // ============================================

    void WiFiProvisioning::_initLED()
    {
        if (_config.ledPin >= 0)
        {
            pinMode(_config.ledPin, OUTPUT);
            _setLED(false); // Start with LED off
            ZENO_LOG_PROV("LED initialized on GPIO %d (Active %s, Blink %ums)",
                          _config.ledPin,
                          _config.ledActiveHigh ? "HIGH" : "LOW",
                          (unsigned int)_config.ledBlinkInterval);
        }
        else
        {
            ZENO_LOG_PROV("LED disabled (ledPin=-1)");
        }
    }

    void WiFiProvisioning::_updateLED()
    {
        // Only blink if LED is configured
        if (_config.ledPin < 0)
        {
            return;
        }

        // One-time confirmation that LED blink loop is running
        static bool _ledLoopConfirmed = false;
        if (!_ledLoopConfirmed)
        {
            _ledLoopConfirmed = true;
            ZENO_LOG_PROV("LED blink loop started on GPIO %d (interval=%ums)",
                          _config.ledPin, (unsigned int)_config.ledBlinkInterval);
        }

        unsigned long now = millis();
        if (now - _lastLedToggle >= _config.ledBlinkInterval)
        {
            _lastLedToggle = now;
            _ledState = !_ledState;
            _setLED(_ledState);
        }
    }

    void WiFiProvisioning::_setLED(bool on)
    {
        if (_config.ledPin < 0)
        {
            return;
        }

        // Handle active high or active low LED
        if (_config.ledActiveHigh)
        {
            digitalWrite(_config.ledPin, on ? HIGH : LOW);
        }
        else
        {
            digitalWrite(_config.ledPin, on ? LOW : HIGH);
        }
    }

} // namespace ZenoPCB

#elif !defined(ZENOPCB_DISABLE_PROVISIONING)  // UNO R4 / STM32 stub block (kept for builds without DISABLE_PROVISIONING)
// ============================================================================
// whole-class capability-gate stub block.
//
// UNO R4 has no captive-portal HTTP-server analog (RESEARCH A1, 3-week
// port deferred to v0.4.0); STM32 has no AP-mode hardware. Every public
// method in WiFiProvisioning stubs to failure with a one-time warn log
// so existing call sites compile + link, while runtime callers get a
// clear signal that the feature is unavailable on this platform.
//
// extension: when `-DZENOPCB_DISABLE_PROVISIONING` is set
// (e.g. ZENOPCB_MICRO_BASIC profile on F103 Blue Pill, 64KB Flash), this
// stub block also compiles out callers must guard their _initProvisioning
// + _wifiProvisioning use sites with the same flag (done in ZenoPCB.cpp).
//
// Members `_hal` (IZenoHal&), `_config`, etc. are still declared in the
// header; the stub default ctor sets `_webServer = nullptr` so the dtor
// can run without UB. Callbacks (`_stateCallback`, etc.) are stored
// (harmless on non-supported platforms) so `onStateChange()` etc. behave
// as no-ops that don't lose the user's lambda.
// ============================================================================

namespace ZenoPCB
{
    namespace
    {
        // One-time log helpers keeps the WiFiProvisioning stub bodies quiet
        // after the first invocation per method, since they may be called in
        // tight loops (loop(), updateLED()).
        static bool g_wifiProvLogged_begin = false;
        static bool g_wifiProvLogged_loop = false;
        static bool g_wifiProvLogged_startAP = false;
        static bool g_wifiProvLogged_stopAP = false;
        static bool g_wifiProvLogged_factory = false;
        static bool g_wifiProvLogged_updateLED = false;
        static bool g_wifiProvLogged_connectSaved = false;

        inline void logOnce(bool &flag, const char *method)
        {
            if (!flag)
            {
                flag = true;
                ZENO_LOG_CORE("[WARN] WiFiProvisioning::%s not available on this platform", method);
            }
        }
    } // namespace

    // ---- Constructor / Destructor ------------------------------------------

    WiFiProvisioning::WiFiProvisioning(IZenoHal &hal)
        : _hal(hal), _webServer(nullptr), _state(ProvisioningState::IDLE),
          _buttonPressTime(0), _apStartTime(0), _buttonPressed(false),
          _lastLedToggle(0), _ledState(false), _lastReconnectAttempt(0),
          _reconnectInterval(5000), _reconnectAttempts(0),
          _maxReconnectAttempts(10), _wasConnected(false),
          _pendingAPShutdownTime(0), _pendingConnectWiFi(false),
          _skipAutoWiFiConnect(false), _mqttTestCallback(nullptr),
          _claimCallback(nullptr)
    {
    }

    // Default ctor (follow-up): mirror the
    // DeviceCredentials default-arg HAL bridge pattern.
    // The IZenoHal& reference member must be initialised on every
    // platform; the prior `*reinterpret_cast<IZenoHal*>(nullptr)`
    // workaround tripped Renesas + STM32 g++ strict-conversion. The
    // canonical HAL singleton on each supported platform is wired here
    // so the default ctor is back-compat across all 4 platforms.
    WiFiProvisioning::WiFiProvisioning()
#if defined(ESP32)
        : WiFiProvisioning(getEsp32Hal())
#elif defined(ESP8266)
        : WiFiProvisioning(getEsp8266Hal())
#elif defined(ARDUINO_UNOR4_WIFI)
        : WiFiProvisioning(getUnoR4Hal())
#elif defined(STM32F1xx) || defined(STM32F4xx)
        : WiFiProvisioning(getStm32Hal())
#else
#error "WiFiProvisioning default ctor: unsupported platform (no canonical HAL singleton). \
        ZenoPCB v0.3.0 supports ESP32, ESP8266, UNO R4 WiFi, STM32 F1/F4."
#endif
    {
        // : provisioning surface stays guarded on UNO R4 / STM32
        // (the web-server / button-trigger / NVS paths are no-op stubs);
        // the HAL reference is real so method bodies that touch _hal
        // (e.g. _hal.nvs()) compile-link cleanly.
    }

    WiFiProvisioning::~WiFiProvisioning()
    {
        // _webServer always nullptr on the stub path; delete-on-nullptr is safe.
        if (_webServer)
        {
            delete _webServer;
            _webServer = nullptr;
        }
    }

    // ---- Configuration (no-op setters) -------------------------------------

    bool WiFiProvisioning::begin(const ProvisioningConfig &config)
    {
        (void)config;
        logOnce(g_wifiProvLogged_begin, "begin");
        return false;
    }

    void WiFiProvisioning::setAPSSIDPrefix(const String &) {}
    void WiFiProvisioning::setAPPassword(const String &) {}
    void WiFiProvisioning::setButtonPin(uint8_t) {}
    void WiFiProvisioning::setButtonHoldTime(uint32_t) {}
    void WiFiProvisioning::setAPTimeout(uint32_t) {}
    void WiFiProvisioning::setDeviceInfo(const DeviceInfo &) {}
    void WiFiProvisioning::setSkipAutoWiFiConnect(bool) {}

    void WiFiProvisioning::setMQTTTestCallback(std::function<bool()> callback)
    {
        _mqttTestCallback = callback;
    }

    void WiFiProvisioning::setClaimCallback(
        std::function<bool(const String &, const String &, const String &)> callback)
    {
        _claimCallback = callback;
    }

    void WiFiProvisioning::markClaimed() {}

    void WiFiProvisioning::connectToSavedWiFi()
    {
        logOnce(g_wifiProvLogged_connectSaved, "connectToSavedWiFi");
    }

    // ---- Lifecycle ---------------------------------------------------------

    void WiFiProvisioning::loop()
    {
        // Logged once at first call; subsequent loop() invocations are silent
        // to avoid spamming the serial console.
        logOnce(g_wifiProvLogged_loop, "loop");
    }

    void WiFiProvisioning::startAPMode()
    {
        logOnce(g_wifiProvLogged_startAP, "startAPMode");
    }

    void WiFiProvisioning::stopAPMode()
    {
        logOnce(g_wifiProvLogged_stopAP, "stopAPMode");
    }

    void WiFiProvisioning::factoryReset()
    {
        logOnce(g_wifiProvLogged_factory, "factoryReset");
    }

    void WiFiProvisioning::setLEDBlink(uint32_t) {}

    void WiFiProvisioning::updateLED()
    {
        logOnce(g_wifiProvLogged_updateLED, "updateLED");
    }

    // ---- Status (return safe defaults) -------------------------------------

    bool WiFiProvisioning::isConfigured() const { return false; }
    bool WiFiProvisioning::isWiFiConnected() const { return false; }
    bool WiFiProvisioning::isAPMode() const { return false; }
    ProvisioningState WiFiProvisioning::getState() const { return _state; }
    DeviceConfig WiFiProvisioning::getConfig() const { return _deviceConfig; }
    String WiFiProvisioning::getAPSSID() const { return String(); }
    String WiFiProvisioning::getAPIP() const { return String(); }

    void WiFiProvisioning::setDeviceCredentials(const String &deviceId, const String &token)
    {
        _provisionedDeviceId = deviceId;
        _provisionedToken = token;
    }

    // ---- Callback registration (stored, never invoked) ---------------------

    void WiFiProvisioning::onStateChange(ProvisioningStateCallback callback)
    {
        _stateCallback = callback;
    }
    void WiFiProvisioning::onConfigReceived(ConfigReceivedCallback callback)
    {
        _configCallback = callback;
    }
    void WiFiProvisioning::onWiFiConnected(WiFiConnectedCallback callback)
    {
        _wifiCallback = callback;
    }
    void WiFiProvisioning::onError(ErrorCallback callback)
    {
        _errorCallback = callback;
    }

} // namespace ZenoPCB

#endif  // ESP32/ESP8266 main block | UNO R4/STM32 stub block | DISABLE_PROVISIONING TU strip
