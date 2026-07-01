// Irrigation subsystem is ESP32-only.
#if defined(ESP32)

#include "IrrigationScheduler.h"
#include "IrrigationStorage.h"
#include "../core/ZenoPCBDebug.h"
#include "../core/TimeManager.h"

namespace ZenoPCB
{

    IrrigationScheduler &IrrigationScheduler::getInstance()
    {
        static IrrigationScheduler instance;
        return instance;
    }

    IrrigationScheduler::IrrigationScheduler()
        : _triggerCb(nullptr),
          _initialized(false),
          _lastDay(0),
          _lastMidnightCheck(0)
    {
    }

    void IrrigationScheduler::begin()
    {
        ZENO_LOG("IrrigationScheduler", "Initializing...");

        if (!TimeManager::isSynced())
        {
            ZENO_LOG("IrrigationScheduler", "NTP not synced, schedules start when time available");
        }

        reloadSchedules();

        _initialized = true;
        ZENO_LOG("IrrigationScheduler", "Initialized with %d schedules", _schedules.size());
    }

    // ============================================
    // Reload from LittleFS
    // ============================================

    void IrrigationScheduler::reloadSchedules()
    {
        ZENO_LOG("IrrigationScheduler", "Reloading schedules from storage...");

        _schedules.clear();
        _states.clear();

        std::vector<IrrigationScheduleConfig> loaded;
        if (IrrigationStorage::loadAllSchedules(loaded))
        {
            for (const auto &config : loaded)
            {
                if (config.enabled)
                {
                    _schedules.push_back(config);
                    _getOrCreateState(config.scheduleId);
                }
            }
            ZENO_LOG("IrrigationScheduler", "Loaded %d enabled schedules", _schedules.size());
        }
        else
        {
            ZENO_LOG("IrrigationScheduler", "Failed to load schedules");
        }
    }

    // ============================================
    // In-memory management
    // ============================================

    void IrrigationScheduler::addOrUpdateSchedule(const IrrigationScheduleConfig &config)
    {
        // Remove existing if present
        for (auto it = _schedules.begin(); it != _schedules.end(); ++it)
        {
            if (strcmp(it->scheduleId, config.scheduleId) == 0)
            {
                _schedules.erase(it);
                break;
            }
        }

        if (config.enabled)
        {
            _schedules.push_back(config);

            // Reset state so updated schedule can fire again
            IrrigationScheduleState &state = _getOrCreateState(config.scheduleId);
            state.executedToday = false;
            state.lastStatus = IrrigationStatus::IDLE;

            ZENO_LOG("IrrigationScheduler", "Updated schedule: %s scenario %s",
                     config.scheduleId, config.scenarioId);
        }
        else
        {
            _states.erase(config.scheduleId);
            ZENO_LOG("IrrigationScheduler", "Removed (disabled): %s", config.scheduleId);
        }
    }

    void IrrigationScheduler::removeSchedule(const String &id)
    {
        for (auto it = _schedules.begin(); it != _schedules.end(); ++it)
        {
            if (strcmp(it->scheduleId, id.c_str()) == 0)
            {
                _schedules.erase(it);
                _states.erase(id);
                ZENO_LOG("IrrigationScheduler", "Removed: %s", id.c_str());
                return;
            }
        }
    }

    void IrrigationScheduler::clearAll()
    {
        _schedules.clear();
        _states.clear();
        ZENO_LOG("IrrigationScheduler", "Cleared all");
    }

    bool IrrigationScheduler::hasSchedule(const String &id) const
    {
        for (const auto &config : _schedules)
        {
            if (strcmp(config.scheduleId, id.c_str()) == 0)
                return true;
        }
        return false;
    }

    // ============================================
    // Main loop called every ~1 second
    // Pattern copied from ScheduleExecutor
    // ============================================

    void IrrigationScheduler::loop()
    {
        if (!_initialized)
            return;
        if (!TimeManager::isSynced())
            return;

        _resetDailyFlagsIfNeeded();
        _checkRecurringSchedules();
        _checkOnceSchedules();
    }

    // ============================================
    // Check recurring schedules
    // Pattern: ScheduleExecutor::_checkRecurringSchedules()
    // ============================================

