/**
 * @file DataMonitorMessageHandler.cpp
 * @brief Implementation of MQTT Message Handler for Data Monitor Configuration
 *
 * @author ZenoPCB Development Team
 * @version 1.0.0
 */
// TU guard for ZENOPCB_MICRO_BASIC profile.
#if !defined(ZENOPCB_DISABLE_STORAGE)

#include "DataMonitorMessageHandler.h"

#include "../core/ZenoPCBDebug.h"

// : Modbus IO is ESP32-only. Headers are conditionally included so the
// management-layer methods of DataMonitorMessageHandler still link on ESP8266 while
// the Modbus polling/buffer call sites are guarded out.
#if defined(ESP32)
#include "../modbus/RegisterPollingEngine.h"
#include "../modbus/ModbusDataBuffer.h"
#endif

// Debug logging
#ifndef ZENOPCB_DEBUG_STORAGE
#define ZENOPCB_DEBUG_STORAGE 1 // SET TO 1 FOR DEBUG
#endif

// route through ZENOPCB_PRINTF for portability.
#if ZENOPCB_DEBUG_STORAGE
#define STORAGE_LOG(fmt, ...) ZENOPCB_PRINTF("[DataMonitor]" fmt "\n", ##__VA_ARGS__)
#else
#define STORAGE_LOG(fmt, ...)
#endif

namespace ZenoPCB
{

    // ============================================
    // Singleton Instance
    // ============================================

    DataMonitorMessageHandler &DataMonitorMessageHandler::getInstance()
    {
        static DataMonitorMessageHandler instance;
        return instance;
    }

    // ============================================
    // Initialization
    // ============================================

    bool DataMonitorMessageHandler::begin()
    {
        if (_initialized)
        {
            STORAGE_LOG("Already initialized");
            return true;
        }

        STORAGE_LOG("Initializing DataMonitorMessageHandler...");

        // Initialize LittleFSManager first (if not already)
        if (!LittleFSManager::initialize())
        {
            STORAGE_LOG("Failed to initialize LittleFSManager");
            return false;
        }

        _initialized = true;
        STORAGE_LOG("DataMonitorMessageHandler initialized successfully");
        return true;
    }

    // ============================================
    // Message Handling
    // ============================================

    DataMonitorHandleResult DataMonitorMessageHandler::handleMessage(const String &topic, const uint8_t *payload, size_t length)
    {
        // Convert bytes to string
        String payloadStr;
        payloadStr.reserve(length + 1);
        for (size_t i = 0; i < length; i++)
        {
            payloadStr += (char)payload[i];
        }
        return handleMessage(topic, payloadStr);
    }

    DataMonitorHandleResult DataMonitorMessageHandler::handleMessage(const String &topic, const String &payload)
    {
        uint32_t startTime = millis();
        _messagesProcessed++;

        STORAGE_LOG("Handling message on topic: %s", maskTopic(topic).c_str());
        STORAGE_LOG("Payload (%d bytes): %s", payload.length(), payload.c_str());

        // Check initialization
        if (!_initialized)
        {
            _errorsCount++;
            _notifyError("Handler not initialized", payload);
            return _errorResult("Handler not initialized");
        }

        // Parse payload
        DataMonitorMessage message;
        String parseError;
        if (!_parsePayload(payload, message, parseError))
        {
            _errorsCount++;
            _notifyError(parseError, payload);
            return _errorResult(parseError);
        }

        // Dispatch based on action
        DataMonitorHandleResult result;
        switch (message.action)
        {
        case DataMonitorAction::CREATE:
            result = _handleCreate(message);
            break;
        case DataMonitorAction::UPDATE:
            result = _handleUpdate(message);
            break;
        case DataMonitorAction::DELETE:
            result = _handleDelete(message);
            break;
        case DataMonitorAction::TOGGLE:
            result = _handleToggle(message);
            break;
        default:
            _errorsCount++;
            _notifyError("Unknown action", payload);
            result = _errorResult("Unknown action");
            break;
        }

        // Calculate processing time
        uint32_t processingTime = millis() - startTime;
        result.processingMs = processingTime;
        _totalProcessingTime += processingTime;

        if (!result.success)
        {
            _errorsCount++;
        }

        STORAGE_LOG("Message handled in %dms, success: %s",
                    processingTime, result.success ? "true" : "false");

        return result;
    }

