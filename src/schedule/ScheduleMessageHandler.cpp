// TU guard for ZENOPCB_MICRO_BASIC profile.
#if !defined(ZENOPCB_DISABLE_SCHEDULE)

#include "ScheduleMessageHandler.h"
#include "../core/ZenoPCBDebug.h"

namespace ZenoPCB
{

    // ============================================
    // Singleton Instance
    // ============================================

    ScheduleMessageHandler &ScheduleMessageHandler::getInstance()
    {
        static ScheduleMessageHandler instance;
        return instance;
    }

    // ============================================
    // Main Message Handler
    // ============================================

    ScheduleHandleResult ScheduleMessageHandler::handleMessage(const String &topic, const String &payload)
    {
        uint32_t startTime = millis();
        ScheduleHandleResult result;

        ZENO_LOG("ScheduleHandler", "Received message on topic: %s", maskTopic(topic).c_str());
        ZENO_LOG("ScheduleHandler", "Payload: %s", payload.c_str());

        // Parse JSON payload
        JsonDocument doc;
        String error;

        if (!_parsePayload(payload, doc, error))
        {
            result.success = false;
            result.errorMessage = error;
            result.processingMs = millis() - startTime;

            ZENO_LOG("ScheduleHandler", "Parse failed: %s", error.c_str());

            if (_onErrorCallback)
            {
                _onErrorCallback(error, payload);
            }

            return result;
        }

        // Validate message structure
        if (!_validateMessageStructure(doc, error))
        {
            result.success = false;
            result.errorMessage = error;
            result.processingMs = millis() - startTime;

            ZENO_LOG("ScheduleHandler", "Validation failed: %s", error.c_str());

            if (_onErrorCallback)
            {
                _onErrorCallback(error, payload);
            }

            return result;
        }

        // Extract action
        const char *actionStr = doc["a"];
        ScheduleAction action = parseScheduleAction(actionStr[0]);
        result.action = action;

        // Dispatch to appropriate handler
        switch (action)
        {
        case ScheduleAction::CREATE:
            result = _handleCreate(doc["d"].as<JsonObject>());
            break;

        case ScheduleAction::UPDATE:
            result = _handleUpdate(doc["d"].as<JsonObject>());
            break;

        case ScheduleAction::DELETE:
            result = _handleDelete(doc["d"].as<JsonObject>());
            break;

        case ScheduleAction::SYNC:
            result = _handleSync(doc["d"]);
            break;
        }

        result.processingMs = millis() - startTime;

        ZENO_LOG("ScheduleHandler", "Message handled in %dms: action=%c, success=%d",
                 result.processingMs, (char)action, result.success);

        return result;
    }

    // ============================================
    // Validation Methods
    // ============================================

    bool ScheduleMessageHandler::validateScheduleId(const String &id)
    {
        // Must be exactly 4 characters
        if (id.length() != 4)
        {
            return false;
        }

        // Must be all digits
        for (char c : id)
        {
            if (!isDigit(c))
            {
                return false;
            }
        }

        // Must be between 0001 and 9999
        int value = id.toInt();
        return (value >= 1 && value <= 9999);
    }

    bool ScheduleMessageHandler::validateRegisterKey(const String &rid)
    {
        if (rid.length() == 0)
            return false;

        // Format 1: Z Key "Z0" to "Z99" (cloud native format)
        if (rid[0] == 'Z' || rid[0] == 'z')
        {
            // Must be Z followed by 1-2 digits: Z0, Z1, ..., Z99
            if (rid.length() < 2 || rid.length() > 3)
                return false;
            for (size_t i = 1; i < rid.length(); i++)
            {
                if (!isDigit(rid[i]))
                    return false;
            }
            return true;
        }

        // Format 2: 6-digit numeric Modbus register "000003"
        if (rid.length() != 6)
            return false;
        for (char c : rid)
        {
            if (!isDigit(c))
                return false;
        }
        return true;
    }

    bool ScheduleMessageHandler::validateTimeFormat(const String &timeStr)
    {
        int hour, minute, second;
        return TimeManager::parseTime(timeStr, hour, minute, second);
    }

    bool ScheduleMessageHandler::validateInterval(uint32_t intervalMs)
    {
        // Backend limits: 1 second to 1 hour (1000ms to 3600000ms)
        return (intervalMs >= 1000 && intervalMs <= 3600000);
    }

    // ============================================
    // Private Parsing & Validation
    // ============================================

    bool ScheduleMessageHandler::_parsePayload(const String &payload, JsonDocument &doc, String &error)
    {
        DeserializationError jsonError = deserializeJson(doc, payload);

        if (jsonError)
        {
            error = "JSON parse error:";
            error += jsonError.c_str();
            return false;
        }

        return true;
    }

