/**
 * @file 04_dc_motor.ino
 * @brief L298N H-bridge — Z0 = speed (-255..+255 signed for direction).
 *
 * @category Actuation
 * @level Intermediate
 *
 * @hardware
 * - Any supported board with 2 PWM outputs.
 * - L298N (or L9110, TB6612FNG) H-bridge DC motor driver.
 * - 6..12 V DC motor + external supply matching motor voltage.
 *
 * @wiring
 * - L298N IN1 -> IN1_PIN.
 * - L298N IN2 -> IN2_PIN.
 * - L298N ENA -> ENA_PIN (PWM).
 * - L298N OUT1/OUT2 -> motor terminals.
 * - L298N VS  -> external motor supply (+).
 * - L298N GND -> common GND (motor supply GND + MCU GND).
 *
 * @usage
 * 1. Set credentials.
 * 2. Cloud-write int Z0 in range -255..+255:
 *      Z0 > 0 -> motor forward, duty = Z0.
 *      Z0 < 0 -> motor reverse, duty = -Z0.
 *      Z0 = 0 -> stop (both IN low, ENA = 0).
 *
 * @safety
 * - Failsafe-init both IN low + ENA low.
 * - Watch for motor inrush — ensure your H-bridge can handle stall current.
 */

#include <ZenoPCBMain.h>

using namespace ZenoPCB;

#define WIFI_SSID "REPLACE_ME"
#define WIFI_PASS "REPLACE_ME"
#define DEVICE_ID "REPLACE_ME"
#define DEVICE_TOKEN "REPLACE_AT_PROVISIONING"

#if defined(ESP32)
  #define IN1_PIN 25
  #define IN2_PIN 26
  #define ENA_PIN 27
#elif defined(ESP8266)
  #define IN1_PIN 5    // D1
  #define IN2_PIN 4    // D2
  #define ENA_PIN 14   // D5 (PWM)
#elif defined(ARDUINO_UNOR4_WIFI)
  #define IN1_PIN 4
  #define IN2_PIN 5
  #define ENA_PIN 6    // PWM
#elif defined(STM32F4)
  #define IN1_PIN PE0
  #define IN2_PIN PE1
  #define ENA_PIN PB6  // TIM4_CH1
#elif defined(STM32F1)
  #define IN1_PIN PB12
  #define IN2_PIN PB13
  #define ENA_PIN PA8  // TIM1_CH1
#endif

Zeno zeno;

static void driveMotor(int signedDuty)
{
    if (signedDuty > 255)  signedDuty = 255;
    if (signedDuty < -255) signedDuty = -255;

    if (signedDuty == 0)
    {
        analogWrite(ENA_PIN, 0);
        digitalWrite(IN1_PIN, LOW);
        digitalWrite(IN2_PIN, LOW);
    }
    else if (signedDuty > 0)
    {
        digitalWrite(IN1_PIN, HIGH);
        digitalWrite(IN2_PIN, LOW);
        analogWrite(ENA_PIN, signedDuty);
    }
    else
    {
        digitalWrite(IN1_PIN, LOW);
        digitalWrite(IN2_PIN, HIGH);
        analogWrite(ENA_PIN, -signedDuty);
    }
}

ZENO_READ(Z0)
{
    const int v = (int)param.toLong();
    driveMotor(v);
    ZENOPCB_PRINTF("[Z0] motor %s duty=%d\n",
        v == 0 ? "STOP" : (v > 0 ? "FWD" : "REV"), v);
}

void setup()
{
    Serial.begin(115200);
    pinMode(IN1_PIN, OUTPUT);
    pinMode(IN2_PIN, OUTPUT);
    pinMode(ENA_PIN, OUTPUT);
    driveMotor(0); // failsafe STOP

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
