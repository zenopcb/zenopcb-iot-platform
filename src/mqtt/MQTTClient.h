#ifndef ZENOPCB_MQTT_CLIENT_H
#define ZENOPCB_MQTT_CLIENT_H

/**
 * @file MQTTClient.h
 * @brief ZenoPubSubClient wrapper with auto-reconnect and callback support
 *
 * This class uses Client interface abstraction to support multiple
 * connection types: WiFi, Ethernet, 4G (TinyGSM), etc.
 * The caller must provide a Client instance via setClient() before connecting.
 */

#include <Arduino.h>
#include <Client.h>
#include "PubSubClient.h"  // vendored knolleary/PubSubClient — declares class ZenoPubSubClient (post Plan 06-2.5c rename)
#include "MQTTTypes.h"
#include "../core/ZenoPCBDebug.h"

namespace ZenoPCB
{

    class MQTTClient
    {
    public:
        MQTTClient();
        ~MQTTClient();

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
         * @note The caller is responsible for managing the Client lifecycle
         *
         * Example:
         * @code
         * // For WiFi
         * WiFiClient wifiClient;
         * mqttClient.setClient(&wifiClient);
         *
         * // For Ethernet
         * EthernetClient ethClient;
         * mqttClient.setClient(&ethClient);
         *
         * // For 4G
         * TinyGsmClient gsmClient(modem);
         * mqttClient.setClient(&gsmClient);
         * @endcode
         */
        void setClient(Client *client);

        /**
         * @brief Check if a network client has been set
         * @return true if client is set
         */
        bool hasClient() const;

        // ============================================
        // Configuration
        // ============================================

        /**
         * @brief Set MQTT configuration
         */
        void setConfig(const MQTTConfig &config);

        /**
         * @brief Get current configuration
         */
        MQTTConfig getConfig() const;

        // ============================================
        // Connection
        // ============================================

        /**
         * @brief Connect to MQTT broker
         * @return true if connection initiated successfully
         */
        bool connect();

        /**
         * @brief Disconnect from MQTT broker
         */
        void disconnect();

        /**
         * @brief Check if connected
         */
        bool isConnected();

        /**
         * @brief Check if a manual connect() call is needed
         * Returns true only when DISCONNECTED (idle).
         * Returns false when CONNECTING, RECONNECTING, ERROR (internal retry handles it),
         * or CONNECTED.
         */
        bool needsManualConnect() const;

        /**
         * @brief Get current connection state
         */
        MQTTState getState() const;

        // ============================================
        // Publish
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
         * Additive overload to prevent implicit String(const char*) construction
         * when callers pass stack-buffered char[] topics/payloads. Delegates
         * straight to the underlying ZenoPubSubClient char* publish form - no
         * String round-trip on the hot path.
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
         * @brief Publish JSON message
         * @param topic Topic name
         * @param json JSON string
         * @return true if published successfully
         */
        bool publishJson(const String &topic, const String &json);

        /**
         * @brief Publish numeric value
         * @param topic Topic name
         * @param value Numeric value
         * @return true if published successfully
         */
        bool publish(const String &topic, float value);
        bool publish(const String &topic, int value);

        // ============================================
        // Subscribe
        // ============================================

        /**
         * @brief Subscribe to topic
         * @param topic Topic pattern (supports wildcards + and #)
         * @param qos Quality of Service
         * @return true if subscribed successfully
         */
        bool subscribe(const String &topic, MQTTQoS qos = MQTTQoS::QOS_0);

        /**
         * @brief Unsubscribe from topic
         * @param topic Topic name
         * @return true if unsubscribed successfully
         */
        bool unsubscribe(const String &topic);

        // ============================================
        // Callbacks
        // ============================================

        void onConnected(MQTTConnectedCallback callback);
        void onDisconnected(MQTTDisconnectedCallback callback);
        void onMessage(MQTTMessageCallback callback);
        void onError(MQTTErrorCallback callback);
        void onStateChange(MQTTStateCallback callback);

        // ============================================
        // Lifecycle
        // ============================================

        /**
         * @brief Process MQTT events - must call in loop()
         */
        void loop();

        /**
         * @brief Internal callback handler (public for static callback bridge)
         */
        void handleMqttMessage(char *topic, uint8_t *payload, unsigned int length);

        /**
         * @brief Get reference to internal ZenoPubSubClient (for direct parameter access)
         * Use sparingly — prefer setConfig() for full configuration
         */
        ZenoPubSubClient &getInternalClient() { return _mqttClient; }

        /**
         * @brief Check if network is available
         *
         * Override this via callback if not using WiFi.
         * Default: returns true (assumes network is managed externally)
         */
        using NetworkCheckCallback = std::function<bool()>;
        void setNetworkCheckCallback(NetworkCheckCallback callback);

    private:
        // Network client (provided externally)
        Client *_networkClient;

        // ZenoPubSubClient instance
        ZenoPubSubClient _mqttClient;

        // Stable broker buffer - ZenoPubSubClient stores raw pointer from setServer()
        // We must keep the string alive in a stable char[] buffer
        char _brokerStable[128];

        // Configuration
        MQTTConfig _config;

        // State
        MQTTState _state;
        unsigned long _lastReconnectAttempt;
        uint8_t _reconnectAttempts;
        uint32_t _currentReconnectInterval;

        // Callbacks
        MQTTConnectedCallback _connectedCallback;
        MQTTDisconnectedCallback _disconnectedCallback;
        MQTTMessageCallback _messageCallback;
        MQTTErrorCallback _errorCallback;
        MQTTStateCallback _stateCallback;
        NetworkCheckCallback _networkCheckCallback;

        // Internal methods
        void _setState(MQTTState newState);
        void _handleReconnect();
        bool _performConnect();
        bool _isNetworkAvailable();
    };

} // namespace ZenoPCB

#endif // ZENOPCB_MQTT_CLIENT_H
