/**
 * @file 02_analog_read.ino
 * @brief Read an analog input (pot or LDR) and publish a normalised 0..100 % to Z2.
 *
 * What you'll learn:
 *   - What analogRead() returns on different boards (different bit-widths)
 *   - How to normalise a raw ADC value to a uniform 0..100 % range
 *   - How ZENO_EVERY(N) schedules periodic Device -> Cloud publishes
 *
 * Hardware needed:
 *   - Any supported board with an ADC pin
 *   - A potentiometer OR an LDR + 10 kOhm divider (anything that puts 0..3.3 V on the pin)
 *   - Jumper wires, breadboard
 *
 * Wiring:
 *   - 3.3 V        -> one outer pin of the pot
 *   - GND          -> other outer pin of the pot
 *   - SENSOR_PIN   -> middle (wiper) pin of the pot
 *   (NEVER feed more than the board's max ADC voltage — 3.3 V for ESP32/ESP8266/STM32.)
 *
 * Cloud dashboard setup:
 *   - Create Z2 of type Float — sensor value normalised to 0..100 %
 *
 * How to use:
 *   1. Replace WIFI_SSID / WIFI_PASS / DEVICE_ID / DEVICE_TOKEN below.
 *   2. Open Tools > Partition Scheme > "Minimal SPIFFS (1.9MB APP)" (ESP32 only).
 *   3. Flash and open Serial Monitor at 115200 baud.
 *   4. Turn the pot — Z2 should sweep 0..100 % on the dashboard.
 *
 * Tips & common mistakes:
 *   - ESP32 GPIO 34 is input-only — fine here, but you cannot use it as an output.
 *   - The ADC reading is fairly noisy. For a stable readout, average a few
 *     samples before publishing (not done here to keep the example small).
 */

#include <ZenoPCBMain.h>

using namespace ZenoPCB;

#define WIFI_SSID "REPLACE_ME"
#define WIFI_PASS "REPLACE_ME"
#define DEVICE_ID "REPLACE_ME"
#define DEVICE_TOKEN "REPLACE_AT_PROVISIONING"

// ============================================
// ADC differs across boards — full-scale changes with bit-width.
// ============================================
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

// Device -> Cloud: read the analog input and publish every 5 seconds.
ZENO_EVERY(5000)
{
    const float raw = (float)analogRead(SENSOR_PIN);
    const float pct = (raw / ADC_FULL_SCALE) * 100.0f;   // normalise across boards
    DEVICE_TO_CLOUD(Z2, pct);
    Serial.printf("[Z2] analog = %.1f%%\n", pct);
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
