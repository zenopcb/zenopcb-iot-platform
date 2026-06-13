/**
 * @file 01_wifi_basic.ino
 * @brief Basic WiFi connection to ZenoPCB Cloud + 30 s heartbeat on Z0.
 *
 * @category Connectivity
 * @level Beginner
 *
 * @hardware
 * - Any supported board with WiFi:
 *     * ESP32 (built-in WiFi)
 *     * ESP8266 (built-in WiFi)
 *     * Arduino UNO R4 WiFi (built-in ESP32-C3 NCP via WiFiS3)
 *     * STM32 Blue Pill F103 (external ESP-AT module via WiFiEspAT)
 *
 *   STM32 Nucleo-F429ZI ships with on-board Ethernet (RMII PHY) and is the
 *   default target for `02_ethernet_w5500` rather than this sketch. If you
 *   want WiFi on the Nucleo, attach an ESP-AT module and the `WiFiEspAT`
 *   library will work the same as on the Blue Pill.
 *
 * @wiring
 * - None. The sketch uses each board's default WiFi peripheral.
 *
 * @usage
 * 1. Set WIFI_SSID + WIFI_PASS + DEVICE_ID + DEVICE_TOKEN below.
 * 2. Flash + open Serial Monitor at 115200 baud.
 * 3. Watch the device connect, then a heartbeat counter publishes to Z0
 *    every 30 s. Confirm on the ZenoPCB Cloud dashboard.
 */

#include <ZenoPCBMain.h>

using namespace ZenoPCB;

// ============================================
// Credentials — replace before flashing
// ============================================
#define WIFI_SSID "REPLACE_ME"
#define WIFI_PASS "REPLACE_ME"
#define DEVICE_ID "REPLACE_ME"
#define DEVICE_TOKEN "REPLACE_AT_PROVISIONING"

// ============================================
// Globals
// ============================================
Zeno zeno;

static const uint32_t HEARTBEAT_PERIOD_MS = 30000;
static uint32_t s_lastBeat = 0;
static uint32_t s_beatCount = 0;

void setup()
{
    Serial.begin(115200);
    delay(200);
    Serial.println();
    Serial.println(F("[01_wifi_basic] boot"));

    zeno.wifi(WIFI_SSID, WIFI_PASS)
        .device(DEVICE_ID, DEVICE_TOKEN)
        .enableZKeys()
        .setZPublishInterval(5000) // publish at most every 5 s
        .onConnected([]()
                     { Serial.println(F("[01_wifi_basic] cloud connected")); })
        .begin();
}

void loop()
{
    const uint32_t now = millis();
    if (now - s_lastBeat >= HEARTBEAT_PERIOD_MS)
    {
        s_lastBeat = now;
        s_beatCount++;
        ZENO_WRITE(Z0, (int32_t)s_beatCount);
        ZENOPCB_PRINTF("[01_wifi_basic] heartbeat %lu\n", (unsigned long)s_beatCount);
    }

    zeno.loop();
}