    // ============================================
    // Parse Payload
    // ============================================

    bool DataMonitorMessageHandler::_parsePayload(const String &payload, DataMonitorMessage &outMessage, String &error)
    {
        // Parse JSON
        JsonDocument doc;
        DeserializationError jsonError = deserializeJson(doc, payload);

        if (jsonError)
        {
            error = "JSON parse error:" + String(jsonError.c_str());
            STORAGE_LOG("Parse error: %s", error.c_str());
            return false;
        }

        // Validate required fields
        if (!doc["t"].is<const char *>())
        {
            error = "Missing 't' (type) field";
            return false;
        }

        String type = doc["t"].as<String>();
        if (type != "dm")
        {
            error = "Invalid type, expected 'dm' got '" + type + "'";
            return false;
        }

        if (!doc["a"].is<const char *>())
        {
            error = "Missing 'a' (action) field";
            return false;
        }

        // Parse action
        String actionStr = doc["a"].as<String>();
        if (actionStr.length() != 1)
        {
            error = "Invalid action format";
            return false;
        }

        outMessage.action = DataMonitorMessage::actionFromChar(actionStr[0]);
        if (outMessage.action == DataMonitorAction::UNKNOWN)
        {
            error = "Unknown action:" + actionStr;
            return false;
        }

        outMessage.type = type;

        // Parse data object and serialize to string to avoid dangling reference
        // (JsonDocument doc will be destroyed after this function returns)
        if (doc["d"].is<JsonObject>())
        {
            // Normal format: d is an object { "id": "123456", ... }
            serializeJson(doc["d"], outMessage.dataJson);
        }
        else if (outMessage.action == DataMonitorAction::DELETE || outMessage.action == DataMonitorAction::TOGGLE)
        {
            // For DELETE/TOGGLE, also accept:
            // 1. d is a string: {"t":"dm","a":"d","d":"123456"}
            // 2. id/key at top-level: {"t":"dm","a":"d","id":"123456"}
            String resolvedKey;

            if (doc["d"].is<const char *>())
            {
                resolvedKey = doc["d"].as<String>();
            }
            else if (doc["id"].is<const char *>())
            {
                resolvedKey = doc["id"].as<String>();
            }
            else if (doc["mqttKey"].is<const char *>())
            {
                resolvedKey = doc["mqttKey"].as<String>();
            }
            else if (doc["key"].is<const char *>())
            {
                resolvedKey = doc["key"].as<String>();
            }

            if (resolvedKey.length() > 0)
            {
                // Build normalized dataJson so _handleDelete/_handleToggle can read it uniformly
                outMessage.dataJson = "{\"id\":\""+ resolvedKey +"\"}";
                STORAGE_LOG("DELETE/TOGGLE: resolved key from alternate field: %s", resolvedKey.c_str());
            }
            // else: dataJson stays empty _handleDelete will return "Missing mqttKey" error
        }
        else
        {
            error = "Missing 'd' (data) field for" + actionStr + "action";
            return false;
        }

        STORAGE_LOG("Parsed message: type=%s, action=%c",
                    outMessage.type.c_str(), static_cast<char>(outMessage.action));

        return true;
    }

    // ============================================
    // Validate Monitor Data
    // ============================================

