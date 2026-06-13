/**
 * @file 01_relay_single.ino
 * @brief Single relay control from the cloud via Z0 (ON/OFF boolean).
 *
 * @category Actuation
 * @level Beginner
 *
 * @hardware
 * - Any supported board.
 * - 1-channel 5 V relay module (opto-isolated recommended).
 *
 * @wiring
 * - Relay VCC -> 5 V.
 * - Relay GND -> GND.
 * - Relay IN  -> RELAY_PIN.
 *
 * @usage
 * 1. Set credentials.
 * 2. Cloud-write Z0 = true to close the relay, false to open.
 * 3. Failsafe: setup() drives the pin LOW BEFORE switching pinMode to
 *    OUTPUT — most opto-relays interpret HIGH = OFF / LOW = ON. Inverting
 *    is handled by RELAY_ACTIVE_LOW below; flip if your hardware differs.
 *
 * @safety
 * This sketch drives a relay. Read CLAUDE.md actuation safety note:
 *   - Always failsafe-init outputs LOW (OFF) in setup().
 *   - Do not latch the relay across boots unless explicitly designed for it.
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
  #define RELAY_PIN 5    // D1
#elif defined(ARDUINO_UNOR4_WIFI)
  #define RELAY_PIN 7
#elif defined(STM32F4)
  #define RELAY_PIN PE0
#elif defined(STM32F1)
  #define RELAY_PIN PB12
#endif

// Most cheap opto-isolated relay boards use active-LOW logic (LOW = relay ON).
// Set to false for active-HIGH boards.
static const bool RELAY_ACTIVE_LOW = true;

Zeno zeno;

static inline void writeRelay(bool on)
{
    const int v = RELAY_ACTIVE_LOW ? (on ? LOW : HIGH) : (on ? HIGH : LOW);
    digitalWrite(RELAY_PIN, v);
}

ZENO_READ(Z0)
{
    const bool on = param.toBool();
    writeRelay(on);
    ZENOPCB_PRINTF("[Z0] relay %s\n", on ? "ON" : "OFF");
}

void setup()
{
    Serial.begin(115200);
    // failsafe OFF BEFORE switching to OUTPUT (no glitch HIGH on boot)
    digitalWrite(RELAY_PIN, RELAY_ACTIVE_LOW ? HIGH : LOW);
    pinMode(RELAY_PIN, OUTPUT);
    digitalWrite(RELAY_PIN, RELAY_ACTIVE_LOW ? HIGH : LOW);

    zeno.wifi(WIFI_SSID, WIFI_PASS)
        .device(DEVICE_ID, DEVICE_TOKEN)
        .enableZKeys()
        .onZKeyChange(ZKey::Z0, onZ0)
        .begin();
}

void loop()
{
    zeno.loop();
}
