// Phase 7 Plan 07-06.6 — TU guard for ZENOPCB_MICRO_BASIC profile.
#if !defined(ZENOPCB_DISABLE_ALARM)

#include "AlarmEngine.h"
#include "../core/ZenoPCBDebug.h"
#include "../core/TimeManager.h"
#include <math.h>

#ifndef ZENOPCB_DEBUG_ALARM
#define ZENOPCB_DEBUG_ALARM ZENOPCB_DEBUG
#endif

#if ZENOPCB_DEBUG_ALARM
#define ZENO_LOG_ALARM(fmt, ...) ZENO_LOG("Alarm", fmt, ##__VA_ARGS__)
#else
#define ZENO_LOG_ALARM(fmt, ...)
#endif

// Verbose alarm logs — only when ZENOPCB_DEBUG_VERBOSE=1
#if ZENOPCB_DEBUG_VERBOSE
#define ZENO_LOG_ALARM_V(fmt, ...) ZENO_LOG("Alarm", fmt, ##__VA_ARGS__)
#else
#define ZENO_LOG_ALARM_V(fmt, ...)
#endif

namespace ZenoPCB
{

    // Epsilon for floating point comparison
    static constexpr double ALARM_EPSILON = 0.0001;

    // ============================================
    // Constructor
    // ============================================
    AlarmEngine::AlarmEngine()
        : _ruleCount(0), _debugEnabled(false),
          _triggerCallback(nullptr), _notifyCallback(nullptr)
    {
        memset(_rules, 0, sizeof(_rules));
    }

    // ============================================
    // Rule Management
    // ============================================

    int AlarmEngine::syncRules(const JsonArray &rules)
    {
        // Snapshot existing cooldown state keyed by rule id.
        // This preserves active cooldowns across MQTT reconnect re-syncs so alarm
        // does not double-fire every time the server re-delivers the retained config.
        uint32_t savedMs[MAX_ALARM_RULES] = {};
        char savedId[MAX_ALARM_RULES][ALARM_ID_LEN] = {};
        int savedCount = _ruleCount;
        for (int i = 0; i < _ruleCount; i++)
        {
            strncpy(savedId[i], _rules[i].id, ALARM_ID_LEN);
            savedMs[i] = _rules[i].lastTriggeredMs;
        }

        // Full sync — replace all
        _ruleCount = 0;
        memset(_rules, 0, sizeof(_rules));

        if (rules.isNull())
        {
            ZENO_LOG_ALARM("Sync: cleared all rules (null array)");
            return 0;
        }

        for (JsonObject r : rules)
        {
            if (_ruleCount >= MAX_ALARM_RULES)
            {
                ZENO_LOG_ALARM("⚠️ Max rules reached (%d), ignoring rest", MAX_ALARM_RULES);
                break;
            }

            const char *id = r["id"];
            const char *key = r["k"];
            if (!id || !key)
            {
                ZENO_LOG_ALARM("⚠️ Skipping rule with missing id/key");
                continue;
            }

            AlarmRule *rule = &_rules[_ruleCount];

            strncpy(rule->id, id, ALARM_ID_LEN - 1);
            rule->id[ALARM_ID_LEN - 1] = '\0';
            strncpy(rule->key, key, ALARM_KEY_LEN - 1);
            rule->key[ALARM_KEY_LEN - 1] = '\0';

            rule->condition = r["c"] | 0;
            rule->severity = r["s"] | 2;
            rule->cooldownSec = r["cd"] | 60;
            rule->enabled = r["en"] | true;

            // Restore cooldown state if this rule already existed (same id).
            // This prevents double-fire when server re-sends retained config on reconnect.
            rule->lastTriggeredMs = 0;
            for (int i = 0; i < savedCount; i++)
            {
                if (strcmp(savedId[i], id) == 0)
                {
                    rule->lastTriggeredMs = savedMs[i];
                    ZENO_LOG_ALARM("  ↻ Rule [%s] cooldown restored (%ums elapsed)",
                                   id, millis() - savedMs[i]);
                    break;
                }
            }

            if (rule->condition == 6 || rule->condition == 7)
            {
                rule->lower = r["lo"] | 0.0;
                rule->upper = r["hi"] | 0.0;
            }
            else
            {
                rule->value = r["v"] | 0.0;
            }

            _ruleCount++;
        }

        ZENO_LOG_ALARM("✅ Synced %d rules", _ruleCount);

        // Log each rule's cooldown for diagnosis
        for (int i = 0; i < _ruleCount; i++)
        {
            const AlarmRule *r = &_rules[i];
            ZENO_LOG_ALARM("  Rule[%d] id=%s key=%s cd=%us", i, r->id, r->key, r->cooldownSec);
        }

        return _ruleCount;
    }

