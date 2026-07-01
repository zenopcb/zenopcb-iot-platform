/**
 * @file 01_dht22_temp_humidity.ino
 * @brief Read temperature and humidity from a DHT22 sensor and publish to Z0 and Z1 every 5 seconds.
 *
 * What you'll learn:
 *   - How to talk to a 1-wire DHT22 sensor through the Adafruit DHT library
 *   - How to check for sensor read failures with isnan() before publishing
 *   - How to publish more than one Z signal from the same ZENO_EVERY block
 *
 * Hardware needed:
 *   - Any supported board
 *   - DHT22 (AM2302) temperature and humidity sensor (3.3 V or 5 V tolerant)
 *   - 4.7 kOhm pull-up resistor between DATA and VCC
 *   - Jumper wires, breadboard
 *
 * Wiring:
 *   - DHT22 VCC  -> 3.3 V (or 5 V)
 *   - DHT22 GND  -> GND
 *   - DHT22 DATA -> DHT_PIN, plus a 4.7 kOhm pull-up to VCC
 *
 * Cloud dashboard setup:
 *   - Create Z0 of type Float — temperature in degC
 *   - Create Z1 of type Float — humidity in % RH
 *
 * How to use:
 *   1. Replace WIFI_SSID / WIFI_PASS / DEVICE_ID / DEVICE_TOKEN below.
 *   2. Install the "DHT sensor library" by Adafruit (see @lib_deps).
 *   3. Open Tools > Partition Scheme > "Minimal SPIFFS (1.9MB APP)" (ESP32 only).
 *   4. Flash and open Serial Monitor at 115200 baud.
 *   5. Z0 / Z1 should update every 5 s; breathe on the sensor to see them change.
 *
 * Tips & common mistakes:
 *   - DHT22 needs at least ~2 s between reads. 5 s is comfortably safe.
 *   - Without the 4.7 kOhm pull-up you'll get random read failures (NaN).
 *   - If readings show as NaN, double-check VCC, GND, and the pull-up resistor.
 *
 * @lib_deps
 *   - DHT sensor library (Adafruit), >= 1.4.6
 *     https://github.com/adafruit/DHT-sensor-library
 *     (Library Manager also pulls in "Adafruit Unified Sensor")
 */

#include <ZenoPCBMain.h>
#include <DHT.h>

using namespace ZenoPCB;

#define WIFI_SSID "REPLACE_ME"
#define WIFI_PASS "REPLACE_ME"
#define DEVICE_ID "REPLACE_ME"
#define DEVICE_TOKEN "REPLACE_AT_PROVISIONING"

#if defined(ESP32)
  #define DHT_PIN 4
#elif defined(ESP8266)
  #define DHT_PIN 4    // D2 on NodeMCU
#elif defined(ARDUINO_UNOR4_WIFI)
  #define DHT_PIN 2
#elif defined(STM32F4)
  #define DHT_PIN PA8
#elif defined(STM32F1)
  #define DHT_PIN PA8
#endif

#define DHT_TYPE DHT22    // change to DHT11 if using the cheaper, less-accurate cousin

Zeno zeno;
DHT  dht(DHT_PIN, DHT_TYPE);

// Device -> Cloud: read temperature + humidity and publish every 5 seconds.
ZENO_EVERY(5000)
{
    const float t = dht.readTemperature();   // degC
    const float h = dht.readHumidity();      // % RH
    // DHT reads can fail — the library returns NaN. Skip publishing in that case
    // so we don't poison the dashboard with bad data.
    if (!isnan(t) && !isnan(h))
    {
        DEVICE_TO_CLOUD(Z0, t);
        DEVICE_TO_CLOUD(Z1, h);
        Serial.printf("[DHT22] T=%.1fC H=%.1f%%\n", t, h);
    }
    else
    {
        Serial.printf("[DHT22] read failed\n");
    }
}

void setup()
{
    Serial.begin(115200);
    dht.begin();   // start the DHT driver before zeno so the first read is ready

    zeno.wifi(WIFI_SSID, WIFI_PASS)
        .device(DEVICE_ID, DEVICE_TOKEN)
        .enableZKeys()
        .begin();
}

void loop()
{
    zeno.loop();
}