    bool ScheduleMessageHandler::_validateMessageStructure(const JsonDocument &doc, String &error)
    {
        // Check required fields
        if (!doc.containsKey("t"))
        {
            error = "Missing required field: t (type)";
            return false;
        }

        if (!doc.containsKey("a"))
        {
            error = "Missing required field: a (action)";
            return false;
        }

        if (!doc.containsKey("d"))
        {
            error = "Missing required field: d (data)";
            return false;
        }

        // Validate type
        const char *type = doc["t"];
        if (strcmp(type, "s") != 0)
        {
            error = "Invalid message type: expected 's', got '";
            error += type;
            error += "'";
            return false;
        }

        // Validate action
        const char *actionStr = doc["a"];
        if (strlen(actionStr) != 1)
        {
            error = "Invalid action format";
            return false;
        }

        char actionChar = actionStr[0];
        if (actionChar != 'c' && actionChar != 'u' && actionChar != 'd' && actionChar != 's')
        {
            error = "Invalid action: expected c/u/d/s, got '";
            error += actionChar;
            error += "'";
            return false;
        }

        return true;
    }

    bool ScheduleMessageHandler::_validateScheduleData(const JsonObject &data, String &error)
    {
        // Validate Schedule ID
        if (!data.containsKey("id"))
        {
            error = "Missing schedule ID";
            return false;
        }

        const char *id = data["id"];
        if (!validateScheduleId(id))
        {
            error = "Invalid schedule ID format (must be 4-digit: 0001-9999)";
            return false;
        }

        // For delete action, only ID is required
        // Other validations are only for create/update

        return true;
    }

    bool ScheduleMessageHandler::_parseScheduleConfig(const JsonObject &data, ScheduleConfig &outConfig, String &error)
    {
        // Basic fields
        strlcpy(outConfig.id, data["id"] | "", sizeof(outConfig.id));
        strlcpy(outConfig.rid, data["rid"] | "", sizeof(outConfig.rid));
        outConfig.address = data["ra"] | 0;

        // Validate register key
        if (!validateRegisterKey(outConfig.rid))
        {
            error = "Invalid register key format (must be 6-digit numeric)";
            return false;
        }

        // Register type
        const char *rtStr = data["rt"];
        if (!rtStr || strlen(rtStr) != 1)
        {
            error = "Invalid register type";
            return false;
        }
        // Map 'H', 'C', 'I', 'D' to RegisterType enum
        switch (rtStr[0])
        {
        case 'H':
            outConfig.registerType = RegisterType::REG_HOLDING;
            break;
        case 'C':
            outConfig.registerType = RegisterType::REG_COIL;
            break;
        case 'I':
            outConfig.registerType = RegisterType::REG_INPUT;
            break;
        case 'D':
            outConfig.registerType = RegisterType::REG_DISCRETE;
            break;
        default:
            outConfig.registerType = RegisterType::REG_HOLDING;
        }

        // Schedule type
        const char *stStr = data["st"];
        if (!stStr || strlen(stStr) != 1)
        {
            error = "Invalid schedule type";
            return false;
        }
        outConfig.scheduleType = parseScheduleType(stStr[0]);

        // Schedule type specific fields
        if (outConfig.scheduleType == ScheduleType::RECURRING)
        {
            // Execute time
            if (!data.containsKey("et"))
            {
                error = "Missing execute time (et) for recurring schedule";
                return false;
            }

            const char *et = data["et"];
            if (!validateTimeFormat(et))
            {
                error = "Invalid time format (expected HH:mm:ss)";
                return false;
            }
            strlcpy(outConfig.executeTime, et, sizeof(outConfig.executeTime));

            // Repeat days
            if (!data.containsKey("rd"))
            {
                error = "Missing repeat days (rd) for recurring schedule";
                return false;
            }

            JsonArray rd = data["rd"];
            outConfig.repeatDaysCount = rd.size();
            if (outConfig.repeatDaysCount == 0 || outConfig.repeatDaysCount > 7)
            {
                error = "Invalid repeat days count (must be 1-7)";
                return false;
            }

            for (uint8_t i = 0; i < outConfig.repeatDaysCount; i++)
            {
                uint8_t day = rd[i];
                if (day > 6)
                {
                    error = "Invalid day value (must be 0-6)";
                    return false;
                }
                outConfig.repeatDays[i] = day;
            }
        }
        else if (outConfig.scheduleType == ScheduleType::ONCE)
        {
            // Execute at timestamp
            if (!data.containsKey("ea"))
            {
                error = "Missing execute at (ea) for once schedule";
                return false;
            }

            outConfig.executeAt = data["ea"];
            if (outConfig.executeAt == 0)
            {
                error = "Invalid execute at timestamp";
                return false;
            }
        }
        else if (outConfig.scheduleType == ScheduleType::INTERVAL)
        {
            // Interval milliseconds
            if (!data.containsKey("iv"))
            {
                error = "Missing interval (iv) for interval schedule";
                return false;
            }

            outConfig.intervalMs = data["iv"];
            if (!validateInterval(outConfig.intervalMs))
            {
                error = "Invalid interval (must be 1000-3600000ms)";
                return false;
            }
        }

        // Action type
        const char *atStr = data["at"];
        if (!atStr || strlen(atStr) != 1)
        {
            error = "Invalid action type";
            return false;
        }
        outConfig.actionType = parseActionType(atStr[0]);

        // Set value (optional for toggle, required for set)
        if (data.containsKey("sv") && !data["sv"].isNull())
        {
            outConfig.setValue = data["sv"].as<int64_t>();
        }
        else
        {
            outConfig.setValue = 0;
        }

        // Enabled flag
        outConfig.enabled = data["en"] | true;

        // Timestamps
        time_t now = time(nullptr);
        outConfig.updatedAt = now;

        if (!data.containsKey("createdAt"))
        {
            outConfig.createdAt = now;
        }
        else
        {
            outConfig.createdAt = data["createdAt"];
        }

        return true;
    }

