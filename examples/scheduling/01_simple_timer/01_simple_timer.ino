/**
 * @file 01_simple_timer.ino
 * @brief Local "every N seconds" timer — toggle LED + publish heartbeat to Z0.
 *
 * @category Scheduling
 * @level Beginner
 *
 * @hardware
 * - Any supported board.
 * - Built-in LED on LED_PIN.
 *
 * @platform_notes
 * STM32F103 Blue Pill uses ZENOPCB_MICRO_BASIC profile — `enableSchedule()`
 * is compile-stripped on F103. This sketch does NOT call enableSchedule()
 * so it runs identically on every supported port. The "timer" here is a
 * plain millis() comparison in loop(), which is the lightest scheduling
 * primitive the maker needs.
 *
 * @usage
 * 1. Set credentials.
 * 2. LED toggles every 5 s; Z0 publishes the boolean state at the same beat.
 * 3. Compare with example `02_daily_schedule` for a CLOUD-driven schedule.
 */

#include <ZenoPCBMain.h>

using namespace ZenoPCB;

#define WIFI_SSID "REPLACE_ME"
#define WIFI_PASS "REPLACE_ME"
#define DEVICE_ID "REPLACE_ME"
#define DEVICE_TOKEN "REPLACE_AT_PROVISIONING"

#if defined(ESP32)
  #define LED_PIN 2
#elif defined(ESP8266)
  #define LED_PIN LED_BUILTIN
#elif defined(ARDUINO_UNOR4_WIFI)
  #define LED_PIN LED_BUILTIN
#elif defined(STM32F4)
  #define LED_PIN PA5
#elif defined(STM32F1)
  #define LED_PIN PC13
#endif

static const uint32_t TIMER_PERIOD_MS = 5000;
static uint32_t s_lastFireMs = 0;
static bool     s_ledState   = false;

Zeno zeno;

static inline void writeLed(bool on)
{
#if defined(STM32F1)
    digitalWrite(LED_PIN, on ? LOW : HIGH); // F1 LED active LOW
#else
    digitalWrite(LED_PIN, on ? HIGH : LOW);
#endif
}

void setup()
{
    Serial.begin(115200);
    pinMode(LED_PIN, OUTPUT);
    writeLed(false);

    zeno.wifi(WIFI_SSID, WIFI_PASS)
        .device(DEVICE_ID, DEVICE_TOKEN)
        .enableZKeys()
        .begin();
}

void loop()
{
    const uint32_t now = millis();
    if (now - s_lastFireMs >= TIMER_PERIOD_MS)
    {
        s_lastFireMs = now;
        s_ledState   = !s_ledState;
        writeLed(s_ledState);
        ZENO_WRITE(Z0, s_ledState);
        ZENOPCB_PRINTF("[Timer] tick, LED %s\n", s_ledState ? "ON" : "OFF");
    }
    zeno.loop();
}
