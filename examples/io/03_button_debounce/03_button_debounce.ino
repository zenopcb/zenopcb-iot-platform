/**
 * @file 03_button_debounce.ino
 * @brief Software-debounced button + publish stable transitions to Z1.
 *
 * @category IO
 * @level Beginner
 *
 * @hardware
 * - Any supported board.
 * - Momentary push button between BUTTON_PIN and GND.
 *
 * @wiring
 * - BUTTON_PIN -> push button -> GND. INPUT_PULLUP used internally.
 *
 * @usage
 * 1. Set credentials.
 * 2. Press the button quickly several times. Without debounce, mechanical
 *    bounce can fire multiple events per press. This sketch waits 30 ms of
 *    stable reading before publishing Z1 — millis() based, never delay().
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

Zeno zeno;

static const uint32_t DEBOUNCE_MS = 30;
static int s_lastRaw    = HIGH;
static int s_stableRaw  = HIGH;
static uint32_t s_lastChangeMs = 0;

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
    const int raw = digitalRead(BUTTON_PIN);
    const uint32_t now = millis();

    if (raw != s_lastRaw)
    {
        s_lastChangeMs = now;
        s_lastRaw      = raw;
    }

    if ((now - s_lastChangeMs) >= DEBOUNCE_MS && raw != s_stableRaw)
    {
        s_stableRaw = raw;
        const bool pressed = (s_stableRaw == LOW);
        ZENO_WRITE(Z1, pressed);
        ZENOPCB_PRINTF("[Z1] debounced %s\n", pressed ? "PRESSED" : "RELEASED");
    }

    zeno.loop();
}
