// Irrigation subsystem is ESP32-only.
#if defined(ESP32)

#include "IrrigationMessageHandler.h"
#include "IrrigationStorage.h"
#include "IrrigationExecutor.h"
#include "IrrigationScheduler.h"
#include "../core/ZenoPCBDebug.h"
#include "../core/TimeManager.h"

namespace ZenoPCB
{

    IrrigationMessageHandler &IrrigationMessageHandler::getInstance()
    {
        static IrrigationMessageHandler instance;
        return instance;
    }

    // ============================================
    // Callback Registration
    // ============================================

    void IrrigationMessageHandler::onSynced(IrrigationSyncedCallback callback)
    {
        _onSyncedCallback = callback;
    }

    void IrrigationMessageHandler::onDeleted(IrrigationDeletedCallback callback)
    {
        _onDeletedCallback = callback;
    }

    void IrrigationMessageHandler::onError(IrrigationMessageErrorCallback callback)
    {
        _onErrorCallback = callback;
    }

    // ============================================
    // Main handler
    // ============================================

    IrrigationHandleResult IrrigationMessageHandler::handleMessage(const String &topic, const String &payload)
    {
        unsigned long startMs = millis();
        IrrigationHandleResult result;

        ZENO_LOG("IrrigationMsgHandler", "Received message (%d bytes)", payload.length());

        // Parse JSON
        JsonDocument doc;
        String parseError;
        if (!_parsePayload(payload, doc, parseError))
        {
            strlcpy(result.errorMessage, parseError.c_str(), sizeof(result.errorMessage));
            result.processingMs = millis() - startMs;
            if (_onErrorCallback)
                _onErrorCallback(parseError, payload);
            return result;
        }

        // Validate structure
        String structError;
        if (!_validateMessageStructure(doc, structError))
        {
            strlcpy(result.errorMessage, structError.c_str(), sizeof(result.errorMessage));
            result.processingMs = millis() - startMs;
            if (_onErrorCallback)
                _onErrorCallback(structError, payload);
            return result;
        }

        // Dispatch by action (V3: 6 actions)
        const char *action = doc["a"];
        JsonObject data = doc["d"].as<JsonObject>();

        if (strcmp(action, "execute") == 0)
        {
            result = _handleExecute(data);
        }
        else if (strcmp(action, "ss") == 0)
        {
            result = _handleSyncScenario(data);
        }
        else if (strcmp(action, "ds") == 0)
        {
            result = _handleDeleteScenario(data);
        }
        else if (strcmp(action, "sc") == 0)
        {
            result = _handleSyncSchedule(data);
        }
        else if (strcmp(action, "dc") == 0)
        {
            result = _handleDeleteSchedule(data);
        }
        else if (strcmp(action, "fa") == 0)
        {
            result = _handleFullSync(data);
        }
        else
        {
            snprintf(result.errorMessage, sizeof(result.errorMessage),
                     "Unknown action: %s", action);
            if (_onErrorCallback)
                _onErrorCallback(result.errorMessage, payload);
        }

        // Store action code for ACK
        strlcpy(result.action, action, sizeof(result.action));

        result.processingMs = millis() - startMs;
        return result;
    }

    // ============================================
    // Parsing & Validation
    // ============================================

    bool IrrigationMessageHandler::_parsePayload(const String &payload, JsonDocument &doc,
                                                 String &error)
    {
        DeserializationError err = deserializeJson(doc, payload);
        if (err)
        {
            error = "Invalid payload";
            ZENO_LOG("IrrigationMsgHandler", "JSON parse error: %s", err.c_str());
            return false;
        }
        return true;
    }

    bool IrrigationMessageHandler::_validateMessageStructure(const JsonDocument &doc,
                                                             String &error)
    {
        // Must have "t", "a", "d"
        if (!doc["t"].is<const char *>() || !doc["a"].is<const char *>() || doc["d"].isNull())
        {
            error = "Invalid payload";
            return false;
        }

        // "t" must be "irrigation"
        if (strcmp(doc["t"], "irrigation") != 0)
        {
            error = "Invalid payload";
            return false;
        }

        return true;
    }

