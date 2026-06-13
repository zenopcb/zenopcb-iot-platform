/**
 * @file 02_button_read.ino
 * @brief Read a momentary push button and publish state changes to Z1.
 *
 * @category IO
 * @level Beginner
 *
 * @hardware
 * - Any supported board.
 * - Momentary push button between BUTTON_PIN and GND (uses INPUT_PULLUP).
 *
 * @wiring
 * - BUTTON_PIN -> one terminal of push button.
 * - Other terminal of push button -> GND.
 *
 * @usage
 * 1. Set credentials below.
 * 2. Press the button; pressed = LOW, released = HIGH (INPUT_PULLUP).
 * 3. State changes only are published to Z1 (no spamming on hold).
 */

#include <ZenoPCBMain.h>

using namespace ZenoPCB;

#define WIFI_SSID "REPLACE_ME"
#define WIFI_PASS "REPLACE_ME"
#define DEVICE_ID "REPLACE_ME"
#define DEVICE_TOKEN "REPLACE_AT_PROVISIONING"

#if defined(ESP32)
  #define BUTTON_PIN 0           // BOOT button on most ESP32 dev boards
#elif defined(ESP8266)
  #define BUTTON_PIN 0           // FLASH button on NodeMCU
#elif defined(ARDUINO_UNOR4_WIFI)
  #define BUTTON_PIN 2
#elif defined(STM32F4)
  #define BUTTON_PIN PC13        // Nucleo-F429ZI user button
#elif defined(STM32F1)
  #define BUTTON_PIN PA0
#endif

Zeno zeno;

static int s_lastReading = HIGH;

void setup()
{
    Serial.begin(115200);
    pinMode(BUTTON_PIN, INPUT_PULLUP);

    zeno.wifi(WIFI_SSID, WIFI_PASS)
        .device(DEVICE_ID, DEVICE_TOKEN)
        .enableZKeys()
        .begin();
}

void loop()
{
    const int reading = digitalRead(BUTTON_PIN);
    if (reading != s_lastReading)
    {
        s_lastReading = reading;
        const bool pressed = (reading == LOW); // active-low button
        ZENO_WRITE(Z1, pressed);
        ZENOPCB_PRINTF("[Z1] button %s\n", pressed ? "PRESSED" : "RELEASED");
    }

    zeno.loop();
}
