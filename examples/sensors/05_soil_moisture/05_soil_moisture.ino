/**
 * @file 05_soil_moisture.ino
 * @brief Read a capacitive soil moisture sensor and publish 0..100 % to Z0 every 30 seconds.
 *
 * What you'll learn:
 *   - The difference between capacitive (recommended) and resistive (corrodes) soil probes
 *   - How to calibrate raw ADC readings to a meaningful 0..100 % scale
 *   - How to clamp values so a probe outside its expected range doesn't break the math
 *
 * Hardware needed:
 *   - Any supported board with an ADC pin
 *   - Capacitive soil moisture sensor v1.2 (avoid resistive — the probes corrode in days)
 *   - Jumper wires, pot of soil to test in
 *
 * Wiring:
 *   - Sensor VCC  -> 3.3 V (the calibration values below assume 3.3 V supply)
 *   - Sensor GND  -> GND
 *   - Sensor AOUT -> SOIL_PIN
 *
 * Cloud dashboard setup:
 *   - Create Z0 of type Float — soil moisture in % (0 = bone dry, 100 = saturated wet)
 *
 * How to use:
 *   1. Replace WIFI_SSID / WIFI_PASS / DEVICE_ID / DEVICE_TOKEN below.
 *   2. Open Tools > Partition Scheme > "Minimal SPIFFS (1.9MB APP)" (ESP32 only).
 *   3. Calibrate DRY_RAW and WET_RAW by running this sketch with the probe
 *      held in dry air (DRY) and submerged in water (WET), then update the
 *      constants below.
 *   4. Flash and open Serial Monitor at 115200 baud — Z0 updates every 30 s.
 *
 * Tips & common mistakes:
 *   - Capacitive probes give LOWER raw ADC values when wet. We invert that
 *     into a "more wet = higher %" reading in toPercent().
 *   - The defaults are a starting point for a typical v1.2 probe at 3.3 V.
 *     Your probe will read slightly differently — always recalibrate.
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

// Calibration constants — measure your own sensor and overwrite.
// On a typical capacitive sensor at 3.3 V: dry air ~ 2900, fully submerged ~ 1200.
static const int DRY_RAW = 2900;
static const int WET_RAW = 1200;

Zeno zeno;

// Map raw ADC -> 0..100 %, with clamping so unexpected readings stay in range.
static float toPercent(int raw)
{
    if (raw > DRY_RAW) raw = DRY_RAW;     // anything drier than DRY -> 0 %
    if (raw < WET_RAW) raw = WET_RAW;     // anything wetter than WET -> 100 %
    // Invert the scale: capacitive probes read LOWER when wet.
    return 100.0f * (float)(DRY_RAW - raw) / (float)(DRY_RAW - WET_RAW);
}

// Device -> Cloud: sample soil moisture and publish every 30 seconds.
ZENO_EVERY(30000)
{
    const int raw = analogRead(SOIL_PIN);
    const float pct = toPercent(raw);
    DEVICE_TO_CLOUD(Z0, pct);
    Serial.printf("[Soil] raw=%d pct=%.1f%%\n", raw, pct);
}

void setup()
{
    Serial.begin(115200);

    zeno.wifi(WIFI_SSID, WIFI_PASS)
        .device(DEVICE_ID, DEVICE_TOKEN)
        .enableZKeys()
        .begin();
}

void loop()
{
    zeno.loop();
}
