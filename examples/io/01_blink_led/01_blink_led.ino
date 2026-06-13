/**
 * @file 01_blink_led.ino
 * @brief Blink the built-in LED + publish LED state to ZSignal Z0.
 *
 * @category IO
 * @level Beginner
 *
 * @hardware
 * - Any supported board (ESP32 / ESP8266 / UNO R4 WiFi / STM32 F1 / STM32 F4).
 * - Built-in LED (most dev boards). External LED + 220 Ohm resistor on LED_PIN
 *   works if your board has no on-board LED.
 *
 * @wiring
 * - LED_PIN -> LED anode -> 220 Ohm resistor -> GND (or just rely on the
 *   on-board LED if your board ships with one).
 *
 * @usage
 * 1. Set WIFI_SSID + WIFI_PASS + DEVICE_ID below.
 * 2. Flash + open Serial Monitor at 115200 baud.
 * 3. The LED blinks every 500 ms and the boolean state is published to Z0
 *    every blink. Watch Z0 toggle on the ZenoPCB Cloud dashboard.
 */

#include <ZenoPCBMain.h>

using namespace ZenoPCB;

// ============================================
// Credentials — replace before flashing
// ============================================
#define WIFI_SSID "REPLACE_ME"
#define WIFI_PASS "REPLACE_ME"
#define DEVICE_ID "REPLACE_ME"
#define DEVICE_TOKEN "REPLACE_AT_PROVISIONING"

// ============================================
// Hardware pins (cross-platform via #if defined)
// ============================================
#if defined(ESP32)
  #define LED_PIN 2
#elif defined(ESP8266)
  #define LED_PIN LED_BUILTIN
#elif defined(ARDUINO_UNOR4_WIFI)
  #define LED_PIN LED_BUILTIN
#elif defined(STM32F4)
  #define LED_PIN PA5      // Nucleo-F429ZI green LED
#elif defined(STM32F1)
  #define LED_PIN PC13     // Blue Pill on-board LED (active LOW)
#endif

// ============================================
// Globals
// ============================================
Zeno zeno;

static const uint32_t BLINK_PERIOD_MS = 500;
static uint32_t s_lastToggle = 0;
static bool s_ledState = false;

void setup()
{
    Serial.begin(115200);
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, LOW);   // failsafe initial state

    zeno.wifi(WIFI_SSID, WIFI_PASS)
        .device(DEVICE_ID, DEVICE_TOKEN)
        .enableZKeys()
        .setZPublishInterval(2000) // publish at most every 2 s
        .begin();
}

void loop()
{
    const uint32_t now = millis();
    if (now - s_lastToggle >= BLINK_PERIOD_MS)
    {
        s_lastToggle = now;
        s_ledState   = !s_ledState;

        // STM32F1 Blue Pill on-board LED is active-LOW.
#if defined(STM32F1)
        digitalWrite(LED_PIN, s_ledState ? LOW : HIGH);
#else
        digitalWrite(LED_PIN, s_ledState ? HIGH : LOW);
#endif

        ZENO_WRITE(Z0, s_ledState);
    }

    zeno.loop();
}
