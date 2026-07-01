/**
 * @file 02_servo_position.ino
 * @brief Drive a hobby servo to an angle (0..180 deg) set from the cloud.
 *
 * What you'll learn:
 *   - Mapping a cloud integer (Z0) to a hardware angle in degrees.
 *   - Clamping user input so dashboard typos can't damage the servo.
 *   - Why ESP32 uses a different Servo library than every other board.
 *
 * Hardware needed:
 *   - Any supported board.
 *   - SG90 / MG90S hobby servo (or any standard 50 Hz PWM servo).
 *   - External 5 V supply for the servo (a hobby servo can pull 500 mA+ when
 *     it stalls — the board's 5 V pin usually can't deliver that cleanly).
 *   - Jumper wires.
 *
 * Wiring:
 *   - Servo BROWN/BLACK -> common GND (servo PSU GND AND board GND tied together).
 *   - Servo RED/ORANGE  -> external 5 V supply.
 *   - Servo YELLOW/WHITE -> SERVO_PIN on the board.
 *
 * Cloud dashboard setup:
 *   - Create Z0 of type Int — desired servo angle in degrees (0..180).
 *
 * @category Actuation
 * @level Beginner
 *
 * @hardware
 * - Any supported board.
 * - SG90 (or similar) hobby servo. External 5 V supply recommended for the
 *   servo VCC (do NOT power from MCU 5V pin under load).
 *
 * @wiring
 * - Servo BROWN/BLACK -> GND.
 * - Servo RED/ORANGE  -> 5 V external supply (share GND with MCU).
 * - Servo YELLOW/WHITE -> SERVO_PIN.
 *
 * @lib_deps
 * Arduino IDE: bundled `Servo` library covers ESP8266 / STM32 / AVR.
 * ESP32: install `ESP32Servo` from Library Manager (the stock Servo lib
 * does not exist on ESP32 because the ESP32 uses LEDC PWM instead of timers).
 *
 * @usage
 * 1. Replace WIFI_SSID / WIFI_PASS / DEVICE_ID / DEVICE_TOKEN below.
 * 2. Install ESP32Servo (ESP32 only — see @lib_deps).
 * 3. Arduino IDE: Tools > Partition Scheme > "Minimal SPIFFS (1.9MB APP)" on ESP32.
 * 4. Flash the board.
 * 5. Open Serial Monitor at 115200 baud.
 * 6. Cloud-write an integer 0..180 to Z0; the servo snaps to that angle.
 *
 * Tips & common mistakes:
 *   - If the servo jitters or browns out the board, you ARE powering it from
 *     the MCU 5 V pin. Use a separate supply with a common ground.
 *   - 0 deg and 180 deg are mechanical end-stops. Pushing past these in
 *     software will grind the gears — that's why we clamp to [0, 180].
 *
 * @lib_deps (only if the sketch needs an external Arduino library)
 *   - ESP32Servo (when targeting ESP32).
 */

#include <ZenoPCBMain.h>
#if defined(ESP32)
  // ESP32 has no hardware timer-based Servo lib; ESP32Servo uses the LEDC
  // peripheral and exposes the same Servo API.
  #include <ESP32Servo.h>
#else
  // Standard Arduino Servo library — bundled with the IDE for AVR, ESP8266,
  // STM32, and UNO R4.
  #include <Servo.h>
#endif

using namespace ZenoPCB;

// ---- Credentials ------------------------------------------------------------
#define WIFI_SSID "REPLACE_ME"
#define WIFI_PASS "REPLACE_ME"
#define DEVICE_ID "REPLACE_ME"
#define DEVICE_TOKEN "REPLACE_AT_PROVISIONING"

// ---- Pin map ----------------------------------------------------------------
#if defined(ESP32)
  #define SERVO_PIN 18
#elif defined(ESP8266)
  #define SERVO_PIN 5    // labelled D1
#elif defined(ARDUINO_UNOR4_WIFI)
  #define SERVO_PIN 9
#elif defined(STM32F4)
  #define SERVO_PIN PB6
#elif defined(STM32F1)
  #define SERVO_PIN PA8
#endif

Zeno  zeno;
Servo servo;

// Cloud -> Device: dashboard writes an integer angle (degrees) to Z0.
CLOUD_TO_DEVICE(Z0)
{
    int deg = (int)param.toInt();
    // Clamp to the mechanical safe range — a hobby servo binds against its
    // end-stops if commanded past 0..180.
    if (deg < 0)   deg = 0;
    if (deg > 180) deg = 180;
    servo.write(deg);
    Serial.printf("[Z0] servo -> %d deg\n", deg);
}

void setup()
{
    Serial.begin(115200);
    servo.attach(SERVO_PIN);
    // Known starting position so power-up never crashes the servo arm into
    // whatever was last commanded before reset.
    servo.write(0);

    zeno.wifi(WIFI_SSID, WIFI_PASS)
        .device(DEVICE_ID, DEVICE_TOKEN)
        .enableZKeys()
        .begin();
}

void loop()
{
    zeno.loop();
}
