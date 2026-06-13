// Phase 7 Plan 07-06.6 — TU guard for ZENOPCB_MICRO_BASIC profile (F103 budget fit).
// When `-DZENOPCB_DISABLE_SCHEDULE` is set, the entire Schedule subsystem TU compiles
// to an empty translation unit — no link symbols emitted, no Flash overhead.
#if !defined(ZENOPCB_DISABLE_SCHEDULE)

#include "ScheduleExecutor.h"

#include "../core/ZenoPCBDebug.h"
#include "../core/ZKeyBuffer.h"

// Plan 06-3.5: Modbus IO is ESP32-only. Headers + Modbus call sites are guarded
// at the call-site level so this TU compiles on ESP8266 (Schedule management +
// ZKey path remain fully functional; Modbus rid paths degrade to a logged failure).
#if defined(ESP32)
#include "../modbus/RegisterPollingEngine.h"
#include "../modbus/ModbusDataBuffer.h"
#endif

namespace ZenoPCB
{

    // ============================================
    // Constructor
    // ============================================

    ScheduleExecutor::ScheduleExecutor()
    {
        // Initialize state
        _initialized = false;
        _debugEnabled = true;
        _lastDay = 0;
        _lastMidnightCheck = 0;
    }

    // ============================================
    // Initialization
    // ============================================

    bool ScheduleExecutor::begin()
    {
        ZENO_LOG("ScheduleExecutor", "🚀 Initializing Schedule Executor...");

        // Check if time is synced
        if (!TimeManager::isSynced())
        {
            ZENO_LOG("ScheduleExecutor", "⚠️ NTP not synced yet, schedules will start when time is available");
        }

        // Load schedules from storage
        reloadSchedules();

        _initialized = true;

        ZENO_LOG("ScheduleExecutor", "✅ Schedule Executor initialized with %d schedules", _schedules.size());

        return true;
    }

    // ============================================
    // Schedule Management
    // ============================================

    void ScheduleExecutor::reloadSchedules()
    {
        ZENO_LOG("ScheduleExecutor", "📂 Reloading schedules from storage...");

        _schedules.clear();

        std::vector<ScheduleConfig> loadedSchedules;
        if (ScheduleStorage::loadAllSchedules(loadedSchedules))
        {
            _schedules = loadedSchedules;

            // Initialize state for each schedule
            for (const auto &config : _schedules)
            {
                _getOrCreateState(config.id);
            }

            ZENO_LOG("ScheduleExecutor", "✅ Loaded %d schedules", _schedules.size());
        }
        else
        {
            ZENO_LOG("ScheduleExecutor", "❌ Failed to load schedules from storage");
        }
    }

    void ScheduleExecutor::addOrUpdateSchedule(const ScheduleConfig &config)
    {
        // Find existing schedule
        for (size_t i = 0; i < _schedules.size(); i++)
        {
            if (strcmp(_schedules[i].id, config.id) == 0)
            {
                // Update existing
                _schedules[i] = config;

                // Reset execution state so updated schedule can re-execute
                // Critical for ONCE schedules: without this, a previously executed
                // schedule would stay in SUCCESS state and never fire again
                ScheduleState &state = _getOrCreateState(config.id);
                state.lastStatus = ExecutionStatus::PENDING;
                state.executedToday = false;
                state.retryCount = 0;

                ZENO_LOG("ScheduleExecutor", "🔄 Updated schedule in memory: %s (state reset)", config.id);
                return;
            }
        }

        // Add new schedule
        _schedules.push_back(config);
        _getOrCreateState(config.id); // Initialize state

        ZENO_LOG("ScheduleExecutor", "➕ Added schedule to memory: %s", config.id);
    }

    void ScheduleExecutor::removeSchedule(const String &scheduleId)
    {
        for (auto it = _schedules.begin(); it != _schedules.end(); ++it)
        {
            if (strcmp(it->id, scheduleId.c_str()) == 0)
            {
                _schedules.erase(it);

                // Remove state
                _states.erase(scheduleId);

                ZENO_LOG("ScheduleExecutor", "➖ Removed schedule from memory: %s", scheduleId.c_str());
                return;
            }
        }
    }

