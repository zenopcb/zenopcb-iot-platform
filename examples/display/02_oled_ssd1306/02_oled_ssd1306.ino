/**
 * @file 02_oled_ssd1306.ino
 * @brief SSD1306 128x64 OLED — render Z0..Z3 as 4 labelled rows.
 *
 * @category Display
 * @level Intermediate
 *
 * @hardware
 * - Any supported board with I2C.
 * - SSD1306 128x64 I2C OLED (typical I2C address 0x3C).
 *
 * @wiring
 * - OLED SDA / SCL -> board default I2C pins.
 * - OLED VCC -> 3.3 V (most SSD1306 boards are 3.3 V; check yours).
 * - OLED GND -> GND.
 *
 * @lib_deps
 * - `adafruit/Adafruit GFX Library`
 * - `adafruit/Adafruit SSD1306`
 *
 * @usage
 * 1. Install libs.
 * 2. Set credentials.
 * 3. Cloud-write Z0..Z3 (numeric or string). Each gets a row on the OLED;
 *    on-screen refresh happens immediately on cloud update.
 */

#include <ZenoPCBMain.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

using namespace ZenoPCB;

#define WIFI_SSID "REPLACE_ME"
#define WIFI_PASS "REPLACE_ME"
#define DEVICE_ID "REPLACE_ME"
#define DEVICE_TOKEN "REPLACE_AT_PROVISIONING"

#define OLED_W   128
#define OLED_H   64
#define OLED_ADDR 0x3C

Adafruit_SSD1306 oled(OLED_W, OLED_H, &Wire, -1);
Zeno             zeno;

static String s_row[4] = { "-", "-", "-", "-" };

static void render()
{
    oled.clearDisplay();
    oled.setTextSize(1);
    oled.setTextColor(SSD1306_WHITE);
    for (int i = 0; i < 4; ++i)
    {
        oled.setCursor(0, i * 16);
        oled.printf("Z%d: %s", i, s_row[i].c_str());
    }
    oled.display();
}

ZENO_READ(Z0) { s_row[0] = param.toString(); render(); }
ZENO_READ(Z1) { s_row[1] = param.toString(); render(); }
ZENO_READ(Z2) { s_row[2] = param.toString(); render(); }
ZENO_READ(Z3) { s_row[3] = param.toString(); render(); }

void setup()
{
    Serial.begin(115200);
    Wire.begin();
    if (!oled.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR))
    {
        ZENOPCB_PRINTF("[OLED] init failed @ 0x%02X\n", OLED_ADDR);
    }
    render();

    zeno.wifi(WIFI_SSID, WIFI_PASS)
        .device(DEVICE_ID, DEVICE_TOKEN)
        .enableZKeys()
        .onZKeyChange(ZKey::Z0, onZ0)
        .onZKeyChange(ZKey::Z1, onZ1)
        .onZKeyChange(ZKey::Z2, onZ2)
        .onZKeyChange(ZKey::Z3, onZ3)
        .begin();
}

void loop()
{
    zeno.loop();
}