    // ============================================
    // Action Handlers
    // ============================================

    ScheduleHandleResult ScheduleMessageHandler::_handleCreate(const JsonObject &data)
    {
        ScheduleHandleResult result;
        result.action = ScheduleAction::CREATE;

        String error;

        // Validate basic data
        if (!_validateScheduleData(data, error))
        {
            result.success = false;
            result.errorMessage = error;
            return result;
        }

        // Check schedule limit
        if (ScheduleStorage::isMaxSchedulesReached())
        {
            result.success = false;
            result.errorMessage = "Schedule limit reached (max 20)";
            result.scheduleId = data["id"].as<String>();

            ZENO_LOG("ScheduleHandler", "Schedule limit reached");
            return result;
        }

        // Parse schedule config
        ScheduleConfig config;
        if (!_parseScheduleConfig(data, config, error))
        {
            result.success = false;
            result.errorMessage = error;
            result.scheduleId = data["id"].as<String>();
            return result;
        }

        // Save to storage
        if (!ScheduleStorage::saveSchedule(config))
        {
            result.success = false;
            result.errorMessage = "Failed to save schedule to storage";
            result.scheduleId = config.id;
            return result;
        }

        // Update metadata
        ScheduleStorage::updateMetadataAfterChange(ScheduleAction::CREATE);

        // Success
        result.success = true;
        result.scheduleId = config.id;

        ZENO_LOG("ScheduleHandler", "Schedule created: %s", config.id);

        // Trigger callback
        if (_onCreatedCallback)
        {
            _onCreatedCallback(config);
        }

        return result;
    }

    ScheduleHandleResult ScheduleMessageHandler::_handleUpdate(const JsonObject &data)
    {
        ScheduleHandleResult result;
        result.action = ScheduleAction::UPDATE;

        String error;

        // Validate basic data
        if (!_validateScheduleData(data, error))
        {
            result.success = false;
            result.errorMessage = error;
            return result;
        }

        // Parse schedule config
        ScheduleConfig config;
        if (!_parseScheduleConfig(data, config, error))
        {
            result.success = false;
            result.errorMessage = error;
            result.scheduleId = data["id"].as<String>();
            return result;
        }

        // Save to storage (creates if not exists, updates if exists)
        if (!ScheduleStorage::saveSchedule(config))
        {
            result.success = false;
            result.errorMessage = "Failed to update schedule in storage";
            result.scheduleId = config.id;
            return result;
        }

        // Update metadata
        ScheduleStorage::updateMetadataAfterChange(ScheduleAction::UPDATE);

        // Success
        result.success = true;
        result.scheduleId = config.id;

        ZENO_LOG("ScheduleHandler", "Schedule updated: %s", config.id);

        // Trigger callback
        if (_onUpdatedCallback)
        {
            _onUpdatedCallback(config);
        }

        return result;
    }

