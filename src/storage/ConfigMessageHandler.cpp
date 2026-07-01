/**
 * @file ConfigMessageHandler.cpp
 * @brief Implementation of MQTT Message Handler for Connection Configuration
 *
 * @author ZenoPCB Development Team
 * @version 1.0.0
 */
// TU guard for ZENOPCB_MICRO_BASIC profile.
#if !defined(ZENOPCB_DISABLE_STORAGE)

#include "ConfigMessageHandler.h"

#include "DataMonitorConfig.h"
#include "../core/ZenoPCBDebug.h"

// : Modbus IO is ESP32-only. ConfigMessageHandler management methods
// (CRUD on LittleFS for ConnectionConfig) remain linkable on ESP8266; the Modbus
// connection-manager + polling-engine calls are guarded out per call site.
#if defined(ESP32)
#include "../modbus/ModbusConnectionManager.h"
#include "../modbus/RegisterPollingEngine.h"
#endif

// Debug logging - BT kim tra
#ifndef ZENOPCB_DEBUG_STORAGE
#define ZENOPCB_DEBUG_STORAGE 1 // SET TO 1 FOR DEBUG
#endif

// route through the portable ZENOPCB_PRINTF shim so
// platforms without ZENOPCB_PRINTF (Renesas UART on UNO R4, STM32duino
// HardwareSerial) compile cleanly. ESP32 / ESP8266 path expands to the
// original `ZENOPCB_PRINTF(...)` byte-for-byte.
#if ZENOPCB_DEBUG_STORAGE
#define STORAGE_LOG(fmt, ...) ZENOPCB_PRINTF("[CC-Handler]" fmt "\n", ##__VA_ARGS__)
#define STORAGE_LOG_RAW(fmt, ...) ZENOPCB_PRINTF(fmt, ##__VA_ARGS__)
#else
#define STORAGE_LOG(fmt, ...)
#define STORAGE_LOG_RAW(fmt, ...)
#endif

namespace ZenoPCB
{

    // ============================================
    // Singleton Instance
    // ============================================

    ConfigMessageHandler &ConfigMessageHandler::getInstance()
    {
        static ConfigMessageHandler instance;
        return instance;
    }

    // ============================================
    // Initialization
    // ============================================

    bool ConfigMessageHandler::begin()
    {
        if (_initialized)
        {
            STORAGE_LOG("Already initialized");
            return true;
        }

        STORAGE_LOG_RAW("\n");
        STORAGE_LOG_RAW("ConfigMessageHandler - Initializing...\n");
        STORAGE_LOG_RAW("\n");

        // Initialize LittleFSManager first
        if (!LittleFSManager::initialize())
        {
            STORAGE_LOG("Failed to initialize LittleFSManager");
            return false;
        }

        _initialized = true;
        STORAGE_LOG("ConfigMessageHandler initialized successfully");
        return true;
    }

    // ============================================
    // Message Handling
    // ============================================

    HandleResult ConfigMessageHandler::handleMessage(const String &topic, const uint8_t *payload, size_t length)
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

    HandleResult ConfigMessageHandler::handleMessage(const String &topic, const String &payload)
    {
        uint32_t startTime = millis();
        _messagesProcessed++;
        STORAGE_LOG_RAW("\n");
        STORAGE_LOG_RAW("Connection Config Message Received\n");
        STORAGE_LOG_RAW("\n");
        STORAGE_LOG_RAW("Topic: %s\n", maskTopic(topic).c_str());
        STORAGE_LOG_RAW("Payload (%d bytes):\n", payload.length());

        // Pretty print payload (limit to 500 chars for readability)
        String truncPayload = payload.substring(0, 500);
        STORAGE_LOG_RAW("%s\n", truncPayload.c_str());
        if (payload.length() > 500)
        {
            STORAGE_LOG_RAW("... (truncated)\n");
        }
        STORAGE_LOG_RAW("\n");

        // Check initialization
        if (!_initialized)
        {
            _errorsCount++;
            _notifyError("Handler not initialized", payload);
            return _errorResult("Handler not initialized");
        }

        // Parse payload
        ConfigMessage message;
        String parseError;
        if (!_parsePayload(payload, message, parseError))
        {
            _errorsCount++;
            STORAGE_LOG_RAW("%s\n", String("Parse Error:" + parseError).c_str());
            _notifyError(parseError, payload);
            return _errorResult(parseError);
        }

        // Log parsed action
        ZENOPCB_PRINTF("Parsed: type=%s, action=%s\n",
                      message.type.c_str(),
                      actionToString(message.action));

        // Dispatch based on action
        HandleResult result;
        switch (message.action)
        {
        case ConfigAction::CREATE:
            result = _handleCreate(message);
            break;
        case ConfigAction::UPDATE:
            result = _handleUpdate(message);
            break;
        case ConfigAction::DELETE:
            result = _handleDelete(message);
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
            ZENOPCB_PRINTF("Handler failed: %s (took %d ms)\n",
                          result.errorMessage.c_str(), processingTime);
        }
        else
        {
            ZENOPCB_PRINTF("Handler success: shortId=%s (took %d ms)\n",
                          result.shortId.c_str(), processingTime);
        }

        return result;
    }

