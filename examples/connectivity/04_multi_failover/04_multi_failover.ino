/**
 * @file 04_multi_failover.ino
 * @brief Priority failover: WiFi -> Ethernet -> 4G with active link reported on Z0.
 *
 * @category Connectivity
 * @level Advanced
 *
 * @hardware
 * - ESP32 only (Phase 7 D-03 — Ethernet + Cellular providers are ESP32-only
 *   in v0.3.0). Non-ESP32 boards fall through with a Serial notice.
 * - WiFi: built into ESP32 (no extra hardware)
 * - Ethernet: WIZnet W5500 SPI module (see `02_ethernet_w5500.ino` for pin
 *   table)
 * - 4G: SIM7600 family modem on UART2 (see `03_4g_sim7600.ino` for pin
 *   table)
 *
 * @wiring
 *   Same as `02_ethernet_w5500.ino` + `03_4g_sim7600.ino` combined — all
 *   three peripherals plugged in at once.
 *
 *     W5500 CS  -> GPIO 5    SIM7600 TX  -> GPIO 16  (ESP32 RX2)
 *     W5500 RST -> GPIO 26   SIM7600 RX  <- GPIO 17  (ESP32 TX2)
 *     W5500 SCK -> GPIO 18   SIM7600 PWR -> GPIO 4
 *     W5500 MISO-> GPIO 19
 *     W5500 MOSI-> GPIO 23
 *
 * @lib_deps (PlatformIO)
 *   build_flags =
 *       -DZENOPCB_ENABLE_ETHERNET
 *       -DZENOPCB_ENABLE_CELLULAR
 *       -DTINY_GSM_MODEM_SIM7600
 *
 *   No extra lib_deps — the W5500 driver is in `ETH.h` (Arduino Core 3.x);
 *   TinyGSM is vendored inside the library.
 *
 * @usage
 * 1. Wire W5500 + SIM7600 + ensure WiFi credentials are flashed.
 * 2. Build with all three flags.
 * 3. Replace credentials below.
 * 4. Flash + watch Serial Monitor — the active provider name is published
 *    to Z0 every 10 s. Unplug Ethernet to see failover to WiFi, then disable
 *    WiFi to see failover to 4G.
 *
 * Priority order (first added = highest):
 *   1. Ethernet  (fastest + most deterministic)
 *   2. WiFi      (free, low-latency when working)
 *   3. 4G        (last-resort fallback)
 */

#include <ZenoPCBMain.h>

using namespace ZenoPCB;

#if defined(ESP32)
  #if !defined(ZENOPCB_ENABLE_ETHERNET) || !defined(ZENOPCB_ENABLE_CELLULAR)
    #warning "Build with -DZENOPCB_ENABLE_ETHERNET -DZENOPCB_ENABLE_CELLULAR for full failover"
  #endif
#endif

// ============================================
// Credentials — replace before flashing
// ============================================
#define WIFI_SSID "REPLACE_ME"
#define WIFI_PASS "REPLACE_ME"
#define DEVICE_ID "REPLACE_ME"
#define DEVICE_TOKEN "REPLACE_AT_PROVISIONING"

// ============================================
// Hardware pins (ESP32)
// ============================================
#if defined(ESP32)
  #define ETH_CS_PIN 5
  #define ETH_RST_PIN 26
  #define MODEM_TX_PIN 17
  #define MODEM_RX_PIN 16
  #define MODEM_PWR_PIN 4
#endif

// ============================================
// Globals — providers MUST live for the lifetime of the program
// ============================================
Zeno zeno;

#if defined(ESP32) && defined(ZENOPCB_ENABLE_ETHERNET) && defined(ZENOPCB_ENABLE_CELLULAR)
  ZenoEthernetProvider ethProvider(ETH_CS_PIN, ETH_RST_PIN);
  Zeno4GProvider       cellProvider(MODEM_TX_PIN, MODEM_RX_PIN, MODEM_PWR_PIN);
  ZenoMultiConnectProvider multiProvider;
#endif

static const uint32_t PUBLISH_PERIOD_MS = 10000;
static uint32_t s_lastPublish = 0;

void setup()
{
    Serial.begin(115200);
    delay(200);
    Serial.println();
    Serial.println(F("[04_multi_failover] boot"));

#if defined(ESP32) && defined(ZENOPCB_ENABLE_ETHERNET) && defined(ZENOPCB_ENABLE_CELLULAR)
    // First-added provider = highest priority. Ethernet -> WiFi -> 4G.
    // WiFi is handled by the Zeno core (.wifi() below) so we don't add it
    // explicitly to multiProvider — the core's WiFi path activates as the
    // fallback whenever neither external provider is connected.
    multiProvider.addProvider(&ethProvider);   // priority 1
    multiProvider.addProvider(&cellProvider);  // priority 2 (fallback to 4G)

    zeno.wifi(WIFI_SSID, WIFI_PASS)            // priority 3 (core fallback)
        .device(DEVICE_ID, DEVICE_TOKEN)
        .setNetworkProvider(&multiProvider)
        .enableZKeys()
        .setZPublishInterval(5000)
        .onConnected([]()
                     { Serial.println(F("[04_multi_failover] cloud connected (some link)")); })
        .begin();
#else
    Serial.println(F("[04_multi_failover] Multi-connectivity not available on this platform."));
    Serial.println(F("                    Ethernet + Cellular providers are ESP32-only in v0.3.0."));
    Serial.println(F("                    Use connectivity/01_wifi_basic for plain WiFi."));
#endif
}

void loop()
{
#if defined(ESP32) && defined(ZENOPCB_ENABLE_ETHERNET) && defined(ZENOPCB_ENABLE_CELLULAR)
    const uint32_t now = millis();
    if (now - s_lastPublish >= PUBLISH_PERIOD_MS)
    {
        s_lastPublish = now;
        // Publish the active provider name + IP so the dashboard can show
        // which link is currently carrying traffic.
        const char *activeName = multiProvider.getName();
        String      activeIP   = multiProvider.getLocalIP();
        ZENO_WRITE(Z0, String(activeName));
        ZENO_WRITE(Z1, activeIP);
        ZENOPCB_PRINTF("[04_multi_failover] active=%s ip=%s\n",
                       activeName, activeIP.c_str());
    }
    zeno.loop();
#else
    delay(1000);
#endif
}