    ScheduleHandleResult ScheduleMessageHandler::_handleDelete(const JsonObject &data)
    {
        ScheduleHandleResult result;
        result.action = ScheduleAction::DELETE;

        String error;

        // Validate basic data
        if (!_validateScheduleData(data, error))
        {
            result.success = false;
            result.errorMessage = error;
            return result;
        }

        String scheduleId = data["id"].as<String>();
        result.scheduleId = scheduleId;

        // Check if exists
        if (!ScheduleStorage::scheduleExists(scheduleId))
        {
            result.success = false;
            result.errorMessage = "Schedule not found";

            ZENO_LOG("ScheduleHandler", "Schedule not found for deletion: %s", scheduleId.c_str());
            return result;
        }

        // Delete from storage
        if (!ScheduleStorage::deleteSchedule(scheduleId))
        {
            result.success = false;
            result.errorMessage = "Failed to delete schedule from storage";
            return result;
        }

        // Update metadata
        ScheduleStorage::updateMetadataAfterChange(ScheduleAction::DELETE);

        // Success
        result.success = true;

        ZENO_LOG("ScheduleHandler", "Schedule deleted: %s", scheduleId.c_str());

        // Trigger callback
        if (_onDeletedCallback)
        {
            _onDeletedCallback(scheduleId);
        }

        return result;
    }

    ScheduleHandleResult ScheduleMessageHandler::_handleSync(const JsonVariant &data)
    {
        ScheduleHandleResult result;
        result.action = ScheduleAction::SYNC;

        ZENO_LOG("ScheduleHandler", "Full sync requested");

        // Data should be an array
        if (!data.is<JsonArray>())
        {
            result.success = false;
            result.errorMessage = "Sync data must be an array";
            return result;
        }

        JsonArray schedules = data.as<JsonArray>();

        ZENO_LOG("ScheduleHandler", "Syncing %d schedules (replacing all existing)", schedules.size());

        // Clear all existing schedules
        if (!ScheduleStorage::clearAllSchedules())
        {
            result.success = false;
            result.errorMessage = "Failed to clear existing schedules";
            return result;
        }

        // If empty array, just cleared all - success
        if (schedules.size() == 0)
        {
            result.success = true;
            result.scheduleId = "all";

            ZENO_LOG("ScheduleHandler", "All schedules cleared (empty sync)");

            if (_onSyncedCallback)
            {
                _onSyncedCallback(0);
            }

            return result;
        }

        // Add each schedule from array
        uint8_t successCount = 0;
        String firstError;

        for (JsonObject scheduleData : schedules)
        {
            String error;
            ScheduleConfig config;

            if (!_parseScheduleConfig(scheduleData, config, error))
            {
                if (firstError.length() == 0)
                {
                    firstError = "Schedule" + String(config.id) + ":" + error;
                }
                ZENO_LOG("ScheduleHandler", "Failed to parse schedule: %s", error.c_str());
                continue;
            }

            if (!ScheduleStorage::saveSchedule(config))
            {
                if (firstError.length() == 0)
                {
                    firstError = "Failed to save schedule" + String(config.id);
                }
                ZENO_LOG("ScheduleHandler", "Failed to save schedule: %s", config.id);
                continue;
            }

            successCount++;
        }

        // Update metadata
        ScheduleStorage::updateMetadataAfterChange(ScheduleAction::SYNC);

        // Result
        if (successCount == schedules.size())
        {
            result.success = true;
            ZENO_LOG("ScheduleHandler", "Full sync completed: %d schedules", successCount);
        }
        else
        {
            result.success = false;
            result.errorMessage = firstError + "(saved" + String(successCount) + "/" + String(schedules.size()) + ")";
            ZENO_LOG("ScheduleHandler", "Partial sync: %d/%d schedules saved", successCount, schedules.size());
        }

        result.scheduleId = String(successCount) + "schedules";

        // Trigger callback
        if (_onSyncedCallback)
        {
            _onSyncedCallback(successCount);
        }

        return result;
    }

    // ============================================
    // Callback Registration
    // ============================================

    void ScheduleMessageHandler::onScheduleCreated(ScheduleCreatedCallback callback)
    {
        _onCreatedCallback = callback;
    }

    void ScheduleMessageHandler::onScheduleUpdated(ScheduleUpdatedCallback callback)
    {
        _onUpdatedCallback = callback;
    }

    void ScheduleMessageHandler::onScheduleDeleted(ScheduleDeletedCallback callback)
    {
        _onDeletedCallback = callback;
    }

    void ScheduleMessageHandler::onScheduleSynced(ScheduleSyncedCallback callback)
    {
        _onSyncedCallback = callback;
    }

    void ScheduleMessageHandler::onError(ScheduleMessageErrorCallback callback)
    {
        _onErrorCallback = callback;
    }

    // ============================================
    // Public Utilities
    // ============================================

    String ScheduleMessageHandler::getAckTopic(const String &accessToken)
    {
        return "v1/devices/" + accessToken + "/schedules/ack";
    }

} // namespace ZenoPCB

#endif // !ZENOPCB_DISABLE_SCHEDULE
