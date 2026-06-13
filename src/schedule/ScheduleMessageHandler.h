#ifndef ZENOPCB_SCHEDULE_MESSAGE_HANDLER_H
#define ZENOPCB_SCHEDULE_MESSAGE_HANDLER_H

#include <Arduino.h>
#include "../vendor/ArduinoJson/ArduinoJson.h"  // ArduinoJson API from vendored copy (namespace ZenoJson; see vendor/ArduinoJson/LICENSE.md)
#include <functional>
#include <vector>
#include "../schedule/ScheduleConfig.h"
#include "../schedule/ScheduleTypes.h"
#include "../schedule/ScheduleStorage.h"
#include "../core/TimeManager.h"

namespace ZenoPCB
{

    /**
     * @brief Result of schedule message handling operation
     */
    struct ScheduleHandleResult
    {
        bool success = false;
        String errorMessage;
        String scheduleId;     // Affected schedule ID
        ScheduleAction action; // Action performed
        uint32_t processingMs; // Processing time in ms

        /**
         * @brief Convert result to JSON response for ACK
         */
        String toJson() const
        {
            JsonDocument doc;
            char actionChar[2] = {(char)action, '\0'};
            doc["a"] = actionChar; // Action acknowledged as string
            doc["id"] = scheduleId;
            doc["status"] = success ? "ok" : "error";

            if (!success && errorMessage.length() > 0)
            {
                doc["error"] = errorMessage;
            }

            String output;
            serializeJson(doc, output);
            return output;
        }
    };

    /**
     * @brief Callback types for schedule events
     */
    using ScheduleCreatedCallback = std::function<void(const ScheduleConfig &config)>;
    using ScheduleUpdatedCallback = std::function<void(const ScheduleConfig &config)>;
    using ScheduleDeletedCallback = std::function<void(const String &scheduleId)>;
    using ScheduleSyncedCallback = std::function<void(uint8_t count)>;
    using ScheduleMessageErrorCallback = std::function<void(const String &error, const String &payload)>;

    /**
     * @brief MQTT Message Handler for Schedule Management
     *
     * Handles incoming MQTT messages with format:
     * {
     *   "t": "s",               // type: schedule
     *   "a": "c|u|d|s",        // action: create/update/delete/sync
     *   "d": { ... } | [ ... ]  // data: schedule object or array (for sync)
     * }
     *
     * Singleton pattern for global access.
     */
    class ScheduleMessageHandler
    {
    public:
        /**
         * @brief Get singleton instance
         */
        static ScheduleMessageHandler &getInstance();

        /**
         * @brief Handle incoming MQTT schedule message
         *
         * @param topic MQTT topic (e.g., "v1/devices/{token}/schedules")
         * @param payload JSON payload string
         * @return ScheduleHandleResult Result of operation
         */
        ScheduleHandleResult handleMessage(const String &topic, const String &payload);

        /**
         * @brief Validate schedule ID format
         *
         * @param id Schedule ID to validate
         * @return true if valid (4-digit numeric: "0001"-"9999")
         */
        static bool validateScheduleId(const String &id);

        /**
         * @brief Validate register MQTT key format
         *
         * @param rid Register MQTT key to validate
         * @return true if valid (6-digit numeric)
         */
        static bool validateRegisterKey(const String &rid);

        /**
         * @brief Validate time format "HH:mm:ss"
         *
         * @param timeStr Time string to validate
         * @return true if valid format
         */
        static bool validateTimeFormat(const String &timeStr);

        /**
         * @brief Validate interval range (1000-3600000ms)
         *
         * @param intervalMs Interval in milliseconds
         * @return true if within valid range
         */
        static bool validateInterval(uint32_t intervalMs);

        // ============================================
        // Callback Registration
        // ============================================

        void onScheduleCreated(ScheduleCreatedCallback callback);
        void onScheduleUpdated(ScheduleUpdatedCallback callback);
        void onScheduleDeleted(ScheduleDeletedCallback callback);
        void onScheduleSynced(ScheduleSyncedCallback callback);
        void onError(ScheduleMessageErrorCallback callback);

        // ============================================
        // Public Utilities
        // ============================================

        /**
         * @brief Get ACK topic for device
         *
         * @param accessToken Device access token
         * @return String ACK topic (e.g., "v1/devices/{token}/schedules/ack")
         */
        static String getAckTopic(const String &accessToken);

    private:
        // Singleton pattern
        ScheduleMessageHandler() = default;
        ScheduleMessageHandler(const ScheduleMessageHandler &) = delete;
        ScheduleMessageHandler &operator=(const ScheduleMessageHandler &) = delete;

        // ============================================
        // Private Parsing & Validation
        // ============================================

        /**
         * @brief Parse MQTT payload to JSON
         */
        bool _parsePayload(const String &payload, JsonDocument &doc, String &error);

        /**
         * @brief Validate message structure {"t":"s", "a":"...", "d":...}
         */
        bool _validateMessageStructure(const JsonDocument &doc, String &error);

        /**
         * @brief Validate schedule data object
         */
        bool _validateScheduleData(const JsonObject &data, String &error);

        /**
         * @brief Parse JSON object to ScheduleConfig structure
         */
        bool _parseScheduleConfig(const JsonObject &data, ScheduleConfig &outConfig, String &error);

        // ============================================
        // Action Handlers
        // ============================================

        /**
         * @brief Handle CREATE action
         */
        ScheduleHandleResult _handleCreate(const JsonObject &data);

        /**
         * @brief Handle UPDATE action
         */
        ScheduleHandleResult _handleUpdate(const JsonObject &data);

        /**
         * @brief Handle DELETE action
         */
        ScheduleHandleResult _handleDelete(const JsonObject &data);

        /**
         * @brief Handle SYNC action (full synchronization)
         */
        ScheduleHandleResult _handleSync(const JsonVariant &data);

        // ============================================
        // Callbacks
        // ============================================

        ScheduleCreatedCallback _onCreatedCallback = nullptr;
        ScheduleUpdatedCallback _onUpdatedCallback = nullptr;
        ScheduleDeletedCallback _onDeletedCallback = nullptr;
        ScheduleSyncedCallback _onSyncedCallback = nullptr;
        ScheduleMessageErrorCallback _onErrorCallback = nullptr;
    };

} // namespace ZenoPCB

#endif // ZENOPCB_SCHEDULE_MESSAGE_HANDLER_H
