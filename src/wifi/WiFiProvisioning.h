#ifndef WIFI_PROVISIONING_H
#define WIFI_PROVISIONING_H

// + platform-specific WiFi + WebServer
// headers. ESP8266 lacks a class literally named `WebServer`, so we expose
// a typedef bridge `using WebServer = ESP8266WebServer;` at namespace
// scope. Downstream member declarations (`WebServer *_webServer;`) and
// the `.cpp` constructor (`new WebServer(...)`) resolve identically on
// both platforms via this alias.
//
// switch extended to UNO R4 + STM32 arms. Per
// ("Whole-Class Capability Gate"), WiFiProvisioning has no
// HTTP-server analog on those platforms (3-week UNO R4 captive-portal port
// deferred to v0.4.0; STM32 has no AP-mode hardware). The class still
// compiles on those targets via stub method bodies see WiFiProvisioning.cpp.
#if defined(ESP32)
  #include <WiFi.h>
  #include <WebServer.h>
#elif defined(ESP8266)
  #include <ESP8266WiFi.h>
  #include <ESP8266WebServer.h>
  using WebServer = ESP8266WebServer;
#elif defined(ARDUINO_UNOR4_WIFI)
  #include <WiFiS3.h>
  // WiFiS3 has WiFiServer but no ESP8266WebServer / WebServer-shaped HTTP
  // server analog. WiFiProvisioning methods stub to failure on
  // UNO R4 per RESEARCH Architectural Responsibility Map.
  class WebServer;  // Forward decl so `WebServer *_webServer;` member compiles.
#elif defined(STM32F1xx) || defined(STM32F4xx)
  // no AP-mode hardware on STM32 (Ethernet F4 / ESP-AT F1).
  // WiFiProvisioning methods stub to failure.
  class WebServer;  // Forward decl so `WebServer *_webServer;` member compiles.
#endif
#include "../core/ZenoPCBTypes.h"
#include "../hal/IZenoHal.h"

namespace ZenoPCB
{

    /**
     * @brief WiFi Provisioning via AP mode with web configuration portal
     *
     * Features:
     * - Hold button (IO-0) to enter AP mode
     * - Auto-generate AP SSID: ZENO-{ChipID}
     * - RESTful API for configuration
     * - WiFi scanning with signal strength
     * - Persistent configuration storage (NVS)
     * - Auto-connect to configured WiFi
     * - Timeout and error handling
     */
    class WiFiProvisioning
    {
    public:
        /**
         * @brief Construct with explicit HAL reference.
         */
        explicit WiFiProvisioning(IZenoHal &hal);

        /**
         * @brief Default constructor  uses the ESP32 HAL singleton.
         *
         * Retained for source compatibility with existing callers
         * (`new WiFiProvisioning()` in ZenoPCB.cpp Zeno::_initProvisioning).
         */
        WiFiProvisioning();

        ~WiFiProvisioning();

        // ============================================
        // Configuration
        // ============================================

        /**
         * @brief Initialize provisioning with configuration
         * @param config Provisioning configuration
         * @return true if initialization successful
         */
        bool begin(const ProvisioningConfig &config = ProvisioningConfig());

        /**
         * @brief Set custom AP SSID prefix (default: "ZENO-")
         */
        void setAPSSIDPrefix(const String &prefix);

        /**
         * @brief Set AP password (empty = open network)
         */
        void setAPPassword(const String &password);

        /**
         * @brief Set button pin for triggering AP mode
         */
        void setButtonPin(uint8_t pin);

        /**
         * @brief Set button hold time to trigger AP mode (ms)
         */
        void setButtonHoldTime(uint32_t ms);

        /**
         * @brief Set AP mode timeout (ms)
         */
        void setAPTimeout(uint32_t ms);

        /**
         * @brief Set device information (type, name, version)
         * @param info DeviceInfo struct containing device details
         */
        void setDeviceInfo(const DeviceInfo &info);

        // ============================================
        // Lifecycle
        // ============================================

        /**
         * @brief Main loop - must be called regularly
         */
        void loop();

        /**
         * @brief Force enter AP mode
         */
        void startAPMode();

        /**
         * @brief Exit AP mode and connect to WiFi
         */
        void stopAPMode();

        /**
         * @brief Reset configuration and restart
         */
        void factoryReset();

        /**
         * @brief Set LED blink interval (called externally to change blink speed)
         * @param intervalMs Blink interval in milliseconds (200=fast, 1000=slow)
         */
        void setLEDBlink(uint32_t intervalMs);

        /**
         * @brief Update LED state (call every loop iteration to drive blinking)
         * Used by ZenoPCB to drive LED independently of provisioning state.
         */
        void updateLED();

        /**
         * @brief Skip automatic WiFi connection on boot
         *
         * Call this when using an external network provider (4G, Ethernet).
         * The provisioning system will still handle button/AP mode/LED,
         * but will NOT try to connect WiFi or run WiFi reconnection.
         */
        void setSkipAutoWiFiConnect(bool skip);

        /**
         * @brief Connect to saved WiFi credentials from NVS
         *
         * Call this when switching from external provider to WiFi mode.
         * Reads saved SSID/password and initiates connection.
         */
        void connectToSavedWiFi();

        // ============================================
        // Status
        // ============================================

        /**
         * @brief Check if device is configured
         */
        bool isConfigured() const;

        /**
         * @brief Check if WiFi is connected
         */
        bool isWiFiConnected() const;

        /**
         * @brief Check if in AP mode
         */
        bool isAPMode() const;

