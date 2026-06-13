#ifndef ZENOPCB_SCHEDULE_CONFIG_H
#define ZENOPCB_SCHEDULE_CONFIG_H

#include <Arduino.h>
#include <time.h>  // Phase 7 Plan 07-06.5 — `time()` is used in markExecuted();
                   // Renesas (UNO R4) doesn't bring it in transitively.
#include "ScheduleTypes.h"

namespace ZenoPCB
{

    /**
     * @brief Schedule configuration structure
     *
     * This structure represents a single schedule that can be stored in LittleFS
     * and executed by the ScheduleExecutor.
     *
     * @note All times are in UTC. Use gmtime() not localtime() for comparison.
     */
    struct ScheduleConfig
    {
        // Identification
        char id[5];  // 4-digit ID: "0001" to "9999"
        char rid[7]; // Register MQTT Key (6-digit numeric)

        // Register information
        uint16_t address;          // Modbus register address (0-65535)
        RegisterType registerType; // H=Holding, C=Coil, I=Input, D=Discrete

        // Schedule type and timing
        ScheduleType scheduleType; // R=Recurring, O=Once, I=Interval

        // Recurring schedule fields (scheduleType == RECURRING)
        char executeTime[9];     // Time to execute daily: "HH:mm:ss" (UTC)
        uint8_t repeatDays[7];   // Days to repeat (0=Sunday, 1=Monday, ..., 6=Saturday)
        uint8_t repeatDaysCount; // Number of days in repeatDays array (0-7)

        // Once schedule fields (scheduleType == ONCE)
        uint32_t executeAt; // Unix timestamp (UTC) for one-time execution

        // Interval schedule fields (scheduleType == INTERVAL)
        uint32_t intervalMs; // Interval in milliseconds (1000-3600000ms = 1s-1h)

        // Action
        ActionType actionType; // S=Set value, T=Toggle
        int64_t setValue;      // Value to write (signed 64-bit integer)
                               // For COIL: 0 (false) or 1 (true)
                               // For HOLDING: any signed value

        // Status
        bool enabled; // true = active, false = disabled

        // Metadata
        uint32_t createdAt; // Unix timestamp when created
        uint32_t updatedAt; // Unix timestamp when last updated

        // Default constructor
        ScheduleConfig()
        {
            memset(this, 0, sizeof(ScheduleConfig));
        }

        /**
         * @brief Check if this is a valid recurring schedule
         */
        bool isRecurring() const
        {
            return scheduleType == ScheduleType::RECURRING && repeatDaysCount > 0;
        }

        /**
         * @brief Check if this is a valid once schedule
         */
        bool isOnce() const
        {
            return scheduleType == ScheduleType::ONCE && executeAt > 0;
        }

        /**
         * @brief Check if this is a valid interval schedule
         */
        bool isInterval() const
        {
            return scheduleType == ScheduleType::INTERVAL &&
                   intervalMs >= 1000 && intervalMs <= 3600000;
        }

        /**
         * @brief Check if schedule should execute today (for recurring schedules)
         *
         * @param dayOfWeek Current day of week (0=Sunday, 1=Monday, ..., 6=Saturday)
         * @return true if schedule should run today
         */
        bool shouldExecuteToday(uint8_t dayOfWeek) const
        {
            if (!isRecurring())
                return false;

            for (uint8_t i = 0; i < repeatDaysCount; i++)
            {
                if (repeatDays[i] == dayOfWeek)
                {
                    return true;
                }
            }
            return false;
        }
    };

    /**
     * @brief Schedule execution state
     *
     * Tracks the execution state of a schedule. This is used to prevent
     * duplicate executions and handle interval-based schedules.
     */
    struct ScheduleState
    {
        char scheduleId[5];            // Schedule ID reference
        uint32_t lastExecutionTime;    // millis() of last execution
        time_t lastExecutionTimestamp; // UTC timestamp of last execution
        bool executedToday;            // Flag for recurring schedules (reset at midnight)
        uint8_t retryCount;            // Number of retries on failure
        ExecutionStatus lastStatus;    // Status of last execution

        // Default constructor
        ScheduleState()
        {
            memset(scheduleId, 0, sizeof(scheduleId));
            lastExecutionTime = 0;
            lastExecutionTimestamp = 0;
            executedToday = false;
            retryCount = 0;
            lastStatus = ExecutionStatus::PENDING;
        }

        /**
         * @brief Check if interval has elapsed (for interval schedules)
         *
         * @param intervalMs Interval duration in milliseconds
         * @return true if enough time has passed
         *
         * @note Uses millis() overflow-safe comparison
         */
        bool intervalElapsed(uint32_t intervalMs) const
        {
            uint32_t currentMillis = millis();
            // Overflow-safe comparison: (current - last >= interval)
            return (currentMillis - lastExecutionTime) >= intervalMs;
        }

        /**
         * @brief Reset execution state (called after successful execution)
         */
        void reset()
        {
            lastExecutionTime = millis();
            lastExecutionTimestamp = time(nullptr);
            retryCount = 0;
        }

        /**
         * @brief Mark as executed today (for recurring schedules)
         */
        void markExecutedToday()
        {
            executedToday = true;
            reset();
        }

        /**
         * @brief Reset daily flag (called at midnight for recurring schedules)
         */
        void resetDaily()
        {
            executedToday = false;
        }
    };

    /**
     * @brief Schedule execution result
     */
    struct ScheduleExecutionResult
    {
        String scheduleId;
        ExecutionStatus status;
        int64_t valueWritten;
        String errorMessage;
        uint32_t executionTime; // millis() when executed

        ScheduleExecutionResult() : status(ExecutionStatus::PENDING),
                                    valueWritten(0),
                                    executionTime(0) {}
    };

} // namespace ZenoPCB

#endif // ZENOPCB_SCHEDULE_CONFIG_H
