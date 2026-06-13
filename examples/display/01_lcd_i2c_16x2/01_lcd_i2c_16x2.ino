/**
 * @file 01_lcd_i2c_16x2.ino
 * @brief 16x2 I2C LCD (PCF8574 backpack) — display value pushed to Z0.
 *
 * @category Display
 * @level Beginner
 *
 * @hardware
 * - Any supported board with I2C.
 * - 16x2 LCD with PCF8574 I2C backpack (typical I2C address 0x27 or 0x3F).
 *
 * @wiring
 * - LCD SDA -> SDA_PIN (board default).
 * - LCD SCL -> SCL_PIN (board default).
 * - LCD VCC -> 5 V (most PCF8574 boards expect 5 V).
 * - LCD GND -> GND.
 *
 * @lib_deps
 * Arduino IDE / platformio.ini: `marcoschwartz/LiquidCrystal_I2C @ ^1.1.4`.
 *
 * @usage
 * 1. Install LiquidCrystal_I2C library.
 * 2. Set credentials.
 * 3. Cloud-write any string-ish value to Z0; it appears on line 2 of the LCD.
 * 4. If the LCD address is not 0x27 try 0x3F (and vice-versa).
 */

#include <ZenoPCBMain.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>

using namespace ZenoPCB;

#define WIFI_SSID "REPLACE_ME"
#define WIFI_PASS "REPLACE_ME"
#define DEVICE_ID "REPLACE_ME"
#define DEVICE_TOKEN "REPLACE_AT_PROVISIONING"

static const uint8_t LCD_ADDR = 0x27;
LiquidCrystal_I2C lcd(LCD_ADDR, 16, 2);
Zeno              zeno;

ZENO_READ(Z0)
{
    const String s = param.toString();
    lcd.setCursor(0, 1);
    lcd.print("                "); // clear line 2
    lcd.setCursor(0, 1);
    lcd.print(s.substring(0, 16));
    ZENOPCB_PRINTF("[LCD] Z0 -> %s\n", s.c_str());
}

void setup()
{
    Serial.begin(115200);
    Wire.begin();
    lcd.init();
    lcd.backlight();
    lcd.setCursor(0, 0);
    lcd.print("ZenoPCB ZSignal");

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
