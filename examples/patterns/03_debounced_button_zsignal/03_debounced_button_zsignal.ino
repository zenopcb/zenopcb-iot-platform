/**
 * @file 03_debounced_button_zsignal.ino
 * @brief Debounced edge detection — publish click-event counter to Z0.
 *
 * @category Patterns
 * @level Intermediate
 *
 * @hardware
 * - Any supported board.
 * - One momentary push button (BUTTON_PIN to GND).
 *
 * @platform_notes
 * Pure local + ZSignals — works on every supported port including F103.
 *
 * @usage
 * 1. Set credentials.
 * 2. Each clean press-then-release counts as one "click". Z0 publishes
 *    the running click count as an int32.
 * 3. Long-press (>= 1 s) publishes a string "long" event to Z1 once per
 *    long hold.
 */

#include <ZenoPCBMain.h>

using namespace ZenoPCB;

#define WIFI_SSID "REPLACE_ME"
#define WIFI_PASS "REPLACE_ME"
#define DEVICE_ID "REPLACE_ME"
#define DEVICE_TOKEN "REPLACE_AT_PROVISIONING"

#if defined(ESP32)
  #define BUTTON_PIN 0
#elif defined(ESP8266)
  #define BUTTON_PIN 0
#elif defined(ARDUINO_UNOR4_WIFI)
  #define BUTTON_PIN 2
#elif defined(STM32F4)
  #define BUTTON_PIN PC13
#elif defined(STM32F1)
  #define BUTTON_PIN PA0
#endif

static const uint32_t DEBOUNCE_MS  = 30;
static const uint32_t LONG_HOLD_MS = 1000;

Zeno     zeno;
static int      s_lastRaw   = HIGH;
static int      s_stableRaw = HIGH;
static uint32_t s_lastChangeMs = 0;
static uint32_t s_pressStartMs = 0;
static uint32_t s_clickCount   = 0;
static bool     s_longSent     = false;

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
    const uint32_t now = millis();
    const int raw = digitalRead(BUTTON_PIN);

    if (raw != s_lastRaw)
    {
        s_lastChangeMs = now;
        s_lastRaw      = raw;
    }

    if ((now - s_lastChangeMs) >= DEBOUNCE_MS && raw != s_stableRaw)
    {
        s_stableRaw = raw;
        const bool pressed = (s_stableRaw == LOW);
        if (pressed)
        {
            s_pressStartMs = now;
            s_longSent     = false;
        }
        else
        {
            // Released — only count as click if NOT already long-held
            if (!s_longSent)
            {
                ++s_clickCount;
                ZENO_WRITE(Z0, (int32_t)s_clickCount);
                ZENOPCB_PRINTF("[Click] count = %lu\n",
                               (unsigned long)s_clickCount);
            }
        }
    }

    // Long-hold detection while button still pressed
    if (s_stableRaw == LOW && !s_longSent &&
        (now - s_pressStartMs) >= LONG_HOLD_MS)
    {
        s_longSent = true;
        ZENO_WRITE(Z1, String("long"));
        ZENOPCB_PRINTF("[LongHold] fired\n");
    }

    zeno.loop();
}