    bool AlarmEngine::deleteRule(const char *ruleId)
    {
        for (int i = 0; i < _ruleCount; i++)
        {
            if (strcmp(_rules[i].id, ruleId) == 0)
            {
                // Shift remaining rules
                for (int j = i; j < _ruleCount - 1; j++)
                {
                    _rules[j] = _rules[j + 1];
                }
                _ruleCount--;
                memset(&_rules[_ruleCount], 0, sizeof(AlarmRule));

                ZENO_LOG_ALARM("🗑️ Removed rule: %s (remaining: %d)", ruleId, _ruleCount);
                return true;
            }
        }

        ZENO_LOG_ALARM("⚠️ Rule not found for delete: %s", ruleId);
        return false;
    }

    void AlarmEngine::clearAll()
    {
        _ruleCount = 0;
        memset(_rules, 0, sizeof(_rules));
        ZENO_LOG_ALARM("Cleared all rules");
    }

    const AlarmRule *AlarmEngine::getRule(int index) const
    {
        if (index < 0 || index >= _ruleCount)
            return nullptr;
        return &_rules[index];
    }

    // ============================================
    // Condition Evaluation
    // ============================================

    void AlarmEngine::checkAlarms(const char *key, double currentValue)
    {
        // Cooldown uses millis() — never depends on NTP.
        // Event timestamp uses NTP epoch (0 if not yet synced — backend ignores ts=0).
        uint32_t nowMs = millis();

        ZENO_LOG_ALARM_V("🔍 checkAlarms: key=%s value=%.4f rules=%d", key, currentValue, _ruleCount);

        for (int i = 0; i < _ruleCount; i++)
        {
            AlarmRule *rule = &_rules[i];

            // Skip disabled or non-matching key
            if (!rule->enabled)
            {
                ZENO_LOG_ALARM_V("  [%s] skip: disabled", rule->id);
                continue;
            }
            if (strcmp(rule->key, key) != 0)
                continue;

            ZENO_LOG_ALARM_V("  [%s] match key=%s c=%d v=%.4f", rule->id, key, rule->condition, rule->value);

            // Cooldown check using millis() — safe unsigned arithmetic handles 49-day overflow
            if (rule->lastTriggeredMs > 0)
            {
                uint32_t elapsedMs = nowMs - rule->lastTriggeredMs; // unsigned subtraction: safe across overflow
                uint32_t elapsedSec = elapsedMs / 1000;
                if (elapsedSec < rule->cooldownSec)
                {
                    ZENO_LOG_ALARM("  [%s] cooldown: %us / %us remaining",
                                   rule->id, elapsedSec, rule->cooldownSec - elapsedSec);
                    continue; // Still in cooldown
                }
            }

            // Evaluate condition
            bool triggered = _evaluateCondition(
                rule->condition, currentValue,
                rule->value, rule->lower, rule->upper);

            ZENO_LOG_ALARM_V("  [%s] eval: %.4f c=%d %.4f → %s",
                             rule->id, currentValue, rule->condition, rule->value,
                             triggered ? "TRIGGERED" : "no");

            if (triggered)
            {
                ZENO_LOG_ALARM("🔔 TRIGGERED: %s=%.2f (rule:%s, sev:%d)",
                               key, currentValue, rule->id, rule->severity);

                // Build alarm event
                AlarmEvent event;
                memset(&event, 0, sizeof(event));
                strncpy(event.ruleId, rule->id, ALARM_ID_LEN - 1);
                strncpy(event.key, key, ALARM_KEY_LEN - 1);
                event.currentValue = currentValue;
                event.timestamp = _getTimestamp(); // NTP epoch if synced, else 0

                // Trigger publish callback (ZenoPCB.cpp handles MQTT publish).
                // Only start cooldown if the event was actually delivered.
                // If not delivered (e.g. MQTT disconnected), alarm will retry on next check.
                bool delivered = true; // assume delivered if no callback registered
                if (_triggerCallback)
                {
                    delivered = _triggerCallback(event);
                }

                if (delivered)
                {
                    rule->lastTriggeredMs = nowMs; // start cooldown via millis()
                    ZENO_LOG_ALARM("  ✅ Delivered — cooldown started (%us for rule %s)", rule->cooldownSec, rule->id);
                }
                else
                {
                    ZENO_LOG_ALARM("  ⚠️ Delivery failed — cooldown NOT started, will retry");
                }

                // Always fire user notification callback (for local LED/buzzer etc.)
                if (_notifyCallback)
                {
                    _notifyCallback(String(rule->id), String(key), currentValue, rule->severity);
                }
            }
        }
    }

