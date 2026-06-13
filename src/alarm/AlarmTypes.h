#ifndef ZENOPCB_ALARM_TYPES_H
#define ZENOPCB_ALARM_TYPES_H

#include <Arduino.h>
#include <functional>

namespace ZenoPCB
{

// Maximum number of alarm rules per device
// Override via build flag: -DMAX_ALARM_RULES=50
#ifndef MAX_ALARM_RULES
#define MAX_ALARM_RULES 20
#endif

// Field length limits
#define ALARM_ID_LEN 9    // 8-char short UUID + null
#define ALARM_KEY_LEN 101 // MQTT key name + null

    /**
     * @brief Alarm condition codes (matches backend enum)
     */
    enum class AlarmCondition : uint8_t
    {
        GREATER_THAN = 0,          // >
        LESS_THAN = 1,             // <
        EQUAL = 2,                 // =
        NOT_EQUAL = 3,             // ≠
        GREATER_THAN_OR_EQUAL = 4, // ≥
        LESS_THAN_OR_EQUAL = 5,    // ≤
        BETWEEN = 6,               // [lo, hi]
        OUTSIDE_RANGE = 7          // ∉[lo, hi]
    };

    /**
     * @brief Alarm severity levels
     */
    enum class AlarmSeverity : uint8_t
    {
        SEVERITY_LOW = 1,
        SEVERITY_WARNING = 2,
        SEVERITY_CRITICAL = 3,
        SEVERITY_EMERGENCY = 4
    };

    /**
     * @brief Single alarm rule (stored in memory)
     *
     * Size: ~145 bytes/rule  × 20 rules = ~2.9 KB RAM
     */
    struct AlarmRule
    {
        char id[ALARM_ID_LEN];   // 8-char short UUID
        char key[ALARM_KEY_LEN]; // MQTT key (telemetry field name)
        uint8_t condition;       // 0-7 (AlarmCondition)
        double value;            // Threshold (condition 0-5)
        double lower;            // Lower threshold (condition 6-7)
        double upper;            // Upper threshold (condition 6-7)
        uint8_t severity;        // 1-4 (AlarmSeverity)
        uint32_t cooldownSec;    // Cooldown in seconds (10-3600)
        bool enabled;
        uint32_t lastTriggeredMs; // millis() at last trigger — for cooldown tracking only
    };

    /**
     * @brief Alarm event (published to backend)
     */
    struct AlarmEvent
    {
        char ruleId[ALARM_ID_LEN];
        char key[ALARM_KEY_LEN];
        double currentValue;
        uint32_t timestamp; // Unix epoch seconds
    };

    /**
     * @brief Callback: alarm rule triggered
     *
     * @param ruleId   Rule short ID (8 chars)
     * @param key      MQTT key that triggered
     * @param value    Current value
     * @param severity Severity level 1-4
     */
    using AlarmTriggeredCallback = std::function<void(
        const String &ruleId,
        const String &key,
        double value,
        uint8_t severity)>;

    /**
     * @brief Callback: alarm config received from backend
     *
     * @param ruleCount Number of rules loaded
     */
    using AlarmConfigCallback = std::function<void(int ruleCount)>;

} // namespace ZenoPCB

#endif // ZENOPCB_ALARM_TYPES_H
