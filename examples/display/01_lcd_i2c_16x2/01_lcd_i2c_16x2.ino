/**
 * @file 01_lcd_i2c_16x2.ino
 * @brief Show a value written from the cloud (Z0) on line 2 of a 16x2 I2C LCD.
 *
 * What you'll learn:
 *   - How to drive a classic 16x2 character LCD over I2C using a PCF8574 backpack
 *   - How to update a display from a CLOUD_TO_DEVICE handler
 *   - How to truncate strings to the LCD's column width without crashing
 *
 * Hardware needed:
 *   - Any supported board with WiFi and I2C
 *   - 16x2 LCD with a PCF8574 I2C backpack (typical addresses: 0x27 or 0x3F)
 *   - Jumper wires, breadboard
 *
 * Wiring:
 *   - LCD SDA -> board default SDA (GPIO 21 on ESP32, A4 on UNO R4)
 *   - LCD SCL -> board default SCL (GPIO 22 on ESP32, A5 on UNO R4)
 *   - LCD VCC -> 5 V (PCF8574 backpacks expect 5 V; the I2C bus pull-ups still
 *     keep SDA/SCL safe for a 3.3 V MCU thanks to open-drain signalling)
 *   - LCD GND -> GND
 *
 * Cloud dashboard setup:
 *   - Create Z0 of type String — text to show on line 2 of the LCD
 *
 * @category Display
 * @level Beginner
 *
 * @hardware
 *   - Any supported board with I2C.
 *   - 16x2 LCD with PCF8574 I2C backpack (address 0x27 or 0x3F).
 *
 * @wiring
 *   See "Wiring" section above.
 *
 * @lib_deps
 *   Arduino IDE / platformio.ini: `marcoschwartz/LiquidCrystal_I2C @ ^1.1.4`.
 *
 * @usage
 *   1. Install the LiquidCrystal_I2C library.
 *   2. Replace credentials below.
 *   3. Cloud-write any string-ish value to Z0; it appears on line 2 of the LCD.
 *   4. If line 1 says "ZenoPCB ZSignal" but Z0 writes never show up, try
 *      changing LCD_ADDR from 0x27 to 0x3F (or vice versa).
 *
 * Tips & common mistakes:
 *   - Two common PCF8574 addresses exist: 0x27 (most modules) and 0x3F (others).
 *     Run examples/communication/03_i2c_device_scanner to find yours.
 *   - The 16x2 LCD shows only 16 characters per line — `substring(0, 16)`
 *     prevents overflow into line 1.
 *   - Backlight off? Wrong VCC? Most modules need 5 V power even with a 3.3 V MCU.
 */

#include <ZenoPCBMain.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>

using namespace ZenoPCB;

#define WIFI_SSID "REPLACE_ME"
#define WIFI_PASS "REPLACE_ME"
#define DEVICE_ID "REPLACE_ME"
#define DEVICE_TOKEN "REPLACE_AT_PROVISIONING"

// I2C bus pins. ESP32 / ESP8266 can remap SDA/SCL, so set them explicitly.
// Other boards use their fixed hardware I2C pins.
#if defined(ESP32)
  #define I2C_SDA 21
  #define I2C_SCL 22
#elif defined(ESP8266)
  #define I2C_SDA 4    // D2 on NodeMCU/Wemos
  #define I2C_SCL 5    // D1 on NodeMCU/Wemos
#endif

static const uint8_t LCD_ADDR = 0x27;     // change to 0x3F if your backpack uses that
LiquidCrystal_I2C lcd(LCD_ADDR, 16, 2);   // 16 columns, 2 rows
Zeno              zeno;

// Cloud -> Device: dashboard wrote a new value to Z0; refresh line 2 of the LCD.
CLOUD_TO_DEVICE(Z0)
{
    const String s = param.toString();
    lcd.setCursor(0, 1);
    lcd.print(" ");                       // clear leftmost char to avoid stale text
    lcd.setCursor(0, 1);
    lcd.print(s.substring(0, 16));        // LCD has 16 columns — truncate longer strings
    Serial.printf("[LCD] Z0 -> %s\n", s.c_str());
}

void setup()
{
    Serial.begin(115200);
#if defined(ESP32) || defined(ESP8266)
    Wire.begin(I2C_SDA, I2C_SCL);
#else
    Wire.begin();
#endif
    lcd.init();
    lcd.backlight();
    lcd.setCursor(0, 0);
    lcd.print("ZenoPCB ZSignal");         // line 1 — static label

    zeno.wifi(WIFI_SSID, WIFI_PASS)
        .device(DEVICE_ID, DEVICE_TOKEN)
        .enableZKeys()
        .begin();
}

void loop()
{
    zeno.loop();                           // keep WiFi + MQTT + Z0 callback alive
}
