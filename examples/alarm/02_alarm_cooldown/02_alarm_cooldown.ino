/**
 * @file 02_alarm_cooldown.ino
 * @brief Rate-limit an alarm-triggered actuator: ignore further trips for
 *        60 seconds after firing.
 *
 * What you'll learn:
 *   - Defending hardware from "alarm storms" with a device-side cooldown.
 *   - Why you might want a device cooldown even when the cloud already has one.
 *   - Pattern: store last-fire time, compute elapsed, skip if too soon.
 *
 * Hardware needed:
 *   - Any supported board EXCEPT STM32F103.
 *   - Analog sensor (pot/LDR) on SENSOR_PIN.
 *   - LED on LED_PIN (built-in is fine — substitute a relay/siren/buzzer in
 *     a real deployment).
 *
 * Wiring:
 *   - Pot wiper -> SENSOR_PIN; pot ends -> 3V3 and GND.
 *   - LED on LED_PIN (built-in works).
 *
 * Cloud dashboard setup:
 *   - Create Z2 of type Float — sensor reading (0..100%).
 *   - Create an alarm rule on Z2. You can ALSO set a server-side cooldown on
 *     the rule; the device cooldown below is a second, independent layer.
 *
 * @category Alarm
 * @level Intermediate
 *
 * @hardware
 * - Any supported board EXCEPT STM32F103.
 * - Analog sensor on SENSOR_PIN to feed Z2.
 *
 * @platform_notes
 * F103 NOT SUPPORTED (Alarm subsystem stripped under MICRO_BASIC).
 *
 * @usage
 * 1. Replace WIFI_SSID / WIFI_PASS / DEVICE_ID / DEVICE_TOKEN below.
 * 2. ESP32 only: Tools > Partition Scheme > "Minimal SPIFFS (1.9MB APP)".
 * 3. Cloud dashboard: create the alarm rule on Z2.
 * 4. Flash the board.
 * 5. Open Serial Monitor at 115200 baud.
 * 6. Move the pot to keep the alarm tripping. Only the first fire per 60 s
 *    window pulses the LED; later trips log "SKIPPED (cooldown)".
 *
 * Tips & common mistakes:
 *   - Two layers of cooldown are a feature, not a bug. The cloud cooldown
 *     protects upstream consumers (notifications, billing); the device
 *     cooldown protects the local actuator (siren, valve, motor) from rapid
 *     cycling that could damage it or annoy a user.
 *   - delay(100) inside the callback is acceptable here because we're not
 *     in the main loop and the actuator pulse is short. In time-critical
 *     code prefer a millis() pulse like sketch 03.
 */

#include <ZenoPCBMain.h>

using namespace ZenoPCB;

// ---- Credentials ------------------------------------------------------------
#define WIFI_SSID "REPLACE_ME"
#define WIFI_PASS "REPLACE_ME"
#define DEVICE_ID "REPLACE_ME"
#define DEVICE_TOKEN "REPLACE_AT_PROVISIONING"

// ---- Pin map + ADC scale ----------------------------------------------------
#if defined(ESP32)
  #define SENSOR_PIN 34
  #define LED_PIN 2
  #define ADC_FULL 4095.0f
#elif defined(ESP8266)
  #define SENSOR_PIN A0
  #define LED_PIN LED_BUILTIN
  #define ADC_FULL 1023.0f
#elif defined(ARDUINO_UNOR4_WIFI)
  #define SENSOR_PIN A0
  #define LED_PIN LED_BUILTIN
  #define ADC_FULL 16383.0f
#elif defined(STM32F4)
  #define SENSOR_PIN PA0
  #define LED_PIN PA5
  #define ADC_FULL 4095.0f
#elif defined(STM32F1)
  #define SENSOR_PIN PA0
  #define LED_PIN PC13
  #define ADC_FULL 4095.0f
#endif

// Minimum gap between two fire events handled locally. Tune to match how
// often the physical actuator (siren/valve/motor) is safe to cycle.
static const uint32_t DEVICE_COOLDOWN_MS = 60000UL; // 1 minute

Zeno zeno;
static uint32_t s_lastFireMs   = 0; // 0 means "never fired yet"
static uint32_t s_firedCount   = 0;
static uint32_t s_skippedCount = 0;

// Device -> Cloud: publish the sensor every 3 s so the cloud rule has data
// to evaluate.
ZENO_EVERY(3000)
{
    const float pct = (float)analogRead(SENSOR_PIN) / ADC_FULL * 100.0f;
    DEVICE_TO_CLOUD(Z2, pct);
}

// Alarm callback — the cloud can call this rapidly during a sustained
// out-of-range condition. The cooldown below absorbs the noise so the
// physical action only happens once per DEVICE_COOLDOWN_MS window.
void onAlarmTriggered(const String &ruleId, const String &key,
                      double value, uint8_t severity)
{
    const uint32_t now = millis();
    // If we fired recently, skip this trip and just log how long until the
    // next allowed fire.
    if (s_lastFireMs != 0 && (now - s_lastFireMs) < DEVICE_COOLDOWN_MS)
    {
        ++s_skippedCount;
        Serial.printf("[ALARM] %s SKIPPED (cooldown), %lu remaining ms\n",
                       ruleId.c_str(),
                       (unsigned long)(DEVICE_COOLDOWN_MS - (now - s_lastFireMs)));
        return;
    }
    s_lastFireMs = now;
    ++s_firedCount;
    // Brief pulse: short delay is acceptable inside a callback that isn't on
    // the hot loop — keeps the example self-contained without a state machine.
    digitalWrite(LED_PIN, HIGH);
    delay(100); // 100 ms visible blink
    digitalWrite(LED_PIN, LOW);
    Serial.printf("[ALARM] %s FIRED #%lu (%lu skipped so far)\n",
                   ruleId.c_str(), (unsigned long)s_firedCount,
                   (unsigned long)s_skippedCount);
}

void setup()
{
    Serial.begin(115200);

#if defined(STM32F1)
    Serial.println(F("[INFO] Alarm not available on STM32F103 (MICRO_BASIC)."));
    return;
#endif

    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, LOW);

    zeno.wifi(WIFI_SSID, WIFI_PASS)
        .device(DEVICE_ID, DEVICE_TOKEN)
        .enableZKeys()
        .enableAlarm()
        .onAlarmTriggered(onAlarmTriggered)
        .begin();
}

void loop()
{
#if defined(STM32F1)
    return;
#else
    zeno.loop();
#endif
}