    // ============================================
    // Parse Payload
    // ============================================

    bool ConfigMessageHandler::_parsePayload(const String &payload, ConfigMessage &outMessage, String &error)
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
        if (type != "cc")
        {
            error = "Invalid type, expected 'cc' got '" + type + "'";
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

        outMessage.action = ConfigMessage::actionFromChar(actionStr[0]);
        if (outMessage.action == ConfigAction::UNKNOWN)
        {
            error = "Unknown action:" + actionStr;
            return false;
        }

        outMessage.type = type;

        // Parse data object and serialize to string to avoid dangling reference
        // (JsonDocument doc will be destroyed after this function returns)
        if (doc["d"].is<JsonObject>())
        {
            // Serialize data object to string
            // IMPORTANT: Use default options to preserve all fields including "en":0
            serializeJson(doc["d"], outMessage.dataJson);

            // Debug: Log serialized data to verify "en" field is preserved
            ZENOPCB_PRINTF("[CC-Handler] Serialized data: %s\n", outMessage.dataJson.c_str());
        }
        else if (outMessage.action != ConfigAction::DELETE)
        {
            error = "Missing 'd' (data) field for" + actionStr + "action";
            return false;
        }

        STORAGE_LOG("Parsed message: type=%s, action=%c",
                    outMessage.type.c_str(), static_cast<char>(outMessage.action));

        return true;
    }

    // ============================================
    // Validate Config Data
    // ============================================

    bool ConfigMessageHandler::_validateConfigData(const JsonObject &data, String &error)
    {
        // Check shortId (required) - support both "id" and "shortId" field names
        String shortId = data["id"] | data["shortId"] | "";
        if (shortId.length() == 0)
        {
            error = "Missing or empty 'id' or 'shortId'";
            return false;
        }

        if (shortId.length() > 8)
        {
            error = "shortId too long (max 8 chars)";
            return false;
        }

        // Note: "nm" (name) field removed - MQTT khng gi na

        // Check protocol if present - support "pr", "protocol"
        // Valid values: "S", "T", "serial", "tcp"
        String protocol = data["pr"] | data["protocol"] | "";
        if (protocol.length() > 0)
        {
            if (protocol != "S" && protocol != "T" && protocol != "serial" && protocol != "tcp")
            {
                error = "Invalid protocol:" + protocol + "(must be 'S', 'T', 'serial' or 'tcp')";
                return false;
            }
        }

        // Validate numeric ranges - support both short and long field names
        int baudRate = data["br"] | data["baudRate"] | 0;
        if (baudRate != 0 && (baudRate < 300 || baudRate > 4000000))
        {
            error = "Invalid baudRate (300-4000000)";
            return false;
        }

        int port = data["p"] | data["port"] | 0;
        if (port != 0 && (port < 1 || port > 65535))
        {
            error = "Invalid port (1-65535)";
            return false;
        }

        if (data["timeout"].is<int>())
        {
            int timeout = data["timeout"];
            if (timeout < 0 || timeout > 300000)
            {
                error = "Invalid timeout (0-300000ms)";
                return false;
            }
        }

        STORAGE_LOG("Config data validated for shortId: %s", shortId.c_str());
        return true;
    }

    // ============================================
    // Handle CREATE
    // ============================================

