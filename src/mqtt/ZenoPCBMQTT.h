#ifndef ZENOPCB_MQTT_H
#define ZENOPCB_MQTT_H

/**
 * @file ZenoPCBMQTT.h
 * @brief High-level MQTT API for ZenoPCB IoT Library
 *
 * Provides fluent API for MQTT communication with ZenoPCB Cloud Platform.
 *
 * @example
 * ZenoPCBMQTT mqtt;
 * mqtt.broker("mqtt.zenopcb.com", 1883)
 *     .credentials("user", "pass")
 *     .onConnected([]() { Serial.println("MQTT Connected!"); })
 *     .onMessage([](const String& topic, const String& payload) {
 *         Serial.printf("Received: %s = %s\n", topic.c_str(), payload.c_str());
 *     })
 *     .begin();
 */

#include <Arduino.h>
#include "../ZenoJson.h"  // ArduinoJson API from vendored copy (namespace ZenoJson; see vendor/ArduinoJson/LICENSE.md)
#include "MQTTClient.h"
#include "MQTTTypes.h"

namespace ZenoPCB
{

    class ZenoPCBMQTT
    {
    public:
        ZenoPCBMQTT();
        ~ZenoPCBMQTT();

        // ============================================
        // Fluent Configuration
        // ============================================

        /**
         * @brief Set MQTT broker address
         * @param host Broker hostname or IP
         * @param port Broker port (default: 1883)
         * @return Reference to this for method chaining
         */
        ZenoPCBMQTT &broker(const String &host, uint16_t port = MQTT_DEFAULT_PORT);

        /**
         * @brief Set MQTT broker with TLS
         * @param host Broker hostname or IP
         * @param port Broker port (default: 8883)
         * @return Reference to this for method chaining
         */
        ZenoPCBMQTT &brokerTLS(const String &host, uint16_t port = MQTT_DEFAULT_TLS_PORT);

        /**
         * @brief Set MQTT credentials
         * @param username Username
         * @param password Password
         * @return Reference to this for method chaining
         */
        ZenoPCBMQTT &credentials(const String &username, const String &password);

        /**
         * @brief Set client ID
         * @param clientId MQTT client ID (auto-generated if not set)
         * @return Reference to this for method chaining
         */
        ZenoPCBMQTT &clientId(const String &clientId);

        /**
         * @brief Set device ID for topic generation
         * @param deviceId Device unique identifier
         * @return Reference to this for method chaining
         */
        ZenoPCBMQTT &deviceId(const String &deviceId);

        /**
         * @brief Set keep alive interval
         * @param seconds Keep alive in seconds (default: 60)
         * @return Reference to this for method chaining
         */
        ZenoPCBMQTT &keepAlive(uint16_t seconds);

        /**
         * @brief Set TCP socket timeout (important for slow networks like 4G)
         * @param seconds Socket timeout in seconds (default: 15s WiFi, 60s 4G)
         * @return Reference to this for method chaining
         */
        ZenoPCBMQTT &socketTimeout(uint16_t seconds);

        /**
         * @brief Set Last Will and Testament
         * @param topic LWT topic
         * @param payload LWT payload
         * @param qos QoS level
         * @param retain Retain flag
         * @return Reference to this for method chaining
         */
        ZenoPCBMQTT &lastWill(const String &topic, const String &payload,
                              MQTTQoS qos = MQTTQoS::QOS_0, bool retain = false);

        /**
         * @brief Enable/disable auto reconnect
         * @param enable Enable auto reconnect (default: true)
         * @param maxAttempts Maximum reconnect attempts (default: 10)
         * @return Reference to this for method chaining
         */
        ZenoPCBMQTT &autoReconnect(bool enable = true, uint8_t maxAttempts = MQTT_MAX_RECONNECT_ATTEMPTS);

        // ============================================
        // Callbacks
        // ============================================

        /**
         * @brief Set callback when connected to broker
         */
        ZenoPCBMQTT &onConnected(MQTTConnectedCallback callback);

        /**
         * @brief Set callback when disconnected from broker
         */
        ZenoPCBMQTT &onDisconnected(MQTTDisconnectedCallback callback);

        /**
         * @brief Set callback for incoming messages
         */
        ZenoPCBMQTT &onMessage(MQTTMessageCallback callback);

