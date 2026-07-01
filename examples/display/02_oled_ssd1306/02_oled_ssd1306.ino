/**
 * @file 02_oled_ssd1306.ino
 * @brief 128x64 SSD1306 OLED — render four cloud-driven slots (Z0..Z3) as labelled rows.
 *
 * What you'll learn:
 *   - How to initialize and draw on an SSD1306 OLED with Adafruit GFX
 *   - How to keep a small in-memory model (`s_row[]`) and re-render on change
 *   - How to register multiple CLOUD_TO_DEVICE handlers, one per Z-key
 *
 * Hardware needed:
 *   - Any supported board with WiFi and I2C
 *   - SSD1306 128x64 I2C OLED (default I2C address 0x3C; some are 0x3D)
 *   - Jumper wires, breadboard
 *
 * Wiring:
 *   - OLED SDA -> board default SDA (GPIO 21 on ESP32, A4 on UNO R4)
 *   - OLED SCL -> board default SCL (GPIO 22 on ESP32, A5 on UNO R4)
 *   - OLED VCC -> 3.3 V (most SSD1306 boards are 3.3 V; check before powering)
 *   - OLED GND -> GND
 *
 * Cloud dashboard setup:
 *   - Create Z0..Z3 of type String (or numeric) — each appears on its own OLED row
 *
 * @category Display
 * @level Intermediate
 *
 * @hardware
 *   - Any supported board with I2C.
 *   - SSD1306 128x64 I2C OLED (typical I2C address 0x3C).
 *
 * @wiring
 *   See "Wiring" section above.
 *
 * @lib_deps
 *   - Adafruit GFX Library — https://github.com/adafruit/Adafruit-GFX-Library
 *   - Adafruit SSD1306 — https://github.com/adafruit/Adafruit_SSD1306
 *
 * @usage
 *   1. Install the two Adafruit libraries.
 *   2. Replace credentials below.
 *   3. Cloud-write Z0..Z3 (numeric or string). Each gets its own row on the
 *      OLED; the display refreshes immediately on each cloud update.
 *
 * Tips & common mistakes:
 *   - Address mismatch is the #1 issue. If `oled.begin()` fails, your panel
 *     may be at 0x3D — change OLED_ADDR.
 *   - The screen has only 64 vertical pixels: 4 rows x 16 px each = 64 px.
 *     If you want more rows, drop to text size that fits.
 *   - SSD1306 OLEDs are 1-bit (white on black). Forget colour APIs — use
 *     SSD1306_WHITE for pixels and SSD1306_BLACK for the background.
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

#define OLED_W   128                       // pixels wide
#define OLED_H   64                        // pixels tall
#define OLED_ADDR 0x3C                     // try 0x3D if 0x3C doesn't ACK

// I2C bus pins. ESP32 / ESP8266 can remap SDA/SCL, so set them explicitly.
// Other boards use their fixed hardware I2C pins.
#if defined(ESP32)
  #define I2C_SDA 21
  #define I2C_SCL 22
#elif defined(ESP8266)
  #define I2C_SDA 4    // D2 on NodeMCU/Wemos
  #define I2C_SCL 5    // D1 on NodeMCU/Wemos
#endif

// -1 = no separate reset pin; the SSD1306 self-resets on power-up.
Adafruit_SSD1306 oled(OLED_W, OLED_H, &Wire, -1);
Zeno             zeno;

// In-memory model — one string per row. "-" = "nothing pushed yet".
static String s_row[4] = { "-", "-", "-", "-" };

// Clear and redraw all four rows from the local model.
static void render()
{
    oled.clearDisplay();
    oled.setTextSize(1);                   // 6x8 pixel glyphs
    oled.setTextColor(SSD1306_WHITE);
    for (int i = 0; i < 4; ++i)
    {
        // 64 px tall / 4 rows = 16 px per row (cursor Y in pixels)
        oled.setCursor(0, i * 16);
        oled.printf("Z%d: %s", i, s_row[i].c_str());
    }
    oled.display();                        // push the framebuffer to the panel
}

// Cloud -> Device: each Z-key writes to its slot in the model and triggers redraw.
CLOUD_TO_DEVICE(Z0) { s_row[0] = param.toString(); render(); }
CLOUD_TO_DEVICE(Z1) { s_row[1] = param.toString(); render(); }
CLOUD_TO_DEVICE(Z2) { s_row[2] = param.toString(); render(); }
CLOUD_TO_DEVICE(Z3) { s_row[3] = param.toString(); render(); }

void setup()
{
    Serial.begin(115200);
#if defined(ESP32) || defined(ESP8266)
    Wire.begin(I2C_SDA, I2C_SCL);
#else
    Wire.begin();
#endif
    // SSD1306_SWITCHCAPVCC tells the driver to generate the panel's display
    // voltage from the 3.3 V input via the on-board charge pump.
    if (!oled.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR))
    {
        Serial.printf("[OLED] init failed @ 0x%02X\n", OLED_ADDR);
    }
    render();                              // draw the empty dashes once at boot

    zeno.wifi(WIFI_SSID, WIFI_PASS)
        .device(DEVICE_ID, DEVICE_TOKEN)
        .enableZKeys()
        .begin();
}

void loop()
{
    zeno.loop();
}
