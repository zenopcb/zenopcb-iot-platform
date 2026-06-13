/**
 * @file 07_multi_button_state.ino
 * @brief Three independent buttons -> Z1 / Z2 / Z3 boolean state per button.
 *
 * @category IO
 * @level Intermediate
 *
 * @hardware
 * - Any supported board.
 * - 3 momentary push buttons, each between its BUTTONx_PIN and GND.
 *
 * @wiring
 * - BTN_A_PIN, BTN_B_PIN, BTN_C_PIN — each to GND through a push button.
 *
 * @usage
 * 1. Set credentials.
 * 2. Each button independently flips the matching Z signal on edge changes.
 *    Z1 = btn A, Z2 = btn B, Z3 = btn C.
 */

#include <ZenoPCBMain.h>

using namespace ZenoPCB;

#define WIFI_SSID "REPLACE_ME"
#define WIFI_PASS "REPLACE_ME"
#define DEVICE_ID "REPLACE_ME"
#define DEVICE_TOKEN "REPLACE_AT_PROVISIONING"

#if defined(ESP32)
  #define BTN_A_PIN 25
  #define BTN_B_PIN 26
  #define BTN_C_PIN 27
#elif defined(ESP8266)
  #define BTN_A_PIN 12  // D6
  #define BTN_B_PIN 13  // D7
  #define BTN_C_PIN 14  // D5
#elif defined(ARDUINO_UNOR4_WIFI)
  #define BTN_A_PIN 2
  #define BTN_B_PIN 3
  #define BTN_C_PIN 4
#elif defined(STM32F4)
  #define BTN_A_PIN PG0
  #define BTN_B_PIN PG1
  #define BTN_C_PIN PG2
#elif defined(STM32F1)
  #define BTN_A_PIN PA0
  #define BTN_B_PIN PA1
  #define BTN_C_PIN PA2
#endif

Zeno zeno;

struct Btn {
    uint8_t pin;
    int     last;
    ZKey    key;
};

static Btn s_btns[3] = {
    { BTN_A_PIN, HIGH, ZKey::Z1 },
    { BTN_B_PIN, HIGH, ZKey::Z2 },
    { BTN_C_PIN, HIGH, ZKey::Z3 },
};

void setup()
{
    Serial.begin(115200);
    for (int i = 0; i < 3; ++i)
        pinMode(s_btns[i].pin, INPUT_PULLUP);

    zeno.wifi(WIFI_SSID, WIFI_PASS)
        .device(DEVICE_ID, DEVICE_TOKEN)
        .enableZKeys()
        .begin();
}

void loop()
{
    for (int i = 0; i < 3; ++i)
    {
        const int r = digitalRead(s_btns[i].pin);
        if (r != s_btns[i].last)
        {
            s_btns[i].last = r;
            const bool pressed = (r == LOW);
            // ZSignal write on enum key
            switch (s_btns[i].key)
            {
                case ZKey::Z1: ZENO_WRITE(Z1, pressed); break;
                case ZKey::Z2: ZENO_WRITE(Z2, pressed); break;
                case ZKey::Z3: ZENO_WRITE(Z3, pressed); break;
                default: break;
            }
            ZENOPCB_PRINTF("[btn %d] %s\n", i, pressed ? "PRESS" : "RELEASE");
        }
    }
    zeno.loop();
}
