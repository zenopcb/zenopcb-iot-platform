#ifndef ZENOPCB_IRRIGATION_TYPES_H
#define ZENOPCB_IRRIGATION_TYPES_H

#include <Arduino.h>
#include <functional>

namespace ZenoPCB
{

    // ============================================
    // Compile-time limits (override via build_flags)
    // ============================================

#ifndef MAX_IRRIGATION_STEPS
#define MAX_IRRIGATION_STEPS 15
#endif

#ifndef MAX_IRRIGATION_TARGETS
#define MAX_IRRIGATION_TARGETS 4
#endif

#ifndef MAX_IRRIGATION_SCENARIOS
#define MAX_IRRIGATION_SCENARIOS 5
#endif

#ifndef MAX_IRRIGATION_SCHEDULES
#define MAX_IRRIGATION_SCHEDULES 16
#endif

#define IRRIGATION_KEY_LEN 51  // Max MQTT key length (50 chars + null)
#define IRRIGATION_SID_LEN 9   // 8 chars + null (first 8 of UUID)
#define IRRIGATION_SCHID_LEN 9 // 8 chars + null (schedule ID)
#define IRRIGATION_EID_LEN 16  // base36 timestamp (~9 chars + null)

    // ============================================
    // Action codes — V2 protocol
    // ============================================

    enum class IrrigationAction : uint8_t
    {
        OPEN_VALVE = 0,  // "ov" — write 1 to targets
        CLOSE_VALVE = 1, // "cv" — write 0 to targets
        START_PUMP = 2,  // "sp" — write 1 to targets
        STOP_PUMP = 3,   // "tp" — write 0 to targets
        WAIT = 4         // "w"  — pause dur seconds
    };

    inline IrrigationAction parseIrrigationAction(const char *code)
    {
        if (!code)
            return IrrigationAction::WAIT;
        if (code[0] == 'o' && code[1] == 'v')
            return IrrigationAction::OPEN_VALVE;
        if (code[0] == 'c' && code[1] == 'v')
            return IrrigationAction::CLOSE_VALVE;
        if (code[0] == 's' && code[1] == 'p')
            return IrrigationAction::START_PUMP;
        if (code[0] == 't' && code[1] == 'p')
            return IrrigationAction::STOP_PUMP;
        return IrrigationAction::WAIT;
    }

    inline const char *irrigationActionToCode(IrrigationAction action)
    {
        switch (action)
        {
        case IrrigationAction::OPEN_VALVE:
            return "ov";
        case IrrigationAction::CLOSE_VALVE:
            return "cv";
        case IrrigationAction::START_PUMP:
            return "sp";
        case IrrigationAction::STOP_PUMP:
            return "tp";
        case IrrigationAction::WAIT:
            return "w";
        default:
            return "w";
        }
    }

    inline int irrigationActionValue(IrrigationAction action)
    {
        switch (action)
        {
        case IrrigationAction::OPEN_VALVE:
            return 1;
        case IrrigationAction::CLOSE_VALVE:
            return 0;
        case IrrigationAction::START_PUMP:
            return 1;
        case IrrigationAction::STOP_PUMP:
            return 0;
        default:
            return 0;
        }
    }

    inline bool isWaitAction(IrrigationAction action)
    {
        return action == IrrigationAction::WAIT;
    }

    // ============================================
    // Execution status
    // ============================================

    enum class IrrigationStatus : uint8_t
    {
        IDLE = 0,
        RUNNING = 1,
        COMPLETED = 2,
        ERROR = 3
    };

    inline const char *irrigationStatusToString(IrrigationStatus status)
    {
        switch (status)
        {
        case IrrigationStatus::IDLE:
            return "idle";
        case IrrigationStatus::RUNNING:
            return "running";
        case IrrigationStatus::COMPLETED:
            return "completed";
        case IrrigationStatus::ERROR:
            return "error";
        default:
            return "unknown";
        }
    }

