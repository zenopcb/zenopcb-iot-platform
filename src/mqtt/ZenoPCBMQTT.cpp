/**
 * @file ZenoPCBMQTT.cpp
 * @brief High-level MQTT API implementation
 */

#include "ZenoPCBMQTT.h"
#include "../core/ZenoPCBDebug.h"

namespace ZenoPCB
{

    ZenoPCBMQTT::ZenoPCBMQTT()
        : _deviceId("")
    {
        // Default configuration
        _config.port = MQTT_DEFAULT_PORT;
        _config.keepAlive = MQTT_DEFAULT_KEEPALIVE;
        _config.socketTimeout = MQTT_DEFAULT_SOCKET_TIMEOUT;
        _config.cleanSession = false; // false để LWT hoạt động + giữ session
        _config.autoReconnect = true;
        _config.maxReconnectAttempts = MQTT_MAX_RECONNECT_ATTEMPTS;
        _config.reconnectInterval = MQTT_RECONNECT_INTERVAL_MS;
    }

    ZenoPCBMQTT::~ZenoPCBMQTT()
    {
        disconnect();
    }

    // ============================================
    // Fluent Configuration
    // ============================================

    ZenoPCBMQTT &ZenoPCBMQTT::broker(const String &host, uint16_t port)
    {
        _config.broker = host;
        _config.port = port;
        _config.useTLS = false;
        return *this;
    }

    ZenoPCBMQTT &ZenoPCBMQTT::brokerTLS(const String &host, uint16_t port)
    {
        _config.broker = host;
        _config.port = port;
        _config.useTLS = true;
        return *this;
    }

    ZenoPCBMQTT &ZenoPCBMQTT::credentials(const String &username, const String &password)
    {
        _config.username = username;
        _config.password = password;
        return *this;
    }

    ZenoPCBMQTT &ZenoPCBMQTT::clientId(const String &clientId)
    {
        _config.clientId = clientId;
        return *this;
    }

    ZenoPCBMQTT &ZenoPCBMQTT::deviceId(const String &deviceId)
    {
        _deviceId = deviceId;
        return *this;
    }

    ZenoPCBMQTT &ZenoPCBMQTT::keepAlive(uint16_t seconds)
    {
        _config.keepAlive = seconds;
        // ⚠️ FIX: Only update keepAlive on ZenoPubSubClient directly
        // DO NOT call full setConfig() — it copies 6 Strings and risks dangling pointers
        _client.getInternalClient().setKeepAlive(seconds);
        return *this;
    }

    ZenoPCBMQTT &ZenoPCBMQTT::socketTimeout(uint16_t seconds)
    {
        _config.socketTimeout = seconds;
        // ⚠️ FIX: Only update socketTimeout on ZenoPubSubClient directly
        _client.getInternalClient().setSocketTimeout(seconds);
        return *this;
    }

    ZenoPCBMQTT &ZenoPCBMQTT::lastWill(const String &topic, const String &payload,
                                       MQTTQoS qos, bool retain)
    {
        _config.lwt_topic = topic;
        _config.lwt_payload = payload;
        _config.lwt_qos = qos;
        _config.lwt_retain = retain;

        // ⚠️ FIX: Do NOT call setConfig() here. LWT is stored in _config
        // and will be applied during begin() → setConfig() and used in connect().

        ZENO_LOG_MQTT("LWT updated - Topic: %s, QoS: %d, Retain: %d",
                      maskTopic(topic).c_str(), qos, retain);

        return *this;
    }

    ZenoPCBMQTT &ZenoPCBMQTT::autoReconnect(bool enable, uint8_t maxAttempts)
    {
        _config.autoReconnect = enable;
        _config.maxReconnectAttempts = maxAttempts;
        return *this;
    }

    // ============================================
    // Callbacks
    // ============================================

    ZenoPCBMQTT &ZenoPCBMQTT::onConnected(MQTTConnectedCallback callback)
    {
        _client.onConnected(callback);
        return *this;
    }

    ZenoPCBMQTT &ZenoPCBMQTT::onDisconnected(MQTTDisconnectedCallback callback)
    {
        _client.onDisconnected(callback);
        return *this;
    }

    ZenoPCBMQTT &ZenoPCBMQTT::onMessage(MQTTMessageCallback callback)
    {
        _client.onMessage(callback);
        return *this;
    }

    ZenoPCBMQTT &ZenoPCBMQTT::onError(MQTTErrorCallback callback)
    {
        _client.onError(callback);
        return *this;
    }

    ZenoPCBMQTT &ZenoPCBMQTT::onStateChange(MQTTStateCallback callback)
    {
        _client.onStateChange(callback);
        return *this;
    }

