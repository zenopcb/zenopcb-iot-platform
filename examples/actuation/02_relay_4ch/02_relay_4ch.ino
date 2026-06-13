/**
 * @file 02_relay_4ch.ino
 * @brief 4-channel relay module — Z0 / Z1 / Z2 / Z3 each toggle one channel.
 *
 * @category Actuation
 * @level Beginner
 *
 * @hardware
 * - Any supported board.
 * - 4-channel 5 V relay module.
 *
 * @wiring
 * - Relay VCC -> 5 V.
 * - Relay GND -> GND.
 * - Relay IN1..IN4 -> RELAY_PINS array entries.
 *
 * @usage
 * 1. Set credentials.
 * 2. Cloud-write Z0..Z3 = true/false to drive each channel independently.
 *
 * @safety
 * Every channel failsafe-initialised OFF in setup() per CLAUDE.md actuation
 * safety note.
 */

#include <ZenoPCBMain.h>

using namespace ZenoPCB;

#define WIFI_SSID "REPLACE_ME"
#define WIFI_PASS "REPLACE_ME"
#define DEVICE_ID "REPLACE_ME"
#define DEVICE_TOKEN "REPLACE_AT_PROVISIONING"

#if defined(ESP32)
  static const uint8_t RELAY_PINS[4] = { 26, 27, 32, 33 };
#elif defined(ESP8266)
  static const uint8_t RELAY_PINS[4] = { 5, 4, 14, 12 }; // D1,D2,D5,D6
#elif defined(ARDUINO_UNOR4_WIFI)
  static const uint8_t RELAY_PINS[4] = { 4, 5, 6, 7 };
#elif defined(STM32F4)
  static const uint8_t RELAY_PINS[4] = { PE0, PE1, PE2, PE3 };
#elif defined(STM32F1)
  static const uint8_t RELAY_PINS[4] = { PB12, PB13, PB14, PB15 };
#endif

static const bool RELAY_ACTIVE_LOW = true;

Zeno zeno;

static inline void writeRelay(uint8_t idx, bool on)
{
    const int v = RELAY_ACTIVE_LOW ? (on ? LOW : HIGH) : (on ? HIGH : LOW);
    digitalWrite(RELAY_PINS[idx], v);
}

ZENO_READ(Z0) { bool on = param.toBool(); writeRelay(0, on); ZENOPCB_PRINTF("[Z0] CH1 %s\n", on ? "ON" : "OFF"); }
ZENO_READ(Z1) { bool on = param.toBool(); writeRelay(1, on); ZENOPCB_PRINTF("[Z1] CH2 %s\n", on ? "ON" : "OFF"); }
ZENO_READ(Z2) { bool on = param.toBool(); writeRelay(2, on); ZENOPCB_PRINTF("[Z2] CH3 %s\n", on ? "ON" : "OFF"); }
ZENO_READ(Z3) { bool on = param.toBool(); writeRelay(3, on); ZENOPCB_PRINTF("[Z3] CH4 %s\n", on ? "ON" : "OFF"); }

void setup()
{
    Serial.begin(115200);
    for (int i = 0; i < 4; ++i)
    {
        digitalWrite(RELAY_PINS[i], RELAY_ACTIVE_LOW ? HIGH : LOW); // failsafe OFF
        pinMode(RELAY_PINS[i], OUTPUT);
        digitalWrite(RELAY_PINS[i], RELAY_ACTIVE_LOW ? HIGH : LOW);
    }

    zeno.wifi(WIFI_SSID, WIFI_PASS)
        .device(DEVICE_ID, DEVICE_TOKEN)
        .enableZKeys()
        .onZKeyChange(ZKey::Z0, onZ0)
        .onZKeyChange(ZKey::Z1, onZ1)
        .onZKeyChange(ZKey::Z2, onZ2)
        .onZKeyChange(ZKey::Z3, onZ3)
        .begin();
}

void loop()
{
    zeno.loop();
}