    void IrrigationScheduler::_checkRecurringSchedules()
    {
        struct tm timeinfo;
        if (!TimeManager::getCurrentTimeInfo(timeinfo))
            return;

        uint8_t currentTmWday = timeinfo.tm_wday; // 0=Sun..6=Sat

        for (auto &config : _schedules)
        {
            if (config.scheduleType != IrrigationScheduleType::RECURRING)
                continue;
            if (!config.enabled)
                continue;

            IrrigationScheduleState &state = _getOrCreateState(config.scheduleId);

            if (state.executedToday)
                continue;

            if (!_shouldExecuteToday(config, currentTmWday))
                continue;

            // Prevent double-trigger in same minute
            if (config.lastTriggeredMin == timeinfo.tm_min)
                continue;

            if (_isTimeToExecute(config, timeinfo))
            {
                ZENO_LOG("IrrigationScheduler", "Recurring triggered: sch=%s scenario=%s",
                         config.scheduleId, config.scenarioId);

                config.lastTriggeredMin = timeinfo.tm_min;

                if (_triggerCb)
                {
                    if (_triggerCb(config.scheduleId, config.scenarioId))
                    {
                        state.markExecutedToday();
                        state.lastStatus = IrrigationStatus::RUNNING;
                    }
                    else
                    {
                        ZENO_LOG("IrrigationScheduler", "Trigger rejected (executor busy?)");
                    }
                }
            }
        }
    }

    // ============================================
    // Check once schedules
    // Pattern: ScheduleExecutor::_checkOnceSchedules()
    // ============================================

    void IrrigationScheduler::_checkOnceSchedules()
    {
        time_t now = TimeManager::getUTC();

        for (auto &config : _schedules)
        {
            if (config.scheduleType != IrrigationScheduleType::ONCE)
                continue;
            if (!config.enabled)
                continue;

            IrrigationScheduleState &state = _getOrCreateState(config.scheduleId);

            if (state.lastStatus == IrrigationStatus::COMPLETED)
                continue;

            if ((uint32_t)now >= config.executeAt)
            {
                ZENO_LOG("IrrigationScheduler", "Once triggered: sch=%s scenario=%s",
                         config.scheduleId, config.scenarioId);

                if (_triggerCb)
                {
                    if (_triggerCb(config.scheduleId, config.scenarioId))
                    {
                        state.markExecutedToday();
                        state.lastStatus = IrrigationStatus::COMPLETED;

                        // Disable once schedule after execution
                        config.enabled = false;
                        IrrigationStorage::deleteSchedule(config.scheduleId);
                    }
                }
            }
        }
    }

    // ============================================
    // Reset daily flags at midnight
    // Pattern: ScheduleExecutor::_resetDailyFlagsIfNeeded()
    // ============================================

    void IrrigationScheduler::_resetDailyFlagsIfNeeded()
    {
        time_t now = TimeManager::getUTC();

        // Check every hour to reduce overhead
        if (now - _lastMidnightCheck < 3600)
            return;
        _lastMidnightCheck = now;

        struct tm timeinfo;
        gmtime_r(&now, &timeinfo);
        uint8_t currentDay = timeinfo.tm_wday;

        if (currentDay != _lastDay)
        {
            ZENO_LOG("IrrigationScheduler", "Day changed resetting daily flags");

            for (auto &pair : _states)
            {
                pair.second.resetDaily();
            }

            _lastDay = currentDay;
        }
    }

    // ============================================
    // Helpers
    // ============================================

    bool IrrigationScheduler::_shouldExecuteToday(const IrrigationScheduleConfig &config,
                                                  uint8_t tmWday) const
    {
        // V3: repeatDays[] stores ISO days (1=Mon..7=Sun)
        // tmWday uses tm_wday format (0=Sun..6=Sat)
        for (uint8_t i = 0; i < config.repeatDaysCount; i++)
        {
            if (isoToTmWday(config.repeatDays[i]) == tmWday)
                return true;
        }
        return false;
    }

    bool IrrigationScheduler::_isTimeToExecute(const IrrigationScheduleConfig &config,
                                               const struct tm &timeinfo) const
    {
        // Parse "HH:MM" format
        int hour = 0, minute = 0;
        if (strlen(config.executeTime) >= 5)
        {
            hour = (config.executeTime[0] - '0') * 10 + (config.executeTime[1] - '0');
            minute = (config.executeTime[3] - '0') * 10 + (config.executeTime[4] - '0');
        }

        // Compare hour and minute only (same strategy as ScheduleExecutor)
        return (timeinfo.tm_hour == hour && timeinfo.tm_min == minute);
    }

    IrrigationScheduleState &IrrigationScheduler::_getOrCreateState(const String &id)
    {
        auto it = _states.find(id);
        if (it != _states.end())
            return it->second;

        IrrigationScheduleState newState;
        strlcpy(newState.scheduleId, id.c_str(), sizeof(newState.scheduleId));
        _states[id] = newState;
        return _states[id];
    }

} // namespace ZenoPCB

#endif  // defined(ESP32)
