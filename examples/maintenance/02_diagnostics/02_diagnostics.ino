/**
 * @file 02_diagnostics.ino
 * @brief Auto-publish device diagnostics to ZenoPCB Cloud.
 *
 * @category Maintenance
 * @level Beginner
 *
 * @hardware
 * - Any supported board with the `CAP_DIAGNOSTICS` capability bit set —
 *   ESP32, ESP8266, UNO R4 WiFi, STM32 Nucleo-F429ZI. STM32 Blue Pill F103
 *   (MICRO_BASIC profile) drops Diagnostics by design.
 *
 * @wiring
 * - None — diagnostics piggyback on the existing MQTT connection.
 *
 * @usage
 * 1. Replace placeholders below.
 * 2. Flash; open Serial Monitor at 115200 baud.
 * 3. Watch the dashboard's diagnostics widget — a record arrives every 60 s
 *    (interval set short so the demo is visible inside a minute). Production
 *    default is 600000 (10 min).
 *
 * Fields included in each report: freeHeap, totalHeap, maxAllocHeap, uptimeMs,
 * wifiRSSI, fwVersion, ipAddr.
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

// ============================================
// Setup
// ============================================
void setup()
{
    Serial.begin(115200);

    zeno.wifi(WIFI_SSID, WIFI_PASS)
        .device(DEVICE_ID, DEVICE_TOKEN)
        .enableDiagnostics(60000) // every 60 s — production default is 600000 (10 min)
        .setConnectionType("WIFI")
        .begin();
}

// ============================================
// Loop
// ============================================
void loop()
{
    zeno.loop();
}
