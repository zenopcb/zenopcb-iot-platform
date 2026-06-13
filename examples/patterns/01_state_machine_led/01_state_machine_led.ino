/**
 * @file 01_state_machine_led.ino
 * @brief Explicit IDLE / RUNNING / PAUSED state machine; LED reflects state.
 *
 * @category Patterns
 * @level Intermediate
 *
 * @hardware
 * - Any supported board.
 * - 3 LEDs (or single RGB) on LED_IDLE / LED_RUN / LED_PAUSE. On-board
 *   single LED + Serial logging also works for first-time learners.
 *
 * @platform_notes
 * Pure local state machine — works on every supported port including F103.
 *
 * @usage
 * 1. Set credentials.
 * 2. Cloud-write Z0 to transition the FSM:
 *      Z0 = 0 -> IDLE.
 *      Z0 = 1 -> RUNNING.
 *      Z0 = 2 -> PAUSED.
 * 3. Z1 reports the current state name back to the cloud after each
 *    transition (one-shot, not periodic).
 */

#include <ZenoPCBMain.h>

using namespace ZenoPCB;

#define WIFI_SSID "REPLACE_ME"
#define WIFI_PASS "REPLACE_ME"
#define DEVICE_ID "REPLACE_ME"
#define DEVICE_TOKEN "REPLACE_AT_PROVISIONING"

#if defined(ESP32)
  #define LED_IDLE 25
  #define LED_RUN  26
  #define LED_PAUSE 27
#elif defined(ESP8266)
  #define LED_IDLE 5
  #define LED_RUN  4
  #define LED_PAUSE 14
#elif defined(ARDUINO_UNOR4_WIFI)
  #define LED_IDLE 4
  #define LED_RUN  5
  #define LED_PAUSE 6
#elif defined(STM32F4)
  #define LED_IDLE PB0
  #define LED_RUN  PB7
  #define LED_PAUSE PB14
#elif defined(STM32F1)
  #define LED_IDLE PB12
  #define LED_RUN  PB13
  #define LED_PAUSE PB14
#endif

enum class State : uint8_t { IDLE = 0, RUNNING = 1, PAUSED = 2 };

Zeno  zeno;
static State s_state = State::IDLE;

static const char* stateName(State s)
{
    switch (s)
    {
        case State::IDLE:    return "IDLE";
        case State::RUNNING: return "RUNNING";
        case State::PAUSED:  return "PAUSED";
    }
    return "?";
}

static void applyStateLeds(State s)
{
    digitalWrite(LED_IDLE,  s == State::IDLE   ? HIGH : LOW);
    digitalWrite(LED_RUN,   s == State::RUNNING ? HIGH : LOW);
    digitalWrite(LED_PAUSE, s == State::PAUSED  ? HIGH : LOW);
}

static void transition(State next)
{
    if (next == s_state) return;
    ZENOPCB_PRINTF("[FSM] %s -> %s\n", stateName(s_state), stateName(next));
    s_state = next;
    applyStateLeds(s_state);
    ZENO_WRITE(Z1, String(stateName(s_state)));
}

ZENO_READ(Z0)
{
    const long v = param.toLong();
    if (v < 0 || v > 2) return;
    transition(static_cast<State>(v));
}

void setup()
{
    Serial.begin(115200);
    pinMode(LED_IDLE, OUTPUT);
    pinMode(LED_RUN,  OUTPUT);
    pinMode(LED_PAUSE, OUTPUT);
    applyStateLeds(s_state);

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
