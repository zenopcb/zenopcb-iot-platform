/**
 * @file DataMonitorMessageHandler.h
 * @brief MQTT Message Handler for Data Monitor (PLC Register) Configuration
 *
 * Parses MQTT messages with format:
 * {
 *   "t": "dm",                          // type: data-monitor
 *   "a": "c" | "u" | "d" | "e",         // action: create/update/delete/toggle
 *   "d": { ... }                        // data: monitor config object
 * }
 *
 * @author ZenoPCB Development Team
 * @version 1.0.0
 */

#ifndef DATA_MONITOR_MESSAGE_HANDLER_H
#define DATA_MONITOR_MESSAGE_HANDLER_H

#include <Arduino.h>
#include "../ZenoJson.h"  // ArduinoJson API from vendored copy (namespace ZenoJson; see vendor/ArduinoJson/LICENSE.md)
#include <functional>
#include "DataMonitorConfig.h"
#include "LittleFSManager.h"

namespace ZenoPCB
{

    /**
     * @brief Result of data monitor message handling operation
     */
    struct DataMonitorHandleResult
    {
        bool success = false;
        String errorMessage;
        String mqttKey;           // Affected monitor mqttKey
        DataMonitorAction action; // Action performed
        uint32_t processingMs;    // Processing time in ms

        /**
         * @brief Convert result to JSON response
         */
        String toJson() const
        {
            JsonDocument doc;
            doc["success"] = success;
            if (!success && errorMessage.length() > 0)
            {
                doc["error"] = errorMessage;
            }
            if (mqttKey.length() > 0)
            {
                doc["mqttKey"] = mqttKey;
            }
            // Convert action enum char to String for ArduinoJson
            char actionChar[2] = {static_cast<char>(action), '\0'};
            doc["action"] = actionChar;
            doc["processingMs"] = processingMs;

            String output;
            serializeJson(doc, output);
            return output;
        }
    };

    /**
     * @brief Callback types for data monitor events
     */
    using DataMonitorCreatedCallback = std::function<void(const DataMonitorConfig &config)>;
    using DataMonitorUpdatedCallback = std::function<void(const DataMonitorConfig &config)>;
    using DataMonitorDeletedCallback = std::function<void(const String &mqttKey)>;
    using DataMonitorToggledCallback = std::function<void(const String &mqttKey, bool enabled)>;
    using DataMonitorErrorCallback = std::function<void(const String &error, const String &payload)>;

    /**
     * @brief MQTT Message Handler for Data Monitor Configuration
     *
     * Handles incoming MQTT messages, parses JSON payload,
     * validates data, and dispatches to LittleFSManager for persistence.
     */
    class DataMonitorMessageHandler
    {
    public:
        /**
         * @brief Get singleton instance
         */
        static DataMonitorMessageHandler &getInstance();

        // Delete copy constructor and assignment
        DataMonitorMessageHandler(const DataMonitorMessageHandler &) = delete;
        DataMonitorMessageHandler &operator=(const DataMonitorMessageHandler &) = delete;

        /**
         * @brief Initialize the handler
         *
         * Must be called before handling messages.
         * Initializes LittleFSManager if not already done.
         *
         * @return true on success
         */
        bool begin();

        /**
         * @brief Check if handler is initialized
         */
        bool isInitialized() const { return _initialized; }

        /**
         * @brief Handle incoming MQTT message
         *
         * Main entry point for processing data monitor messages.
         *
         * @param topic MQTT topic (for validation)
         * @param payload JSON payload string
         * @return DataMonitorHandleResult with success/error info
         */
        DataMonitorHandleResult handleMessage(const String &topic, const String &payload);

        /**
         * @brief Handle raw bytes payload
         *
         * @param topic MQTT topic
         * @param payload Byte array payload
         * @param length Payload length
         * @return DataMonitorHandleResult
         */
        DataMonitorHandleResult handleMessage(const String &topic, const uint8_t *payload, size_t length);

        // ============================================
        // Event Callbacks
        // ============================================

        /**
         * @brief Set callback for monitor created event
         */
        void onMonitorCreated(DataMonitorCreatedCallback callback)
        {
            _onCreated = callback;
        }

