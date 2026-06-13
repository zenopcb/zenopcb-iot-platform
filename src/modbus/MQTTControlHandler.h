/**
 * @file MQTTControlHandler.h
 * @brief MQTT Control message handler for Modbus write operations
 *
 * Handles control messages from MQTT topic v1/devices/{token}/control
 * Message format: {"mqttKey1": value1, "mqttKey2": value2, ...}
 *
 * Supports up to 10 registers per message.
 * Automatically determines register type, connection, and data type for writing.
 *
 * @author ZenoPCB Development Team
 * @version 1.0.0
 */

#ifndef MQTT_CONTROL_HANDLER_H
#define MQTT_CONTROL_HANDLER_H

#include <Arduino.h>
#include "../ZenoJson.h"  // ArduinoJson API from vendored copy (namespace ZenoJson; see vendor/ArduinoJson/LICENSE.md)
#include <vector>
#include <functional>
#include "../storage/DataMonitorConfig.h"

namespace ZenoPCB
{
    // ============================================
    // Constants
    // ============================================
    constexpr size_t MAX_CONTROL_REGISTERS = 10; // Max registers per control message
    constexpr size_t CONTROL_JSON_SIZE = 1024;   // Max JSON size for control message

    // ============================================
    // Control Result Structure
    // ============================================

    /**
     * @brief Result of a single register write operation
     */
    struct ControlWriteResult
    {
        String mqttKey;      // Register mqttKey (6-digit)
        bool success;        // Write operation success
        String errorMessage; // Error message if failed
        double valueWritten; // Value that was written

        ControlWriteResult()
            : success(false), valueWritten(0) {}

        ControlWriteResult(const String &key, bool ok, double value = 0, const String &error = "")
            : mqttKey(key), success(ok), errorMessage(error), valueWritten(value) {}
    };

    /**
     * @brief Result of a control message (batch write)
     */
    struct ControlMessageResult
    {
        size_t totalRequests; // Total register write requests
        size_t successCount;  // Successful writes
        size_t failedCount;   // Failed writes
        std::vector<ControlWriteResult> results;

        ControlMessageResult() : totalRequests(0), successCount(0), failedCount(0) {}

        void addResult(const ControlWriteResult &result)
        {
            results.push_back(result);
            totalRequests++;
            if (result.success)
                successCount++;
            else
                failedCount++;
        }

        bool allSuccess() const { return failedCount == 0 && totalRequests > 0; }

        /**
         * @brief Build response JSON for MQTT publish
         * Format: {"success": true/false, "total": n, "ok": n, "failed": n, "errors": [...]}
         */
        String toJson() const
        {
            JsonDocument doc;
            doc["success"] = allSuccess();
            doc["total"] = totalRequests;
            doc["ok"] = successCount;
            doc["failed"] = failedCount;

            if (failedCount > 0)
            {
                JsonArray errors = doc["errors"].to<JsonArray>();
                for (const auto &r : results)
                {
                    if (!r.success)
                    {
                        JsonObject err = errors.add<JsonObject>();
                        err["key"] = r.mqttKey;
                        err["error"] = r.errorMessage;
                    }
                }
            }

            String output;
            serializeJson(doc, output);
            return output;
        }
    };

    // ============================================
    // Get-All State Tracking
    // ============================================
    struct GetAllRequest
    {
        bool pending;
        uint32_t startTime;
        uint32_t waitTimeMs;

        GetAllRequest() : pending(false), startTime(0), waitTimeMs(150) {}

        bool isReady(uint32_t now) const
        {
            return pending && (now - startTime >= waitTimeMs);
        }
    };

    // ============================================
    // Callbacks
    // ============================================
    using ControlResultCallback = std::function<void(const ControlMessageResult &result)>;
    using GetAllTelemetryCallback = std::function<void(const String &telemetryJson)>;

    // ============================================
    // MQTT Control Handler Class
    // ============================================

    /**
     * @brief Handler for MQTT control messages
     *
     * Parses incoming control messages and writes values to Modbus registers.
     * Automatically looks up register configuration (connection, address, type)
     * from ModbusDataBuffer.
     *
     * Supports special command {"get_all": true} to trigger immediate read of all registers.
     *
     * @example
     * // In MQTT message callback
     * MQTTControlHandler handler;
     * handler.onResult([](const ControlMessageResult& result) {
     *     Serial.printf("Control: %d/%d success\n", result.successCount, result.totalRequests);
     * });
     * handler.handleMessage(payload);
     */
    class MQTTControlHandler
    {
    public:
        static MQTTControlHandler &getInstance();

        /**
         * @brief Handle incoming control message
         * @param payload JSON payload {"mqttKey": value, ...} or {"get_all": true}
         * @return ControlMessageResult with results of all write operations
         */
        ControlMessageResult handleMessage(const String &payload);

        /**
         * @brief Set callback for control results
         * @param callback Function to call with results
         */
        void onResult(ControlResultCallback callback) { _resultCallback = callback; }

        /**
         * @brief Set callback for get_all telemetry publishing
         * @param callback Function to call with telemetry JSON when get_all is triggered
         */
        void onGetAllTelemetry(GetAllTelemetryCallback callback) { _getallCallback = callback; }

        /**
         * @brief Process pending get_all requests (call in loop)
         * Checks if enough time has passed since forceReadAll() was triggered
         */
        void processGetAll();

        /**
         * @brief Enable/disable debug logging
         */
        void setDebug(bool enable) { _debug = enable; }

    private:
        MQTTControlHandler() : _debug(true) {}
        ~MQTTControlHandler() = default;

        // Prevent copy
        MQTTControlHandler(const MQTTControlHandler &) = delete;
        MQTTControlHandler &operator=(const MQTTControlHandler &) = delete;

        /**
         * @brief Write a single register value via Write Queue
         * Uses RegisterPollingEngine::enqueueWriteByKey() to avoid Modbus conflicts
         * @param mqttKey Register mqttKey (6-digit)
         * @param value Value to write (will be converted based on data type)
         * @return ControlWriteResult indicating queued status
         */
        ControlWriteResult writeRegister(const String &mqttKey, double value);

        /**
         * @brief Helper to build telemetry JSON from buffer
         */
        String _buildGetAllTelemetry();

        // Callbacks
        ControlResultCallback _resultCallback;
        GetAllTelemetryCallback _getallCallback;

        // Get-All state
        GetAllRequest _getallRequest;

        // Debug flag
        bool _debug;
    };

} // namespace ZenoPCB

#endif // MQTT_CONTROL_HANDLER_H