    // ============================================
    // Lifecycle
    // ============================================

    bool ZenoPCBMQTT::begin()
    {
        ZENO_LOG_MQTT("Initializing MQTT...");

        // Validate configuration
        if (_config.broker.length() == 0)
        {
            ZENO_LOG_MQTT("ERROR: Broker not configured");
            return false;
        }

        // Set default LWT if not configured and deviceId is set
        // Must match sendStatus() exactly: same topic, payload format, QoS 1, retain=true
        // → Broker replaces retained "online" with "offline" when client dies
        if (_config.lwt_topic.length() == 0 && _deviceId.length() > 0)
        {
            _config.lwt_topic = buildTopic(TOPIC_STATUS);
            _config.lwt_payload = "offline";
            _config.lwt_qos = MQTTQoS::QOS_1;
            _config.lwt_retain = true;
        }

        // Apply configuration to client
        _client.setConfig(_config);

        // No-leak rule (user feedback 2026-06-06): never print broker host
        // or port to serial. They identify the cloud account.
        ZENO_LOG_MQTT("MQTT initialized");

        return true;
    }

    void ZenoPCBMQTT::loop()
    {
        _client.loop();

        // Periodic online heartbeat
        if (_client.isConnected() && _deviceId.length() > 0)
        {
            unsigned long now = millis();
            if (now - _lastStatusTime >= _statusInterval)
            {
                _lastStatusTime = now;
                sendStatus("online");
            }
        }
    }

    bool ZenoPCBMQTT::connect()
    {
        if (!_client.connect())
        {
            return false;
        }

        // Publish online status
        if (_deviceId.length() > 0)
        {
            sendStatus("online");
        }

        return true;
    }

    void ZenoPCBMQTT::disconnect()
    {
        // Publish offline status before disconnecting
        if (_client.isConnected() && _deviceId.length() > 0)
        {
            sendStatus("offline");
        }

        _client.disconnect();
    }

    void ZenoPCBMQTT::forceDisconnect()
    {
        // Skip sendStatus — network already down, TCP socket dead
        // Avoids blocking on write to dead socket (up to 60s TCP retransmit timeout)
        _client.disconnect();
    }

    // ============================================
    // Status
    // ============================================

    bool ZenoPCBMQTT::isConnected()
    {
        return _client.isConnected();
    }

    bool ZenoPCBMQTT::needsManualConnect() const
    {
        return _client.needsManualConnect();
    }

    MQTTState ZenoPCBMQTT::getState() const
    {
        return _client.getState();
    }

    // ============================================
    // Publish API
    // ============================================

    bool ZenoPCBMQTT::publish(const String &topic, const String &payload,
                              MQTTQoS qos, bool retain)
    {
        return _client.publish(topic, payload, qos, retain);
    }

    bool ZenoPCBMQTT::publish(const char *topic, const char *payload,
                              MQTTQoS qos, bool retain)
    {
        // Null-pointer guard (T-3-02). MQTTClient::publish enforces this too,
        // but we short-circuit here to avoid the extra call frame on the hot
        // path.
        if (!topic || !payload)
        {
            return false;
        }

        // Mirror the String overload: thin passthrough to MQTTClient. The
        // existing String overload contains no additional pre-publish guards
        // or queue handling at this layer (verified 2026-06-01) — both
        // overloads delegate directly to _client.publish.
        return _client.publish(topic, payload, qos, retain);
    }

    bool ZenoPCBMQTT::sendTelemetry(const String &key, float value)
    {
        String topic = buildTopic(String(TOPIC_TELEMETRY) + "/" + key);
        return _client.publish(topic, value);
    }

    bool ZenoPCBMQTT::sendTelemetry(const String &key, int value)
    {
        String topic = buildTopic(String(TOPIC_TELEMETRY) + "/" + key);
        return _client.publish(topic, value);
    }

    bool ZenoPCBMQTT::sendTelemetry(const String &key, const String &value)
    {
        String topic = buildTopic(String(TOPIC_TELEMETRY) + "/" + key);
        return _client.publish(topic, value);
    }

    bool ZenoPCBMQTT::sendTelemetryJson(const String &json)
    {
        String topic = buildTopic(TOPIC_TELEMETRY);
        return _client.publishJson(topic, json);
    }

    bool ZenoPCBMQTT::sendStatus(const String &status)
    {
        String topic = buildTopic(TOPIC_STATUS);
        return _client.publish(topic, status, MQTTQoS::QOS_1, true); // Retain status
    }

    bool ZenoPCBMQTT::sendEvent(const String &eventType, const String &data)
    {
        String topic = buildTopic(String(TOPIC_EVENT) + "/" + eventType);
        return _client.publish(topic, data);
    }