    void ScheduleExecutor::clearAllSchedules()
    {
        _schedules.clear();
        _states.clear();

        ZENO_LOG("ScheduleExecutor", "🗑️ Cleared all schedules from memory");
    }

    uint8_t ScheduleExecutor::getScheduleCount() const
    {
        return _schedules.size();
    }

    ScheduleType ScheduleExecutor::getScheduleType(const String &scheduleId) const
    {
        for (const auto &cfg : _schedules)
        {
            if (strcmp(cfg.id, scheduleId.c_str()) == 0)
                return cfg.scheduleType;
        }
        return ScheduleType::INTERVAL; // safe default
    }

    bool ScheduleExecutor::hasSchedule(const String &scheduleId) const
    {
        for (const auto &config : _schedules)
        {
            if (strcmp(config.id, scheduleId.c_str()) == 0)
            {
                return true;
            }
        }
        return false;
    }

    // ============================================
    // Main Loop
    // ============================================

    void ScheduleExecutor::loop()
    {
        if (!_initialized)
        {
            return;
        }

        // Check if NTP is synced
        if (!TimeManager::isSynced())
        {
            return; // Skip execution until time is synced
        }

        // Reset daily flags at midnight
        _resetDailyFlagsIfNeeded();

        // Check different schedule types
        _checkRecurringSchedules();
        _checkOnceSchedules();
        _checkIntervalSchedules();
    }

    // ============================================
    // Schedule Checking Methods
    // ============================================

    void ScheduleExecutor::_checkRecurringSchedules()
    {
        struct tm timeinfo;
        if (!TimeManager::getCurrentTimeInfo(timeinfo))
        {
            return;
        }

        for (const auto &config : _schedules)
        {
            // Skip if not recurring or not enabled
            if (config.scheduleType != ScheduleType::RECURRING || !config.enabled)
            {
                continue;
            }

            // Get state
            ScheduleState &state = _getOrCreateState(config.id);

            // Skip if already executed today
            if (state.executedToday)
            {
                continue;
            }

            // Check if should execute today
            uint8_t currentDay = timeinfo.tm_wday; // 0=Sunday, 1=Monday, ..., 6=Saturday
            if (!_shouldExecuteToday(config, currentDay))
            {
                continue;
            }

            // Check if current time matches execute time
            if (_isTimeToExecute(config, timeinfo))
            {
                ZENO_LOG("ScheduleExecutor", "⏰ Recurring schedule triggered: %s", config.id);

                if (_executeSchedule(config))
                {
                    state.markExecutedToday();
                }
            }
        }
    }

    void ScheduleExecutor::_checkOnceSchedules()
    {
        time_t now = TimeManager::getUTC();

        for (const auto &config : _schedules)
        {
            // Skip if not once or not enabled
            if (config.scheduleType != ScheduleType::ONCE || !config.enabled)
            {
                continue;
            }

            // Get state
            ScheduleState &state = _getOrCreateState(config.id);

            // Skip if already executed
            if (state.lastStatus == ExecutionStatus::SUCCESS)
            {
                continue;
            }

            // Check if time to execute
            if (now >= config.executeAt)
            {
                ZENO_LOG("ScheduleExecutor", "⏰ Once schedule triggered: %s", config.id);

                if (_executeSchedule(config))
                {
                    // Disable schedule after successful execution
                    // Note: This only updates in-memory state
                    // Backend should handle disabling in storage via MQTT update
                    state.lastStatus = ExecutionStatus::SUCCESS;
                }
            }
        }
    }

    void ScheduleExecutor::_checkIntervalSchedules()
    {
        for (const auto &config : _schedules)
        {
            // Skip if not interval or not enabled
            if (config.scheduleType != ScheduleType::INTERVAL || !config.enabled)
            {
                continue;
            }

            // Get state
            ScheduleState &state = _getOrCreateState(config.id);

            // Check if interval elapsed (overflow-safe)
            if (state.intervalElapsed(config.intervalMs))
            {
                if (_debugEnabled)
                {
                    ZENO_LOG("ScheduleExecutor", "⏰ Interval schedule triggered: %s (every %dms)",
                             config.id, config.intervalMs);
                }

                _executeSchedule(config);
            }
        }
    }