    HandleResult ConfigMessageHandler::_handleCreate(const ConfigMessage &message)
    {
        STORAGE_LOG_RAW("\n Processing CREATE Connection Config...\n");

        // Parse dataJson string back to JsonObject
        JsonDocument doc;
        DeserializationError jsonError = deserializeJson(doc, message.dataJson);
        if (jsonError)
        {
            return _errorResult("Failed to parse data JSON", ConfigAction::CREATE);
        }
        JsonObject data = doc.as<JsonObject>();

        // Validate data
        String validationError;
        if (!_validateConfigData(data, validationError))
        {
            return _errorResult(validationError, ConfigAction::CREATE);
        }

        // Convert JsonObject to ConnectionConfig
        ConnectionConfig config;
        if (!config.fromJson(data))
        {
            return _errorResult("Failed to parse config data", ConfigAction::CREATE);
        }

        // Print debug info for the parsed config
        STORAGE_LOG_RAW("Parsed Connection Config:\n");
        config.printDebug();

        // Check if already exists
        if (LittleFSManager::configExists(String(config.shortId)))
        {
            ZENOPCB_PRINTF("Config already exists: %s - will skip create\n", config.shortId);
            return _errorResult(String("Config already exists:") + config.shortId, ConfigAction::CREATE);
        }

        // Create in storage
        if (!LittleFSManager::createConfig(config))
        {
            String lastError = LittleFSManager::getLastError();
            return _errorResult(String("Storage error:") + lastError, ConfigAction::CREATE);
        }

        ZENOPCB_PRINTF("Config saved to LittleFS: /connections/%s.json\n", config.shortId);

        // Add to Modbus Connection Manager
#if defined(ESP32)
        STORAGE_LOG_RAW("Adding connection to Modbus Manager...\n");
        if (ModbusConnectionManager::getInstance().addConnection(config))
        {
            ZENOPCB_PRINTF("Modbus connection added: %s\n", config.shortId);
        }
        else
        {
            ZENOPCB_PRINTF("Failed to add Modbus connection: %s\n", config.shortId);
        }
#else
        STORAGE_LOG("Modbus not available on this platform connection %s persisted only",
                    config.shortId);
#endif

        // Notify callback
        if (_onCreated)
        {
            _onCreated(config);
        }

        return _successResult(String(config.shortId), ConfigAction::CREATE, 0);
    }

    // ============================================
    // Handle UPDATE
    // ============================================