    void AlarmEngine::checkAlarmsFromJson(const String &telemetryJson)
    {
        if (_ruleCount == 0)
            return;

        JsonDocument doc;
        DeserializationError err = deserializeJson(doc, telemetryJson);
        if (err)
        {
            ZENO_LOG_ALARM("⚠️ JSON parse error: %s", err.c_str());
            return;
        }

        JsonObject obj = doc.as<JsonObject>();
        for (JsonPair kv : obj)
        {
            const char *key = kv.key().c_str();
            if (kv.value().is<double>() || kv.value().is<int>() || kv.value().is<float>())
            {
                double value = kv.value().as<double>();
                checkAlarms(key, value);
            }
        }
    }

    // ============================================
    // Private: Evaluate Condition
    // ============================================

    bool AlarmEngine::_evaluateCondition(uint8_t condition, double current,
                                         double threshold, double lower, double upper)
    {
        switch (condition)
        {
        case 0:
            return current > threshold; // GT
        case 1:
            return current < threshold; // LT
        case 2:
            return fabs(current - threshold) < ALARM_EPSILON; // EQ
        case 3:
            return fabs(current - threshold) >= ALARM_EPSILON; // NEQ
        case 4:
            return current >= threshold; // GTE
        case 5:
            return current <= threshold; // LTE
        case 6:
            return current >= lower && current <= upper; // BETWEEN
        case 7:
            return current < lower || current > upper; // OUTSIDE
        default:
            return false;
        }
    }

    // ============================================
    // Private: Get Timestamp
    // ============================================

    uint32_t AlarmEngine::_getTimestamp()
    {
        // Returns NTP-synced UTC epoch for use in alarm event timestamp.
        // Returns 0 if not synced — backend should treat ts=0 as "time unknown".
        // NOT used for cooldown logic (cooldown uses millis()).
        if (TimeManager::isSynced())
        {
            return (uint32_t)TimeManager::getUTC();
        }
        return 0;
    }

    // ============================================
    // Debug: Print Rules
    // ============================================

    void AlarmEngine::printRules() const
    {
        ZENO_LOG_ALARM("======= ALARM RULES (%d) =======", _ruleCount);
        for (int i = 0; i < _ruleCount; i++)
        {
            const AlarmRule *r = &_rules[i];
            if (r->condition == 6 || r->condition == 7)
            {
                ZENO_LOG_ALARM("  [%d] id=%s key=%s c=%d sev=%d cd=%d lo=%.2f hi=%.2f en=%d",
                               i, r->id, r->key, r->condition, r->severity,
                               r->cooldownSec, r->lower, r->upper, r->enabled);
            }
            else
            {
                ZENO_LOG_ALARM("  [%d] id=%s key=%s c=%d sev=%d cd=%d v=%.2f en=%d",
                               i, r->id, r->key, r->condition, r->severity,
                               r->cooldownSec, r->value, r->enabled);
            }
        }
        ZENO_LOG_ALARM("================================");
    }

} // namespace ZenoPCB

#endif // !ZENOPCB_DISABLE_ALARM
