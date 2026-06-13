#ifndef ZENOPCB_SCHEDULE_EXECUTOR_H
#define ZENOPCB_SCHEDULE_EXECUTOR_H

#include <Arduino.h>
#include <vector>
#include <map>
#include "../schedule/ScheduleConfig.h"
#include "../schedule/ScheduleTypes.h"
#include "../schedule/ScheduleStorage.h"
#include "../core/TimeManager.h"

namespace ZenoPCB
{

    /**
     * @brief Schedule Execution Engine
     *
     * Checks and executes schedules based on their type:
     * - Recurring: Execute on specific days at specific time (UTC)
     * - Once: Execute once at specific timestamp (UTC)
     * - Interval: Execute repeatedly every X milliseconds
     *
     * Called from main loop every 1 second.
     */
    class ScheduleExecutor
    {
    public:
        /**
         * @brief Constructor
         */
        ScheduleExecutor();

        /**
         * @brief Initialize executor and load schedules from storage
         *
         * @return true if initialized successfully
         */
        bool begin();

        /**
         * @brief Main loop - check and execute schedules
         *
         * Should be called every ~1 second from main loop.
         * Checks all enabled schedules and executes if conditions met.
         */
        void loop();

        /**
         * @brief Reload all schedules from storage
         *
         * Call this after MQTT sync to refresh in-memory schedules
         */
        void reloadSchedules();

        /**
         * @brief Add or update a schedule in memory
         *
         * @param config Schedule configuration
         */
        void addOrUpdateSchedule(const ScheduleConfig &config);

        /**
         * @brief Remove a schedule from memory
         *
         * @param scheduleId Schedule ID to remove
         */
        void removeSchedule(const String &scheduleId);

        /**
         * @brief Clear all schedules from memory
         */
        void clearAllSchedules();

        /**
         * @brief Get count of loaded schedules
         */
        uint8_t getScheduleCount() const;

        /**
         * @brief Check if a schedule exists in memory
         */
        bool hasSchedule(const String &scheduleId) const;

        /**
         * @brief Get schedule type by ID (returns INTERVAL if not found)
         */
        ScheduleType getScheduleType(const String &scheduleId) const;

        // ============================================
        // Callbacks
        // ============================================

        /**
         * @brief Set callback for schedule executed
         */
        void onScheduleExecuted(ScheduleExecutedCallback callback);

        /**
         * @brief Set callback for schedule error
         */
        void onScheduleError(ScheduleErrorCallback callback);

        /**
         * @brief Enable/disable debug logging
         */
        void setDebugEnabled(bool enabled);

    private:
        // ============================================
        // Schedule Checking Methods
        // ============================================

        /**
         * @brief Check and execute recurring schedules
         *
         * Checks if current UTC time matches schedule time and day
         */
        void _checkRecurringSchedules();

        /**
         * @brief Check and execute once schedules
         *
         * Checks if current UTC timestamp >= executeAt
         */
        void _checkOnceSchedules();

        /**
         * @brief Check and execute interval schedules
         *
         * Checks if millis() elapsed since last execution
         */
        void _checkIntervalSchedules();

        /**
         * @brief Reset daily flags at midnight (for recurring schedules)
         */
        void _resetDailyFlagsIfNeeded();

        // ============================================
        // Execution Methods
        // ============================================

        /**
         * @brief Execute a single schedule
         *
         * @param config Schedule configuration
         * @return true if execution initiated successfully
         */
        bool _executeSchedule(const ScheduleConfig &config);

        /**
         * @brief Execute SET action (write value to register)
         */
        bool _executeSetAction(const ScheduleConfig &config);

        /**
         * @brief Execute TOGGLE action (flip register value)
         */
        bool _executeToggleAction(const ScheduleConfig &config);

        /**
         * @brief Publish execution report to MQTT
         */
        void _publishExecutionReport(const String &scheduleId,
                                     ExecutionStatus status,
                                     int64_t valueWritten,
                                     const String &error = "");

        // ============================================
        // State Management
        // ============================================

        /**
         * @brief Get or create state for schedule
         */
        ScheduleState &_getOrCreateState(const String &scheduleId);

        /**
         * @brief Update state after execution
         */
        void _updateStateAfterExecution(const String &scheduleId, ExecutionStatus status);

        /**
         * @brief Check if schedule should execute today (recurring)
         */
        bool _shouldExecuteToday(const ScheduleConfig &config, uint8_t currentDay) const;

        /**
         * @brief Check if current time matches schedule time (recurring)
         */
        bool _isTimeToExecute(const ScheduleConfig &config, const struct tm &timeinfo) const;

        // ============================================
        // Member Variables
        // ============================================

        std::vector<ScheduleConfig> _schedules;  // In-memory schedules
        std::map<String, ScheduleState> _states; // Execution states

        ScheduleExecutedCallback _onExecutedCallback = nullptr;
        ScheduleErrorCallback _onErrorCallback = nullptr;

        bool _initialized = false;
        bool _debugEnabled = true;

        uint8_t _lastDay = 0;          // Track day change for reset
        time_t _lastMidnightCheck = 0; // Last midnight reset timestamp
    };

} // namespace ZenoPCB

#endif // ZENOPCB_SCHEDULE_EXECUTOR_H
