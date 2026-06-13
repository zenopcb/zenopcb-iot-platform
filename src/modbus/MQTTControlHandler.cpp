/**
 * @file MQTTControlHandler.cpp
 * @brief Implementation of MQTT Control message handler
 *
 * Handles control messages from MQTT and writes values to Modbus registers.
 * Uses write queue to avoid conflicts with polling reads.
 *
 * @author ZenoPCB Development Team
 * @version 1.1.0
 */

// Plan 06-03 D-03 — Modbus subsystem is ESP32-only; TU guard so PIO
// library scanner reduces this file to empty TU on ESP8266.
#if defined(ESP32)

#include "MQTTControlHandler.h"
#include "ModbusDataBuffer.h"
#include "ModbusConnectionManager.h"
#include "RegisterPollingEngine.h"
#include "../core/ZenoPCBDebug.h"
#include "../core/ZKeyTypes.h"
#include "../core/ZKeyBuffer.h"

namespace ZenoPCB
{
    // Singleton instance
    MQTTControlHandler &MQTTControlHandler::getInstance()
    {
        static MQTTControlHandler instance;
        return instance;
    }

    ControlMessageResult MQTTControlHandler::handleMessage(const String &payload)
    {
        ControlMessageResult result;

        if (_debug)
        {
            Serial.printf("[Control] 📥 Received: %s\n", payload.c_str());
        }

        // Parse JSON
        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, payload);
        if (error)
        {
            Serial.printf("[Control] ❌ JSON parse error: %s\n", error.c_str());
            return result;
        }

        JsonObject obj = doc.as<JsonObject>();

        // ⭐ Check for get_all: legacy format {"get_all":true}
        // OR cloud format {"requestType":"get_telemetry","command":"GET_DATA"}
        bool isGetAll = (obj.containsKey("get_all") && obj["get_all"].as<bool>());
        if (!isGetAll && obj.containsKey("command"))
        {
            const char *cmd = obj["command"] | "";
            if (strcmp(cmd, "GET_DATA") == 0)
                isGetAll = true;
        }
        if (!isGetAll && obj.containsKey("requestType"))
        {
            const char *rt = obj["requestType"] | "";
            if (strcmp(rt, "get_telemetry") == 0)
                isGetAll = true;
        }

        if (isGetAll)
        {
            Serial.println("[Control] 🔄 GET_ALL command received - Triggering non-blocking read...");

            // Force read all registers (non-blocking)
            auto &pollingEngine = RegisterPollingEngine::getInstance();
            pollingEngine.forceReadAll();

            // Set pending state with timestamp (will be processed in loop)
            _getallRequest.pending = true;
            _getallRequest.startTime = millis();

            Serial.printf("[Control] ⏱️ Scheduled get_all processing after %dms\n", _getallRequest.waitTimeMs);

            // Mark as successful command
            result.totalRequests = 1;
            result.successCount = 1;
            result.addResult(ControlWriteResult("get_all", true, 1.0, "Read triggered, pending"));

            return result;
        }

        // Iterate through all key-value pairs
        size_t count = 0;
        for (JsonPair kv : obj)
        {
            if (count >= MAX_CONTROL_REGISTERS)
            {
                Serial.printf("[Control] ⚠️ Max %d registers per message, ignoring rest\n",
                              MAX_CONTROL_REGISTERS);
                break;
            }

            String mqttKey = kv.key().c_str();

            // ⭐ Check if this is a Z key (Z0-Z254)
            if (isZKey(mqttKey.c_str()))
            {
                ZKey zk = stringToZKey(mqttKey.c_str());
                ZKeyBuffer::getInstance().setFromJson(zk, kv.value());

                if (_debug)
                {
                    Serial.printf("[Control] 🔑 Z key %s routed to ZKeyBuffer\n", mqttKey.c_str());
                }

                result.addResult(ControlWriteResult(mqttKey, true, 0, "Z key accepted"));
                count++;
                continue;
            }

            // Validate mqttKey format (6 digits) - Modbus keys
            if (mqttKey.length() != MQTT_KEY_LENGTH)
            {
                result.addResult(ControlWriteResult(mqttKey, false, 0,
                                                    "Invalid mqttKey format"));
                continue;
            }

            // Get value (support int, float, bool)
            double value = 0;
            if (kv.value().is<bool>())
            {
                value = kv.value().as<bool>() ? 1.0 : 0.0;
            }
            else if (kv.value().is<int64_t>())
            {
                value = static_cast<double>(kv.value().as<int64_t>());
            }
            else if (kv.value().is<double>())
            {
                value = kv.value().as<double>();
            }
            else
            {
                result.addResult(ControlWriteResult(mqttKey, false, 0,
                                                    "Invalid value type"));
                continue;
            }

            // Write register
            ControlWriteResult writeResult = writeRegister(mqttKey, value);
            result.addResult(writeResult);

            count++;
        }