    // ============================================
    // State machine phase (executor internal)
    // ============================================

    enum class StepPhase : uint8_t
    {
        IDLE = 0,
        EXECUTING = 1,
        WAITING = 2
    };

    // ============================================
    // Schedule type (for auto-execute scenarios)
    // ============================================

    enum class IrrigationScheduleType : char
    {
        NONE = 'N',      // No schedule — manual execute only
        RECURRING = 'R', // Repeat on specific days at specific time
        ONCE = 'O'       // Execute once at specific timestamp
    };

    inline IrrigationScheduleType parseIrrigationScheduleType(char c)
    {
        switch (c)
        {
        case 'R':
            return IrrigationScheduleType::RECURRING;
        case 'O':
            return IrrigationScheduleType::ONCE;
        default:
            return IrrigationScheduleType::NONE;
        }
    }

    // ============================================
    // ISO day conversion: ISO 1=Mon..7=Sun → tm_wday 0=Sun..6=Sat
    // ============================================

    inline uint8_t isoToTmWday(uint8_t isoDay)
    {
        // ISO: 1=Mon,2=Tue,...,7=Sun → tm_wday: 0=Sun,1=Mon,...,6=Sat
        return (isoDay == 7) ? 0 : isoDay;
    }

    // ============================================
    // Step data structure
    // ============================================

    struct IrrigationStep
    {
        uint8_t order;                                             // 1-based step order
        IrrigationAction action;                                   // Action code
        char mqttKeys[MAX_IRRIGATION_TARGETS][IRRIGATION_KEY_LEN]; // Multi-target keys
        uint8_t keyCount;                                          // 0 for WAIT steps
        uint32_t waitDuration;                                     // Seconds (WAIT only)

        IrrigationStep()
        {
            memset(this, 0, sizeof(IrrigationStep));
        }
    };

    // ============================================
    // Execution context (RAM-only, for active run)
    // ============================================

    struct IrrigationExecution
    {
        char scenarioId[IRRIGATION_SID_LEN];
        char executionId[IRRIGATION_EID_LEN];
        uint32_t timestamp;
        IrrigationStep steps[MAX_IRRIGATION_STEPS];
        uint8_t stepCount;
        uint8_t currentStep; // 0-based index into steps[]
        IrrigationStatus status;
        uint32_t startTimeMs; // millis() when execution started

        IrrigationExecution()
        {
            memset(this, 0, sizeof(IrrigationExecution));
            status = IrrigationStatus::IDLE;
        }

        void reset()
        {
            memset(this, 0, sizeof(IrrigationExecution));
            status = IrrigationStatus::IDLE;
        }
    };

    // ============================================
    // Scenario config (stored in LittleFS)
    // V3: Scenario only has steps, no schedule info
    // ============================================

    struct IrrigationScenarioConfig
    {
        char scenarioId[IRRIGATION_SID_LEN]; // 8 chars + null
        IrrigationStep steps[MAX_IRRIGATION_STEPS];
        uint8_t stepCount;
        bool enabled;

        IrrigationScenarioConfig()
        {
            memset(this, 0, sizeof(IrrigationScenarioConfig));
        }
    };

    // ============================================
    // Schedule config (stored in LittleFS, separate entity)
    // V3: Schedule has own ID, references scenario by sid
    // ============================================

    struct IrrigationScheduleConfig
    {
        char scheduleId[IRRIGATION_SCHID_LEN]; // 8 chars + null
        char scenarioId[IRRIGATION_SID_LEN];   // Reference to scenario
        bool enabled;
        IrrigationScheduleType scheduleType;
        char executeTime[6];   // "HH:mm" (UTC), null-terminated
        uint8_t repeatDays[7]; // ISO: 1=Mon...7=Sun
        uint8_t repeatDaysCount;
        uint32_t executeAt; // Unix timestamp (ONCE type)

