/**
 * @file MQTTClient.cpp
 * @brief ZenoPubSubClient wrapper implementation with auto-reconnect
 *
 * Uses Client interface abstraction for WiFi/Ethernet/4G support
 */

#include "MQTTClient.h"

namespace ZenoPCB
{
    // Static callback bridge for ZenoPubSubClient
    static MQTTClient *_mqttClientInstance = nullptr;
    static void _mqttCallbackBridge(char *topic, uint8_t *payload, unsigned int length)
    {
        if (_mqttClientInstance)
        {
            _mqttClientInstance->handleMqttMessage(topic, payload, length);
        }
    }

    MQTTClient::MQTTClient()
        : _networkClient(nullptr),
          _state(MQTTState::DISCONNECTED),
          _lastReconnectAttempt(0),
          _reconnectAttempts(0),
          _currentReconnectInterval(MQTT_RECONNECT_INTERVAL_MS),
          _connectedCallback(nullptr),
          _disconnectedCallback(nullptr),
          _messageCallback(nullptr),
          _errorCallback(nullptr),
          _stateCallback(nullptr),
          _networkCheckCallback(nullptr)
    {
        _mqttClientInstance = this;
    }

    MQTTClient::~MQTTClient()
    {
        disconnect();
        _mqttClientInstance = nullptr;
    }

    // ============================================
    // Client Interface
    // ============================================

    void MQTTClient::setClient(Client *client)
    {
        _networkClient = client;
        if (client)
        {
            _mqttClient.setClient(*client);
            ZENO_LOG_MQTT("Network client set");
        }
    }

    bool MQTTClient::hasClient() const
    {
        return _networkClient != nullptr;
    }

    void MQTTClient::setNetworkCheckCallback(NetworkCheckCallback callback)
    {
        _networkCheckCallback = callback;
    }

    bool MQTTClient::_isNetworkAvailable()
    {
        if (_networkCheckCallback)
        {
            return _networkCheckCallback();
        }
        // Default: assume network is available (caller manages network)
        return true;
    }

    // ============================================
    // Configuration
    // ============================================

    void MQTTClient::setConfig(const MQTTConfig &config)
    {
        _config = config;

        // Client ID will be set via clientId() method if needed
        // No need to generate here

        // Set buffer size for large messages
        _mqttClient.setBufferSize(ZENOPCB_MQTT_BUFFER_SIZE);

        // Set socket timeout (important for slow networks like 4G)
        _mqttClient.setSocketTimeout(_config.socketTimeout);

        // CRITICAL FIX: ZenoPubSubClient::setServer() stores a RAW POINTER to domain.
        // If we pass _config.broker.c_str(), the pointer becomes dangling when
        // _config is reassigned later. Copy to a stable char[] buffer instead.
        memset(_brokerStable, 0, sizeof(_brokerStable));
        strncpy(_brokerStable, _config.broker.c_str(), sizeof(_brokerStable) - 1);
        _mqttClient.setServer(_brokerStable, _config.port);

        // Set callback
        _mqttClient.setCallback(_mqttCallbackBridge);

        // Set keep alive
        _mqttClient.setKeepAlive(_config.keepAlive);

        // No-leak rule (user feedback 2026-06-06): never print broker host
        // or device credentials to serial. KeepAlive + SocketTimeout are
        // tuning values, safe to keep in the debug log.
        ZENO_LOG_MQTT("Config set (keepAlive=%ds, socketTimeout=%ds)",
                      _config.keepAlive, _config.socketTimeout);
    }

    MQTTConfig MQTTClient::getConfig() const
    {
        return _config;
    }

    // ============================================
    // Connection
    // ============================================

    bool MQTTClient::connect()
    {
        if (_config.broker.length() == 0)
        {
            ZENO_LOG_MQTT("ERROR: Broker not configured");
            if (_errorCallback)
            {
                _errorCallback("Broker not configured");
            }
            return false;
        }

        if (!hasClient())
        {
            ZENO_LOG_MQTT("ERROR: Network client not set. Call setClient() first.");
            if (_errorCallback)
            {
                _errorCallback("Network client not set");
            }
            return false;
        }

        if (!_isNetworkAvailable())
        {
            ZENO_LOG_MQTT("ERROR: Network not available");
            if (_errorCallback)
            {
                _errorCallback("Network not available");
            }
            return false;
        }

        _setState(MQTTState::CONNECTING);
        return _performConnect();
    }

