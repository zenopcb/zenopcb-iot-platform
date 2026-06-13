/**
 * @file 02_daily_schedule.ino
 * @brief Cloud-driven daily Schedule — relay ON at 06:00, OFF at 18:00.
 *
 * @category Scheduling
 * @level Intermediate
 *
 * @hardware
 * - Any supported board EXCEPT STM32F103 Blue Pill.
 * - 1-channel relay (or LED + resistor for testing) on RELAY_PIN.
 *
 * @platform_notes
 * **STM32 F103 (MICRO_BASIC profile) NOT SUPPORTED** — Schedule subsystem is
 * compile-stripped to fit the 64KB Flash budget per Plan 07-06.6. On F103
 * this sketch falls through to a Serial.print warning + early return in
 * setup() so it still cross-compiles cleanly. Use 01_simple_timer or build
 * your own millis-based timer on F103.
 *
 * @usage
 * 1. Set credentials.
 * 2. From the ZenoPCB Cloud dashboard, create two daily schedules:
 *      - 06:00 every day -> value = 1 (turn relay ON)
 *      - 18:00 every day -> value = 0 (turn relay OFF)
 * 3. Schedules sync to device on first MQTT connect, then fire locally even
 *    if internet drops mid-day.
 */

#include <ZenoPCBMain.h>

using namespace ZenoPCB;

#define WIFI_SSID "REPLACE_ME"
#define WIFI_PASS "REPLACE_ME"
#define DEVICE_ID "REPLACE_ME"
#define DEVICE_TOKEN "REPLACE_AT_PROVISIONING"

#if defined(ESP32)
  #define RELAY_PIN 26
#elif defined(ESP8266)
  #define RELAY_PIN 5
#elif defined(ARDUINO_UNOR4_WIFI)
  #define RELAY_PIN 7
#elif defined(STM32F4)
  #define RELAY_PIN PE0
#elif defined(STM32F1)
  #define RELAY_PIN PB12 // unused on F103 (see early-return below)
#endif

Zeno zeno;
static bool s_relayState = false;

static void setRelay(bool on)
{
    s_relayState = on;
    digitalWrite(RELAY_PIN, on ? HIGH : LOW);
    ZENO_WRITE(Z0, (int32_t)(on ? 1 : 0));
    ZENOPCB_PRINTF("[Relay] %s\n", on ? "ON" : "OFF");
}

void onScheduleExecuted(const String &id,
                        ExecutionStatus status,
                        int64_t value,
                        const String &error)
{
    if (status == ExecutionStatus::SUCCESS)
    {
        ZENOPCB_PRINTF("[Schedule] %s fired value=%lld\n",
                       id.c_str(), (long long)value);
        setRelay(value != 0);
    }
    else
    {
        ZENOPCB_PRINTF("[Schedule] %s FAILED: %s\n",
                       id.c_str(), error.c_str());
    }
}

void setup()
{
    Serial.begin(115200);

#if defined(STM32F1)
    Serial.println(F("[INFO] Schedule not available on STM32F103 (MICRO_BASIC). "
                     "Use basics/05_scheduling/01_simple_timer instead."));
    return; // skip Zeno init; loop() is a no-op
#endif

    pinMode(RELAY_PIN, OUTPUT);
    setRelay(false);

    zeno.wifi(WIFI_SSID, WIFI_PASS)
        .device(DEVICE_ID, DEVICE_TOKEN)
        .enableZKeys()
        .setZPublishInterval(5000)
        .enableSchedule()
        .onScheduleExecuted(onScheduleExecuted)
        .begin();
}

void loop()
{
#if defined(STM32F1)
    return;
#else
    zeno.loop();
#endif
}
