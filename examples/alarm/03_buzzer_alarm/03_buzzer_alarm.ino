/**
 * @file 03_buzzer_alarm.ino
 * @brief Active buzzer fires when a cloud alarm rule trips on Z2.
 *
 * @category Alarm
 * @level Beginner
 *
 * @hardware
 * - Any supported board EXCEPT STM32F103.
 * - Active buzzer (3.3 V or 5 V) on BUZZER_PIN.
 * - Pot or LDR on SENSOR_PIN (or any analog source to drive Z2).
 *
 * @platform_notes
 * F103 NOT SUPPORTED (Alarm subsystem stripped under MICRO_BASIC).
 *
 * @usage
 * 1. Set credentials.
 * 2. Cloud dashboard: create rule on Z2 (e.g. `> 70`).
 * 3. When the alarm trips, buzzer pulses ON for 3 s then auto-clears.
 */

#include <ZenoPCBMain.h>

using namespace ZenoPCB;

#define WIFI_SSID "REPLACE_ME"
#define WIFI_PASS "REPLACE_ME"
#define DEVICE_ID "REPLACE_ME"
#define DEVICE_TOKEN "REPLACE_AT_PROVISIONING"

#if defined(ESP32)
  #define SENSOR_PIN 34
  #define BUZZER_PIN 25
  #define ADC_FULL 4095.0f
#elif defined(ESP8266)
  #define SENSOR_PIN A0
  #define BUZZER_PIN 5
  #define ADC_FULL 1023.0f
#elif defined(ARDUINO_UNOR4_WIFI)
  #define SENSOR_PIN A0
  #define BUZZER_PIN 8
  #define ADC_FULL 16383.0f
#elif defined(STM32F4)
  #define SENSOR_PIN PA0
  #define BUZZER_PIN PB0
  #define ADC_FULL 4095.0f
#elif defined(STM32F1)
  #define SENSOR_PIN PA0
  #define BUZZER_PIN PB12
  #define ADC_FULL 4095.0f
#endif

Zeno zeno;
static uint32_t s_buzzerOffMs = 0;
static const uint32_t PULSE_MS = 3000;

ZENO_READ_ALL
{
    const float pct = (float)analogRead(SENSOR_PIN) / ADC_FULL * 100.0f;
    ZENO_WRITE(Z2, pct);
}

void onAlarmTriggered(const String &ruleId, const String &key,
                      double value, uint8_t severity)
{
    ZENOPCB_PRINTF("[ALARM->BUZZER] %s %s=%.2f\n",
                   ruleId.c_str(), key.c_str(), value);
    digitalWrite(BUZZER_PIN, HIGH);
    s_buzzerOffMs = millis() + PULSE_MS;
}

void setup()
{
    Serial.begin(115200);

#if defined(STM32F1)
    Serial.println(F("[INFO] Alarm not available on STM32F103 (MICRO_BASIC)."));
    return;
#endif

    pinMode(BUZZER_PIN, OUTPUT);
    digitalWrite(BUZZER_PIN, LOW);

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
    if (s_buzzerOffMs != 0 && millis() >= s_buzzerOffMs)
    {
        s_buzzerOffMs = 0;
        digitalWrite(BUZZER_PIN, LOW);
    }
    zeno.loop();
#endif
}