    // ============================================
    // Handle EXECUTE run scenario immediately
    // Steps are inline, NOT saved to LittleFS
    // ============================================

    IrrigationHandleResult IrrigationMessageHandler::_handleExecute(const JsonObject &data)
    {
        IrrigationHandleResult result;

        // Extract sid, eid, ts
        const char *sid = data["sid"];
        const char *eid = data["eid"];
        uint32_t ts = data["ts"] | 0;

        if (!sid || strlen(sid) == 0)
        {
            strlcpy(result.errorMessage, "Missing sid", sizeof(result.errorMessage));
            return result;
        }

        strlcpy(result.scenarioId, sid, sizeof(result.scenarioId));
        strlcpy(result.executionId, eid ? eid : "", sizeof(result.executionId));

        // Check if already running
        IrrigationExecutor &executor = IrrigationExecutor::getInstance();
        if (executor.isRunning())
        {
            strlcpy(result.errorMessage, "Already running another scenario",
                    sizeof(result.errorMessage));
            return result;
        }

        // Parse steps
        if (!data["steps"].is<JsonArray>())
        {
            strlcpy(result.errorMessage, "No steps provided", sizeof(result.errorMessage));
            return result;
        }

        JsonArray stepsArr = data["steps"].as<JsonArray>();
        IrrigationStep steps[MAX_IRRIGATION_STEPS];
        uint8_t stepCount = 0;
        String stepError;

        if (!_parseSteps(stepsArr, steps, stepCount, stepError))
        {
            strlcpy(result.errorMessage, stepError.c_str(), sizeof(result.errorMessage));
            return result;
        }

        // Start execution
        if (!executor.startExecution(sid, eid ? eid : "", ts, steps, stepCount))
        {
            strlcpy(result.errorMessage, "Failed to start execution",
                    sizeof(result.errorMessage));
            return result;
        }

        result.success = true;
        ZENO_LOG("IrrigationMsgHandler", "Execute started: %s (eid=%s, %d steps)",
                 sid, eid ? eid : "?", stepCount);
        return result;
    }

    // ============================================
    // Handle SS save scenario to LittleFS (V3: no schedule info)
    // ============================================

    IrrigationHandleResult IrrigationMessageHandler::_handleSyncScenario(const JsonObject &data)
    {
        IrrigationHandleResult result;

        const char *sid = data["sid"];
        if (!sid || strlen(sid) == 0)
        {
            strlcpy(result.errorMessage, "Missing sid", sizeof(result.errorMessage));
            return result;
        }

        strlcpy(result.scenarioId, sid, sizeof(result.scenarioId));

        if (!IrrigationStorage::scenarioExists(sid) &&
            IrrigationStorage::isMaxScenariosReached())
        {
            strlcpy(result.errorMessage, "Flash full", sizeof(result.errorMessage));
            return result;
        }

        IrrigationScenarioConfig config;
        memset(&config, 0, sizeof(config));
        strlcpy(config.scenarioId, sid, sizeof(config.scenarioId));
        config.enabled = data["en"] | true;

        // Parse steps
        if (data["steps"].is<JsonArray>())
        {
            JsonArray stepsArr = data["steps"].as<JsonArray>();
            String stepError;
            if (!_parseSteps(stepsArr, config.steps, config.stepCount, stepError))
            {
                strlcpy(result.errorMessage, stepError.c_str(), sizeof(result.errorMessage));
                return result;
            }
        }

        if (!IrrigationStorage::saveScenario(config))
        {
            strlcpy(result.errorMessage, "Flash write error", sizeof(result.errorMessage));
            return result;
        }

        result.success = true;
        ZENO_LOG("IrrigationMsgHandler", "SS: %s (%d steps)", sid, config.stepCount);

        if (_onSyncedCallback)
            _onSyncedCallback(sid);
        return result;
    }

