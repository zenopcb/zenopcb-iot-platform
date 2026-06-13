/**
 * @file 01_ota_basic.ino
 * @brief OTA firmware update from ZenoPCB Cloud — MQTT-triggered.
 *
 * @category Maintenance
 * @level Intermediate
 *
 * @hardware
 * - Any ESP32 dev board with at least 4 MB flash and an OTA-aware partition
 *   layout (the default Arduino ESP32 partition layout works).
 * - OTA is ESP32-only on the current release (Phase 7 D-16 RESCOPED — UNO R4
 *   custom OTA opt-in via `-DZENOPCB_ENABLE_UNOR4_OTA`; STM32 OTA off-default).
 *
 * @usage
 * 1. Replace placeholders below + set FIRMWARE_VER to your current build.
 * 2. Flash this sketch.
 * 3. From the ZenoPCB Cloud dashboard, push a new firmware build (built with
 *    the same FIRMWARE_VER scheme bumped) — the device picks it up, streams
 *    progress through onOTAProgress, and reboots on success.
 * 4. Pattern G surface in loop() below shows how user code can also trigger
 *    OTA from any command path (MQTT, button, scheduled task, etc.).
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
#define FIRMWARE_VER "1.0.0"

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
    ZENOPCB_PRINTF("[OTA] Boot, firmware v%s\n", FIRMWARE_VER);

    zeno.wifi(WIFI_SSID, WIFI_PASS)
        .device(DEVICE_ID, DEVICE_TOKEN)
        .enableOTA()
        .onOTAProgress([](float pct)
                       { ZENOPCB_PRINTF("[OTA] progress %.1f%%\n", pct); })
        .onOTAComplete([](const String &ver)
                       { ZENOPCB_PRINTF("[OTA] complete, now v%s — rebooting\n", ver.c_str()); })
        .onOTAError([](OTAError code, const String &msg)
                    { ZENOPCB_PRINTF("[OTA] error %d: %s\n", (int)code, msg.c_str()); })
        .begin();
}

// ============================================
// Loop
// ============================================
void loop()
{
    zeno.loop();

    // ============================================
    // Pattern G — Phase 7 D-06 canonical surface for cloud-triggered OTA.
    //
    // The fluent enableOTA() + onOTAProgress / onOTAComplete / onOTAError
    // callback chain above (D-07) continues to work for cloud-pushed OTA
    // commands. The switch pattern below is the additional Pattern G
    // surface for user code that wants to trigger an OTA itself (e.g.,
    // from its own MQTT command handler, button-press routine, scheduled
    // task, etc.).
    //
    // In a real application `mqttCommand` would arrive from your message
    // handler; here it is a static placeholder so the demo compiles
    // unchanged. The switch covers all four ZenoCapability outcomes —
    // there is NO bool alias (per OQ-4 RESOLVED, user direction).
    // ============================================
    static String mqttCommand = "";  // placeholder — wire to your MQTT handler in production
    if (mqttCommand.startsWith("OTA:"))
    {
        String urlStr = mqttCommand.substring(4);
        switch (zeno.ota(urlStr.c_str()))
        {
            case ZenoCapability::OK:
                Serial.println(F("[OTA] download started"));
                break;
            case ZenoCapability::Unavailable:
                Serial.println(F("[OTA] not available on this platform"));
                break;
            case ZenoCapability::Error:
                Serial.println(F("[OTA] download failed — check URL or network"));
                break;
            case ZenoCapability::Pending:
                Serial.println(F("[OTA] already in progress, ignoring duplicate request"));
                break;
        }
        mqttCommand = "";
    }
}