    HandleResult ConfigMessageHandler::_handleUpdate(const ConfigMessage &message)
    {
        STORAGE_LOG_RAW("\n Processing UPDATE Connection Config...\n");

        // Parse dataJson string back to JsonObject
        JsonDocument doc;
        DeserializationError jsonError = deserializeJson(doc, message.dataJson);
        if (jsonError)
        {
            ZENOPCB_PRINTF("JSON deserialize error: %s\n", jsonError.c_str());
            return _errorResult("Failed to parse data JSON", ConfigAction::UPDATE);
        }

        JsonObject data = doc.as<JsonObject>();

        // Extract shortId from data
        String shortId = data["id"] | data["shortId"] | "";
        if (shortId.length() == 0)
        {
            return _errorResult("Missing 'id' or 'shortId' in update data", ConfigAction::UPDATE);
        }

        // Validate data
        String validationError;
        if (!_validateConfigData(data, validationError))
        {
            return _errorResult(validationError, ConfigAction::UPDATE);
        }

        // Convert JsonObject to ConnectionConfig
        ConnectionConfig config;
        if (!config.fromJson(data))
        {
            return _errorResult("Failed to parse config data", ConfigAction::UPDATE);
        }

        // Print debug info for the parsed config
        STORAGE_LOG_RAW("Updated Connection Config:\n");
        config.printDebug();

        // UPSERT: If config doesn't exist create it (backend may send "u" for new configs)
        bool isNewConfig = !LittleFSManager::configExists(shortId);

        if (isNewConfig)
        {
            STORAGE_LOG("Config %s not found UPSERT: creating new", shortId.c_str());

            if (!LittleFSManager::createConfig(config))
            {
                String lastError = LittleFSManager::getLastError();
                return _errorResult(String("Storage create error:") + lastError, ConfigAction::UPDATE);
            }

            ZENOPCB_PRINTF("[UPSERT] Config created in LittleFS: /connections/%s.json\n", config.shortId);

            // Add to Modbus Connection Manager
#if defined(ESP32)
            if (config.enabled)
            {
                if (ModbusConnectionManager::getInstance().addConnection(config))
                {
                    ZENOPCB_PRINTF("[UPSERT] Modbus connection added: %s\n", config.shortId);
                }
                else
                {
                    ZENOPCB_PRINTF("[UPSERT] Failed to add Modbus connection: %s\n", config.shortId);
                }
            }

            // Load any saved monitors that reference this new connection
            if (config.enabled)
            {
                std::vector<String> monitorIds = LittleFSManager::listAllDataMonitors();
                size_t loadedCount = 0;
                for (const String &mqttKey : monitorIds)
                {
                    DataMonitorConfig monitor;
                    if (LittleFSManager::readDataMonitor(mqttKey, monitor))
                    {
                        if (String(monitor.connectionId) == shortId && monitor.enabled)
                        {
                            if (RegisterPollingEngine::getInstance().addRegister(monitor))
                            {
                                ZENOPCB_PRINTF("Loaded saved register: %s\n", monitor.mqttKey);
                                loadedCount++;
                            }
                        }
                    }
                }
                if (loadedCount > 0)
                {
                    ZENOPCB_PRINTF("[UPSERT] Loaded %d saved registers for new connection: %s\n", loadedCount, shortId.c_str());
                }
            }
#else
            STORAGE_LOG("[UPSERT] Modbus not available connection %s persisted only", config.shortId);
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
            if (!LittleFSManager::updateConfig(config))
            {
                String lastError = LittleFSManager::getLastError();
                return _errorResult(String("Storage error:") + lastError, ConfigAction::UPDATE);
            }

            ZENOPCB_PRINTF("Config updated in LittleFS: /connections/%s.json\n", config.shortId);

            // Update Modbus connection based on enabled status
#if defined(ESP32)
            if (config.enabled)
            {
                // Connection enabled - remove old and add new
                ModbusConnectionManager::getInstance().removeConnection(shortId.c_str());
                if (ModbusConnectionManager::getInstance().addConnection(config))
                {
                    ZENOPCB_PRINTF("Modbus connection updated: %s (enabled)\n", config.shortId);
                }
                else
                {
                    ZENOPCB_PRINTF("Failed to update Modbus connection: %s\n", config.shortId);
                }

                // Load all registers of this connection from LittleFS (if not already loaded)
                ZENOPCB_PRINTF("Loading registers for connection: %s\n", shortId.c_str());
                std::vector<String> monitorIds = LittleFSManager::listAllDataMonitors();
                size_t loadedCount = 0;

                for (const String &mqttKey : monitorIds)
                {
                    DataMonitorConfig monitor;
                    if (LittleFSManager::readDataMonitor(mqttKey, monitor))
                    {
                        // Check if this monitor belongs to this connection
                        if (String(monitor.connectionId) == shortId && monitor.enabled)
                        {
                            // Try to add (will update if already exists)
                            if (RegisterPollingEngine::getInstance().addRegister(monitor))
                            {
                                ZENOPCB_PRINTF("Loaded register: %s\n", monitor.mqttKey);
                                loadedCount++;
                            }
                            else
                            {
                                ZENOPCB_PRINTF("Register already exists or failed: %s\n", monitor.mqttKey);
                            }
                        }
                    }
                }

                ZENOPCB_PRINTF("Loaded %d registers for connection: %s\n", loadedCount, shortId.c_str());

                // Enable all registers of this connection
                size_t enabledCount = RegisterPollingEngine::getInstance().enableRegistersByConnection(shortId);
                ZENOPCB_PRINTF("Enabled %d registers for connection: %s\n", enabledCount, config.shortId);
            }
            else
            {
                // Connection disabled - just remove it (don't add back)
                if (ModbusConnectionManager::getInstance().removeConnection(shortId.c_str()))
                {
                    ZENOPCB_PRINTF("Modbus connection removed (disabled): %s\n", config.shortId);
                }
                else
                {
                    ZENOPCB_PRINTF("Modbus connection was not active: %s\n", config.shortId);
                }

                // Disable all registers of this connection
                size_t disabledCount = RegisterPollingEngine::getInstance().disableRegistersByConnection(shortId);
                ZENOPCB_PRINTF("Disabled %d registers for connection: %s\n", disabledCount, config.shortId);
            }
#else
            STORAGE_LOG("Modbus not available config %s update persisted only (enabled=%d)",
                        config.shortId, config.enabled);
#endif

            // Notify updated callback
            if (_onUpdated)
            {
                _onUpdated(config);
            }
        }

        return _successResult(shortId, ConfigAction::UPDATE, 0);
    }

