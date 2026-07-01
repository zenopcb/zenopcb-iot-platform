/**
 * @file 02_debounced_button_zsignal.ino
 * @brief Debounced button — publish click counter to Z0 and long-press events to Z1.
 *
 * What you'll learn:
 *   - Why mechanical buttons need debouncing (one press = many electrical edges)
 *   - How to do a simple time-based software debounce without delay()
 *   - How to distinguish a short click from a long-hold inside loop()
 *
 * Hardware needed:
 *   - Any supported board with WiFi
 *   - One momentary push button
 *   - Jumper wires
 *
 * Wiring:
 *   - Button pin 1 -> BUTTON_PIN (board-specific, see #if block)
 *   - Button pin 2 -> GND
 *   No external resistor needed — the sketch enables the MCU's internal pull-up,
 *   so the pin reads HIGH when the button is open, LOW when pressed.
 *
 * Cloud dashboard setup:
 *   - Create Z0 of type Int    — receives the running click count
 *   - Create Z1 of type String — fires "long" each time the button is held >= 1 s
 *
 * @category Patterns
 * @level Intermediate
 *
 * @hardware
 *   - Any supported board with WiFi.
 *   - One momentary push button between BUTTON_PIN and GND.
 *
 * @platform_notes
 *   Pure local logic + ZSignals — works on every supported port, including the
 *   slim STM32 Blue Pill (F103) MICRO_BASIC profile.
 *
 * @wiring
 *   See "Wiring" section above (button between BUTTON_PIN and GND).
 *
 * @usage
 *   1. Replace credentials below.
 *   2. Each clean press-then-release counts as one "click". Z0 publishes
 *      the running click count as an int32.
 *   3. Holding the button for >= 1 second fires a "long" event on Z1 once
 *     per hold (it does NOT also count as a click).
 *
 * Tips & common mistakes:
 *   - 30 ms is a common debounce window. If you still see double-counts,
 *     bump DEBOUNCE_MS to 50 or 80 ms (cheap tactile switches bounce longer).
 *   - INPUT_PULLUP inverts the logic: LOW means pressed, HIGH means released.
 *   - On ESP32 / ESP8266, GPIO 0 is also the bootloader-strap pin — holding it
 *     during reset puts the chip into flash mode. That's fine while running,
 *     but if you cannot upload, release the button.
 */

#include <ZenoPCBMain.h>

using namespace ZenoPCB;

#define WIFI_SSID "REPLACE_ME"
#define WIFI_PASS "REPLACE_ME"
#define DEVICE_ID "REPLACE_ME"
#define DEVICE_TOKEN "REPLACE_AT_PROVISIONING"

// Pick a sensible button pin per board. GPIO 0 on ESP32/ESP8266 is the boot
// button on most dev boards — reuse it so you don't need extra hardware.
#if defined(ESP32)
  #define BUTTON_PIN 0
#elif defined(ESP8266)
  #define BUTTON_PIN 0
#elif defined(ARDUINO_UNOR4_WIFI)
  #define BUTTON_PIN 2
#elif defined(STM32F4)
  #define BUTTON_PIN PC13                  // on-board USER button on Nucleo-F429ZI
#elif defined(STM32F1)
  #define BUTTON_PIN PA0
#endif

static const uint32_t DEBOUNCE_MS  = 30;   // ignore edges that flip back within 30 ms
static const uint32_t LONG_HOLD_MS = 1000; // 1 s hold -> "long" event

Zeno     zeno;
static int      s_lastRaw      = HIGH;     // last instantaneous reading
static int      s_stableRaw    = HIGH;     // last reading that survived debounce
static uint32_t s_lastChangeMs = 0;        // time of last edge on `raw`
static uint32_t s_pressStartMs = 0;        // when current press started
static uint32_t s_clickCount   = 0;
static bool     s_longSent     = false;    // suppress click on release if long fired

void setup()
{
    Serial.begin(115200);
    pinMode(BUTTON_PIN, INPUT_PULLUP);     // internal pull-up; HIGH = released, LOW = pressed

    zeno.wifi(WIFI_SSID, WIFI_PASS)
        .device(DEVICE_ID, DEVICE_TOKEN)
        .enableZKeys()
        .begin();
}

void loop()
{
    const uint32_t now = millis();
    const int raw = digitalRead(BUTTON_PIN);

    // Record every raw edge so the debounce timer restarts on each bounce.
    if (raw != s_lastRaw)
    {
        s_lastChangeMs = now;
        s_lastRaw      = raw;
    }

    // Edge has been stable for DEBOUNCE_MS — accept it as the real state.
    if ((now - s_lastChangeMs) >= DEBOUNCE_MS && raw != s_stableRaw)
    {
        s_stableRaw = raw;
        const bool pressed = (s_stableRaw == LOW);  // pull-up means LOW = pressed
        if (pressed)
        {
            s_pressStartMs = now;
            s_longSent     = false;
        }
        else
        {
            // Released — only count as a click if we did NOT already fire a
            // long-hold event for this press.
            if (!s_longSent)
            {
                ++s_clickCount;
                // Device -> Cloud: publish the new click count
                DEVICE_TO_CLOUD(Z0, (int32_t)s_clickCount);
                Serial.printf("[Click] count = %lu\n",
                               (unsigned long)s_clickCount);
            }
        }
    }

    // While the button is still held down, check whether 1 s has elapsed.
    // Fire the "long" event exactly once per hold (gated by s_longSent).
    if (s_stableRaw == LOW && !s_longSent &&
        (now - s_pressStartMs) >= LONG_HOLD_MS)
    {
        s_longSent = true;
        // Device -> Cloud: announce a long-press
        DEVICE_TO_CLOUD(Z1, String("long"));
        Serial.printf("[LongHold] fired\n");
    }

    zeno.loop();
}