    bool DataMonitorMessageHandler::_validateMonitorData(const JsonObject &data, String &error)
    {
        // Check mqttKey (required) - support both "id" and "mqttKey"
        String mqttKey = data["id"] | data["mqttKey"] | "";
        if (mqttKey.length() == 0)
        {
            error = "Missing or empty 'id' or 'mqttKey'";
            return false;
        }

        // mqttKey must be 6 digits (000000-999999)
        if (mqttKey.length() != 6)
        {
            error = "mqttKey must be 6 digits (got" + String(mqttKey.length()) + "chars)";
            return false;
        }

        // Check all chars are digits
        for (size_t i = 0; i < mqttKey.length(); i++)
        {
            if (!isDigit(mqttKey[i]))
            {
                error = "mqttKey must contain only digits";
                return false;
            }
        }

        // Check register type (required for create) - "rt" field
        // Valid values: H/HOLDING, I/INPUT, C/COIL, D/DISCRETE
        String regType = data["rt"] | "";
        if (regType.length() > 0)
        {
            if (regType != "H" && regType != "I" && regType != "C" && regType != "D" &&
                regType != "HOLDING" && regType != "INPUT" && regType != "COIL" && regType != "DISCRETE" &&
                regType != "h" && regType != "i" && regType != "c" && regType != "d" &&
                regType != "holding" && regType != "input" && regType != "coil" && regType != "discrete")
            {
                error = "Invalid register type:" + regType + "(must be H/I/C/D or HOLDING/INPUT/COIL/DISCRETE)";
                return false;
            }
        }

        // Check data type (required for create) - "dt" field
        // Valid values (v2.2): i16/u16/i32/u32/f32/i64/u64/f64/bool and legacy formats
        String dataType = data["dt"] | "";
        if (dataType.length() > 0)
        {
            // v2.2 compact format: i16, u16, i32, u32, f32, i64, u64, f64, bool
            // Legacy format: s16/signed_int16, u16/unsigned_int16, s32/signed_int32, u32/unsigned_int32, f32/float32/float_ieee754, bool/boolean/bool_coil
            // v2.2 64-bit: i64/signed_int64, u64/unsigned_int64, f64/float64/double
            if (dataType != "i16" && dataType != "u16" && dataType != "i32" && dataType != "u32" &&
                dataType != "f32" && dataType != "i64" && dataType != "u64" && dataType != "f64" &&
                dataType != "bool" &&
                dataType != "s16" && dataType != "s32" && // Legacy compact
                dataType != "signed_int16" && dataType != "unsigned_int16" &&
                dataType != "signed_int32" && dataType != "unsigned_int32" &&
                dataType != "signed_int64" && dataType != "unsigned_int64" &&
                dataType != "float32" && dataType != "float_ieee754" &&
                dataType != "float64" && dataType != "double" &&
                dataType != "boolean" && dataType != "bool_coil")
            {
                error = "Invalid data type:" + dataType + "(valid: i16/u16/i32/u32/f32/i64/u64/f64/bool)";
                return false;
            }
        }

        // Validate numeric ranges
        if (data["ad"].is<int>())
        {
            int address = data["ad"];
            if (address < 0 || address > 65535)
            {
                error = "Invalid address (0-65535)";
                return false;
            }
        }

        if (data["sid"].is<int>())
        {
            int slaveId = data["sid"];
            if (slaveId < 1 || slaveId > 247)
            {
                error = "Invalid slaveId (1-247)";
                return false;
            }
        }

        // Validate connectionId (cn) - should be 4 chars if present
        String connId = data["cn"] | "";
        if (connId.length() > 0 && connId.length() > 4)
        {
            error = "connectionId too long (max 4 chars)";
            return false;
        }

        STORAGE_LOG("Monitor data validated for mqttKey: %s", mqttKey.c_str());
        return true;
    }

    // ============================================
    // Handle CREATE
    // ============================================

    DataMonitorHandleResult DataMonitorMessageHandler::_handleCreate(const DataMonitorMessage &message)
    {
        STORAGE_LOG("Handling CREATE action");

        // Parse dataJson string back to JsonObject
        JsonDocument doc;
        DeserializationError jsonError = deserializeJson(doc, message.dataJson);
        if (jsonError)
        {
            return _errorResult("Failed to parse data JSON", DataMonitorAction::CREATE);
        }
        JsonObject data = doc.as<JsonObject>();

        // Validate data
        String validationError;
        if (!_validateMonitorData(data, validationError))
        {
            return _errorResult(validationError, DataMonitorAction::CREATE);
        }

        // Convert JsonObject to DataMonitorConfig
        DataMonitorConfig config;
        if (!config.fromJson(data))
        {
            return _errorResult("Failed to parse monitor config data", DataMonitorAction::CREATE);
        }

        // Check if already exists
        if (LittleFSManager::dataMonitorExists(String(config.mqttKey)))
        {
            return _errorResult(String("Monitor already exists:") + config.mqttKey, DataMonitorAction::CREATE);
        }

        // Create in storage
        if (!LittleFSManager::createDataMonitor(config))
        {
            String lastError = LittleFSManager::getLastError();
            return _errorResult(String("Storage error:") + lastError, DataMonitorAction::CREATE);
        }

        STORAGE_LOG("Monitor created: %s", config.mqttKey);

        // Add to RegisterPollingEngine and DataBuffer
#if defined(ESP32)
        ZENO_LOG_RAW("Adding register to Polling Engine...\n");
        if (RegisterPollingEngine::getInstance().addRegister(config))
        {
            ZENOPCB_PRINTF("Register added for polling: %s\n", config.mqttKey);
        }
        else
        {
            ZENOPCB_PRINTF("Failed to add register: %s\n", config.mqttKey);
        }

        // Add to data buffer
        ModbusDataBuffer::getInstance().addRegister(config);
        ZENOPCB_PRINTF("Register added to data buffer: %s\n", config.mqttKey);
#else
        // ESP8266: Modbus subsystem not available; monitor stays only in LittleFS storage
        STORAGE_LOG("Modbus polling not available on this platform monitor %s persisted only",
                    config.mqttKey);
#endif

        // Notify callback
        if (_onCreated)
        {
            _onCreated(config);
        }

        return _successResult(String(config.mqttKey), DataMonitorAction::CREATE, 0);
    }

