#ifndef ZENOPCB_DIAGNOSTICS_HANDLER_H
#define ZENOPCB_DIAGNOSTICS_HANDLER_H

#include "DiagnosticsTypes.h"
#include "DiagnosticsCollector.h"
#include "../ZenoJson.h"  // ArduinoJson API from vendored copy (namespace ZenoJson; see vendor/ArduinoJson/LICENSE.md)

namespace ZenoPCB
{

    // Forward declaration
    class ZenoPCBMQTT;

    /**
     * @brief Handles diagnostics MQTT messaging with retry logic
     *
     * Responsibilities:
     * - Format diagnostics data to JSON
     * - Send passive updates periodically
     * - Handle on-demand requests from backend
     * - Retry failed on-demand responses (1-2 times)
     */
    class DiagnosticsHandler
    {
    public:
        /**
         * @brief Construct a new DiagnosticsHandler
         *
         * @param collector Pointer to DiagnosticsCollector
         * @param mqtt Pointer to MQTT client
         */
        DiagnosticsHandler(DiagnosticsCollector *collector, ZenoPCBMQTT *mqtt);

        /**
         * @brief Set maximum retry attempts
         *
         * @param maxRetries Max retry count (default: 2)
         */
        void setMaxRetries(uint8_t maxRetries);

        /**
         * @brief Send passive diagnostics update
         *
         * Sends diagnostics data without requestId (periodic update)
         * Fire-and-forget, no retry
         *
         * @return true if sent successfully
         * @return false if send failed
         */
        bool sendPassive();

        /**
         * @brief Send on-demand diagnostics response
         *
         * Sends diagnostics data with requestId (response to backend request)
         * Retries up to maxRetries times on failure
         *
         * @param requestId Request ID from backend
         * @return true if sent successfully (possibly after retries)
         * @return false if all retry attempts failed
         */
        bool sendOnDemand(const String &requestId);

        /**
         * @brief Handle on-demand request from backend
         *
         * Called by MQTT callback when request is received
         *
         * @param payload JSON payload with requestId
         */
        void handleRequest(const String &payload);

        /**
         * @brief Set callbacks
         */
        void setRequestCallback(DiagnosticsRequestCallback callback);
        void setSentCallback(DiagnosticsSentCallback callback);
        void setErrorCallback(DiagnosticsErrorCallback callback);

    private:
        DiagnosticsCollector *_collector;
        ZenoPCBMQTT *_mqtt;
        uint8_t _maxRetries;

        // Callbacks
        DiagnosticsRequestCallback _requestCallback;
        DiagnosticsSentCallback _sentCallback;
        DiagnosticsErrorCallback _errorCallback;

        // Helper methods
        String _formatJson(const DiagnosticsData &data);
        bool _sendWithRetry(const String &topic, const String &payload, bool isPassive);
        String _extractRequestId(const String &jsonPayload);
    };

} // namespace ZenoPCB

#endif // ZENOPCB_DIAGNOSTICS_HANDLER_H