    bool MQTTClient::_performConnect()
    {
        // No-leak rule (user feedback 2026-06-06): never print broker host,
        // clientID, username, password, LWT topic, or LWT payload to serial.
        // Any one of those identifies the device on the cloud and helps an
        // attacker correlate captured traffic. Just say "connecting".
        ZENO_LOG_MQTT("Connecting...");
        // Port + TLS state are not credentials safe to print so the
        // user can confirm 1883 (plain) vs 8883 (TLS) per env.
        ZENO_LOG_MQTT("transport: port=%u tls=%s",
                      (unsigned)_config.port,
                      _config.useTLS ? "yes" : "no");

        bool result = false;
        unsigned long connectStart = millis();

        // Connect with or without LWT
        if (_config.lwt_topic.length() > 0)
        {
            // Pass empty string "" when username is set but password is empty.
            // Some brokers require the password field to be present in the
            // CONNECT packet even if empty.
            // Sending nullptr omits the field entirely BAD_CREDENTIALS (state 4).
            const char *user = _config.username.length() == 0 ? nullptr : _config.username.c_str();
            const char *pass = user ? _config.password.c_str() : nullptr; // "" is valid

            result = _mqttClient.connect(
                _config.clientId.c_str(),
                user,
                pass,
                _config.lwt_topic.c_str(),
                static_cast<uint8_t>(_config.lwt_qos),
                _config.lwt_retain,
                _config.lwt_payload.c_str(),
                _config.cleanSession);
        }
        else if (_config.username.length() > 0)
        {
            result = _mqttClient.connect(
                _config.clientId.c_str(),
                _config.username.c_str(),
                _config.password.c_str()); // "" is valid (not nullptr)
        }
        else
        {
            result = _mqttClient.connect(_config.clientId.c_str());
        }

        if (result)
        {
            // Always-on minimal success log. Per user feedback 2026-06-06,
            // serial output for MQTT collapses to two states only:
            // "connected" or "not connected" no broker, no creds.
            ZENOPCB_PRINTF("[MQTT] connected\n");
            _setState(MQTTState::CONNECTED);
            _reconnectAttempts = 0;
            _currentReconnectInterval = _config.reconnectInterval;

            if (_connectedCallback)
            {
                _connectedCallback();
            }
        }
        else
        {
            int state = _mqttClient.state();
            unsigned long elapsed = millis() - connectStart;

            // Always-on minimal failure log. Per user feedback 2026-06-06,
            // serial output for MQTT collapses to two states only:
            // "connected" or "not connected" no broker, no port, no
            // clientID, no username, no password, no error code,
            // no actionable carrier-specific hints. Any of those help an
            // attacker (or a curious neighbour on the same serial dump)
            // correlate the device with a cloud account.
            ZENOPCB_PRINTF("[MQTT] not connected\n");
            // Detailed state code + elapsed time + hints are debug-gated
            // and only visible when ZENOPCB_DEBUG_MQTT is explicitly on.
            // Broker / credential fields remain stripped even from the
            // debug path security guarantee, not a verbosity setting.
            ZENO_LOG_MQTT("connect state=%d, elapsed=%lums", state, elapsed);

            // Nu ang reconnect, gi state RECONNECTING tip tc retry
            // Ch set ERROR cho trng hp li nghim trng (auth, protocol)
            bool isFatalError = (state == 1 || state == 2 || state == 4 || state == 5);
            if (isFatalError)
            {
                _setState(MQTTState::ERROR);
            }
            else if (_state != MQTTState::RECONNECTING)
            {
                // Ln connect u tin tht bi - t ng bt u reconnect
                if (_config.autoReconnect)
                {
                    ZENO_LOG_MQTT("Initial connect failed, starting auto-reconnect...");
                    _setState(MQTTState::RECONNECTING);
                    _reconnectAttempts = 0;
                    _currentReconnectInterval = _config.reconnectInterval;
                    _lastReconnectAttempt = millis();
                }
                else
                {
                    _setState(MQTTState::ERROR);
                }
            }
            // Nu ang RECONNECTING th gi nguyn state _handleReconnect tip tc

            if (_errorCallback)
            {
                String error = "Connection failed:";
                switch (state)
                {
                case -4:
                    error += "Connection timeout";
                    break;
                case -3:
                    error += "Connection lost";
                    break;
                case -2:
                    error += "Connect failed";
                    break;
                case -1:
                    error += "Disconnected";
                    break;
                case 1:
                    error += "Bad protocol";
                    break;
                case 2:
                    error += "Bad client ID";
                    break;
                case 3:
                    error += "Unavailable";
                    break;
                case 4:
                    error += "Bad credentials";
                    break;
                case 5:
                    error += "Unauthorized";
                    break;
                default:
                    error += String(state);
                }
                _errorCallback(error);
            }
        }

        return result;
    }

