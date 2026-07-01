/**
 * @file 04_dallas_ds18b20.ino
 * @brief Read a Dallas DS18B20 1-Wire temperature sensor and publish degC to Z0 every 5 seconds.
 *
 * What you'll learn:
 *   - What 1-Wire is and why DS18B20 only needs one data pin for many sensors
 *   - How OneWire + DallasTemperature work together to scan and read sensors
 *   - How to detect a disconnected sensor and skip publishing in that case
 *
 * Hardware needed:
 *   - Any supported board
 *   - DS18B20 temperature probe (TO-92 or waterproof stainless probe)
 *   - 4.7 kOhm pull-up resistor between DATA and VCC
 *   - Jumper wires, breadboard
 *
 * Wiring:
 *   - DS18B20 VCC  -> 3.3 V (or 5 V)
 *   - DS18B20 GND  -> GND
 *   - DS18B20 DATA -> ONEWIRE_PIN, plus 4.7 kOhm pull-up to VCC
 *
 * Cloud dashboard setup:
 *   - Create Z0 of type Float — temperature in degC
 *
 * How to use:
 *   1. Replace WIFI_SSID / WIFI_PASS / DEVICE_ID / DEVICE_TOKEN below.
 *   2. Install OneWire + DallasTemperature libraries (see @lib_deps).
 *   3. Open Tools > Partition Scheme > "Minimal SPIFFS (1.9MB APP)" (ESP32 only).
 *   4. Flash and open Serial Monitor at 115200 baud.
 *   5. Grip the probe — Z0 should rise toward body temperature.
 *
 * Tips & common mistakes:
 *   - The 4.7 kOhm pull-up is mandatory. Without it the bus floats and you get
 *     DEVICE_DISCONNECTED_C readings.
 *   - You can put many DS18B20s on the same data line; this sketch reads index 0.
 *     Use sensors.getAddress(...) + getTempC(...) to address others.
 *
 * @lib_deps
 *   - paulstoffregen/OneWire           (>= 2.3.8)
 *   - milesburton/DallasTemperature    (>= 3.11.0)
 */

#include <ZenoPCBMain.h>
#include <OneWire.h>
#include <DallasTemperature.h>

using namespace ZenoPCB;

#define WIFI_SSID "REPLACE_ME"
#define WIFI_PASS "REPLACE_ME"
#define DEVICE_ID "REPLACE_ME"
#define DEVICE_TOKEN "REPLACE_AT_PROVISIONING"

#if defined(ESP32)
  #define ONEWIRE_PIN 4
#elif defined(ESP8266)
  #define ONEWIRE_PIN 4   // D2 on NodeMCU
#elif defined(ARDUINO_UNOR4_WIFI)
  #define ONEWIRE_PIN 2
#elif defined(STM32F4)
  #define ONEWIRE_PIN PB0
#elif defined(STM32F1)
  #define ONEWIRE_PIN PB0
#endif

Zeno zeno;
OneWire             oneWire(ONEWIRE_PIN);
DallasTemperature   sensors(&oneWire);    // wraps the 1-Wire bus with a friendly API

// Device -> Cloud: read DS18B20 and publish degC every 5 seconds.
ZENO_EVERY(5000)
{
    sensors.requestTemperatures();           // tell every sensor on the bus to convert
    const float t = sensors.getTempCByIndex(0);
    // DallasTemperature returns DEVICE_DISCONNECTED_C (a sentinel value) on read
    // failure. Skip the publish so a brief bus glitch doesn't poison the dashboard.
    if (t != DEVICE_DISCONNECTED_C)
    {
        DEVICE_TO_CLOUD(Z0, t);
        Serial.printf("[DS18B20] T = %.2f C\n", t);
    }
}

void setup()
{
    Serial.begin(115200);
    sensors.begin();   // scans the 1-Wire bus for connected devices

    zeno.wifi(WIFI_SSID, WIFI_PASS)
        .device(DEVICE_ID, DEVICE_TOKEN)
        .enableZKeys()
        .begin();
}

void loop()
{
    zeno.loop();
}
