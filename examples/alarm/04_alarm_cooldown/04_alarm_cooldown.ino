/**
 * @file 04_alarm_cooldown.ino
 * @brief Demonstrate per-rule cooldown — rate-limit alarm-triggered actions.
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
 * 1. Set credentials.
 * 2. Cloud dashboard: create alarm rule on Z2 with a 60 s cooldown
 *    configured in the rule editor. The Alarm engine enforces cooldown
 *    cloud-side per rule.
 * 3. This sketch also enforces a *device-side* cooldown wrapper as a
 *    second layer — useful when the rule cooldown is permissive but the
 *    physical actuator (e.g. siren) must not fire faster than every N
 *    seconds regardless.
 */

#include <ZenoPCBMain.h>

using namespace ZenoPCB;

#define WIFI_SSID "REPLACE_ME"
#define WIFI_PASS "REPLACE_ME"
#define DEVICE_ID "REPLACE_ME"
#define DEVICE_TOKEN "REPLACE_AT_PROVISIONING"

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

static const uint32_t DEVICE_COOLDOWN_MS = 60000UL; // 1 minute

Zeno zeno;
static uint32_t s_lastFireMs = 0;
static uint32_t s_firedCount = 0;
static uint32_t s_skippedCount = 0;

ZENO_READ_ALL
{
    const float pct = (float)analogRead(SENSOR_PIN) / ADC_FULL * 100.0f;
    ZENO_WRITE(Z2, pct);
}

void onAlarmTriggered(const String &ruleId, const String &key,
                      double value, uint8_t severity)
{
    const uint32_t now = millis();
    if (s_lastFireMs != 0 && (now - s_lastFireMs) < DEVICE_COOLDOWN_MS)
    {
        ++s_skippedCount;
        ZENOPCB_PRINTF("[ALARM] %s SKIPPED (cooldown), %lu remaining ms\n",
                       ruleId.c_str(),
                       (unsigned long)(DEVICE_COOLDOWN_MS - (now - s_lastFireMs)));
        return;
    }
    s_lastFireMs = now;
    ++s_firedCount;
    digitalWrite(LED_PIN, HIGH);
    delay(100);                  // a brief pulse (acceptable here — not in main loop)
    digitalWrite(LED_PIN, LOW);
    ZENOPCB_PRINTF("[ALARM] %s FIRED #%lu (%lu skipped so far)\n",
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
        .setZPublishInterval(3000)
        .onZKeyRead(_zenoReadAll)
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