    // ============================================
    // Handle DS delete scenario from LittleFS
    // ============================================

    IrrigationHandleResult IrrigationMessageHandler::_handleDeleteScenario(const JsonObject &data)
    {
        IrrigationHandleResult result;

        const char *sid = data["sid"];
        if (!sid || strlen(sid) == 0)
        {
            strlcpy(result.errorMessage, "Missing sid", sizeof(result.errorMessage));
            return result;
        }

        strlcpy(result.scenarioId, sid, sizeof(result.scenarioId));

        if (!IrrigationStorage::scenarioExists(sid))
        {
            strlcpy(result.errorMessage, "Scenario not found",
                    sizeof(result.errorMessage));
            return result;
        }

        // If this scenario is currently running, stop it
        IrrigationExecutor &executor = IrrigationExecutor::getInstance();
        if (executor.isRunning())
        {
            const IrrigationExecution &exec = executor.getExecution();
            if (strcmp(exec.scenarioId, sid) == 0)
            {
                executor.stopExecution();
                ZENO_LOG("IrrigationMsgHandler", "Stopped running scenario: %s", sid);
            }
        }

        // Delete from storage
        if (!IrrigationStorage::deleteScenario(sid))
        {
            strlcpy(result.errorMessage, "Storage delete failed",
                    sizeof(result.errorMessage));
            return result;
        }

        // Remove from scheduler (any schedules referencing this scenario won't run)
        // Note: We don't auto-delete schedules they become orphaned (server handles cleanup)

        result.success = true;
        ZENO_LOG("IrrigationMsgHandler", "DS: %s", sid);

        if (_onDeletedCallback)
            _onDeletedCallback(sid);
        return result;
    }

    // ============================================
    // Handle SC sync schedule to LittleFS (V3: separate entity)
    // ============================================

    IrrigationHandleResult IrrigationMessageHandler::_handleSyncSchedule(const JsonObject &data)
    {
        IrrigationHandleResult result;

        const char *id = data["id"];
        if (!id || strlen(id) == 0)
        {
            strlcpy(result.errorMessage, "Missing id", sizeof(result.errorMessage));
            return result;
        }

        strlcpy(result.scheduleId, id, sizeof(result.scheduleId));

        const char *sid = data["sid"];
        if (!sid || strlen(sid) == 0)
        {
            strlcpy(result.errorMessage, "Missing sid", sizeof(result.errorMessage));
            return result;
        }

        strlcpy(result.scenarioId, sid, sizeof(result.scenarioId));

        // Verify referenced scenario exists in flash
        if (!IrrigationStorage::scenarioExists(sid))
        {
            strlcpy(result.errorMessage, "Scenario not found", sizeof(result.errorMessage));
            return result;
        }

        if (!IrrigationStorage::scheduleExists(id) &&
            IrrigationStorage::isMaxSchedulesReached())
        {
            strlcpy(result.errorMessage, "Flash full", sizeof(result.errorMessage));
            return result;
        }

        // Build schedule config
        IrrigationScheduleConfig schConfig;
        memset(&schConfig, 0, sizeof(schConfig));
        strlcpy(schConfig.scheduleId, id, sizeof(schConfig.scheduleId));
        strlcpy(schConfig.scenarioId, sid, sizeof(schConfig.scenarioId));
        schConfig.enabled = data["en"] | true;

        const char *st = data["st"] | "N";
        schConfig.scheduleType = parseIrrigationScheduleType(st[0]);

        if (schConfig.scheduleType == IrrigationScheduleType::RECURRING)
        {
            const char *et = data["et"];
            if (et)
            {
                strlcpy(schConfig.executeTime, et, sizeof(schConfig.executeTime));
            }

            if (data["rd"].is<JsonArray>())
            {
                JsonArray rd = data["rd"].as<JsonArray>();
                schConfig.repeatDaysCount = min((int)rd.size(), 7);
                for (uint8_t i = 0; i < schConfig.repeatDaysCount; i++)
                {
                    schConfig.repeatDays[i] = rd[i] | 0;
                }
            }
        }
        else if (schConfig.scheduleType == IrrigationScheduleType::ONCE)
        {
            schConfig.executeAt = data["ea"] | (uint32_t)0;
        }

        if (!IrrigationStorage::saveSchedule(schConfig))
        {
            strlcpy(result.errorMessage, "Flash write error", sizeof(result.errorMessage));
            return result;
        }

        // Update scheduler in-memory
        IrrigationScheduler::getInstance().addOrUpdateSchedule(schConfig);

        result.success = true;
        ZENO_LOG("IrrigationMsgHandler", "SC: %s scenario %s (type=%c)",
                 id, sid, (char)schConfig.scheduleType);
        return result;
    }