    // ============================================
    // Subscribe API
    // ============================================

    bool ZenoPCBMQTT::subscribe(const String &topic, MQTTQoS qos)
    {
        return _client.subscribe(topic, qos);
    }

    bool ZenoPCBMQTT::subscribeCommands()
    {
        if (_deviceId.length() == 0)
        {
            ZENO_LOG_MQTT("ERROR: Device ID not set for command subscription");
            return false;
        }

        String topic = buildTopic(String(TOPIC_COMMAND) + "/#");
        return _client.subscribe(topic, MQTTQoS::QOS_1);
    }

    bool ZenoPCBMQTT::subscribeConfig()
    {
        if (_deviceId.length() == 0)
        {
            ZENO_LOG_MQTT("ERROR: Device ID not set for config subscription");
            return false;
        }

        String topic = buildTopic(String(TOPIC_CONFIG) + "/#");
        return _client.subscribe(topic, MQTTQoS::QOS_1);
    }

    bool ZenoPCBMQTT::subscribeDiagnosticsRequest()
    {
        if (_deviceId.length() == 0)
        {
            ZENO_LOG_MQTT("ERROR: Device ID not set for diagnostics request subscription");
            return false;
        }

        String topic = buildTopic(TOPIC_DIAGNOSTICS_REQUEST);
        ZENO_LOG_MQTT("Subscribing to diagnostics request: %s", maskTopic(topic).c_str());
        return _client.subscribe(topic, MQTTQoS::QOS_1);
    }

    bool ZenoPCBMQTT::unsubscribe(const String &topic)
    {
        return _client.unsubscribe(topic);
    }

    // ============================================
    // Diagnostics API
    // ============================================

    bool ZenoPCBMQTT::sendDiagnostics(const String &json)
    {
        if (_deviceId.length() == 0)
        {
            ZENO_LOG_MQTT("ERROR: Device ID not set for diagnostics");
            return false;
        }

        String topic = buildTopic(TOPIC_DIAGNOSTICS);
        ZENO_LOG_MQTT("Sending diagnostics to: %s", maskTopic(topic).c_str());
        return _client.publish(topic, json, MQTTQoS::QOS_1, false);
    }

    bool ZenoPCBMQTT::sendDiagnosticsResponse(const String &json)
    {
        if (_deviceId.length() == 0)
        {
            ZENO_LOG_MQTT("ERROR: Device ID not set for diagnostics response");
            return false;
        }

        String topic = buildTopic(TOPIC_DIAGNOSTICS_RESPONSE);
        ZENO_LOG_MQTT("Sending diagnostics response to: %s", maskTopic(topic).c_str());
        return _client.publish(topic, json, MQTTQoS::QOS_1, false);
    }

    // ============================================
    // Client Interface
    // ============================================

    ZenoPCBMQTT &ZenoPCBMQTT::setClient(Client *client)
    {
        _client.setClient(client);
        return *this;
    }

    ZenoPCBMQTT &ZenoPCBMQTT::setNetworkCheck(MQTTClient::NetworkCheckCallback callback)
    {
        _client.setNetworkCheckCallback(callback);
        return *this;
    }

    MQTTClient &ZenoPCBMQTT::getClient()
    {
        return _client;
    }

    // ============================================
    // Topic Helpers
    // ============================================

    String ZenoPCBMQTT::getBaseTopic() const
    {
        if (_deviceId.length() == 0)
        {
            return String(TOPIC_PREFIX);
        }
        return String(TOPIC_PREFIX) + "/" + _deviceId;
    }

    String ZenoPCBMQTT::buildTopic(const String &path) const
    {
        return getBaseTopic() + "/" + path;
    }

    // THREAD-NOTE: _deviceId is set once at provisioning (Zeno::setDeviceCredentials)
    // and never mutated post-begin(). _deviceId.c_str() is stable for the lifetime
    // of this call. Output is byte-identical to buildTopic(const String&) for the
    // same path input — required invariant for downstream MQTT routing.
    bool ZenoPCBMQTT::buildTopic(const char *path, char *out, size_t outSize) const
    {
        if (!path || !out || outSize == 0)
        {
            return false;
        }
        int n;
        if (_deviceId.length() == 0)
        {
            n = snprintf(out, outSize, "%s/%s", TOPIC_PREFIX, path);
        }
        else
        {
            n = snprintf(out, outSize, "%s/%s/%s", TOPIC_PREFIX, _deviceId.c_str(), path);
        }
        return (n > 0 && (size_t)n < outSize); // false on truncation or snprintf error
    }

} // namespace ZenoPCB
