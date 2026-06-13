/**
 * @file 03_i2c_device_scanner.ino
 * @brief Scan the I2C bus 0x03..0x77, publish found addresses as CSV to Z0.
 *
 * @category Communication
 * @level Beginner
 *
 * @hardware
 * - Any supported board.
 * - At least one I2C device connected to SDA / SCL.
 *
 * @wiring
 * - Use the board's default Wire SDA / SCL pins.
 * - 4.7 kOhm pull-ups on SDA + SCL to 3.3 V (most modules already include them).
 *
 * @usage
 * 1. Set credentials.
 * 2. On boot the sketch scans once and prints a list of responding
 *    addresses to Serial + publishes a CSV string to Z0
 *    (e.g. "0x3C, 0x68, 0x76").
 * 3. Push a true to Z1 from the cloud to trigger a re-scan at runtime.
 */

#include <ZenoPCBMain.h>
#include <Wire.h>

using namespace ZenoPCB;

#define WIFI_SSID "REPLACE_ME"
#define WIFI_PASS "REPLACE_ME"
#define DEVICE_ID "REPLACE_ME"
#define DEVICE_TOKEN "REPLACE_AT_PROVISIONING"

Zeno zeno;

static void scanAndPublish()
{
    String csv;
    uint8_t found = 0;
    for (uint8_t addr = 0x03; addr <= 0x77; ++addr)
    {
        Wire.beginTransmission(addr);
        const uint8_t err = Wire.endTransmission();
        if (err == 0)
        {
            if (found > 0) csv += ", ";
            csv += "0x";
            if (addr < 0x10) csv += "0";
            csv += String(addr, HEX);
            ++found;
        }
    }
    if (found == 0) csv = "(none)";
    ZENO_WRITE(Z0, csv);
    ZENOPCB_PRINTF("[I2C] scan: %u device(s) -> %s\n",
                   (unsigned)found, csv.c_str());
}

ZENO_READ(Z1)
{
    if (param.toBool())
    {
        ZENOPCB_PRINTF("[I2C] rescan triggered by cloud\n");
        scanAndPublish();
    }
}

void setup()
{
    Serial.begin(115200);
    Wire.begin();

    zeno.wifi(WIFI_SSID, WIFI_PASS)
        .device(DEVICE_ID, DEVICE_TOKEN)
        .enableZKeys()
        .onZKeyChange(ZKey::Z1, onZ1)
        .begin();

    delay(500); // let MQTT connect first so Z0 publish has a destination
    scanAndPublish();
}

void loop()
{
    zeno.loop();
}