        /**
         * @brief Get current provisioning state
         */
        ProvisioningState getState() const;

        /**
         * @brief Get current configuration
         */
        DeviceConfig getConfig() const;

        /**
         * @brief Get AP SSID
         */
        String getAPSSID() const;

        /**
         * @brief Get AP IP address
         */
        String getAPIP() const;

        /**
         * @brief Set device credentials for /api/info response
         * @param deviceId Provisioned device ID (32 chars)
         * @param token Provisioned token (32 chars)
         */
        void setDeviceCredentials(const String &deviceId, const String &token);

        // ============================================
        // Callbacks
        // ============================================

        /**
         * @brief Set callback for state changes
         */
        void onStateChange(ProvisioningStateCallback callback);

        /**
         * @brief Set callback when configuration received
         */
        void onConfigReceived(ConfigReceivedCallback callback);

        /**
         * @brief Set callback when WiFi connected
         */
        void onWiFiConnected(WiFiConnectedCallback callback);

        /**
         * @brief Set callback for errors
         */
        void onError(ErrorCallback callback);

        /**
         * @brief Set MQTT connectivity test callback
         *
         * Called by /api/connect/wifi after WiFi verify succeeds.
         * ZenoPCB registers this to test MQTT connection before saving config.
         * WiFi is in WIFI_AP_STA mode and connected when this is called.
         *
         * @param callback Returns true if MQTT connection test passed
         */
        void setMQTTTestCallback(std::function<bool()> callback);

        /**
         * @brief Set device claim callback
         *
         * Called after MQTT test passes. Publishes a claim message to MQTT broker
         * and waits for backend acknowledgement within timeout.
         *
         * @param callback (userId, deviceId, token) -> true if claim acknowledged
         */
        void setClaimCallback(std::function<bool(const String &userId, const String &deviceId, const String &token)> callback);

        /**
         * @brief Mark device as claimed and persist to NVS
         *
         * Called externally (e.g. from ZenoPCB.cpp) after a deferred claim
         * succeeds over 4G/Ethernet MQTT. Updates isClaimed in NVS so the
         * device stays claimed across reboots.
         */
        void markClaimed();

    private:
        // Configuration
        IZenoHal &_hal;
        ProvisioningConfig _config;
        DeviceConfig _deviceConfig;
        DeviceInfo _deviceInfo;

        // Web Server
        WebServer *_webServer;

        // State
        ProvisioningState _state;
        unsigned long _buttonPressTime;
        unsigned long _apStartTime;
        bool _buttonPressed;
        String _apSSID;

        // Provisioned Device Credentials (from DeviceCredentials class)
        String _provisionedDeviceId;
        String _provisionedToken;

        // LED indicator
        unsigned long _lastLedToggle;
        bool _ledState;

        // WiFi Reconnect (Non-blocking)
        unsigned long _lastReconnectAttempt;
        uint32_t _reconnectInterval;
        uint8_t _reconnectAttempts;
        uint8_t _maxReconnectAttempts;
        bool _wasConnected;        // Track if WiFi was previously connected
        bool _skipAutoWiFiConnect; // Skip WiFi when using 4G/Ethernet

        // Delayed AP shutdown (non-blocking)
        unsigned long _pendingAPShutdownTime;
        bool _pendingConnectWiFi;

        // Callbacks
        ProvisioningStateCallback _stateCallback;
        ConfigReceivedCallback _configCallback;
        WiFiConnectedCallback _wifiCallback;
        ErrorCallback _errorCallback;
        std::function<bool()> _mqttTestCallback;                                            // MQTT test before saving config
        std::function<bool(const String &, const String &, const String &)> _claimCallback; // Device claim via MQTT

        // ============================================
        // Internal Methods
        // ============================================

        // Button handling
        void _checkButton();
        void _handleButtonPress();
        void _handleButtonRelease();

        // AP Mode
        void _startAccessPoint();
        void _stopAccessPoint();
        void _checkAPTimeout();

        // Web Server (RESTful API)
        void _setupWebServer();
        void _handleRoot();
        void _handleGetInfo();
        void _handleGetConfig();
        void _handleGetNetworks();
        void _handlePostConfig();
        void _handleConnectWiFi();     // POST /api/connect/wifi
        void _handleConnectEthernet(); // POST /api/connect/ethernet
        void _handleConnectCellular(); // POST /api/connect/cellular
        void _handleVerifyWiFi();
        void _handleReset();
        void _handleNotFound();
        void _sendCORS();

        // WiFi
        void _connectToWiFi();
        void _checkWiFiConnection(); // Non-blocking reconnect
        void _attemptReconnect();    // Single reconnect attempt
        bool _scanNetworks(std::vector<WiFiNetwork> &networks);

        // Storage
        bool _loadConfig();
        bool _saveConfig();
        void _clearConfig();

        // State management
        void _setState(ProvisioningState newState);
        void _triggerError(const String &error);

        // Utility
        String _getChipID();
        String _encryptionTypeToString(uint8_t type);
        String _connectionTypeToString(ConnectionType type);
        ConnectionType _stringToConnectionType(const String &str);
        String _getWiFiStatusString(int status);
        String _getWiFiReasonCode(int status);
        String _generateConfigJSON();
        String _generateNetworksJSON(const std::vector<WiFiNetwork> &networks);

        // LED indicator
        void _initLED();
        void _updateLED();
        void _setLED(bool on);
    };

} // namespace ZenoPCB

#endif // WIFI_PROVISIONING_H