    // ============================================
    // Handle UPDATE
    // ============================================

    DataMonitorHandleResult DataMonitorMessageHandler::_handleUpdate(const DataMonitorMessage &message)
    {
        STORAGE_LOG("Handling UPDATE action");

        // Parse dataJson string back to JsonObject
        JsonDocument doc;
        DeserializationError jsonError = deserializeJson(doc, message.dataJson);
        if (jsonError)
        {
            return _errorResult("Failed to parse data JSON", DataMonitorAction::UPDATE);
        }
        JsonObject data = doc.as<JsonObject>();

        // Validate data
        String validationError;
        if (!_validateMonitorData(data, validationError))
        {
            return _errorResult(validationError, DataMonitorAction::UPDATE);
        }

        // Get mqttKey from data
        String mqttKey = data["id"] | data["mqttKey"] | "";

        // Convert JsonObject to DataMonitorConfig
        DataMonitorConfig config;
        if (!config.fromJson(data))
        {
            return _errorResult("Failed to parse monitor config data", DataMonitorAction::UPDATE);
        }

        // UPSERT: If monitor doesn't exist create it (backend may send "u" for new monitors)
        bool isNewMonitor = !LittleFSManager::dataMonitorExists(mqttKey);

        if (isNewMonitor)
        {
            STORAGE_LOG("Monitor %s not found UPSERT: creating new", mqttKey.c_str());

            if (!LittleFSManager::createDataMonitor(config))
            {
                String lastError = LittleFSManager::getLastError();
                return _errorResult(String("Storage create error:") + lastError, DataMonitorAction::UPDATE);
            }

            // Add to RegisterPollingEngine
#if defined(ESP32)
            if (RegisterPollingEngine::getInstance().addRegister(config))
            {
                ZENOPCB_PRINTF("[UPSERT] Register created & added: %s\n", config.mqttKey);
            }
            else
            {
                ZENOPCB_PRINTF("[UPSERT] Failed to add register: %s\n", config.mqttKey);
            }

            // Add to data buffer
            ModbusDataBuffer::getInstance().addRegister(config);
#else
            STORAGE_LOG("[UPSERT] Modbus polling not available on this platform %s persisted only",
                        config.mqttKey);
#endif

            // Notify created callback
            if (_onCreated)
            {
                _onCreated(config);
            }
        }
        else
        {
            // Normal update path
            if (!LittleFSManager::updateDataMonitor(config))
            {
                String lastError = LittleFSManager::getLastError();
                return _errorResult(String("Storage error:") + lastError, DataMonitorAction::UPDATE);
            }

            STORAGE_LOG("Monitor updated: %s", config.mqttKey);

            // Update in RegisterPollingEngine (uses updateRegister which handles remove+add)
#if defined(ESP32)
            if (RegisterPollingEngine::getInstance().updateRegister(config))
            {
                ZENOPCB_PRINTF("Register updated: %s (enabled=%d)\n", config.mqttKey, config.enabled);
            }
            else
            {
                ZENOPCB_PRINTF("Failed to update register: %s\n", config.mqttKey);
            }

            // Update config in data buffer (enabled flag, etc.)
            ModbusDataBuffer::getInstance().updateRegisterConfig(config);
#else
            STORAGE_LOG("Modbus polling not available monitor %s update persisted only",
                        config.mqttKey);
#endif

            // Notify updated callback
            if (_onUpdated)
            {
                _onUpdated(config);
            }
        }

        return _successResult(mqttKey, DataMonitorAction::UPDATE, 0);
    }