    // ============================================
    // Handle DELETE
    // ============================================

    HandleResult ConfigMessageHandler::_handleDelete(const ConfigMessage &message)
    {
        STORAGE_LOG_RAW("\n Processing DELETE Connection Config...\n");

        // Get shortId - parse from dataJson if available
        String shortId;

        if (message.dataJson.length() > 0)
        {
            JsonDocument doc;
            DeserializationError jsonError = deserializeJson(doc, message.dataJson);
            if (!jsonError)
            {
                // Support both "id" and "shortId" field names
                shortId = doc["id"] | doc["shortId"] | "";
            }
        }

        ZENOPCB_PRINTF("Target shortId: %s\n", shortId.c_str());

        if (shortId.length() == 0)
        {
            STORAGE_LOG_RAW("Missing shortId for delete\n");
            return _errorResult("Missing shortId for delete", ConfigAction::DELETE);
        }

        // Check if exists
        if (!LittleFSManager::configExists(shortId))
        {
            ZENOPCB_PRINTF("Config not found for delete: %s\n", shortId.c_str());
            return _errorResult(String("Config not found:") + shortId, ConfigAction::DELETE);
        }

        // Delete from storage
        if (!LittleFSManager::deleteConfig(shortId))
        {
            String lastError = LittleFSManager::getLastError();
            return _errorResult("Storage error:" + lastError, ConfigAction::DELETE);
        }

        ZENOPCB_PRINTF("Config deleted from LittleFS: /connections/%s.json\n", shortId.c_str());

        // Remove from Modbus Connection Manager
#if defined(ESP32)
        STORAGE_LOG_RAW("Removing connection from Modbus Manager...\n");
        if (ModbusConnectionManager::getInstance().removeConnection(shortId.c_str()))
        {
            ZENOPCB_PRINTF("Modbus connection removed: %s\n", shortId.c_str());
        }
        else
        {
            ZENOPCB_PRINTF("Modbus connection not found (already removed?): %s\n", shortId.c_str());
        }
#else
        STORAGE_LOG("Modbus not available connection %s removed only from storage", shortId.c_str());
#endif

        // Notify callback
        if (_onDeleted)
        {
            _onDeleted(shortId);
        }

        return _successResult(shortId, ConfigAction::DELETE, 0);
    }

    // ============================================
    // Helper Methods
    // ============================================

    HandleResult ConfigMessageHandler::_errorResult(const String &error, ConfigAction action)
    {
        HandleResult result;
        result.success = false;
        result.errorMessage = error;
        result.action = action;
        result.processingMs = 0;
        return result;
    }

    HandleResult ConfigMessageHandler::_successResult(const String &shortId, ConfigAction action, uint32_t processingMs)
    {
        HandleResult result;
        result.success = true;
        result.shortId = shortId;
        result.action = action;
        result.processingMs = processingMs;
        return result;
    }

    void ConfigMessageHandler::_notifyError(const String &error, const String &payload)
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

    bool ConfigMessageHandler::getConfig(const String &shortId, ConnectionConfig &outConfig)
    {
        if (!_initialized)
        {
            return false;
        }
        return LittleFSManager::readConfig(shortId, outConfig);
    }

    std::vector<String> ConfigMessageHandler::getAllConfigIds()
    {
        if (!_initialized)
        {
            return std::vector<String>();
        }
        return LittleFSManager::listAllConfigs();
    }

    ConnectionMetadata ConfigMessageHandler::getMetadata()
    {
        ConnectionMetadata meta;
        if (_initialized)
        {
            LittleFSManager::readMetadata(meta);
        }
        return meta;
    }

    bool ConfigMessageHandler::configExists(const String &shortId)
    {
        if (!_initialized)
        {
            return false;
        }
        return LittleFSManager::configExists(shortId);
    }

    void ConfigMessageHandler::resetStats()
    {
        _messagesProcessed = 0;
        _errorsCount = 0;
        _totalProcessingTime = 0;
    }

} // namespace ZenoPCB

#endif // !ZENOPCB_DISABLE_STORAGE