        /**
         * @brief Set callback for monitor updated event
         */
        void onMonitorUpdated(DataMonitorUpdatedCallback callback)
        {
            _onUpdated = callback;
        }

        /**
         * @brief Set callback for monitor deleted event
         */
        void onMonitorDeleted(DataMonitorDeletedCallback callback)
        {
            _onDeleted = callback;
        }

        /**
         * @brief Set callback for monitor toggled event
         */
        void onMonitorToggled(DataMonitorToggledCallback callback)
        {
            _onToggled = callback;
        }

        /**
         * @brief Set callback for error event
         */
        void onError(DataMonitorErrorCallback callback)
        {
            _onError = callback;
        }

        // ============================================
        // Query Methods
        // ============================================

        /**
         * @brief Get monitor by mqttKey
         *
         * @param mqttKey Monitor identifier (6-digit)
         * @param outConfig Output config object
         * @return true if found
         */
        bool getMonitor(const String &mqttKey, DataMonitorConfig &outConfig);

        /**
         * @brief Get all monitor mqttKeys
         *
         * @return Vector of mqttKeys
         */
        std::vector<String> getAllMonitorIds();

        /**
         * @brief Get data monitor metadata
         */
        DataMonitorMetadata getMetadata();

        /**
         * @brief Check if monitor exists
         */
        bool monitorExists(const String &mqttKey);

        /**
         * @brief Get monitors by connection ID
         *
         * @param connectionId Connection shortId
         * @return Vector of monitors linked to this connection
         */
        std::vector<DataMonitorConfig> getMonitorsByConnection(const String &connectionId);

        // ============================================
        // Statistics
        // ============================================

        /**
         * @brief Get total messages processed
         */
        uint32_t getMessagesProcessed() const { return _messagesProcessed; }

        /**
         * @brief Get total errors occurred
         */
        uint32_t getErrorsCount() const { return _errorsCount; }

        /**
         * @brief Get average processing time in ms
         */
        uint32_t getAvgProcessingTime() const
        {
            return _messagesProcessed > 0 ? _totalProcessingTime / _messagesProcessed : 0;
        }

        /**
         * @brief Reset statistics
         */
        void resetStats();

    private:
        // Private constructor for singleton
        DataMonitorMessageHandler() = default;

        // Initialization flag
        bool _initialized = false;

        // Callbacks
        DataMonitorCreatedCallback _onCreated = nullptr;
        DataMonitorUpdatedCallback _onUpdated = nullptr;
        DataMonitorDeletedCallback _onDeleted = nullptr;
        DataMonitorToggledCallback _onToggled = nullptr;
        DataMonitorErrorCallback _onError = nullptr;

        // Statistics
        uint32_t _messagesProcessed = 0;
        uint32_t _errorsCount = 0;
        uint32_t _totalProcessingTime = 0;

        // ============================================
        // Internal Methods
        // ============================================

        /**
         * @brief Parse JSON payload into DataMonitorMessage
         */
        bool _parsePayload(const String &payload, DataMonitorMessage &outMessage, String &error);

        /**
         * @brief Handle CREATE action
         */
        DataMonitorHandleResult _handleCreate(const DataMonitorMessage &message);

        /**
         * @brief Handle UPDATE action
         */
        DataMonitorHandleResult _handleUpdate(const DataMonitorMessage &message);

        /**
         * @brief Handle DELETE action
         */
        DataMonitorHandleResult _handleDelete(const DataMonitorMessage &message);

        /**
         * @brief Handle TOGGLE action
         */
        DataMonitorHandleResult _handleToggle(const DataMonitorMessage &message);

        /**
         * @brief Validate monitor data before create/update
         */
        bool _validateMonitorData(const JsonObject &data, String &error);

        /**
         * @brief Generate error result
         */
        DataMonitorHandleResult _errorResult(const String &error, DataMonitorAction action = DataMonitorAction::UNKNOWN);

        /**
         * @brief Generate success result
         */
        DataMonitorHandleResult _successResult(const String &mqttKey, DataMonitorAction action, uint32_t processingMs);

        /**
         * @brief Notify error callback
         */
        void _notifyError(const String &error, const String &payload);
    };

} // namespace ZenoPCB

#endif // DATA_MONITOR_MESSAGE_HANDLER_H