    // ============================================
    // Handle DC delete schedule from LittleFS
    // ============================================

    IrrigationHandleResult IrrigationMessageHandler::_handleDeleteSchedule(const JsonObject &data)
    {
        IrrigationHandleResult result;

        const char *id = data["id"];
        if (!id || strlen(id) == 0)
        {
            strlcpy(result.errorMessage, "Missing id", sizeof(result.errorMessage));
            return result;
        }

        strlcpy(result.scheduleId, id, sizeof(result.scheduleId));

        // Delete from storage (idempotent ok if not found)
        IrrigationStorage::deleteSchedule(id);

        // Remove from scheduler in-memory
        IrrigationScheduler::getInstance().removeSchedule(id);

        result.success = true;
        ZENO_LOG("IrrigationMsgHandler", "DC: %s", id);
        return result;
    }

    // ============================================
    // Handle FA full sync (clear all + reload)
    // ============================================

    IrrigationHandleResult IrrigationMessageHandler::_handleFullSync(const JsonObject &data)
    {
        IrrigationHandleResult result;

        // Stop any running execution
        IrrigationExecutor &executor = IrrigationExecutor::getInstance();
        if (executor.isRunning())
        {
            executor.stopExecution();
            ZENO_LOG("IrrigationMsgHandler", "Stopped running scenario for full sync");
        }

        // Clear all existing data
        IrrigationStorage::clearAll();
        IrrigationScheduler::getInstance().clearAll();

        uint8_t scCount = 0;
        uint8_t schCount = 0;

        // Save scenarios
        if (data["scenarios"].is<JsonArray>())
        {
            JsonArray scenarios = data["scenarios"].as<JsonArray>();
            for (JsonObject scObj : scenarios)
            {
                if (scCount >= MAX_IRRIGATION_SCENARIOS)
                    break;

                IrrigationScenarioConfig config;
                memset(&config, 0, sizeof(config));

                const char *sid = scObj["sid"];
                if (!sid)
                    continue;

                strlcpy(config.scenarioId, sid, sizeof(config.scenarioId));
                config.enabled = scObj["en"] | true;

                if (scObj["steps"].is<JsonArray>())
                {
                    JsonArray stepsArr = scObj["steps"].as<JsonArray>();
                    String stepError;
                    if (_parseSteps(stepsArr, config.steps, config.stepCount, stepError))
                    {
                        if (IrrigationStorage::saveScenario(config))
                        {
                            scCount++;
                        }
                    }
                }
            }
        }

        // Save schedules
        if (data["schedules"].is<JsonArray>())
        {
            JsonArray schedules = data["schedules"].as<JsonArray>();
            for (JsonObject schObj : schedules)
            {
                if (schCount >= MAX_IRRIGATION_SCHEDULES)
                    break;

                IrrigationScheduleConfig schConfig;
                memset(&schConfig, 0, sizeof(schConfig));

                const char *id = schObj["id"];
                const char *sid = schObj["sid"];
                if (!id || !sid)
                    continue;

                strlcpy(schConfig.scheduleId, id, sizeof(schConfig.scheduleId));
                strlcpy(schConfig.scenarioId, sid, sizeof(schConfig.scenarioId));
                schConfig.enabled = schObj["en"] | true;

                const char *st = schObj["st"] | "N";
                schConfig.scheduleType = parseIrrigationScheduleType(st[0]);

                if (schConfig.scheduleType == IrrigationScheduleType::RECURRING)
                {
                    const char *et = schObj["et"];
                    if (et)
                        strlcpy(schConfig.executeTime, et, sizeof(schConfig.executeTime));

                    if (schObj["rd"].is<JsonArray>())
                    {
                        JsonArray rd = schObj["rd"].as<JsonArray>();
                        schConfig.repeatDaysCount = min((int)rd.size(), 7);
                        for (uint8_t i = 0; i < schConfig.repeatDaysCount; i++)
                        {
                            schConfig.repeatDays[i] = rd[i] | 0;
                        }
                    }
                }
                else if (schConfig.scheduleType == IrrigationScheduleType::ONCE)
                {
                    schConfig.executeAt = schObj["ea"] | (uint32_t)0;
                }

                if (IrrigationStorage::saveSchedule(schConfig))
                {
                    schCount++;
                }
            }
        }

        // Reload scheduler with new data
        IrrigationScheduler::getInstance().reloadSchedules();

        result.success = true;
        result.scenarioCount = scCount;
        result.scheduleCount = schCount;
        ZENO_LOG("IrrigationMsgHandler", "FA: %d scenarios, %d schedules", scCount, schCount);
        return result;
    }

