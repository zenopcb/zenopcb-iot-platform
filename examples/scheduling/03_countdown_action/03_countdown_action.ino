/**
 * @file 03_countdown_action.ino
 * @brief Cloud-set countdown — Z1 = seconds. LED stays on for that many
 *        seconds, then Z0 fires a single "done" event.
 *
 * What you'll learn:
 *   - Implementing a countdown timer entirely on the device (no cloud Schedule).
 *   - Using a pair of cloud values: one to SET the timer (Z1), one to SIGNAL
 *     completion back to the cloud (Z0).
 *   - Edge-event pattern: write true then false on Z0 to emit a one-shot pulse.
 *
 * Hardware needed:
 *   - Any supported board (works on F103 too — no Schedule subsystem needed).
 *   - On-board LED.
 *
 * Wiring:
 *   - None — uses the built-in LED.
 *
 * Cloud dashboard setup:
 *   - Create Z1 of type Int — duration in seconds. Write a positive int to
 *     start the countdown; write 0 (or negative) to cancel.
 *   - Create Z0 of type Bool — fires true->false when the countdown ends.
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
 * 1. Replace WIFI_SSID / WIFI_PASS / DEVICE_ID / DEVICE_TOKEN below.
 * 2. ESP32 only: Tools > Partition Scheme > "Minimal SPIFFS (1.9MB APP)".
 * 3. Flash the board.
 * 4. Open Serial Monitor at 115200 baud.
 * 5. Cloud-write Z1 = 10 (for example). The LED lights up for 10 s, then
 *    Z0 fires "true" briefly to signal completion. Cloud-write Z1 = 0 at
 *    any time to cancel an in-flight countdown.
 *
 * Tips & common mistakes:
 *   - This is "absolute deadline" timing — we record the start time and
 *     compare elapsed against the duration. It will not drift even over
 *     a long countdown.
 *   - The double-write of Z0 (true then false) emits a one-shot edge for
 *     anything watching for a button-press-style event on the dashboard.
 */

#include <ZenoPCBMain.h>

using namespace ZenoPCB;

// ---- Credentials ------------------------------------------------------------
#define WIFI_SSID "REPLACE_ME"
#define WIFI_PASS "REPLACE_ME"
#define DEVICE_ID "REPLACE_ME"
#define DEVICE_TOKEN "REPLACE_AT_PROVISIONING"

// ---- Pin map ----------------------------------------------------------------
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

// Wrap digitalWrite for the F103 active-LOW LED quirk (PC13 lights on LOW).
static inline void writeLed(bool on)
{
#if defined(STM32F1)
    digitalWrite(LED_PIN, on ? LOW : HIGH);
#else
    digitalWrite(LED_PIN, on ? HIGH : LOW);
#endif
}

// Cloud -> Device: dashboard writes Z1 = seconds. <= 0 cancels.
CLOUD_TO_DEVICE(Z1)
{
    long secs = param.toInt();
    if (secs <= 0)
    {
        // Cancel: stop the countdown and shut the LED off.
        s_running = false;
        writeLed(false);
        Serial.printf("[Countdown] cancelled\n");
        return;
    }
    // Start a fresh countdown — replaces any in-flight timer.
    s_durationMs = (uint32_t)secs * 1000UL;
    s_startMs    = millis();
    s_running    = true;
    writeLed(true);
    Serial.printf("[Countdown] start %ld s\n", secs);
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
    // Has the countdown reached zero? Compare elapsed against duration.
    if (s_running && (millis() - s_startMs >= s_durationMs))
    {
        s_running = false;
        writeLed(false);
        // One-shot edge: write true then false so dashboard event handlers
        // see a single "fire" event, not a sticky state.
        DEVICE_TO_CLOUD(Z0, true);
        Serial.printf("[Countdown] FIRE\n");
        DEVICE_TO_CLOUD(Z0, false);
    }
    zeno.loop();
}