        /**
         * @brief Set callback for errors
         */
        ZenoPCBMQTT &onError(MQTTErrorCallback callback);

        /**
         * @brief Set callback for state changes
         */
        ZenoPCBMQTT &onStateChange(MQTTStateCallback callback);

        // ============================================
        // Lifecycle
        // ============================================

        /**
         * @brief Initialize and connect to MQTT broker
         * @return true if initialized successfully
         */
        bool begin();

        /**
         * @brief Process MQTT events - must call in loop()
         */
        void loop();

        /**
         * @brief Connect to MQTT broker
         * @return true if connected
         */
        bool connect();

        /**
         * @brief Disconnect from MQTT broker
         * Publishes offline status before disconnecting
         */
        void disconnect();

        /**
         * @brief Force disconnect without publishing status
         * Use when network is already down (physical disconnect)
         * to avoid blocking on dead TCP socket
         */
        void forceDisconnect();

        // ============================================
        // Status
        // ============================================

        /**
         * @brief Check if connected to broker
         */
        bool isConnected();

        /**
         * @brief Check if a manual connect() call is needed (state == DISCONNECTED)
         * Returns false when already CONNECTING, RECONNECTING, ERROR, or CONNECTED.
         */
        bool needsManualConnect() const;

        /**
         * @brief Get current MQTT state
         */
        MQTTState getState() const;

        // ============================================
        // Publish API
        // ============================================

        /**
         * @brief Publish message to topic
         * @param topic Topic name
         * @param payload Message payload
         * @param qos Quality of Service (default: QOS_0)
         * @param retain Retain flag (default: false)
         * @return true if published successfully
         */
        bool publish(const String &topic, const String &payload,
                     MQTTQoS qos = MQTTQoS::QOS_0, bool retain = false);

        /**
         * @brief Publish - char* overload (zero-alloc hot path)
         *
         * High-level entry point used by ZenoPCB.cpp via _mqtt->publish(...).
         * Delegates to the MQTTClient char* publish overload so callers with
         * stack-buffered char[] topics/payloads do not trigger implicit
         * String(const char*) construction.
         *
         * @param topic   Null-terminated topic name
         * @param payload Null-terminated payload
         * @param qos     Quality of Service (default: QOS_0)
         * @param retain  Retain flag (default: false)
         * @return true if published successfully
         */
        bool publish(const char *topic, const char *payload,
                     MQTTQoS qos = MQTTQoS::QOS_0, bool retain = false);

        /**
         * @brief Publish telemetry data (auto-generates topic)
         * Topic format: zenopcb/{deviceId}/telemetry/{key}
         * @param key Data key
         * @param value Data value
         * @return true if published
         */
        bool sendTelemetry(const String &key, float value);
        bool sendTelemetry(const String &key, int value);
        bool sendTelemetry(const String &key, const String &value);

        /**
         * @brief Publish JSON telemetry data
         * Topic format: zenopcb/{deviceId}/telemetry
         * @param json JSON string
         * @return true if published
         */
        bool sendTelemetryJson(const String &json);

        /**
         * @brief Publish device status
         * Topic format: zenopcb/{deviceId}/status
         * @param status Status string ("online", "offline", etc.)
         * @return true if published
         */
        bool sendStatus(const String &status);

        /**
         * @brief Publish event
         * Topic format: zenopcb/{deviceId}/event/{eventType}
         * @param eventType Event type
         * @param data Event data
         * @return true if published
         */
        bool sendEvent(const String &eventType, const String &data);

        // ============================================
        // Subscribe API
        // ============================================

        /**
         * @brief Subscribe to topic
         * @param topic Topic pattern (supports + and # wildcards)
         * @param qos Quality of Service
         * @return true if subscribed
         */
        bool subscribe(const String &topic, MQTTQoS qos = MQTTQoS::QOS_0);

        /**
         * @brief Subscribe to device command topic
         * Topic format: zenopcb/{deviceId}/command/#
         * @return true if subscribed
         */
        bool subscribeCommands();

        /**
         * @brief Subscribe to device config topic
         * Topic format: zenopcb/{deviceId}/config/#
         * @return true if subscribed
         */
        bool subscribeConfig();

        /**
         * @brief Subscribe to diagnostics request topic
         * Topic format: v1/devices/{deviceToken}/diagnostics/request
         * @return true if subscribed
         */
        bool subscribeDiagnosticsRequest();

