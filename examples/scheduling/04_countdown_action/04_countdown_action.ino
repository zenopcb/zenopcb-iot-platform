/**
 * @file 04_countdown_action.ino
 * @brief Cloud-set countdown — Z1 sets seconds, Z0 fires at 0, LED follows.
 *
 * @category Scheduling
 * @level Intermediate
 *
 * @hardware
 * - Any supported board.
 * - LED on LED_PIN (built-in fine).
 *
 * @platform_notes
 * This sketch uses ONLY millis()-based local scheduling — no enableSchedule()
 * call — so it runs identically on every supported port INCLUDING F103
 * MICRO_BASIC. It demonstrates that "schedule" doesn't always mean the
 * cloud-side Schedule subsystem; lightweight countdowns belong in user code.
 *
 * @usage
 * 1. Set credentials.
 * 2. Cloud-write Z1 = N (seconds, integer). Sketch starts a countdown.
 *    LED stays ON for N seconds, then turns OFF and Z0 is published "fire".
 * 3. Cloud-write Z1 = 0 to cancel an in-flight countdown.
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

Zeno zeno;
static bool     s_running    = false;
static uint32_t s_startMs    = 0;
static uint32_t s_durationMs = 0;

static inline void writeLed(bool on)
{
#if defined(STM32F1)
    digitalWrite(LED_PIN, on ? LOW : HIGH);
#else
    digitalWrite(LED_PIN, on ? HIGH : LOW);
#endif
}

ZENO_READ(Z1)
{
    long secs = param.toLong();
    if (secs <= 0)
    {
        s_running = false;
        writeLed(false);
        ZENOPCB_PRINTF("[Countdown] cancelled\n");
        return;
    }
    s_durationMs = (uint32_t)secs * 1000UL;
    s_startMs    = millis();
    s_running    = true;
    writeLed(true);
    ZENOPCB_PRINTF("[Countdown] start %ld s\n", secs);
}

void setup()
{
    Serial.begin(115200);
    pinMode(LED_PIN, OUTPUT);
    writeLed(false);

    zeno.wifi(WIFI_SSID, WIFI_PASS)
        .device(DEVICE_ID, DEVICE_TOKEN)
        .enableZKeys()
        .onZKeyChange(ZKey::Z1, onZ1)
        .begin();
}

void loop()
{
    if (s_running && (millis() - s_startMs >= s_durationMs))
    {
        s_running = false;
        writeLed(false);
        ZENO_WRITE(Z0, true);  // pulse a "fire" event
        ZENOPCB_PRINTF("[Countdown] FIRE\n");
        ZENO_WRITE(Z0, false); // reset edge
    }
    zeno.loop();
}
