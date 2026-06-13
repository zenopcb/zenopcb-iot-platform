/**
 * @file ConfigMessageHandler.h
 * @brief MQTT Message Handler for Connection Configuration
 *
 * Parses MQTT messages with format:
 * {
 *   "t": "cc",              // type: connection-config
 *   "a": "c" | "u" | "d",   // action: create/update/delete
 *   "d": { ... }            // data: config object
 * }
 *
 * @author ZenoPCB Development Team
 * @version 1.0.0
 */

#ifndef CONFIG_MESSAGE_HANDLER_H
#define CONFIG_MESSAGE_HANDLER_H

#include <Arduino.h>
#include "../vendor/ArduinoJson/ArduinoJson.h"  // ArduinoJson API from vendored copy (namespace ZenoJson; see vendor/ArduinoJson/LICENSE.md)
#include <functional>
#include "ConnectionConfig.h"
#include "LittleFSManager.h"

namespace ZenoPCB
{

    /**
     * @brief Result of message handling operation
     */
    struct HandleResult
    {
        bool success = false;
        String errorMessage;
        String shortId;        // Affected config shortId
        ConfigAction action;   // Action performed
        uint32_t processingMs; // Processing time in ms

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
            if (shortId.length() > 0)
            {
                doc["shortId"] = shortId;
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
     * @brief Callback types for config events
     */
    using ConfigCreatedCallback = std::function<void(const ConnectionConfig &config)>;
    using ConfigUpdatedCallback = std::function<void(const ConnectionConfig &config)>;
    using ConfigDeletedCallback = std::function<void(const String &shortId)>;
    using ConfigErrorCallback = std::function<void(const String &error, const String &payload)>;

    /**
     * @brief MQTT Message Handler for Connection Configuration
     *
     * Handles incoming MQTT messages, parses JSON payload,
     * validates data, and dispatches to LittleFSManager for persistence.
     */
    class ConfigMessageHandler
    {
    public:
        /**
         * @brief Get singleton instance
         */
        static ConfigMessageHandler &getInstance();

        // Delete copy constructor and assignment
        ConfigMessageHandler(const ConfigMessageHandler &) = delete;
        ConfigMessageHandler &operator=(const ConfigMessageHandler &) = delete;

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
         * Main entry point for processing config messages.
         *
         * @param topic MQTT topic (for validation)
         * @param payload JSON payload string
         * @return HandleResult with success/error info
         */
        HandleResult handleMessage(const String &topic, const String &payload);

        /**
         * @brief Handle raw bytes payload
         *
         * @param topic MQTT topic
         * @param payload Byte array payload
         * @param length Payload length
         * @return HandleResult
         */
        HandleResult handleMessage(const String &topic, const uint8_t *payload, size_t length);

        // ============================================
        // Event Callbacks
        // ============================================

        /**
         * @brief Set callback for config created event
         */
        void onConfigCreated(ConfigCreatedCallback callback)
        {
            _onCreated = callback;
        }

        /**
         * @brief Set callback for config updated event
         */
        void onConfigUpdated(ConfigUpdatedCallback callback)
        {
            _onUpdated = callback;
        }

        /**
         * @brief Set callback for config deleted event
         */
        void onConfigDeleted(ConfigDeletedCallback callback)
        {
            _onDeleted = callback;
        }

        /**
         * @brief Set callback for error event
         */
        void onError(ConfigErrorCallback callback)
        {
            _onError = callback;
        }

        // ============================================
        // Query Methods
        // ============================================

        /**
         * @brief Get config by shortId
         *
         * @param shortId Config identifier
         * @param outConfig Output config object
         * @return true if found
         */
        bool getConfig(const String &shortId, ConnectionConfig &outConfig);

        /**
         * @brief Get all config shortIds
         *
         * @return Vector of shortIds
         */
        std::vector<String> getAllConfigIds();

        /**
         * @brief Get connection metadata
         */
        ConnectionMetadata getMetadata();

        /**
         * @brief Check if config exists
         */
        bool configExists(const String &shortId);

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
        ConfigMessageHandler() = default;

        // Initialization flag
        bool _initialized = false;

        // Callbacks
        ConfigCreatedCallback _onCreated = nullptr;
        ConfigUpdatedCallback _onUpdated = nullptr;
        ConfigDeletedCallback _onDeleted = nullptr;
        ConfigErrorCallback _onError = nullptr;

        // Statistics
        uint32_t _messagesProcessed = 0;
        uint32_t _errorsCount = 0;
        uint32_t _totalProcessingTime = 0;

        // ============================================
        // Internal Methods
        // ============================================

        /**
         * @brief Parse JSON payload into ConfigMessage
         */
        bool _parsePayload(const String &payload, ConfigMessage &outMessage, String &error);

        /**
         * @brief Handle CREATE action
         */
        HandleResult _handleCreate(const ConfigMessage &message);

        /**
         * @brief Handle UPDATE action
         */
        HandleResult _handleUpdate(const ConfigMessage &message);

        /**
         * @brief Handle DELETE action
         */
        HandleResult _handleDelete(const ConfigMessage &message);

        /**
         * @brief Validate config data before create/update
         */
        bool _validateConfigData(const JsonObject &data, String &error);

        /**
         * @brief Generate error result
         */
        HandleResult _errorResult(const String &error, ConfigAction action = ConfigAction::UNKNOWN);

        /**
         * @brief Generate success result
         */
        HandleResult _successResult(const String &shortId, ConfigAction action, uint32_t processingMs);

        /**
         * @brief Notify error callback
         */
        void _notifyError(const String &error, const String &payload);
    };

} // namespace ZenoPCB

#endif // CONFIG_MESSAGE_HANDLER_H
