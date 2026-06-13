/**
 * @file 03_servo_position.ino
 * @brief SG90 / MG90S servo positioned by cloud value on Z0 (degrees 0..180).
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
 * Arduino IDE: bundled `Servo` library covers ESP32 / ESP8266 / STM32 / AVR.
 * platformio.ini may need explicit `arduino-libraries/Servo` on some boards.
 *
 * @usage
 * 1. Set credentials.
 * 2. Cloud-write an integer 0..180 to Z0; the servo rotates to that angle.
 */

#include <ZenoPCBMain.h>
#include <Servo.h>

using namespace ZenoPCB;

#define WIFI_SSID "REPLACE_ME"
#define WIFI_PASS "REPLACE_ME"
#define DEVICE_ID "REPLACE_ME"
#define DEVICE_TOKEN "REPLACE_AT_PROVISIONING"

#if defined(ESP32)
  #define SERVO_PIN 18
#elif defined(ESP8266)
  #define SERVO_PIN 5    // D1
#elif defined(ARDUINO_UNOR4_WIFI)
  #define SERVO_PIN 9
#elif defined(STM32F4)
  #define SERVO_PIN PB6
#elif defined(STM32F1)
  #define SERVO_PIN PA8
#endif

Zeno  zeno;
Servo servo;

ZENO_READ(Z0)
{
    int deg = (int)param.toLong();
    if (deg < 0)   deg = 0;
    if (deg > 180) deg = 180;
    servo.write(deg);
    ZENOPCB_PRINTF("[Z0] servo -> %d deg\n", deg);
}

void setup()
{
    Serial.begin(115200);
    servo.attach(SERVO_PIN);
    servo.write(0);   // failsafe at the 0-deg endpoint

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