    // ============================================
    // Handle DELETE
    // ============================================

    DataMonitorHandleResult DataMonitorMessageHandler::_handleDelete(const DataMonitorMessage &message)
    {
        STORAGE_LOG("Handling DELETE action");

        // Get mqttKey - parse from dataJson if available
        String mqttKey;

        if (message.dataJson.length() > 0)
        {
            JsonDocument doc;
            DeserializationError jsonError = deserializeJson(doc, message.dataJson);
            if (!jsonError)
            {
                // Support both "id" and "mqttKey" field names
                mqttKey = doc["id"] | doc["mqttKey"] | "";
            }
        }

        STORAGE_LOG("DELETE mqttKey: %s", mqttKey.c_str());

        if (mqttKey.length() == 0)
        {
            return _errorResult("Missing mqttKey for delete", DataMonitorAction::DELETE);
        }

        // Check if exists idempotent: already gone is still success
        if (!LittleFSManager::dataMonitorExists(mqttKey))
        {
            STORAGE_LOG("DELETE: monitor %s not found treating as already deleted (idempotent)", mqttKey.c_str());
            ZENOPCB_PRINTF("[DataMonitor] DELETE %s not found, ACK success (idempotent)\n", mqttKey.c_str());
            return _successResult(mqttKey, DataMonitorAction::DELETE, 0);
        }

        // Delete from storage
        if (!LittleFSManager::deleteDataMonitor(mqttKey))
        {
            String lastError = LittleFSManager::getLastError();
            return _errorResult("Storage error:" + lastError, DataMonitorAction::DELETE);
        }

        STORAGE_LOG("Monitor deleted: %s", mqttKey.c_str());

        // Remove from RegisterPollingEngine
#if defined(ESP32)
        ZENO_LOG_RAW("Removing register from Polling Engine...\n");
        if (RegisterPollingEngine::getInstance().removeRegister(mqttKey.c_str()))
        {
            ZENOPCB_PRINTF("Register removed: %s\n", mqttKey.c_str());
        }
        else
        {
            ZENOPCB_PRINTF("Register not found in polling engine: %s\n", mqttKey.c_str());
        }
#else
        STORAGE_LOG("Modbus polling not available %s removed only from storage", mqttKey.c_str());
#endif

        // Notify callback
        if (_onDeleted)
        {
            _onDeleted(mqttKey);
        }

        return _successResult(mqttKey, DataMonitorAction::DELETE, 0);
    }

    // ============================================
    // Handle TOGGLE
    // ============================================

    DataMonitorHandleResult DataMonitorMessageHandler::_handleToggle(const DataMonitorMessage &message)
    {
        STORAGE_LOG("Handling TOGGLE action");

        // Get mqttKey and enabled state - parse from dataJson
        String mqttKey;
        bool enabled = true; // Default to enabling if not specified

        if (message.dataJson.length() > 0)
        {
            JsonDocument doc;
            DeserializationError jsonError = deserializeJson(doc, message.dataJson);
            if (!jsonError)
            {
                // Support both "id" and "mqttKey" field names
                mqttKey = doc["id"] | doc["mqttKey"] | "";

                // Get enabled state - support "en" or "enabled"
                if (doc["en"].is<bool>())
                {
                    enabled = doc["en"].as<bool>();
                }
                else if (doc["enabled"].is<bool>())
                {
                    enabled = doc["enabled"].as<bool>();
                }
            }
        }

        STORAGE_LOG("TOGGLE mqttKey: %s, enabled: %s", mqttKey.c_str(), enabled ? "true" : "false");

        if (mqttKey.length() == 0)
        {
            return _errorResult("Missing mqttKey for toggle", DataMonitorAction::TOGGLE);
        }

        // Check if exists TOGGLE on non-existent key: ACK success (no-op)
        if (!LittleFSManager::dataMonitorExists(mqttKey))
        {
            STORAGE_LOG("TOGGLE: monitor %s not found no-op, ACK success", mqttKey.c_str());
            return _successResult(mqttKey, DataMonitorAction::TOGGLE, 0);
        }

        // Toggle in storage
        if (!LittleFSManager::toggleDataMonitor(mqttKey, enabled))
        {
            String lastError = LittleFSManager::getLastError();
            return _errorResult("Storage error:" + lastError, DataMonitorAction::TOGGLE);
        }

        STORAGE_LOG("Monitor toggled: %s -> %s", mqttKey.c_str(), enabled ? "enabled" : "disabled");

        // Enable/Disable in RegisterPollingEngine
#if defined(ESP32)
        ZENOPCB_PRINTF("%s register: %s\n", enabled ? "Enabling" : "Disabling", mqttKey.c_str());
        if (enabled)
        {
            RegisterPollingEngine::getInstance().enableRegister(mqttKey.c_str());
        }
        else
        {
            RegisterPollingEngine::getInstance().disableRegister(mqttKey.c_str());
        }
        ZENOPCB_PRINTF("Register %s: %s\n", enabled ? "enabled" : "disabled", mqttKey.c_str());
#else
        STORAGE_LOG("Modbus polling not available %s toggle persisted only (enabled=%d)",
                    mqttKey.c_str(), enabled);
#endif

        // Notify callback
        if (_onToggled)
        {
            _onToggled(mqttKey, enabled);
        }

        return _successResult(mqttKey, DataMonitorAction::TOGGLE, 0);
    }

