#ifndef ZENOPCB_ALARM_ENGINE_H
#define ZENOPCB_ALARM_ENGINE_H

#include "AlarmTypes.h"
#include "../vendor/ArduinoJson/ArduinoJson.h"  // ArduinoJson API from vendored copy (namespace ZenoJson; see vendor/ArduinoJson/LICENSE.md)

namespace ZenoPCB
{

    /**
     * @brief Edge Alarm Engine — evaluates alarm rules locally on device
     *
     * Responsibilities:
     * - Store alarm rules received from backend
     * - Evaluate conditions against current telemetry values
     * - Manage cooldown per rule
     * - Generate alarm events for publishing
     *
     * Memory: ~3KB (20 rules × 145 bytes + overhead)
     */
    class AlarmEngine
    {
    public:
        AlarmEngine();

        // ============================================
        // Rule Management
        // ============================================

        /**
         * @brief Full sync — replace all rules with new set
         * @param rules JsonArray from backend config payload "r"
         * @return Number of rules loaded
         */
        int syncRules(const JsonArray &rules);

        /**
         * @brief Delete a single rule by ID
         * @param ruleId 8-char short UUID
         * @return true if found and removed
         */
        bool deleteRule(const char *ruleId);

        /**
         * @brief Clear all rules
         */
        void clearAll();

        /**
         * @brief Get current rule count
         */
        int getRuleCount() const { return _ruleCount; }

        /**
         * @brief Get rule by index (for debug/display)
         */
        const AlarmRule *getRule(int index) const;

        // ============================================
        // Condition Evaluation
        // ============================================

        /**
         * @brief Check all alarm conditions for a given key + value
         *
         * Called when telemetry data is ready to publish.
         * For each triggered alarm, calls the publish callback.
         *
         * @param key   MQTT key name (e.g., "Z0", "temperature")
         * @param value Current value
         */
        void checkAlarms(const char *key, double value);

        /**
         * @brief Check alarms against a full JSON telemetry object
         *
         * Iterates all keys in the JSON and checks each against stored rules.
         *
         * @param telemetryJson JSON string like {"Z0":1,"Z1":25.5}
         */
        void checkAlarmsFromJson(const String &telemetryJson);

        // ============================================
        // Callbacks
        // ============================================

        /**
         * @brief Set callback for when an alarm triggers
         *
         * The callback receives the AlarmEvent and returns true if the event was
         * successfully delivered (e.g., published to MQTT). Returning false prevents
         * the cooldown timer from starting — the alarm will retry on the next check.
         */
        void onAlarmTriggered(std::function<bool(const AlarmEvent &event)> callback)
        {
            _triggerCallback = callback;
        }

        /**
         * @brief Set user-facing callback for alarm notifications
         */
        void onAlarmNotify(AlarmTriggeredCallback callback)
        {
            _notifyCallback = callback;
        }

        // ============================================
        // Debug
        // ============================================
        void setDebugEnabled(bool enabled) { _debugEnabled = enabled; }
        void printRules() const;

    private:
        AlarmRule _rules[MAX_ALARM_RULES];
        int _ruleCount;
        bool _debugEnabled;

        std::function<bool(const AlarmEvent &event)> _triggerCallback;
        AlarmTriggeredCallback _notifyCallback;

        /**
         * @brief Evaluate a single condition
         */
        static bool _evaluateCondition(uint8_t condition, double current,
                                       double threshold, double lower, double upper);

        /**
         * @brief Get current epoch timestamp (from TimeManager or millis fallback)
         */
        static uint32_t _getTimestamp();
    };

} // namespace ZenoPCB

#endif // ZENOPCB_ALARM_ENGINE_H
