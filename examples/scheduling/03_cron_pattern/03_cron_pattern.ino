/**
 * @file 03_cron_pattern.ino
 * @brief Cloud cron-style schedule — every 15 minutes, weekdays only.
 *
 * @category Scheduling
 * @level Intermediate
 *
 * @hardware
 * - Any supported board EXCEPT STM32F103.
 * - LED or relay on OUTPUT_PIN (optional — sketch is also useful for testing
 *   the cron pattern in Serial output alone).
 *
 * @platform_notes
 * F103 NOT SUPPORTED — Schedule is compile-stripped under MICRO_BASIC.
 * See sketch 01_simple_timer for an F103-friendly local timer.
 *
 * @usage
 * 1. Set credentials.
 * 2. From the ZenoPCB Cloud dashboard create a CRON schedule with a Mon-Fri
 *    every-15-minutes spec (specific cron syntax depends on the cloud-side
 *    schedule editor; this sketch only needs the ScheduleExecuted callback).
 * 3. Each fire flips Z1, pulses OUTPUT_PIN for 2 s, and logs to Serial.
 */

#include <ZenoPCBMain.h>

using namespace ZenoPCB;

#define WIFI_SSID "REPLACE_ME"
#define WIFI_PASS "REPLACE_ME"
#define DEVICE_ID "REPLACE_ME"
#define DEVICE_TOKEN "REPLACE_AT_PROVISIONING"

#if defined(ESP32)
  #define OUTPUT_PIN 26
#elif defined(ESP8266)
  #define OUTPUT_PIN 5
#elif defined(ARDUINO_UNOR4_WIFI)
  #define OUTPUT_PIN 7
#elif defined(STM32F4)
  #define OUTPUT_PIN PE0
#elif defined(STM32F1)
  #define OUTPUT_PIN PB12 // unused; F103 early-returns
#endif

Zeno     zeno;
static uint32_t s_fireCount    = 0;
static uint32_t s_pulseStartMs = 0;
static bool     s_pulseActive  = false;
static const uint32_t PULSE_MS = 2000;

static void startPulse()
{
    s_pulseActive  = true;
    s_pulseStartMs = millis();
    digitalWrite(OUTPUT_PIN, HIGH);
}

void onScheduleExecuted(const String &id,
                        ExecutionStatus status,
                        int64_t value,
                        const String &error)
{
    if (status != ExecutionStatus::SUCCESS)
    {
        ZENOPCB_PRINTF("[Cron] %s FAIL: %s\n", id.c_str(), error.c_str());
        return;
    }
    ++s_fireCount;
    ZENOPCB_PRINTF("[Cron] %s fire #%lu (value=%lld)\n",
                   id.c_str(), (unsigned long)s_fireCount, (long long)value);
    ZENO_WRITE(Z0, (int32_t)s_fireCount);
    ZENO_WRITE(Z1, true);
    startPulse();
}

void setup()
{
    Serial.begin(115200);

#if defined(STM32F1)
    Serial.println(F("[INFO] cron Schedule not available on STM32F103. "
                     "See basics/05_scheduling/01_simple_timer."));
    return;
#endif

    pinMode(OUTPUT_PIN, OUTPUT);
    digitalWrite(OUTPUT_PIN, LOW);

    zeno.wifi(WIFI_SSID, WIFI_PASS)
        .device(DEVICE_ID, DEVICE_TOKEN)
        .enableZKeys()
        .enableSchedule()
        .onScheduleExecuted(onScheduleExecuted)
        .begin();
}

void loop()
{
#if defined(STM32F1)
    return;
#else
    if (s_pulseActive && (millis() - s_pulseStartMs >= PULSE_MS))
    {
        s_pulseActive = false;
        digitalWrite(OUTPUT_PIN, LOW);
        ZENO_WRITE(Z1, false);
    }
    zeno.loop();
#endif
}
