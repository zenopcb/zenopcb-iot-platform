/**
 * @file 03_4g_sim7600.ino
 * @brief 4G/LTE cellular connection to ZenoPCB Cloud + 60 s heartbeat on Z0.
 *
 * @category Connectivity
 * @level Intermediate
 *
 * @hardware
 * - ESP32 only (Phase 7 D-03 — Modbus + Cellular are ESP32-only library
 *   subsystems on the v0.3.0 release). Non-ESP32 boards fall through with
 *   a Serial notice.
 * - SIM7600E / SIM7600G / SIM7670 / A7680C 4G modem module connected
 *   to ESP32 UART2.
 *
 * @wiring (ESP32 + SIM7600 family)
 *   ESP32  GPIO 17 (TX2)  -> Modem RX
 *   ESP32  GPIO 16 (RX2)  -> Modem TX
 *   ESP32  GPIO 4         -> Modem PWRKEY
 *   GND                    -> GND
 *   External 5 V supply    -> Modem VCC (4.0 .. 5.0 V — do NOT use ESP32 3V3)
 *
 *   The pins above match the canonical ZenoPCB Mini Gateway (ZMG-01) layout.
 *   Other modules in the TinyGSM family (A7670, SIM800, BG96, MC60, EC200U)
 *   work too — set the matching `TINY_GSM_MODEM_*` define at the very top.
 *
 * @lib_deps (PlatformIO)
 *   build_flags =
 *       -DZENOPCB_ENABLE_CELLULAR
 *       -DTINY_GSM_MODEM_SIM7600
 *
 *   No extra lib_deps — TinyGSM is vendored inside the library as
 *   `lib/ZenoPCB/src/vendor/TinyGSM/`.
 *
 * @usage
 * 1. Insert a SIM card with active data + APN in the modem.
 * 2. Build with `-DZENOPCB_ENABLE_CELLULAR -DTINY_GSM_MODEM_SIM7600`.
 * 3. Replace credentials below.
 * 4. (Optional) Configure APN once via the captive portal (hold IO-0 for
 *    3 s on first boot) — the library remembers it in NVS afterwards.
 * 5. Flash + watch Serial Monitor; heartbeat publishes every 60 s
 *    (cellular data budget-friendly).
 */

// Cellular modem family (must precede #include). Uncomment the matching one:
#define TINY_GSM_MODEM_SIM7600   // SIM7600E / SIM7600G / SIM7670 / A7680C
// #define TINY_GSM_MODEM_A7670    // A7670C / A7670E
// #define TINY_GSM_MODEM_SIM800   // SIM800L / SIM800C
// #define TINY_GSM_MODEM_EC200U   // Quectel EC200U / EC600N
// #define TINY_GSM_MODEM_BG96     // Quectel BG96
// #define TINY_GSM_MODEM_MC60     // Quectel MC60

#include <ZenoPCBMain.h>

using namespace ZenoPCB;

#if defined(ESP32)
  #ifndef ZENOPCB_ENABLE_CELLULAR
    #warning "Build with -DZENOPCB_ENABLE_CELLULAR to enable 4G"
  #endif
#endif

// ============================================
// Credentials — replace before flashing
// ============================================
#define DEVICE_ID "REPLACE_ME"
#define DEVICE_TOKEN "REPLACE_AT_PROVISIONING"

// ============================================
// Modem pins (ESP32 + SIM7600)
// ============================================
#if defined(ESP32)
  #define MODEM_TX_PIN 17    // ESP32 TX2 -> Modem RX
  #define MODEM_RX_PIN 16    // ESP32 RX2 <- Modem TX
  #define MODEM_PWR_PIN 4    // Modem PWRKEY
  #define MODEM_RST_PIN 255  // 255 = not connected
#endif

// ============================================
// Globals — provider must live for the lifetime of the program
// ============================================
Zeno zeno;

#if defined(ESP32) && defined(ZENOPCB_ENABLE_CELLULAR)
  Zeno4GProvider cellProvider(MODEM_TX_PIN, MODEM_RX_PIN,
                              MODEM_PWR_PIN, MODEM_RST_PIN);
#endif

static const uint32_t HEARTBEAT_PERIOD_MS = 60000; // 60 s — friendly to data budget
static uint32_t s_lastBeat = 0;
static uint32_t s_beatCount = 0;

void setup()
{
    Serial.begin(115200);
    delay(200);
    Serial.println();
    Serial.println(F("[03_4g_sim7600] boot"));

#if defined(ESP32) && defined(ZENOPCB_ENABLE_CELLULAR)
    zeno.device(DEVICE_ID, DEVICE_TOKEN)
        .setNetworkProvider(&cellProvider)
        .enableZKeys()
        .setZPublishInterval(10000) // 10 s — but heartbeat is every 60 s
        .onConnected([]()
                     { Serial.println(F("[03_4g_sim7600] cloud connected via 4G")); })
        .begin();
#else
    Serial.println(F("[03_4g_sim7600] Cellular not available on this platform."));
    Serial.println(F("                Cellular is ESP32-only in v0.3.0."));
    Serial.println(F("                Use connectivity/01_wifi_basic instead."));
#endif
}

void loop()
{
#if defined(ESP32) && defined(ZENOPCB_ENABLE_CELLULAR)
    const uint32_t now = millis();
    if (now - s_lastBeat >= HEARTBEAT_PERIOD_MS)
    {
        s_lastBeat = now;
        s_beatCount++;
        ZENO_WRITE(Z0, (int32_t)s_beatCount);
        ZENOPCB_PRINTF("[03_4g_sim7600] heartbeat %lu (signal=%d, IP=%s)\n",
                       (unsigned long)s_beatCount,
                       cellProvider.getSignalQuality(),
                       cellProvider.getLocalIP().c_str());
    }
    zeno.loop();
#else
    delay(1000);
#endif
}