        // Runtime state (NOT persisted)
        uint8_t lastTriggeredMin; // Prevent double-trigger in same minute

        IrrigationScheduleConfig()
        {
            memset(this, 0, sizeof(IrrigationScheduleConfig));
            scheduleType = IrrigationScheduleType::NONE;
        }

        bool isRecurring() const
        {
            return scheduleType == IrrigationScheduleType::RECURRING &&
                   repeatDaysCount > 0;
        }

        bool isOnce() const
        {
            return scheduleType == IrrigationScheduleType::ONCE &&
                   executeAt > 0;
        }
    };

    // ============================================
    // Schedule state (per-schedule, in-memory)
    // ============================================

    struct IrrigationScheduleState
    {
        char scheduleId[IRRIGATION_SCHID_LEN];
        uint32_t lastExecutionTime;    // millis()
        time_t lastExecutionTimestamp; // UTC time_t
        bool executedToday;
        IrrigationStatus lastStatus;

        IrrigationScheduleState()
        {
            memset(scheduleId, 0, sizeof(scheduleId));
            lastExecutionTime = 0;
            lastExecutionTimestamp = 0;
            executedToday = false;
            lastStatus = IrrigationStatus::IDLE;
        }

        void markExecutedToday()
        {
            executedToday = true;
            lastExecutionTime = millis();
            lastExecutionTimestamp = time(nullptr);
        }

        void resetDaily()
        {
            executedToday = false;
        }
    };

    // ============================================
    // Message handler result
    // ============================================

    struct IrrigationHandleResult
    {
        bool success;
        char errorMessage[64];
        char scenarioId[IRRIGATION_SID_LEN];
        char scheduleId[IRRIGATION_SCHID_LEN];
        char executionId[IRRIGATION_EID_LEN];
        char action[8]; // Action code for ACK ("ss","ds","sc","dc","fa","execute")
        uint32_t processingMs;
        uint8_t scenarioCount; // For "fa" ACK
        uint8_t scheduleCount; // For "fa" ACK

        IrrigationHandleResult()
        {
            success = false;
            memset(errorMessage, 0, sizeof(errorMessage));
            memset(scenarioId, 0, sizeof(scenarioId));
            memset(scheduleId, 0, sizeof(scheduleId));
            memset(executionId, 0, sizeof(executionId));
            memset(action, 0, sizeof(action));
            processingMs = 0;
            scenarioCount = 0;
            scheduleCount = 0;
        }
    };

    // ============================================
    // Callback typedefs
    // ============================================

    // Called when irrigation needs to write a ZKey value
    // Parameters: mqttKey, value (0 or 1)
    // Returns: true if write succeeded
    using IrrigationWriteCallback = std::function<bool(const char *mqttKey, int value)>;

    // Called on step progress
    // Parameters: scenarioId, executionId, currentStep (1-based), totalSteps, action, keys, keyCount
    using IrrigationStepCallback = std::function<void(
        const char *scenarioId, const char *executionId,
        uint8_t step, uint8_t total,
        IrrigationAction action,
        const IrrigationStep &stepData)>;

    // Called on execution completed
    // Parameters: scenarioId, executionId, totalDurationSec
    using IrrigationCompletedCallback = std::function<void(
        const char *scenarioId, const char *executionId,
        uint32_t totalDurationSec)>;

    // Called on execution error
    // Parameters: scenarioId, executionId, step (1-based), totalSteps, error
    using IrrigationErrorCallback = std::function<void(
        const char *scenarioId, const char *executionId,
        uint8_t step, uint8_t total,
        const char *error)>;

    // Called when scheduler triggers a scenario from a schedule
    // Parameters: scheduleId, scenarioId
    // Returns: true if execution started successfully
    using IrrigationScheduleTriggerCallback = std::function<bool(const char *scheduleId, const char *scenarioId)>;

} // namespace ZenoPCB

#endif // ZENOPCB_IRRIGATION_TYPES_H
