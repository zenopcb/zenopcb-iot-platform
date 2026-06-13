/**
 * @file 05_solenoid_valve.ino
 * @brief 12 V solenoid valve via relay, with safety auto-off timeout.
 *
 * @category Actuation
 * @level Intermediate
 *
 * @hardware
 * - Any supported board.
 * - 1-channel relay (rated for solenoid current — typically 5..10 A coil
 *   inrush on a 12 V irrigation solenoid).
 * - 12 V solenoid valve + external 12 V supply.
 * - **Flyback diode (1N4007 or similar)** across the solenoid coil.
 *
 * @wiring
 * - Relay IN  -> VALVE_PIN.
 * - Relay COM -> 12 V +.
 * - Relay NO  -> solenoid + terminal.
 * - Solenoid - terminal -> 12 V GND.
 * - Flyback diode reverse-biased across solenoid coil terminals.
 *
 * @safety
 * - **Auto-off after MAX_ON_MS = 60 s** even if cloud Z0 stays true.
 *   Prevents a stuck valve flooding the irrigation zone if the cloud
 *   connection drops mid-cycle.
 * - Cloud writes false to Z0 cancel the auto-off and close the valve
 *   immediately.
 * - CLAUDE.md actuation safety: explicitly LOW-initialise the relay BEFORE
 *   setting pinMode OUTPUT, and document that the OSS user is responsible
 *   for fail-safe hardware (mechanical normally-closed valve preferred).
 */

#include <ZenoPCBMain.h>

using namespace ZenoPCB;

#define WIFI_SSID "REPLACE_ME"
#define WIFI_PASS "REPLACE_ME"
#define DEVICE_ID "REPLACE_ME"
#define DEVICE_TOKEN "REPLACE_AT_PROVISIONING"

#if defined(ESP32)
  #define VALVE_PIN 26
#elif defined(ESP8266)
  #define VALVE_PIN 5   // D1
#elif defined(ARDUINO_UNOR4_WIFI)
  #define VALVE_PIN 7
#elif defined(STM32F4)
  #define VALVE_PIN PE0
#elif defined(STM32F1)
  #define VALVE_PIN PB12
#endif

static const bool VALVE_ACTIVE_LOW = true;
static const uint32_t MAX_ON_MS = 60000UL; // 60 s hard ceiling

Zeno zeno;
static bool     s_valveOn       = false;
static uint32_t s_valveStartMs  = 0;

static void writeValve(bool on)
{
    const int v = VALVE_ACTIVE_LOW ? (on ? LOW : HIGH) : (on ? HIGH : LOW);
    digitalWrite(VALVE_PIN, v);
    s_valveOn      = on;
    s_valveStartMs = on ? millis() : 0;
    ZENO_WRITE(Z0, on); // echo state back to cloud
    ZENOPCB_PRINTF("[VALVE] %s\n", on ? "OPEN" : "CLOSED");
}

ZENO_READ(Z0)
{
    writeValve(param.toBool());
}

void setup()
{
    Serial.begin(115200);
    digitalWrite(VALVE_PIN, VALVE_ACTIVE_LOW ? HIGH : LOW); // failsafe OFF
    pinMode(VALVE_PIN, OUTPUT);
    digitalWrite(VALVE_PIN, VALVE_ACTIVE_LOW ? HIGH : LOW);

    zeno.wifi(WIFI_SSID, WIFI_PASS)
        .device(DEVICE_ID, DEVICE_TOKEN)
        .enableZKeys()
        .onZKeyChange(ZKey::Z0, onZ0)
        .begin();
}

void loop()
{
    // Safety auto-off
    if (s_valveOn && (millis() - s_valveStartMs >= MAX_ON_MS))
    {
        ZENOPCB_PRINTF("[VALVE] safety auto-off after %lu ms\n",
                       (unsigned long)MAX_ON_MS);
        writeValve(false);
    }
    zeno.loop();
}
