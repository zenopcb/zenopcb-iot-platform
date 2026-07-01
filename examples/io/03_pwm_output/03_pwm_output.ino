/**
 * @file 03_pwm_output.ino
 * @brief Cloud-controlled LED brightness over PWM — write Z3 (0..255) from the dashboard.
 *
 * What you'll learn:
 *   - What PWM is (rapid on/off pulses that the eye sees as dimming)
 *   - How analogWrite() drives PWM with a duty cycle of 0..255
 *   - How to receive an integer command from the cloud and clamp it to a safe range
 *
 * Hardware needed:
 *   - Any supported board with a PWM-capable pin
 *   - LED + 220 Ohm resistor
 *   - Jumper wires, breadboard
 *
 * Wiring:
 *   - PWM_PIN -> 220 Ohm -> LED anode -> GND
 *
 * Cloud dashboard setup:
 *   - Create Z3 of type Int — value 0..255 controls LED brightness (duty cycle)
 *
 * How to use:
 *   1. Replace WIFI_SSID / WIFI_PASS / DEVICE_ID / DEVICE_TOKEN below.
 *   2. Open Tools > Partition Scheme > "Minimal SPIFFS (1.9MB APP)" (ESP32 only).
 *   3. Flash and open Serial Monitor at 115200 baud.
 *   4. From the dashboard, send Z3 = 0 (off), 128 (half), 255 (full bright).
 *
 * Tips & common mistakes:
 *   - On ESP32, analogWrite() is implemented on top of the LEDC peripheral by
 *     Arduino Core 3.x — it works just like on classic Arduino.
 *   - Not every pin supports PWM. On Blue Pill use a TIM-backed pin (e.g. PA8).
 *   - We clamp duty to 0..255 because the dashboard could send out-of-range ints.
 */

#include <ZenoPCBMain.h>

using namespace ZenoPCB;

#define WIFI_SSID "REPLACE_ME"
#define WIFI_PASS "REPLACE_ME"
#define DEVICE_ID "REPLACE_ME"
#define DEVICE_TOKEN "REPLACE_AT_PROVISIONING"

#if defined(ESP32)
  #define PWM_PIN 25
#elif defined(ESP8266)
  #define PWM_PIN 5      // D1 / GPIO5 on NodeMCU
#elif defined(ARDUINO_UNOR4_WIFI)
  #define PWM_PIN 3      // D3 (PWM-capable)
#elif defined(STM32F4)
  #define PWM_PIN PB6    // TIM4_CH1 on Nucleo-F429ZI
#elif defined(STM32F1)
  #define PWM_PIN PA8    // TIM1_CH1 on Blue Pill
#endif

Zeno zeno;

// Cloud -> Device: dashboard writes Z3 -> set LED brightness.
CLOUD_TO_DEVICE(Z3)
{
    int duty = (int)param.toInt();
    // Clamp into the valid analogWrite range — the dashboard could send anything.
    if (duty < 0)   duty = 0;
    if (duty > 255) duty = 255;
    analogWrite(PWM_PIN, duty);
    Serial.printf("[Z3] PWM duty = %d/255\n", duty);
}

void setup()
{
    Serial.begin(115200);
    pinMode(PWM_PIN, OUTPUT);
    analogWrite(PWM_PIN, 0);   // start fully OFF so the LED doesn't flash on boot

    zeno.wifi(WIFI_SSID, WIFI_PASS)
        .device(DEVICE_ID, DEVICE_TOKEN)
        .enableZKeys()
        .begin();
}

void loop()
{
    zeno.loop();
}