    void ScheduleExecutor::_resetDailyFlagsIfNeeded()
    {
        time_t now = TimeManager::getUTC();

        // Check every hour to reduce overhead
        if (now - _lastMidnightCheck < 3600)
        {
            return;
        }

        _lastMidnightCheck = now;

        struct tm timeinfo;
        gmtime_r(&now, &timeinfo);

        uint8_t currentDay = timeinfo.tm_wday;

        // If day changed, reset all daily flags
        if (currentDay != _lastDay)
        {
            ZENO_LOG("ScheduleExecutor", "📅 Day changed: resetting daily execution flags");

            for (auto &pair : _states)
            {
                pair.second.resetDaily();
            }

            _lastDay = currentDay;
        }
    }

    // ============================================
    // Execution Methods
    // ============================================

    // Returns true if rid is Z Key format: "Z0".."Z99"
    static bool _isZKeyRid(const char *rid)
    {
        if (!rid || (rid[0] != 'Z' && rid[0] != 'z'))
            return false;
        size_t len = strlen(rid);
        if (len < 2 || len > 3)
            return false;
        for (size_t i = 1; i < len; i++)
            if (!isDigit(rid[i]))
                return false;
        return true;
    }

    bool ScheduleExecutor::_executeSchedule(const ScheduleConfig &config)
    {
        ZENO_LOG("ScheduleExecutor", "▶️ Executing schedule: %s (type=%s, action=%c)",
                 config.id,
                 scheduleTypeToString(config.scheduleType),
                 (char)config.actionType);

        bool success = false;

        if (config.actionType == ActionType::SET)
        {
            success = _executeSetAction(config);
        }
        else if (config.actionType == ActionType::TOGGLE)
        {
            success = _executeToggleAction(config);
        }

        if (success)
        {
            _updateStateAfterExecution(config.id, ExecutionStatus::SUCCESS);
        }
        else
        {
            _updateStateAfterExecution(config.id, ExecutionStatus::FAILED);
        }

        return success;
    }

    bool ScheduleExecutor::_executeSetAction(const ScheduleConfig &config)
    {
        ZENO_LOG("ScheduleExecutor", "📤 SET action: writing value %lld to register %s",
                 config.setValue, config.rid);

        // ── Z Key path ────────────────────────────────────────────────
        // rid "Z0".."Z99" → write directly to ZKeyBuffer (no Modbus needed)
        if (_isZKeyRid(config.rid))
        {
            int idx = atoi(config.rid + 1); // "Z3" → 3
            ZKeyBuffer::getInstance().set((ZKey)idx, (int32_t)config.setValue);
            ZKeyBuffer::getInstance().notifyChange((ZKey)idx);
            ZENO_LOG("ScheduleExecutor", "✅ Z Key SET: Z%d = %lld", idx, config.setValue);
            _publishExecutionReport(config.id, ExecutionStatus::SUCCESS, config.setValue);
            if (_onExecutedCallback)
                _onExecutedCallback(config.id, ExecutionStatus::SUCCESS, config.setValue, "");
            return true;
        }

        // ── Modbus path ───────────────────────────────────────────────
#if defined(ESP32)
        bool callbackFired = false;

        bool enqueued = RegisterPollingEngine::getInstance().enqueueWriteByKey(
            config.rid,
            (double)config.setValue,
            [this, config, &callbackFired](bool success, const String &error)
            {
                callbackFired = true;
                if (success)
                {
                    ZENO_LOG("ScheduleExecutor", "✅ Schedule %s executed successfully", config.id);
                    _publishExecutionReport(config.id, ExecutionStatus::SUCCESS, config.setValue);

                    if (_onExecutedCallback)
                    {
                        _onExecutedCallback(config.id, ExecutionStatus::SUCCESS, config.setValue, "");
                    }
                }
                else
                {
                    ZENO_LOG("ScheduleExecutor", "❌ Schedule %s execution failed: %s",
                             config.id, error.c_str());
                    _publishExecutionReport(config.id, ExecutionStatus::FAILED, 0, error);

                    if (_onErrorCallback)
                    {
                        _onErrorCallback(config.id, error);
                    }
                }
            });

        if (!enqueued && !callbackFired)
        {
            String error = "Write queue full";
            ZENO_LOG("ScheduleExecutor", "❌ %s", error.c_str());
            _publishExecutionReport(config.id, ExecutionStatus::FAILED, 0, error);

            if (_onErrorCallback)
            {
                _onErrorCallback(config.id, error);
            }
        }

        return enqueued;
#else
        // ESP8266: Modbus IO subsystem not available — schedule with Modbus rid is a no-op.
        // Z Key rids (handled above) remain fully functional.
        ZENO_LOG("ScheduleExecutor",
                 "⚠️ Modbus SET skipped on this platform (rid=%s) — use Z Key rids on ESP8266",
                 config.rid);
        String error = "Modbus not available on this platform";
        _publishExecutionReport(config.id, ExecutionStatus::FAILED, 0, error);
        if (_onErrorCallback)
        {
            _onErrorCallback(config.id, error);
        }
        return false;
#endif
    }