    // ============================================
    // Helper Methods
    // ============================================

    DataMonitorHandleResult DataMonitorMessageHandler::_errorResult(const String &error, DataMonitorAction action)
    {
        DataMonitorHandleResult result;
        result.success = false;
        result.errorMessage = error;
        result.action = action;
        result.processingMs = 0;
        return result;
    }

    DataMonitorHandleResult DataMonitorMessageHandler::_successResult(const String &mqttKey, DataMonitorAction action, uint32_t processingMs)
    {
        DataMonitorHandleResult result;
        result.success = true;
        result.mqttKey = mqttKey;
        result.action = action;
        result.processingMs = processingMs;
        return result;
    }

    void DataMonitorMessageHandler::_notifyError(const String &error, const String &payload)
    {
        STORAGE_LOG("Error: %s", error.c_str());
        if (_onError)
        {
            _onError(error, payload);
        }
    }

    // ============================================
    // Query Methods
    // ============================================

    bool DataMonitorMessageHandler::getMonitor(const String &mqttKey, DataMonitorConfig &outConfig)
    {
        if (!_initialized)
        {
            return false;
        }
        return LittleFSManager::readDataMonitor(mqttKey, outConfig);
    }

    std::vector<String> DataMonitorMessageHandler::getAllMonitorIds()
    {
        if (!_initialized)
        {
            return std::vector<String>();
        }
        return LittleFSManager::listAllDataMonitors();
    }

    DataMonitorMetadata DataMonitorMessageHandler::getMetadata()
    {
        DataMonitorMetadata meta;
        if (_initialized)
        {
            LittleFSManager::readDataMonitorMetadata(meta);
        }
        return meta;
    }

    bool DataMonitorMessageHandler::monitorExists(const String &mqttKey)
    {
        if (!_initialized)
        {
            return false;
        }
        return LittleFSManager::dataMonitorExists(mqttKey);
    }

    std::vector<DataMonitorConfig> DataMonitorMessageHandler::getMonitorsByConnection(const String &connectionId)
    {
        std::vector<DataMonitorConfig> result;

        if (!_initialized)
        {
            return result;
        }

        // Get all monitor IDs
        std::vector<String> allIds = LittleFSManager::listAllDataMonitors();

        // Filter by connectionId
        for (const String &mqttKey : allIds)
        {
            DataMonitorConfig config;
            if (LittleFSManager::readDataMonitor(mqttKey, config))
            {
                if (String(config.connectionId) == connectionId)
                {
                    result.push_back(config);
                }
            }
        }

        STORAGE_LOG("Found %d monitors for connection: %s", result.size(), connectionId.c_str());
        return result;
    }

    void DataMonitorMessageHandler::resetStats()
    {
        _messagesProcessed = 0;
        _errorsCount = 0;
        _totalProcessingTime = 0;
    }

} // namespace ZenoPCB

#endif // !ZENOPCB_DISABLE_STORAGE