    // ============================================
    // Step Parser V2 format
    // ============================================

    bool IrrigationMessageHandler::_parseSteps(const JsonArray &stepsArr,
                                               IrrigationStep *outSteps,
                                               uint8_t &outCount,
                                               String &error)
    {
        if (stepsArr.size() == 0)
        {
            error = "No steps provided";
            return false;
        }

        outCount = min((int)stepsArr.size(), (int)MAX_IRRIGATION_STEPS);

        for (uint8_t i = 0; i < outCount; i++)
        {
            JsonObject stepObj = stepsArr[i];
            IrrigationStep &step = outSteps[i];
            memset(&step, 0, sizeof(IrrigationStep));

            // Order
            step.order = stepObj["o"] | (i + 1);

            // Action
            const char *actionCode = stepObj["a"];
            if (!actionCode || strlen(actionCode) == 0)
            {
                error = "Step missing action";
                return false;
            }

            step.action = parseIrrigationAction(actionCode);
            // Validate: unknown codes default to WAIT which requires dur
            if (!isWaitAction(step.action) && strcmp(actionCode, irrigationActionToCode(step.action)) != 0)
            {
                error = "Invalid action code";
                return false;
            }

            // Parse based on action type
            if (isWaitAction(step.action))
            {
                // WAIT step: has "dur", no "k"
                step.waitDuration = stepObj["dur"] | 0;
                if (step.waitDuration == 0)
                {
                    error = "WAIT step missing dur";
                    return false;
                }
                step.keyCount = 0;
            }
            else
            {
                // Non-WAIT: has "k" array, no "dur"
                step.waitDuration = 0;

                if (!stepObj["k"].is<JsonArray>())
                {
                    error = "Step missing k array";
                    return false;
                }

                JsonArray keysArr = stepObj["k"].as<JsonArray>();
                step.keyCount = min((int)keysArr.size(), (int)MAX_IRRIGATION_TARGETS);

                if (step.keyCount == 0)
                {
                    error = "Step has empty k array";
                    return false;
                }

                for (uint8_t j = 0; j < step.keyCount; j++)
                {
                    const char *key = keysArr[j];
                    if (key)
                    {
                        strlcpy(step.mqttKeys[j], key, IRRIGATION_KEY_LEN);
                    }
                }
            }
        }

        return true;
    }

} // namespace ZenoPCB

#endif  // defined(ESP32)
