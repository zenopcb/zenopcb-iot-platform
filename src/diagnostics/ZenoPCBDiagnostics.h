#ifndef ZENOPCB_DIAGNOSTICS_H
#define ZENOPCB_DIAGNOSTICS_H

#include "DiagnosticsTypes.h"
#include "DiagnosticsCollector.h"
#include "DiagnosticsHandler.h"

namespace ZenoPCB
{

    // Forward declaration
    class ZenoPCBMQTT;

    /**
     * @brief Public API for ZenoPCB Diagnostics Module
     *
     * Features:
     * - Automatic passive updates every 10 minutes
     * - Send first diagnostics immediately when MQTT connected
     * - On-demand request/response with retry logic
     * - ConnectionType management (set by mobile app)
     * - Fluent API with callback support
     */
    class ZenoPCBDiagnostics
    {
    public:
        /**
         * @brief Construct a new ZenoPCBDiagnostics
         *
         * @param deviceInfo Pointer to device information
         * @param mqtt Pointer to MQTT client
         */
        ZenoPCBDiagnostics(const DeviceInfo *deviceInfo, ZenoPCBMQTT *mqtt);

        /**
         * @brief Destructor
         */
        ~ZenoPCBDiagnostics();

        /**
         * @brief Initialize diagnostics module
         *
         * @param config Diagnostics configuration
         * @return true if initialized successfully
         * @return false if initialization failed
         */
        bool begin(const DiagnosticsConfig &config = DiagnosticsConfig());

        /**
         * @brief Main loop - must be called periodically
         *
         * Handles:
         * - Sending passive updates at configured interval
         * - Auto-send on first MQTT connection
         */
        void loop();

        /**
         * @brief Set network provider for cellular diagnostics data
         *
         * @param provider Pointer to network provider (4G, Ethernet, etc.)
         */
        void setNetworkProvider(ZenoNetworkProvider *provider);

        /**
         * @brief Set diagnostics send interval
         *
         * @param intervalMs Interval in milliseconds (default: 600000 = 10 minutes)
         */
        void setInterval(uint32_t intervalMs);

        /**
         * @brief Set connection type (configured by mobile app)
         *
         * @param type Connection type string ("WIFI", "ETHERNET", "4G", "5G")
         */
        void setConnectionType(const String &type);

        /**
         * @brief Set connection type enum
         *
         * @param type Connection type enum
         */
        void setConnectionType(DiagnosticsConnectionType type);

        /**
         * @brief Enable/disable passive updates
         *
         * @param enable true to enable, false to disable
         */
        void enablePassive(bool enable);

        /**
         * @brief Set maximum retry attempts for on-demand responses
         *
         * @param maxRetries Max retry count (default: 2)
         */
        void setMaxRetries(uint8_t maxRetries);

        /**
         * @brief Manually send diagnostics now (bypasses interval timer)
         *
         * @return true if sent successfully
         * @return false if send failed
         */
        bool sendNow();

        /**
         * @brief Register callback for on-demand requests
         *
         * @param callback Callback function
         */
        void onRequest(DiagnosticsRequestCallback callback);

        /**
         * @brief Register callback for successful send
         *
         * @param callback Callback function
         */
        void onSent(DiagnosticsSentCallback callback);

        /**
         * @brief Register callback for send errors
         *
         * @param callback Callback function
         */
        void onError(DiagnosticsErrorCallback callback);

        /**
         * @brief Handle on-demand request from MQTT
         *
         * Called by MQTT callback when diagnostics/request is received
         *
         * @param payload JSON payload with requestId
         */
        void handleRequest(const String &payload);

    private:
        const DeviceInfo *_deviceInfo;
        ZenoPCBMQTT *_mqtt;

        DiagnosticsCollector *_collector;
        DiagnosticsHandler *_handler;

        DiagnosticsConfig _config;
        unsigned long _lastSendTime;
        bool _firstSendDone;

        // Track MQTT connection state for auto-send on connect
        bool _mqttWasConnected;
        unsigned long _firstConnectTime = 0; // Delayed first send for 4G stability
    };

} // namespace ZenoPCB

#endif // ZENOPCB_DIAGNOSTICS_H