    bool ScheduleExecutor::_executeToggleAction(const ScheduleConfig &config)
    {
        ZENO_LOG("ScheduleExecutor", "🔄 TOGGLE action: flipping register %s", config.rid);

        // ── Z Key path ────────────────────────────────────────────────
        if (_isZKeyRid(config.rid))
        {
            int idx = atoi(config.rid + 1);
            ZKey key = (ZKey)idx;
            int32_t current = ZKeyBuffer::getInstance().getInt(key, 0);
            int32_t newVal = current ? 0 : 1;
            ZKeyBuffer::getInstance().set(key, newVal);
            ZKeyBuffer::getInstance().notifyChange(key);
            ZENO_LOG("ScheduleExecutor", "✅ Z Key TOGGLE: Z%d: %d → %d", idx, current, newVal);
            _publishExecutionReport(config.id, ExecutionStatus::SUCCESS, (int64_t)newVal);
            if (_onExecutedCallback)
                _onExecutedCallback(config.id, ExecutionStatus::SUCCESS, (int64_t)newVal, "");
            return true;
        }

        // ── Modbus path ───────────────────────────────────────────────
#if defined(ESP32)
        // Get current value from ModbusDataBuffer
        RegisterValue currentValue = ModbusDataBuffer::getInstance().getValue(config.rid);

        if (currentValue.status != RegisterValue::VALID)
        {
            String error = "Register not found or not readable";
            ZENO_LOG("ScheduleExecutor", "❌ %s: %s", error.c_str(), config.rid);
            _publishExecutionReport(config.id, ExecutionStatus::FAILED, 0, error);

            if (_onErrorCallback)
            {
                _onErrorCallback(config.id, error);
            }

            return false;
        }

        // Calculate toggled value using scaled value from buffer
        // This works correctly regardless of DataMonitorConfig.dataType
        // (boolValue is only set when dataType==BOOLEAN, but coils may use i16/u16 etc.)
        double currentScaled = ModbusDataBuffer::getInstance().getScaledValue(config.rid);
        double newValue = (currentScaled == 0.0) ? 1.0 : 0.0;

        ZENO_LOG("ScheduleExecutor", "🔄 Toggle: %.0f -> %.0f", currentScaled, newValue);

        // Enqueue write
        bool callbackFired = false;
        bool enqueued = RegisterPollingEngine::getInstance().enqueueWriteByKey(
            config.rid,
            newValue,
            [this, config, newValue, &callbackFired](bool success, const String &error)
            {
                callbackFired = true;
                if (success)
                {
                    ZENO_LOG("ScheduleExecutor", "✅ Toggle schedule %s executed successfully", config.id);
                    _publishExecutionReport(config.id, ExecutionStatus::SUCCESS, (int64_t)newValue);

                    if (_onExecutedCallback)
                    {
                        _onExecutedCallback(config.id, ExecutionStatus::SUCCESS, (int64_t)newValue, "");
                    }
                }
                else
                {
                    ZENO_LOG("ScheduleExecutor", "❌ Toggle schedule %s failed: %s",
                             config.id, error.c_str());
                    _publishExecutionReport(config.id, ExecutionStatus::FAILED, 0, error);

                    if (_onErrorCallback)
                    {
                        _onErrorCallback(config.id, error);
                    }
                }
            });

        if (!enqueued && !callbackFired)
        {
            String error = "Failed to enqueue toggle write";
            ZENO_LOG("ScheduleExecutor", "❌ %s", error.c_str());
            _publishExecutionReport(config.id, ExecutionStatus::FAILED, 0, error);

            if (_onErrorCallback)
            {
                _onErrorCallback(config.id, error);
            }

            return false;
        }

        return true;
#else
        // ESP8266: Modbus IO subsystem not available — TOGGLE on a Modbus rid is a no-op.
        // Z Key TOGGLE (handled above) remains fully functional.
        ZENO_LOG("ScheduleExecutor",
                 "⚠️ Modbus TOGGLE skipped on this platform (rid=%s) — use Z Key rids on ESP8266",
                 config.rid);
        String error = "Modbus not available on this platform";
        _publishExecutionReport(config.id, ExecutionStatus::FAILED, 0, error);
        if (_onErrorCallback)
        {
            _onErrorCallback(config.id, error);
        }
        return false;
#endif
    }