    void MQTTClient::disconnect()
    {
        if (_mqttClient.connected())
        {
            _mqttClient.disconnect();
            ZENO_LOG_MQTT("Disconnected");
        }

        _setState(MQTTState::DISCONNECTED);

        if (_disconnectedCallback)
        {
            _disconnectedCallback(0);
        }
    }

    bool MQTTClient::isConnected()
    {
        // Use cached _state avoids calling _mqttClient.connected() on every check.
        // With TinyGsmClient, connected() sends AT commands to the modem (blocking 1-3s).
        // The _state is updated by loop() when it detects a disconnect (when network available).
        return _state == MQTTState::CONNECTED;
    }

    bool MQTTClient::needsManualConnect() const
    {
        return _state == MQTTState::DISCONNECTED;
    }

    MQTTState MQTTClient::getState() const
    {
        return _state;
    }

    // ============================================
    // Publish
    // ============================================

    bool MQTTClient::publish(const String &topic, const String &payload, MQTTQoS qos, bool retain)
    {
        if (!isConnected())
        {
            ZENO_LOG_MQTT("Cannot publish - not connected");
            return false;
        }

        bool result = _mqttClient.publish(topic.c_str(), payload.c_str(), retain);

        if (result)
        {
            ZENO_LOG_VERBOSE("Published to %s: %s", maskTopic(topic).c_str(),
                             payload.length() > 50 ? (payload.substring(0, 50) + "...").c_str() : payload.c_str());
        }
        else
        {
            ZENO_LOG_MQTT("Publish failed to %s", maskTopic(topic).c_str());
        }

        return result;
    }

    bool MQTTClient::publish(const char *topic, const char *payload, MQTTQoS qos, bool retain)
    {
        // Null-pointer guard - Threat mitigation per PLAN
        if (!topic || !payload)
        {
            return false;
        }

        if (!isConnected())
        {
            ZENO_LOG_MQTT("Cannot publish - not connected");
            return false;
        }

        // Underlying ZenoPubSubClient already takes char* - no .c_str() round-trip,
        // no String construction. This is the zero-alloc hot path.
        bool result = _mqttClient.publish(topic, payload, retain);

        if (result)
        {
            // Conditional verbose log - the one-time String(topic) for maskTopic()
            // is acceptable here because this branch only fires under verbose log
            // mode, not on the production hot path.
            ZENO_LOG_VERBOSE("Published to %s: %.50s%s",
                             maskTopic(String(topic)).c_str(),
                             payload,
                             strlen(payload) > 50 ? "..." : "");
        }
        else
        {
            ZENO_LOG_MQTT("Publish failed to %s", maskTopic(String(topic)).c_str());
        }

        return result;
    }

    bool MQTTClient::publishJson(const String &topic, const String &json)
    {
        return publish(topic, json, MQTTQoS::QOS_0, false);
    }

    bool MQTTClient::publish(const String &topic, float value)
    {
        return publish(topic, String(value, 2), MQTTQoS::QOS_0, false);
    }

    bool MQTTClient::publish(const String &topic, int value)
    {
        return publish(topic, String(value), MQTTQoS::QOS_0, false);
    }

    // ============================================
    // Subscribe
    // ============================================

    bool MQTTClient::subscribe(const String &topic, MQTTQoS qos)
    {
        if (!isConnected())
        {
            ZENO_LOG_MQTT("Cannot subscribe - not connected");
            return false;
        }

        bool result = _mqttClient.subscribe(topic.c_str(), static_cast<uint8_t>(qos));

        if (result)
        {
            ZENO_LOG_MQTT("Subscribed to: %s", maskTopic(topic).c_str());
        }
        else
        {
            ZENO_LOG_MQTT("Subscribe failed: %s", maskTopic(topic).c_str());
        }

        return result;
    }

    bool MQTTClient::unsubscribe(const String &topic)
    {
        if (!isConnected())
        {
            return false;
        }

        bool result = _mqttClient.unsubscribe(topic.c_str());

        if (result)
        {
            ZENO_LOG_MQTT("Unsubscribed from: %s", maskTopic(topic).c_str());
        }

        return result;
    }

    // ============================================
    // Callbacks
    // ============================================

    void MQTTClient::onConnected(MQTTConnectedCallback callback)
    {
        _connectedCallback = callback;
    }

    void MQTTClient::onDisconnected(MQTTDisconnectedCallback callback)
    {
        _disconnectedCallback = callback;
    }

    void MQTTClient::onMessage(MQTTMessageCallback callback)
    {
        _messageCallback = callback;
    }

