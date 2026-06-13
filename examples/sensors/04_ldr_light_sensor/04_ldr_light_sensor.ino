/**
 * @file 04_ldr_light_sensor.ino
 * @brief Light-dependent resistor (LDR) voltage divider -> Z0 light percent.
 *
 * @category Sensors
 * @level Beginner
 *
 * @hardware
 * - Any supported board with an ADC pin.
 * - LDR + 10 kOhm fixed resistor (voltage divider).
 *
 * @wiring
 * - 3.3 V -> LDR -> ADC_PIN -> 10 kOhm -> GND.
 *   (Bright light = low LDR R = high ADC reading.)
 *
 * @usage
 * 1. Set credentials.
 * 2. Z0 publishes 0..100% representing relative brightness every 1 s.
 */

#include <ZenoPCBMain.h>

using namespace ZenoPCB;

#define WIFI_SSID "REPLACE_ME"
#define WIFI_PASS "REPLACE_ME"
#define DEVICE_ID "REPLACE_ME"
#define DEVICE_TOKEN "REPLACE_AT_PROVISIONING"

#if defined(ESP32)
  #define LDR_PIN 34
  #define ADC_FULL_SCALE 4095.0f
#elif defined(ESP8266)
  #define LDR_PIN A0
  #define ADC_FULL_SCALE 1023.0f
#elif defined(ARDUINO_UNOR4_WIFI)
  #define LDR_PIN A0
  #define ADC_FULL_SCALE 16383.0f
#elif defined(STM32F4)
  #define LDR_PIN PA0
  #define ADC_FULL_SCALE 4095.0f
#elif defined(STM32F1)
  #define LDR_PIN PA0
  #define ADC_FULL_SCALE 4095.0f
#endif

Zeno zeno;
static uint32_t s_lastSampleMs = 0;
static const uint32_t SAMPLE_MS = 1000;

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
    if (now - s_lastSampleMs >= SAMPLE_MS)
    {
        s_lastSampleMs = now;
        const float raw = (float)analogRead(LDR_PIN);
        const float pct = (raw / ADC_FULL_SCALE) * 100.0f;
        ZENO_WRITE(Z0, pct);
        ZENOPCB_PRINTF("[LDR] brightness = %.1f%%\n", pct);
    }
    zeno.loop();
}