    void ScheduleExecutor::_publishExecutionReport(const String &scheduleId,
                                                   ExecutionStatus status,
                                                   int64_t valueWritten,
                                                   const String &error)
    {
        // TODO: Implement MQTT publish to v1/devices/{token}/schedules/executed
        // This will be integrated when connecting to ZenoPCB main class

        // For now, just log
        ZENO_LOG("ScheduleExecutor", "📊 Execution report: id=%s, status=%s, value=%lld, error=%s",
                 scheduleId.c_str(),
                 executionStatusToString(status),
                 valueWritten,
                 error.c_str());
    }

    // ============================================
    // State Management
    // ============================================

    ScheduleState &ScheduleExecutor::_getOrCreateState(const String &scheduleId)
    {
        auto it = _states.find(scheduleId);
        if (it != _states.end())
        {
            return it->second;
        }

        // Create new state
        ScheduleState newState;
        strlcpy(newState.scheduleId, scheduleId.c_str(), sizeof(newState.scheduleId));
        _states[scheduleId] = newState;

        return _states[scheduleId];
    }

    void ScheduleExecutor::_updateStateAfterExecution(const String &scheduleId, ExecutionStatus status)
    {
        ScheduleState &state = _getOrCreateState(scheduleId);
        state.lastStatus = status;
        state.reset(); // Updates lastExecutionTime and resets retry count
    }

    bool ScheduleExecutor::_shouldExecuteToday(const ScheduleConfig &config, uint8_t currentDay) const
    {
        for (uint8_t i = 0; i < config.repeatDaysCount; i++)
        {
            if (config.repeatDays[i] == currentDay)
            {
                return true;
            }
        }
        return false;
    }

    bool ScheduleExecutor::_isTimeToExecute(const ScheduleConfig &config, const struct tm &timeinfo) const
    {
        // Parse execute time
        int hour, minute, second;
        if (!TimeManager::parseTime(config.executeTime, hour, minute, second))
        {
            return false;
        }

        // Compare hour and minute only (ignore seconds)
        // Reason: if loop() takes >1s (Modbus timeout, MQTT processing),
        // exact second matching would miss the schedule entirely.
        // executedToday flag prevents duplicate execution within the same day.
        return (timeinfo.tm_hour == hour &&
                timeinfo.tm_min == minute);
    }

    // ============================================
    // Callback Registration
    // ============================================

    void ScheduleExecutor::onScheduleExecuted(ScheduleExecutedCallback callback)
    {
        _onExecutedCallback = callback;
    }

    void ScheduleExecutor::onScheduleError(ScheduleErrorCallback callback)
    {
        _onErrorCallback = callback;
    }

    void ScheduleExecutor::setDebugEnabled(bool enabled)
    {
        _debugEnabled = enabled;
    }

} // namespace ZenoPCB

#endif // !ZENOPCB_DISABLE_SCHEDULE
