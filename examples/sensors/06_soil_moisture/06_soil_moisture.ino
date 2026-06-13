/**
 * @file 06_soil_moisture.ino
 * @brief Capacitive (or resistive) soil moisture sensor -> Z0 in %.
 *
 * @category Sensors
 * @level Beginner
 *
 * @hardware
 * - Any supported board with an ADC pin.
 * - Capacitive soil moisture sensor v1.2 (recommended — resistive sensors
 *   corrode quickly).
 *
 * @wiring
 * - Sensor VCC -> 3.3 V.
 * - Sensor GND -> GND.
 * - Sensor AOUT -> ADC_PIN.
 *
 * @usage
 * 1. Set credentials.
 * 2. Calibrate WET_RAW / DRY_RAW below by reading raw ADC in a wet vs dry
 *    soil sample with your specific sensor + supply voltage. Defaults are
 *    typical capacitive sensor values on 3.3 V.
 * 3. Z0 publishes a normalised 0 (bone dry) .. 100% (saturated wet) every 30 s.
 */

#include <ZenoPCBMain.h>

using namespace ZenoPCB;

#define WIFI_SSID "REPLACE_ME"
#define WIFI_PASS "REPLACE_ME"
#define DEVICE_ID "REPLACE_ME"
#define DEVICE_TOKEN "REPLACE_AT_PROVISIONING"

#if defined(ESP32)
  #define SOIL_PIN 34
#elif defined(ESP8266)
  #define SOIL_PIN A0
#elif defined(ARDUINO_UNOR4_WIFI)
  #define SOIL_PIN A0
#elif defined(STM32F4)
  #define SOIL_PIN PA0
#elif defined(STM32F1)
  #define SOIL_PIN PA0
#endif

// Calibration — measure your own sensor's raw values to update these.
// On a capacitive sensor at 3.3 V, dry air ~ 2900, fully submerged ~ 1200.
static const int DRY_RAW = 2900;
static const int WET_RAW = 1200;

Zeno zeno;
static uint32_t s_lastSampleMs = 0;
static const uint32_t SAMPLE_MS = 30000;

static float toPercent(int raw)
{
    // Clamp and invert (higher raw = drier soil)
    if (raw > DRY_RAW) raw = DRY_RAW;
    if (raw < WET_RAW) raw = WET_RAW;
    return 100.0f * (float)(DRY_RAW - raw) / (float)(DRY_RAW - WET_RAW);
}

void setup()
{
    Serial.begin(115200);

    zeno.wifi(WIFI_SSID, WIFI_PASS)
        .device(DEVICE_ID, DEVICE_TOKEN)
        .enableZKeys()
        .setZPublishInterval(30000)
        .begin();
}

void loop()
{
    const uint32_t now = millis();
    if (now - s_lastSampleMs >= SAMPLE_MS)
    {
        s_lastSampleMs = now;
        const int raw = analogRead(SOIL_PIN);
        const float pct = toPercent(raw);
        ZENO_WRITE(Z0, pct);
        ZENOPCB_PRINTF("[Soil] raw=%d pct=%.1f%%\n", raw, pct);
    }
    zeno.loop();
}
