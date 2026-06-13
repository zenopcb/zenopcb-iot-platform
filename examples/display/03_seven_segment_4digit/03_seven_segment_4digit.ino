/**
 * @file 03_seven_segment_4digit.ino
 * @brief TM1637 4-digit 7-segment display — show numeric value pushed to Z0.
 *
 * @category Display
 * @level Beginner
 *
 * @hardware
 * - Any supported board.
 * - TM1637 4-digit 7-segment display module.
 *
 * @wiring
 * - TM1637 CLK -> CLK_PIN.
 * - TM1637 DIO -> DIO_PIN.
 * - TM1637 VCC -> 3.3 V or 5 V.
 * - TM1637 GND -> GND.
 *
 * @lib_deps
 * `avishorp/TM1637 @ ^1.2.0`
 *
 * @usage
 * 1. Install TM1637 library.
 * 2. Set credentials.
 * 3. Cloud-write an integer to Z0 (range -999..9999); the display shows it.
 *    Values outside range are clamped.
 */

#include <ZenoPCBMain.h>
#include <TM1637Display.h>

using namespace ZenoPCB;

#define WIFI_SSID "REPLACE_ME"
#define WIFI_PASS "REPLACE_ME"
#define DEVICE_ID "REPLACE_ME"
#define DEVICE_TOKEN "REPLACE_AT_PROVISIONING"

#if defined(ESP32)
  #define CLK_PIN 18
  #define DIO_PIN 19
#elif defined(ESP8266)
  #define CLK_PIN 5  // D1
  #define DIO_PIN 4  // D2
#elif defined(ARDUINO_UNOR4_WIFI)
  #define CLK_PIN 6
  #define DIO_PIN 7
#elif defined(STM32F4)
  #define CLK_PIN PB6
  #define DIO_PIN PB7
#elif defined(STM32F1)
  #define CLK_PIN PB6
  #define DIO_PIN PB7
#endif

Zeno          zeno;
TM1637Display display(CLK_PIN, DIO_PIN);

ZENO_READ(Z0)
{
    long v = param.toLong();
    if (v >  9999) v =  9999;
    if (v < -999)  v = -999;
    display.showNumberDec((int)v, false);
    ZENOPCB_PRINTF("[7SEG] Z0 -> %ld\n", v);
}

void setup()
{
    Serial.begin(115200);
    display.setBrightness(0x0f);
    display.clear();

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
