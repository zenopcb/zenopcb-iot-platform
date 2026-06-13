/**
 * @file 02_threshold_range.ino
 * @brief Out-of-range alarm — rule "Z2 < 10 OR Z2 > 90" (configured on cloud).
 *
 * @category Alarm
 * @level Beginner
 *
 * @hardware
 * - Any supported board EXCEPT STM32F103.
 * - Analog sensor (pot/LDR) on SENSOR_PIN.
 * - Two LEDs (LOW_LED_PIN, HIGH_LED_PIN) to indicate which boundary tripped.
 *   On-board single LED is fine — the sketch logs which side fired anyway.
 *
 * @platform_notes
 * F103 NOT SUPPORTED (Alarm subsystem stripped).
 *
 * @usage
 * 1. Set credentials.
 * 2. Cloud dashboard: create two alarm rules — `Z2 < 10` (rule id "low")
 *    and `Z2 > 90` (rule id "high"). Sketch matches on the inbound ruleId
 *    string to light the matching LED.
 */

#include <ZenoPCBMain.h>

using namespace ZenoPCB;

#define WIFI_SSID "REPLACE_ME"
#define WIFI_PASS "REPLACE_ME"
#define DEVICE_ID "REPLACE_ME"
#define DEVICE_TOKEN "REPLACE_AT_PROVISIONING"

#if defined(ESP32)
  #define SENSOR_PIN 34
  #define LOW_LED_PIN 25
  #define HIGH_LED_PIN 26
  #define ADC_FULL 4095.0f
#elif defined(ESP8266)
  #define SENSOR_PIN A0
  #define LOW_LED_PIN 5
  #define HIGH_LED_PIN 4
  #define ADC_FULL 1023.0f
#elif defined(ARDUINO_UNOR4_WIFI)
  #define SENSOR_PIN A0
  #define LOW_LED_PIN 2
  #define HIGH_LED_PIN 3
  #define ADC_FULL 16383.0f
#elif defined(STM32F4)
  #define SENSOR_PIN PA0
  #define LOW_LED_PIN PB0
  #define HIGH_LED_PIN PB1
  #define ADC_FULL 4095.0f
#elif defined(STM32F1)
  #define SENSOR_PIN PA0
  #define LOW_LED_PIN PB12
  #define HIGH_LED_PIN PB13
  #define ADC_FULL 4095.0f
#endif

Zeno zeno;

ZENO_READ_ALL
{
    const float pct = (float)analogRead(SENSOR_PIN) / ADC_FULL * 100.0f;
    ZENO_WRITE(Z2, pct);
}

void onAlarmTriggered(const String &ruleId, const String &key,
                      double value, uint8_t severity)
{
    ZENOPCB_PRINTF("[ALARM] rule=%s key=%s value=%.2f sev=%u\n",
                   ruleId.c_str(), key.c_str(), value, (unsigned)severity);
    if (ruleId.indexOf("low") >= 0)
    {
        digitalWrite(LOW_LED_PIN, HIGH);
    }
    else if (ruleId.indexOf("high") >= 0)
    {
        digitalWrite(HIGH_LED_PIN, HIGH);
    }
}

void setup()
{
    Serial.begin(115200);

#if defined(STM32F1)
    Serial.println(F("[INFO] Alarm not available on STM32F103 (MICRO_BASIC)."));
    return;
#endif

    pinMode(LOW_LED_PIN, OUTPUT);
    pinMode(HIGH_LED_PIN, OUTPUT);
    digitalWrite(LOW_LED_PIN, LOW);
    digitalWrite(HIGH_LED_PIN, LOW);

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