        /**
         * @brief Unsubscribe from topic
         */
        bool unsubscribe(const String &topic);

        // ============================================
        // Diagnostics API
        // ============================================

        /**
         * @brief Send diagnostics data (passive update)
         * Topic format: v1/devices/{deviceToken}/diagnostics
         * @param json JSON payload with diagnostics data
         * @return true if published
         */
        bool sendDiagnostics(const String &json);

        /**
         * @brief Send diagnostics response (on-demand)
         * Topic format: v1/devices/{deviceToken}/diagnostics/response
         * @param json JSON payload with requestId and diagnostics data
         * @return true if published
         */
        bool sendDiagnosticsResponse(const String &json);

        // ============================================
        // Client Interface (Connection Abstraction)
        // ============================================

        /**
         * @brief Set the network client to use for MQTT connection
         *
         * This allows MQTT to work with any Client implementation:
         * - WiFiClient (ESP32 WiFi)
         * - WiFiClientSecure (ESP32 WiFi with TLS)
         * - EthernetClient (Ethernet shield/W5500)
         * - TinyGsmClient (4G/LTE modem)
         *
         * @param client Pointer to Client instance (must remain valid)
         * @return Reference to this for method chaining
         *
         * Example:
         * @code
         * WiFiClient wifiClient;
         * mqtt.setClient(&wifiClient).begin();
         * @endcode
         */
        ZenoPCBMQTT &setClient(Client *client);

        /**
         * @brief Set network check callback
         *
         * The callback should return true if network is available.
         * Used for auto-reconnect to check if network is ready.
         *
         * @param callback Function returning bool (true = network ready)
         * @return Reference to this for method chaining
         *
         * Example:
         * @code
         * mqtt.setNetworkCheck([]() {
         *     return WiFi.status() == WL_CONNECTED;
         * });
         * @endcode
         */
        ZenoPCBMQTT &setNetworkCheck(MQTTClient::NetworkCheckCallback callback);

        /**
         * @brief Get internal MQTTClient instance
         * For advanced configuration
         */
        MQTTClient &getClient();

        // ============================================
        // Topic Helpers
        // ============================================

        /**
         * @brief Get base topic for this device
         * @return Base topic: zenopcb/{deviceId}
         */
        String getBaseTopic() const;

        /**
         * @brief Build full topic from relative path
         * @param path Relative path (e.g., "telemetry/temperature")
         * @return Full topic: v1/devices/{deviceToken}/{path}
         */
        String buildTopic(const String &path) const;

        /**
         * @brief Build full topic into caller-owned buffer (zero-alloc hot path).
         *
         * Writes the same byte sequence as the String-returning overload
         * directly into a caller-supplied char buffer via snprintf. Returns
         * false on null inputs or snprintf truncation, allowing the caller
         * to short-circuit before a malformed publish.
         *
         * @param path    Null-terminated sub-path (e.g. "telemetry", "ota/response")
         * @param out     Caller-owned buffer (must be non-null)
         * @param outSize Capacity of @p out (including NUL terminator)
         * @return true on success, false on null inputs OR snprintf truncation
         */
        bool buildTopic(const char *path, char *out, size_t outSize) const;

    private:
        MQTTClient _client;
        MQTTConfig _config;
        String _deviceId;

        // Periodic online heartbeat
        unsigned long _lastStatusTime = 0;
        uint32_t _statusInterval = 60000; // 60s default

        // Topic prefixes (v1 protocol)
        static constexpr const char *TOPIC_PREFIX = "v1/devices";
        static constexpr const char *TOPIC_TELEMETRY = "telemetry";
        static constexpr const char *TOPIC_STATUS = "status";
        static constexpr const char *TOPIC_COMMAND = "command";
        static constexpr const char *TOPIC_CONFIG = "config";
        static constexpr const char *TOPIC_EVENT = "event";
        static constexpr const char *TOPIC_DIAGNOSTICS = "diagnostics";
        static constexpr const char *TOPIC_DIAGNOSTICS_REQUEST = "diagnostics/request";
        static constexpr const char *TOPIC_DIAGNOSTICS_RESPONSE = "diagnostics/response";
    };

} // namespace ZenoPCB

#endif // ZENOPCB_MQTT_H
