/**
 * @file 05_dallas_ds18b20.ino
 * @brief Dallas DS18B20 1-Wire temperature sensor -> Z0 in degC.
 *
 * @category Sensors
 * @level Beginner
 *
 * @hardware
 * - Any supported board.
 * - DS18B20 1-Wire temperature probe (waterproof or TO-92).
 * - 4.7 kOhm pull-up resistor between DATA and VCC.
 *
 * @wiring
 * - DS18B20 VCC -> 3.3 V (or 5 V).
 * - DS18B20 GND -> GND.
 * - DS18B20 DATA -> ONEWIRE_PIN, with 4.7 kOhm to VCC.
 *
 * @lib_deps
 * platformio.ini:
 *   paulstoffregen/OneWire @ ^2.3.8
 *   milesburton/DallasTemperature @ ^3.11.0
 *
 * @usage
 * 1. Install lib_deps.
 * 2. Set credentials.
 * 3. Z0 publishes temperature in degC every 5 s. Multiple sensors on the
 *    same 1-Wire bus are addressable via sensors.getAddress(...) — this
 *    sketch reads index 0 only.
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
  #define ONEWIRE_PIN 4   // D2
#elif defined(ARDUINO_UNOR4_WIFI)
  #define ONEWIRE_PIN 2
#elif defined(STM32F4)
  #define ONEWIRE_PIN PB0
#elif defined(STM32F1)
  #define ONEWIRE_PIN PB0
#endif

Zeno zeno;
OneWire             oneWire(ONEWIRE_PIN);
DallasTemperature   sensors(&oneWire);

static uint32_t s_lastReadMs = 0;
static const uint32_t READ_MS = 5000;

void setup()
{
    Serial.begin(115200);
    sensors.begin();

    zeno.wifi(WIFI_SSID, WIFI_PASS)
        .device(DEVICE_ID, DEVICE_TOKEN)
        .enableZKeys()
        .setZPublishInterval(5000)
        .begin();
}

void loop()
{
    const uint32_t now = millis();
    if (now - s_lastReadMs >= READ_MS)
    {
        s_lastReadMs = now;
        sensors.requestTemperatures();
        const float t = sensors.getTempCByIndex(0);
        if (t != DEVICE_DISCONNECTED_C)
        {
            ZENO_WRITE(Z0, t);
            ZENOPCB_PRINTF("[DS18B20] T = %.2f C\n", t);
        }
    }
    zeno.loop();
}
