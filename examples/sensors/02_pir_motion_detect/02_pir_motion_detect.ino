/**
 * @file 02_pir_motion_detect.ino
 * @brief HC-SR501 PIR motion sensor -> publish boolean motion event to Z0.
 *
 * @category Sensors
 * @level Beginner
 *
 * @hardware
 * - Any supported board.
 * - HC-SR501 PIR module (5 V VCC, digital output 3.3 V — safe to feed to
 *   3.3 V boards).
 *
 * @wiring
 * - PIR VCC -> 5 V (or board 5 V pin).
 * - PIR GND -> GND.
 * - PIR OUT -> PIR_PIN.
 *
 * @usage
 * 1. Set credentials.
 * 2. After the PIR settles (~30 s power-on calibration), motion in the
 *    sensor's cone flips Z0 to true; quiet flips it back to false.
 * 3. Adjust PIR potentiometers for sensitivity / hold time.
 */

#include <ZenoPCBMain.h>

using namespace ZenoPCB;

#define WIFI_SSID "REPLACE_ME"
#define WIFI_PASS "REPLACE_ME"
#define DEVICE_ID "REPLACE_ME"
#define DEVICE_TOKEN "REPLACE_AT_PROVISIONING"

#if defined(ESP32)
  #define PIR_PIN 13
#elif defined(ESP8266)
  #define PIR_PIN 14   // D5
#elif defined(ARDUINO_UNOR4_WIFI)
  #define PIR_PIN 5
#elif defined(STM32F4)
  #define PIR_PIN PB10
#elif defined(STM32F1)
  #define PIR_PIN PB10
#endif

Zeno zeno;
static int s_lastState = LOW;

void setup()
{
    Serial.begin(115200);
    pinMode(PIR_PIN, INPUT);

    zeno.wifi(WIFI_SSID, WIFI_PASS)
        .device(DEVICE_ID, DEVICE_TOKEN)
        .enableZKeys()
        .begin();
}

void loop()
{
    const int s = digitalRead(PIR_PIN);
    if (s != s_lastState)
    {
        s_lastState = s;
        const bool motion = (s == HIGH);
        ZENO_WRITE(Z0, motion);
        ZENOPCB_PRINTF("[PIR] %s\n", motion ? "MOTION" : "quiet");
    }
    zeno.loop();
}
