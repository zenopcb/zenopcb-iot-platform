/**
 * @file 02_ethernet_w5500.ino
 * @brief Wired Ethernet connection to ZenoPCB Cloud + 30 s heartbeat on Z0.
 *
 * @category Connectivity
 * @level Intermediate
 *
 * @hardware
 * - ESP32 + WIZnet W5500 SPI module (canonical ZenoPCB ethernet path)
 * - STM32 Nucleo-F429ZI on-board Ethernet (RMII PHY, no W5500 needed)
 * - Other boards: fall through to a Serial notice — use
 *   `connectivity/01_wifi_basic` instead.
 *
 * @wiring (ESP32 + W5500)
 *   W5500 CS   -> GPIO 5
 *   W5500 RST  -> GPIO 26   (optional, -1 to disable)
 *   W5500 SCK  -> GPIO 18
 *   W5500 MISO -> GPIO 19
 *   W5500 MOSI -> GPIO 23
 *   W5500 VCC  -> 3.3V, GND -> GND
 *
 * @wiring (STM32 Nucleo-F429ZI)
 *   The RMII PHY is on-board — no extra wiring needed. Plug an RJ-45 cable
 *   into the Nucleo's Ethernet jack and you are done.
 *
 * @lib_deps (PlatformIO)
 *   ESP32 path uses the Arduino Core 3.x built-in `ETH.h` (W5500 native
 *   driver) — no extra lib_deps. Requires build flag `-DZENOPCB_ENABLE_ETHERNET`.
 *
 *   STM32 path requires the `STM32Ethernet` library (BSD-3, Arduino official).
 *   Add `stm32duino/STM32Ethernet` to lib_deps.
 *
 * @usage
 * 1. Wire the W5500 (ESP32) or plug a cable into the Nucleo (STM32 F4).
 * 2. Replace credentials below.
 * 3. Flash + open Serial Monitor at 115200 baud.
 * 4. Watch DHCP assign an IP; heartbeat counter publishes to Z0 every 30 s.
 */

#include <ZenoPCBMain.h>

using namespace ZenoPCB;

#if defined(ESP32)
  #ifndef ZENOPCB_ENABLE_ETHERNET
    #warning "Build with -DZENOPCB_ENABLE_ETHERNET to enable W5500 Ethernet"
  #endif
#endif

// ============================================
// Credentials — replace before flashing
// ============================================
#define DEVICE_ID "REPLACE_ME"
#define DEVICE_TOKEN "REPLACE_AT_PROVISIONING"

// ============================================
// Hardware pins (ESP32 + W5500)
// ============================================
#if defined(ESP32)
  #define ETH_CS_PIN 5
  #define ETH_RST_PIN 26
#endif

// ============================================
// Globals — providers must live for the lifetime of the program
// ============================================
Zeno zeno;

#if defined(ESP32) && defined(ZENOPCB_ENABLE_ETHERNET)
  ZenoEthernetProvider ethProvider(ETH_CS_PIN, ETH_RST_PIN);
#endif

static const uint32_t HEARTBEAT_PERIOD_MS = 30000;
static uint32_t s_lastBeat = 0;
static uint32_t s_beatCount = 0;

void setup()
{
    Serial.begin(115200);
    delay(200);
    Serial.println();
    Serial.println(F("[02_ethernet_w5500] boot"));

#if defined(ESP32) && defined(ZENOPCB_ENABLE_ETHERNET)
    zeno.device(DEVICE_ID, DEVICE_TOKEN)
        .setNetworkProvider(&ethProvider)
        .enableZKeys()
        .setZPublishInterval(5000)
        .onConnected([]()
                     { Serial.println(F("[02_ethernet_w5500] cloud connected via Ethernet")); })
        .begin();
#elif defined(STM32F4)
    // The STM32 Nucleo-F429ZI Ethernet path uses the on-board RMII PHY via
    // the STM32Ethernet library. Wire it up through your own
    // `ZenoNetworkProvider` subclass — Phase 7 v0.3.0 ships W5500 as the
    // first-class ethernet provider; native STM32Ethernet adapter is on the
    // backlog. For now, fall through with an explanatory notice.
    Serial.println(F("[02_ethernet_w5500] STM32 F4 native Ethernet provider"));
    Serial.println(F("                    is not bundled in v0.3.0 — see backlog."));
    Serial.println(F("                    Use connectivity/01_wifi_basic instead"));
    Serial.println(F("                    or wire a W5500 module if you need wired."));
#else
    Serial.println(F("[02_ethernet_w5500] Ethernet not available on this platform."));
    Serial.println(F("                    Build for ESP32 (-DZENOPCB_ENABLE_ETHERNET)"));
    Serial.println(F("                    or use connectivity/01_wifi_basic."));
#endif
}

void loop()
{
#if defined(ESP32) && defined(ZENOPCB_ENABLE_ETHERNET)
    const uint32_t now = millis();
    if (now - s_lastBeat >= HEARTBEAT_PERIOD_MS)
    {
        s_lastBeat = now;
        s_beatCount++;
        ZENO_WRITE(Z0, (int32_t)s_beatCount);
        ZENOPCB_PRINTF("[02_ethernet_w5500] heartbeat %lu (IP=%s)\n",
                       (unsigned long)s_beatCount, ethProvider.getLocalIP().c_str());
    }
    zeno.loop();
#else
    // Non-ESP32 fall-through: keep cooperative scheduler running but emit
    // nothing — the boot-time Serial notice already explained the situation.
    delay(1000);
#endif
}