        // Call result callback if set
        if (_resultCallback)
        {
            _resultCallback(result);
        }

        if (_debug)
        {
            Serial.printf("[Control] 📊 Result: %d/%d success\n",
                          result.successCount, result.totalRequests);
        }

        return result;
    }

    ControlWriteResult MQTTControlHandler::writeRegister(const String &mqttKey, double value)
    {
        auto &buffer = ModbusDataBuffer::getInstance();
        auto &pollingEngine = RegisterPollingEngine::getInstance();

        // Get register configuration
        const DataMonitorConfig *config = buffer.getRegisterConfig(mqttKey);
        if (!config)
        {
            return ControlWriteResult(mqttKey, false, value,
                                      "Register not found");
        }

        // Check if register is writable (HOLDING or COIL only)
        if (config->registerType != RegisterType::REG_HOLDING &&
            config->registerType != RegisterType::REG_COIL)
        {
            return ControlWriteResult(mqttKey, false, value,
                                      "Register not writable (INPUT/DISCRETE)");
        }

        // ⭐ NEW: Use write queue instead of direct write
        // This avoids conflicts with ongoing read operations
        // Write-hold and buffer injection handled by _completeCurrentWrite in polling engine
        // This callback handles instant telemetry publish (both success and failure)
        bool enqueued = pollingEngine.enqueueWriteByKey(mqttKey, value,
                                                        [mqttKey, value](bool success, const String &error)
                                                        {
                                                            if (success)
                                                            {
                                                                Serial.printf("[Control] ✅ Write successful: %s = %.2f\n", mqttKey.c_str(), value);
                                                            }
                                                            else
                                                            {
                                                                Serial.printf("[Control] ❌ Write failed: %s \u2014 %s\n", mqttKey.c_str(), error.c_str());
                                                            }
                                                            // Always publish telemetry immediately after write completes:
                                                            // - Success: publishes new held value so mobile UI stays correct
                                                            // - Failure: publishes actual device value so mobile reverts fast (not wait 30s)
                                                            ModbusDataBuffer::getInstance().requestInstantPublish();
                                                        });

        if (enqueued)
        {
            if (_debug)
            {
                Serial.printf("[Control] 📥 Queued: %s = %.2f (addr=%d, type=%d)\n",
                              mqttKey.c_str(), value, config->address, (int)config->dataType);
            }
            return ControlWriteResult(mqttKey, true, value); // Queued = success
        }
        else
        {
            return ControlWriteResult(mqttKey, false, value, "Failed to enqueue write");
        }
    }

    // ============================================
    // Process pending get_all requests
    // ============================================
    void MQTTControlHandler::processGetAll()
    {
        // Check if get_all is pending and ready
        if (!_getallRequest.isReady(millis()))
            return;

        Serial.println("[Control] ✅ Get_all wait time elapsed - Building telemetry...");

        // Build telemetry JSON
        String telemetryJson = _buildGetAllTelemetry();

        Serial.printf("[Control] 📤 Telemetry JSON built (%d bytes)\n", telemetryJson.length());
        if (telemetryJson.length() < 500)
        {
            Serial.printf("[Control] 📊 Data: %s\n", telemetryJson.c_str());
        }

        // Trigger callback to publish
        if (_getallCallback)
        {
            Serial.println("[Control] 📮 Triggering callback to publish control response...");
            _getallCallback(telemetryJson);
            Serial.println("[Control] ✅ Callback completed");
        }
        else
        {
            Serial.println("[Control] ⚠️ No callback registered for get_all response!");
        }

        // Clear pending state
        _getallRequest.pending = false;
    }

    // ============================================
    // Helper: Build telemetry JSON (Modbus + Z Keys)
    // ============================================
    String MQTTControlHandler::_buildGetAllTelemetry()
    {
        JsonDocument doc;

        // 1. Merge Modbus register data (if any registers configured)
        // includeNulls=true: get_all must show ALL registers including stale-expired as null
        String modbusJson = ModbusDataBuffer::getInstance().buildTelemetryJson(4096, true);
        if (modbusJson.length() > 2 && modbusJson != "null") // skip empty "{}" or "null"
        {
            JsonDocument modbusDoc;
            if (deserializeJson(modbusDoc, modbusJson) == DeserializationError::Ok)
            {
                for (JsonPair kv : modbusDoc.as<JsonObject>())
                {
                    doc[kv.key()] = kv.value();
                }
            }
        }

        // 2. Merge ALL set Z Keys (snapshot, ignores dirty flag)
        ZKeyBuffer::getInstance().mergeAllIntoJson(doc);

        String output;
        serializeJson(doc, output);
        return output;
    }

} // namespace ZenoPCB

#endif  // Plan 06-03 D-03 — defined(ESP32)
