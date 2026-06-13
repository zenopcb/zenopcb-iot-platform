#ifndef ZENOPCB_MQTT_TYPES_H
#define ZENOPCB_MQTT_TYPES_H

/**
 * @file MQTTTypes.h
 * @brief MQTT types, structures and callbacks for ZenoPCB IoT Library
 */

#include <Arduino.h>
#include <functional>

namespace ZenoPCB
{
    // ============================================
    // MQTT Constants
    // ============================================

    constexpr uint16_t MQTT_DEFAULT_PORT = 1883;
    constexpr uint16_t MQTT_DEFAULT_TLS_PORT = 8883;
    constexpr uint16_t MQTT_DEFAULT_KEEPALIVE = 15;      // Default: 15s — broker detects dead client in ~22s
    constexpr uint16_t MQTT_CELLULAR_KEEPALIVE = 30;     // 4G: 30s — broker detects in ~45s (modem AT overhead)
    constexpr uint16_t MQTT_DEFAULT_SOCKET_TIMEOUT = 5;  // 5s: fail fast when no internet, IO-0 button responsive within ~10s
    constexpr uint16_t MQTT_CELLULAR_SOCKET_TIMEOUT = 5; // 4G socket timeout: 5s
    constexpr uint16_t ZENOPCB_MQTT_BUFFER_SIZE = 4096;  // Large buffer: supports 50+ registers JSON + diagnostics over 4G
    constexpr uint8_t MQTT_MAX_RECONNECT_ATTEMPTS = 10;
    constexpr uint32_t MQTT_RECONNECT_INTERVAL_MS = 5000;
    constexpr uint32_t MQTT_RECONNECT_MAX_INTERVAL_MS = 60000;

    // ============================================
    // MQTT QoS Levels
    // ============================================

    enum class MQTTQoS : uint8_t
    {
        QOS_0 = 0, // At most once (Fire and forget)
        QOS_1 = 1, // At least once (Acknowledged delivery)
        QOS_2 = 2  // Exactly once (Assured delivery)
    };

    // ============================================
    // MQTT Connection State
    // ============================================

    enum class MQTTState : uint8_t
    {
        DISCONNECTED, // Not connected
        CONNECTING,   // Connection in progress
        CONNECTED,    // Connected to broker
        RECONNECTING, // Attempting to reconnect
        ERROR         // Connection error
    };

    // ============================================
    // MQTT Configuration
    // ============================================

    struct MQTTConfig
    {
        String broker;                                        // Broker hostname or IP
        uint16_t port = MQTT_DEFAULT_PORT;                    // Broker port
        String clientId;                                      // MQTT Client ID
        String username;                                      // Authentication username
        String password;                                      // Authentication password
        bool useTLS = false;                                  // Use TLS/SSL
        uint16_t keepAlive = MQTT_DEFAULT_KEEPALIVE;          // Keep alive interval (seconds)
        uint16_t socketTimeout = MQTT_DEFAULT_SOCKET_TIMEOUT; // TCP socket timeout (seconds)
        bool cleanSession = false;                            // ⚡ false để LWT hoạt động + giữ session
        bool autoReconnect = true;                            // Auto reconnect on disconnect
        uint8_t maxReconnectAttempts = MQTT_MAX_RECONNECT_ATTEMPTS;
        uint32_t reconnectInterval = MQTT_RECONNECT_INTERVAL_MS;

        // Last Will and Testament (LWT)
        String lwt_topic;
        String lwt_payload;
        MQTTQoS lwt_qos = MQTTQoS::QOS_0; // QoS 0 for 4G compatibility (same as reference)
        bool lwt_retain = false;          // Retain false for 4G (same as reference)
    };

    // ============================================
    // MQTT Message
    // ============================================

    struct MQTTMessage
    {
        String topic;
        String payload;
        MQTTQoS qos = MQTTQoS::QOS_0;
        bool retain = false;
    };

    // ============================================
    // MQTT Callbacks
    // ============================================

    /**
     * @brief Callback when connected to MQTT broker
     */
    using MQTTConnectedCallback = std::function<void()>;

    /**
     * @brief Callback when disconnected from MQTT broker
     * @param reason Disconnect reason code
     */
    using MQTTDisconnectedCallback = std::function<void(int reason)>;

    /**
     * @brief Callback when message received
     * @param topic Message topic
     * @param payload Message payload
     */
    using MQTTMessageCallback = std::function<void(const String &topic, const String &payload)>;

    /**
     * @brief Callback for MQTT errors
     * @param error Error message
     */
    using MQTTErrorCallback = std::function<void(const String &error)>;

    /**
     * @brief Callback for connection state changes
     * @param state New MQTT state
     */
    using MQTTStateCallback = std::function<void(MQTTState state)>;

    /**
     * @brief Callback for diagnostics request from backend
     * @param payload JSON payload with requestId
     */
    using MQTTDiagnosticsRequestCallback = std::function<void(const String &payload)>;

} // namespace ZenoPCB

#endif // ZENOPCB_MQTT_TYPES_H
