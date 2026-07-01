/**
 * @file 03_dc_motor.ino
 * @brief Spin a DC motor forward/reverse with PWM speed from the cloud.
 *
 * What you'll learn:
 *   - Driving an H-bridge (L298N etc.) with two direction pins + one PWM pin.
 *   - Encoding sign + magnitude in a single cloud value: negative = reverse.
 *   - Why both IN pins go LOW for STOP (coast) and never both HIGH (shoot-through).
 *
 * Hardware needed:
 *   - Any supported board with at least 2 digital pins + 1 PWM-capable pin.
 *   - L298N (or L9110, TB6612FNG) H-bridge DC motor driver.
 *   - 6..12 V DC brushed motor.
 *   - External DC supply matching the motor voltage.
 *   - Jumper wires.
 *
 * Wiring:
 *   - L298N IN1 -> IN1_PIN.
 *   - L298N IN2 -> IN2_PIN.
 *   - L298N ENA -> ENA_PIN (must be PWM-capable).
 *   - L298N OUT1/OUT2 -> motor terminals.
 *   - L298N VS  -> external motor supply (+).
 *   - L298N GND -> common GND (motor supply GND AND board GND tied together).
 *
 * Cloud dashboard setup:
 *   - Create Z0 of type Int — signed motor command (-255..+255).
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
 * 1. Replace WIFI_SSID / WIFI_PASS / DEVICE_ID / DEVICE_TOKEN below.
 * 2. ESP32 only: Tools > Partition Scheme > "Minimal SPIFFS (1.9MB APP)".
 * 3. Flash the board.
 * 4. Open Serial Monitor at 115200 baud.
 * 5. Cloud-write int Z0 in range -255..+255:
 *      Z0 > 0 -> motor forward, PWM duty = Z0.
 *      Z0 < 0 -> motor reverse, PWM duty = -Z0.
 *      Z0 = 0 -> stop (coast).
 *
 * Tips & common mistakes:
 *   - Always share GND between the motor supply and the board, or the
 *     H-bridge sees garbage logic levels and the motor twitches.
 *   - DC motors can pull large inrush current at stall — pick an H-bridge
 *     rated above the motor's stall current.
 *   - Do NOT drive both IN pins HIGH at the same time on an L298N — that
 *     causes shoot-through and can destroy the driver. driveMotor() below
 *     never sets both pins HIGH.
 *   - Inductive load: H-bridges include flyback diodes, so you do not need
 *     external diodes here. If you wire a relay across the motor instead,
 *     add one (see sketch 05_solenoid_valve).
 *
 * @safety
 * - Failsafe-init both IN low + ENA low.
 * - Watch for motor inrush — ensure your H-bridge can handle stall current.
 */

#include <ZenoPCBMain.h>

using namespace ZenoPCB;

// ---- Credentials ------------------------------------------------------------
#define WIFI_SSID "REPLACE_ME"
#define WIFI_PASS "REPLACE_ME"
#define DEVICE_ID "REPLACE_ME"
#define DEVICE_TOKEN "REPLACE_AT_PROVISIONING"

// ---- Pin map ----------------------------------------------------------------
// ENA must be a PWM-capable pin per board. The platform-specific blocks
// account for which timer channels the Arduino core exposes as analogWrite()
// targets.
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
  #define ENA_PIN PB6  // TIM4_CH1 (PWM-capable on F4 cores)
#elif defined(STM32F1)
  #define IN1_PIN PB12
  #define IN2_PIN PB13
  #define ENA_PIN PA8  // TIM1_CH1 (PWM-capable on F1 cores)
#endif

Zeno zeno;

// Drive the motor with a SIGNED duty cycle.
//   signedDuty > 0 = forward
//   signedDuty < 0 = reverse
//   signedDuty = 0 = stop
// Always wire both IN pins LOW + ENA = 0 for STOP — never both HIGH.
static void driveMotor(int signedDuty)
{
    // Clamp to the analogWrite() 8-bit range. The dashboard might send a
    // typo like 9999 — clamping turns that into "full speed" instead of
    // wrapping back to a small value.
    if (signedDuty > 255)  signedDuty = 255;
    if (signedDuty < -255) signedDuty = -255;

    if (signedDuty == 0)
    {
        // STOP: both inputs low cuts power to the motor (coast).
        analogWrite(ENA_PIN, 0);
        digitalWrite(IN1_PIN, LOW);
        digitalWrite(IN2_PIN, LOW);
    }
    else if (signedDuty > 0)
    {
        // FORWARD: IN1 HIGH, IN2 LOW, PWM controls speed.
        digitalWrite(IN1_PIN, HIGH);
        digitalWrite(IN2_PIN, LOW);
        analogWrite(ENA_PIN, signedDuty);
    }
    else
    {
        // REVERSE: IN1 LOW, IN2 HIGH, PWM uses magnitude.
        digitalWrite(IN1_PIN, LOW);
        digitalWrite(IN2_PIN, HIGH);
        analogWrite(ENA_PIN, -signedDuty);
    }
}

// Cloud -> Device: dashboard writes a signed integer to Z0 (-255..+255).
CLOUD_TO_DEVICE(Z0)
{
    const int v = (int)param.toInt();
    driveMotor(v);
    Serial.printf("[Z0] motor %s duty=%d\n",
        v == 0 ? "STOP" : (v > 0 ? "FWD" : "REV"), v);
}

void setup()
{
    Serial.begin(115200);
    pinMode(IN1_PIN, OUTPUT);
    pinMode(IN2_PIN, OUTPUT);
    pinMode(ENA_PIN, OUTPUT);
    // Failsafe: the motor must be stopped before the cloud has a chance to
    // send a value. A stale "go" command should never carry across a reboot.
    driveMotor(0);

    zeno.wifi(WIFI_SSID, WIFI_PASS)
        .device(DEVICE_ID, DEVICE_TOKEN)
        .enableZKeys()
        .begin();
}

void loop()
{
    zeno.loop();
}