    void MQTTClient::onError(MQTTErrorCallback callback)
    {
        _errorCallback = callback;
    }

    void MQTTClient::onStateChange(MQTTStateCallback callback)
    {
        _stateCallback = callback;
    }

    // ============================================
    // Lifecycle
    // ============================================

    void MQTTClient::loop()
    {
        // Process MQTT messages only when network is available.
        // With TinyGsmClient, _mqttClient.loop() internally calls connected() which sends
        // AT commands to the modem (blocking 1-3s). Skipping during AP mode / network-down
        // prevents starving _wifiProvisioning->loop() (handleClient).
        // Note: removing the || _state==CONNECTED backup network-unavailable means
        // either AP mode is active OR connection is truly down; either way no point running.
        if (_isNetworkAvailable())
        {
            _mqttClient.loop();
        }

        // Check connection state only when network available to avoid AT commands in AP mode
        if (_state == MQTTState::CONNECTED && _isNetworkAvailable() && !_mqttClient.connected())
        {
            // Connection lost
            ZENO_LOG_MQTT("Connection lost!");
            _setState(MQTTState::DISCONNECTED);

            if (_disconnectedCallback)
            {
                _disconnectedCallback(-3); // Connection lost code
            }

            // Start reconnection if enabled
            if (_config.autoReconnect)
            {
                _setState(MQTTState::RECONNECTING);
                _reconnectAttempts = 0;
                _currentReconnectInterval = _config.reconnectInterval;
                _lastReconnectAttempt = millis();
                ZENO_LOG_MQTT("Auto-reconnect started (interval: %lums)", _currentReconnectInterval);
            }
        }

        // Auto-recover from ERROR state (nu network available)
        if (_state == MQTTState::ERROR && _config.autoReconnect)
        {
            unsigned long now = millis();
            if (now - _lastReconnectAttempt >= MQTT_RECONNECT_MAX_INTERVAL_MS)
            {
                ZENO_LOG_MQTT("Recovering from ERROR state, restarting reconnection...");
                _setState(MQTTState::RECONNECTING);
                _reconnectAttempts = 0;
                _currentReconnectInterval = _config.reconnectInterval;
                _lastReconnectAttempt = now;
            }
        }

        // Handle auto-reconnection
        if (_state == MQTTState::RECONNECTING)
        {
            _handleReconnect();
        }
    }

    // ============================================
    // Internal Methods
    // ============================================

    void MQTTClient::_setState(MQTTState newState)
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

    void MQTTClient::_handleReconnect()
    {
        if (!_isNetworkAvailable())
        {
            return; // Wait for network
        }

        unsigned long now = millis();
        if (now - _lastReconnectAttempt >= _currentReconnectInterval)
        {
            _lastReconnectAttempt = now;
            _reconnectAttempts++;

            ZENO_LOG_MQTT("Reconnect attempt %d (interval: %lums)...",
                          _reconnectAttempts, _currentReconnectInterval);

            if (_performConnect())
            {
                // Success - reset counters
                ZENO_LOG_MQTT("Reconnected after %d attempts!", _reconnectAttempts);
                _reconnectAttempts = 0;
                _currentReconnectInterval = _config.reconnectInterval;
            }
            else
            {
                // Failed - exponential backoff
                _currentReconnectInterval = min(
                    _currentReconnectInterval * 2,
                    MQTT_RECONNECT_MAX_INTERVAL_MS);

                if (_reconnectAttempts >= _config.maxReconnectAttempts)
                {
                    // KHNG DNG LI - Reset counter v tip tc retry max interval
                    ZENO_LOG_MQTT("Reconnect cycle %d attempts done, continuing at %lums interval...",
                                  _reconnectAttempts, _currentReconnectInterval);
                    _reconnectAttempts = 0;
                    // Gi nguyn _currentReconnectInterval = max (60s)
                    // Thit b s KHNG BAO GI b cuc reconnect
                }
            }
        }
    }

    void MQTTClient::handleMqttMessage(char *topic, uint8_t *payload, unsigned int length)
    {
        // Convert payload to String
        String payloadStr;
        payloadStr.reserve(length);
        for (unsigned int i = 0; i < length; i++)
        {
            payloadStr += (char)payload[i];
        }

        ZENO_LOG_MQTT("Message received - Topic: %s, Payload: %s",
                      maskTopic(String(topic)).c_str(), payloadStr.length() > 50 ? (payloadStr.substring(0, 50) + "...").c_str() : payloadStr.c_str());

        if (_messageCallback)
        {
            _messageCallback(String(topic), payloadStr);
        }
    }

} // namespace ZenoPCB
