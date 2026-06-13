#ifndef ZENOPCB_SCHEDULE_TYPES_H
#define ZENOPCB_SCHEDULE_TYPES_H

#include <Arduino.h>
#include <functional>
#include "../storage/DataMonitorConfig.h" // For RegisterType enum

namespace ZenoPCB
{

// Maximum number of schedules allowed
// Override via build flag: -DMAX_SCHEDULES=50
#ifndef MAX_SCHEDULES
#define MAX_SCHEDULES 20
#endif

    /**
     * @brief Schedule type enumeration
     */
    enum class ScheduleType : char
    {
        RECURRING = 'R', // Repeat on specific days at specific time
        ONCE = 'O',      // Execute once at specific timestamp
        INTERVAL = 'I'   // Repeat every X milliseconds (1s-1h)
    };

    // NOTE: RegisterType is already defined in storage/DataMonitorConfig.h
    // We reuse that enum instead of redefining it here to avoid conflicts

    /**
     * @brief Action type enumeration
     */
    enum class ActionType : char
    {
        SET = 'S',   // Set value (write setValue to register)
        TOGGLE = 'T' // Toggle (flip boolean/bit value)
    };

    /**
     * @brief Schedule action enumeration (for MQTT messages)
     */
    enum class ScheduleAction : char
    {
        CREATE = 'c', // Create new schedule
        UPDATE = 'u', // Update existing schedule
        DELETE = 'd', // Delete schedule
        SYNC = 's'    // Full synchronization (replace all)
    };

    /**
     * @brief Schedule execution status
     */
    enum class ExecutionStatus
    {
        SUCCESS, // Execution completed successfully
        FAILED,  // Execution failed
        PENDING, // Waiting for execution
        SKIPPED  // Skipped (e.g., register not found)
    };

    /**
     * @brief Callback for schedule executed event
     *
     * @param scheduleId Schedule ID (e.g., "0001")
     * @param status Execution status
     * @param value Value written (for SET action)
     * @param error Error message (if failed)
     */
    using ScheduleExecutedCallback = std::function<void(
        const String &scheduleId,
        ExecutionStatus status,
        int64_t value,
        const String &error)>;

    /**
     * @brief Callback for schedule error event
     *
     * @param scheduleId Schedule ID
     * @param error Error message
     */
    using ScheduleErrorCallback = std::function<void(
        const String &scheduleId,
        const String &error)>;

    /**
     * @brief Callback for schedule received event (MQTT)
     *
     * @param action Action type (create/update/delete/sync)
     * @param scheduleId Schedule ID
     */
    using ScheduleReceivedCallback = std::function<void(
        ScheduleAction action,
        const String &scheduleId)>;

    /**
     * @brief Helper functions to convert enums to/from chars and strings
     */
    inline ScheduleType parseScheduleType(char c)
    {
        switch (c)
        {
        case 'R':
            return ScheduleType::RECURRING;
        case 'O':
            return ScheduleType::ONCE;
        case 'I':
            return ScheduleType::INTERVAL;
        default:
            return ScheduleType::RECURRING;
        }
    }

    inline RegisterType parseRegisterType(char c)
    {
        switch (c)
        {
        case 'H':
            return RegisterType::REG_HOLDING;
        case 'C':
            return RegisterType::REG_COIL;
        case 'I':
            return RegisterType::REG_INPUT;
        case 'D':
            return RegisterType::REG_DISCRETE;
        default:
            return RegisterType::REG_HOLDING;
        }
    }

    inline ActionType parseActionType(char c)
    {
        switch (c)
        {
        case 'S':
            return ActionType::SET;
        case 'T':
            return ActionType::TOGGLE;
        default:
            return ActionType::SET;
        }
    }

    inline ScheduleAction parseScheduleAction(char c)
    {
        switch (c)
        {
        case 'c':
            return ScheduleAction::CREATE;
        case 'u':
            return ScheduleAction::UPDATE;
        case 'd':
            return ScheduleAction::DELETE;
        case 's':
            return ScheduleAction::SYNC;
        default:
            return ScheduleAction::CREATE;
        }
    }

    inline const char *scheduleTypeToString(ScheduleType type)
    {
        switch (type)
        {
        case ScheduleType::RECURRING:
            return "Recurring";
        case ScheduleType::ONCE:
            return "Once";
        case ScheduleType::INTERVAL:
            return "Interval";
        default:
            return "Unknown";
        }
    }

    inline const char *executionStatusToString(ExecutionStatus status)
    {
        switch (status)
        {
        case ExecutionStatus::SUCCESS:
            return "success";
        case ExecutionStatus::FAILED:
            return "failed";
        case ExecutionStatus::PENDING:
            return "pending";
        case ExecutionStatus::SKIPPED:
            return "skipped";
        default:
            return "unknown";
        }
    }

} // namespace ZenoPCB

#endif // ZENOPCB_SCHEDULE_TYPES_H
