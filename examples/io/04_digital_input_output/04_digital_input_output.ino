/**
 * @file 04_digital_input_output.ino
 * @brief Combine digital input (button) and output (LED) — button echoes to LED.
 *
 * @category IO
 * @level Beginner
 *
 * @hardware
 * - Any supported board.
 * - Button between BUTTON_PIN and GND.
 * - LED + 220 Ohm resistor on LED_PIN (or use the on-board LED).
 *
 * @wiring
 * - BUTTON_PIN -> push button -> GND.
 * - LED_PIN -> LED anode -> 220 Ohm -> GND (or on-board LED).
 *
 * @usage
 * 1. Set credentials.
 * 2. Pressing the button lights the LED locally AND publishes Z1 = pressed.
 * 3. Cloud writes to Z0 also drive the LED (so the LED reflects either local
 *    press or remote command — last-write-wins).
 */

#include <ZenoPCBMain.h>

using namespace ZenoPCB;

#define WIFI_SSID "REPLACE_ME"
#define WIFI_PASS "REPLACE_ME"
#define DEVICE_ID "REPLACE_ME"
#define DEVICE_TOKEN "REPLACE_AT_PROVISIONING"

#if defined(ESP32)
  #define LED_PIN 2
  #define BUTTON_PIN 0
#elif defined(ESP8266)
  #define LED_PIN LED_BUILTIN
  #define BUTTON_PIN 0
#elif defined(ARDUINO_UNOR4_WIFI)
  #define LED_PIN LED_BUILTIN
  #define BUTTON_PIN 2
#elif defined(STM32F4)
  #define LED_PIN PA5
  #define BUTTON_PIN PC13
#elif defined(STM32F1)
  #define LED_PIN PC13
  #define BUTTON_PIN PA0
#endif

Zeno zeno;
static int s_lastBtn = HIGH;

static inline void writeLed(bool on)
{
#if defined(STM32F1)
    digitalWrite(LED_PIN, on ? LOW : HIGH); // F1 Blue Pill LED is active LOW
#else
    digitalWrite(LED_PIN, on ? HIGH : LOW);
#endif
}

// Cloud writes Z0 -> LED on/off
ZENO_READ(Z0)
{
    const bool on = param.toBool();
    writeLed(on);
    ZENOPCB_PRINTF("[Z0] cloud LED %s\n", on ? "ON" : "OFF");
}

void setup()
{
    Serial.begin(115200);
    pinMode(LED_PIN, OUTPUT);
    pinMode(BUTTON_PIN, INPUT_PULLUP);
    writeLed(false); // failsafe OFF

    zeno.wifi(WIFI_SSID, WIFI_PASS)
        .device(DEVICE_ID, DEVICE_TOKEN)
        .enableZKeys()
        .onZKeyChange(ZKey::Z0, onZ0)
        .begin();
}

void loop()
{
    const int btn = digitalRead(BUTTON_PIN);
    if (btn != s_lastBtn)
    {
        s_lastBtn = btn;
        const bool pressed = (btn == LOW);
        writeLed(pressed);          // echo to LED locally
        ZENO_WRITE(Z1, pressed);    // tell cloud the button state
    }

    zeno.loop();
}
