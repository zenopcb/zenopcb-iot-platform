/**
 * @file 01_digital_input_output.ino
 * @brief Button + LED on the same board, with the LED also controllable from the cloud.
 *
 * What you'll learn:
 *   - How to combine digital input (button) and digital output (LED)
 *   - How a local button press and a cloud command can drive the same output
 *     ("last-write-wins" — whichever event happened most recently)
 *   - How to factor LED writes into a helper so active-LOW boards stay one-liners
 *
 * Hardware needed:
 *   - Any supported board
 *   - One momentary push button
 *   - LED + 220 Ohm resistor (or use the on-board LED)
 *   - Jumper wires, breadboard
 *
 * Wiring:
 *   - BUTTON_PIN -> button -> GND
 *   - LED_PIN    -> LED anode -> 220 Ohm -> GND
 *
 * Cloud dashboard setup:
 *   - Create Z0 of type Bool — write from dashboard to drive the LED remotely
 *   - Create Z1 of type Bool — published when the local button is pressed
 *
 * How to use:
 *   1. Replace WIFI_SSID / WIFI_PASS / DEVICE_ID / DEVICE_TOKEN below.
 *   2. Open Tools > Partition Scheme > "Minimal SPIFFS (1.9MB APP)" (ESP32 only).
 *   3. Flash and open Serial Monitor at 115200 baud.
 *   4. Press the button: LED follows the press, dashboard sees Z1 update.
 *   5. Toggle Z0 from the dashboard: LED follows the cloud command.
 *
 * Tips & common mistakes:
 *   - This sketch is intentionally not debounced — see 03_button_debounce for that.
 *   - "Last-write-wins" is fine here. For production you usually want to track
 *     the active source and avoid fighting each other.
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
  #define LED_PIN PC13              // active-LOW on Blue Pill
  #define BUTTON_PIN PA0
#endif

Zeno zeno;
static int s_lastBtn = HIGH;        // pull-up keeps idle reading HIGH

// One helper so the active-LOW special case lives in exactly one place.
static inline void writeLed(bool on)
{
#if defined(STM32F1)
    digitalWrite(LED_PIN, on ? LOW : HIGH);    // Blue Pill LED is active-LOW
#else
    digitalWrite(LED_PIN, on ? HIGH : LOW);
#endif
}

// Cloud -> Device: dashboard writes Z0 -> turn LED on/off.
CLOUD_TO_DEVICE(Z0)
{
    const bool on = param.toBool();
    writeLed(on);
    Serial.printf("[Z0] cloud LED %s\n", on ? "ON" : "OFF");
}

void setup()
{
    Serial.begin(115200);
    pinMode(LED_PIN, OUTPUT);
    pinMode(BUTTON_PIN, INPUT_PULLUP);
    writeLed(false);    // start OFF so we don't glitch on boot

    zeno.wifi(WIFI_SSID, WIFI_PASS)
        .device(DEVICE_ID, DEVICE_TOKEN)
        .enableZKeys()
        .begin();
}

void loop()
{
    const int btn = digitalRead(BUTTON_PIN);
    // Edge detection — react only when the button changes state.
    if (btn != s_lastBtn)
    {
        s_lastBtn = btn;
        const bool pressed = (btn == LOW);          // active-LOW button
        writeLed(pressed);                          // local mirror: LED follows the button
        DEVICE_TO_CLOUD(Z1, pressed);               // Device -> Cloud: tell the dashboard
    }

    zeno.loop();
}
