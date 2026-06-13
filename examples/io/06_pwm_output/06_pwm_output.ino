/**
 * @file 06_pwm_output.ino
 * @brief Cloud-controlled PWM brightness on an LED (Z3 = 0..255 duty).
 *
 * @category IO
 * @level Beginner
 *
 * @hardware
 * - Any supported board with PWM-capable output.
 * - LED + 220 Ohm resistor on PWM_PIN.
 *
 * @wiring
 * - PWM_PIN -> 220 Ohm -> LED anode -> GND.
 *
 * @usage
 * 1. Set credentials.
 * 2. From the ZenoPCB Cloud dashboard, send an int value 0..255 to Z3 — the
 *    LED brightness follows immediately via analogWrite().
 * 3. On ESP32, analogWrite() is provided by Arduino Core 3.x (LEDC under the
 *    hood). Other platforms use the standard Arduino analogWrite contract.
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
  #define PWM_PIN 3      // D3 (PWM)
#elif defined(STM32F4)
  #define PWM_PIN PB6    // TIM4_CH1 on Nucleo-F429ZI
#elif defined(STM32F1)
  #define PWM_PIN PA8    // TIM1_CH1 on Blue Pill
#endif

Zeno zeno;

ZENO_READ(Z3)
{
    int duty = (int)param.toLong();
    if (duty < 0)   duty = 0;
    if (duty > 255) duty = 255;
    analogWrite(PWM_PIN, duty);
    ZENOPCB_PRINTF("[Z3] PWM duty = %d/255\n", duty);
}

void setup()
{
    Serial.begin(115200);
    pinMode(PWM_PIN, OUTPUT);
    analogWrite(PWM_PIN, 0);   // failsafe OFF

    zeno.wifi(WIFI_SSID, WIFI_PASS)
        .device(DEVICE_ID, DEVICE_TOKEN)
        .enableZKeys()
        .onZKeyChange(ZKey::Z3, onZ3)
        .begin();
}

void loop()
{
    zeno.loop();
}
