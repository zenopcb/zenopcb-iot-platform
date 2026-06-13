/**
 * @file 01_threshold_high.ino
 * @brief Cloud alarm rule "Z2 > 80" — local LED lights when threshold trips.
 *
 * @category Alarm
 * @level Beginner
 *
 * @hardware
 * - Any supported board EXCEPT STM32F103 (alarm subsystem stripped under
 *   MICRO_BASIC).
 * - Pot or LDR on SENSOR_PIN.
 * - LED on LED_PIN (built-in is fine).
 *
 * @platform_notes
 * F103 NOT SUPPORTED — Alarm subsystem is compile-stripped in MICRO_BASIC.
 * Sketch early-returns on F103 with a Serial notice.
 *
 * @usage
 * 1. Set credentials.
 * 2. Cloud dashboard: create an alarm rule on key Z2 with condition `> 80`.
 * 3. Turn the pot toward max — Z2 publishes 0..100%, alarm trips, LED ON.
 * 4. Below threshold the LED clears automatically (cleared after 5 s of
 *    no further trigger).
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

Zeno zeno;
static uint32_t s_alarmLitUntilMs = 0;
static const uint32_t LIT_MS = 5000;

ZENO_READ_ALL
{
    const float pct = (float)analogRead(SENSOR_PIN) / ADC_FULL * 100.0f;
    ZENO_WRITE(Z2, pct);
}

void onAlarmTriggered(const String &ruleId, const String &key,
                      double value, uint8_t severity)
{
    ZENOPCB_PRINTF("[ALARM] %s %s=%.2f sev=%u\n",
                   ruleId.c_str(), key.c_str(), value, (unsigned)severity);
    digitalWrite(LED_PIN, HIGH);
    s_alarmLitUntilMs = millis() + LIT_MS;
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
    if (s_alarmLitUntilMs != 0 && millis() >= s_alarmLitUntilMs)
    {
        s_alarmLitUntilMs = 0;
        digitalWrite(LED_PIN, LOW);
    }
    zeno.loop();
#endif
}
