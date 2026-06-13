/**
 * @file 05_analog_read.ino
 * @brief Read an analog sensor (pot / LDR) and publish the value 0..100 to Z2.
 *
 * @category IO
 * @level Beginner
 *
 * @hardware
 * - Any supported board with an ADC pin.
 * - Potentiometer or LDR voltage divider on SENSOR_PIN (3.3 V max input).
 *
 * @wiring
 * - 3.3 V -> potentiometer middle pin -> ADC SENSOR_PIN.
 * - Potentiometer outer pins -> 3.3 V and GND.
 *
 * @usage
 * 1. Set credentials.
 * 2. Turn the pot or shine light on the LDR; Z2 updates every 1 s with a
 *    normalised 0..100 value (cross-platform ADC bit-depth handled below).
 */

#include <ZenoPCBMain.h>

using namespace ZenoPCB;

#define WIFI_SSID "REPLACE_ME"
#define WIFI_PASS "REPLACE_ME"
#define DEVICE_ID "REPLACE_ME"
#define DEVICE_TOKEN "REPLACE_AT_PROVISIONING"

#if defined(ESP32)
  #define SENSOR_PIN 34                 // input-only ADC pin
  #define ADC_FULL_SCALE 4095.0f        // 12-bit
#elif defined(ESP8266)
  #define SENSOR_PIN A0
  #define ADC_FULL_SCALE 1023.0f        // 10-bit
#elif defined(ARDUINO_UNOR4_WIFI)
  #define SENSOR_PIN A0
  #define ADC_FULL_SCALE 16383.0f       // 14-bit (default on UNO R4)
#elif defined(STM32F4)
  #define SENSOR_PIN PA0
  #define ADC_FULL_SCALE 4095.0f        // 12-bit
#elif defined(STM32F1)
  #define SENSOR_PIN PA0
  #define ADC_FULL_SCALE 4095.0f        // 12-bit
#endif

Zeno zeno;
static uint32_t s_lastSampleMs = 0;
static const uint32_t SAMPLE_PERIOD_MS = 1000;

void setup()
{
    Serial.begin(115200);

    zeno.wifi(WIFI_SSID, WIFI_PASS)
        .device(DEVICE_ID, DEVICE_TOKEN)
        .enableZKeys()
        .setZPublishInterval(5000)
        .begin();
}

void loop()
{
    const uint32_t now = millis();
    if (now - s_lastSampleMs >= SAMPLE_PERIOD_MS)
    {
        s_lastSampleMs = now;
        const float raw = (float)analogRead(SENSOR_PIN);
        const float pct = (raw / ADC_FULL_SCALE) * 100.0f;
        ZENO_WRITE(Z2, pct);
        ZENOPCB_PRINTF("[Z2] analog = %.1f%%\n", pct);
    }
    zeno.loop();
}
