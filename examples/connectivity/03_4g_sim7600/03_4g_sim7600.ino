/**
 * @file 03_4g_sim7600.ino
 * @brief Connect to the ZenoPCB Cloud over a 4G/LTE cellular modem and publish a heartbeat.
 *
 * What you'll learn:
 *   - How to bring up a cellular link from an ESP32 (great for outdoor/remote devices)
 *   - How to point the library at a SIM7600-family modem via Zeno4GProvider
 *   - Why heartbeat cadence matters when paying for cellular data
 *
 * Hardware needed:
 *   - ESP32 dev board (cellular support is ESP32-only in v0.3.0)
 *   - SIM7600-family 4G modem module: SIM7600E / SIM7600G / SIM7670 / A7680C
 *     (the same TinyGSM family also covers A7670, SIM800, BG96, MC60, EC200U)
 *   - Active SIM card with a data plan (and a known APN for your carrier)
 *   - External 5 V power supply for the modem — modems pull 2 A peaks that
 *     a typical USB port cannot provide
 *   - Jumper wires, breadboard
 *
 * Wiring (ESP32 + SIM7600):
 *   - ESP32 GPIO 17 (TX2) -> Modem RX     (ESP32 talks AT commands to the modem)
 *   - ESP32 GPIO 16 (RX2) <- Modem TX     (modem replies)
 *   - ESP32 GPIO 4        -> Modem PWRKEY (power-on pulse)
 *   - ESP32 GND            -> Modem GND   (common ground is mandatory)
 *   - External 5 V supply  -> Modem VCC   (4.0 - 5.0 V — do NOT use ESP32 3V3)
 *
 *   The pins above match the canonical ZenoPCB Mini Gateway (ZMG-01) layout.
 *
 * Cloud dashboard setup:
 *   - Create Z0 of type Int — receives the heartbeat counter
 *
 * Build flags (required):
 *   - -DZENOPCB_ENABLE_CELLULAR       enables the cellular code paths in the library
 *   - -DTINY_GSM_MODEM_SIM7600        selects the SIM7600 modem family (change to
 *                                     -DTINY_GSM_MODEM_A7670 / _SIM800 / _BG96 / etc.
 *                                     if you use a different module)
 *   How to set:
 *     PlatformIO:  build_flags = -DZENOPCB_ENABLE_CELLULAR -DTINY_GSM_MODEM_SIM7600
 *     Arduino IDE: edit `platform.local.txt` to add the flags, or define them at
 *                  the very top of this sketch BEFORE `#include <ZenoPCBMain.h>`.
 *
 * @category Connectivity
 * @level Intermediate
 *
 * @hardware
 *   - ESP32 only (Modbus + Cellular are ESP32-only subsystems in v0.3.0).
 *     Non-ESP32 boards print a Serial notice and stop.
 *   - SIM7600E / SIM7600G / SIM7670 / A7680C 4G modem on ESP32 UART2.
 *
 * @wiring
 *   See "Wiring" section above.
 *
 * @lib_deps (PlatformIO)
 *   build_flags = -DZENOPCB_ENABLE_CELLULAR -DTINY_GSM_MODEM_SIM7600
 *
 *   No extra lib_deps — TinyGSM is vendored inside the library at
 *   `lib/ZenoPCB/src/vendor/TinyGSM/`.
 *
 * @usage
 *   1. Insert a SIM card with active data and a known APN into the modem.
 *   2. Build with `-DZENOPCB_ENABLE_CELLULAR -DTINY_GSM_MODEM_SIM7600`.
 *   3. Replace DEVICE_ID + DEVICE_TOKEN below.
 *   4. (Optional) Configure the APN once via the captive portal: hold IO-0
 *      for 3 seconds on first boot. The library stores it in NVS afterwards.
 *   5. Flash and open the Serial Monitor at 115200 baud. A heartbeat
 *      publishes to Z0 every 60 seconds — slower cadence saves cellular data.
 *
 * Tips & common mistakes:
 *   - Cellular modems draw heavy current spikes (up to 2 A during TX bursts).
 *     A weak power supply causes the modem to brown-out and disappear from
 *     the bus. Use a dedicated 5 V / 2 A buck supply.
 *   - First registration after power-on takes 15-60 seconds. Be patient and
 *     check the Serial Monitor for `getSignalQuality()` values — 0 means
 *     "no signal", 31 is "excellent".
 *   - Heartbeats are intentionally every 60 s (not 30 s like WiFi) because
 *     cellular data plans are usually metered.
 */

// IMPORTANT: select your modem family BEFORE `#include <ZenoPCBMain.h>`.
// TinyGSM uses this macro to pull in the right AT-command table.
#define TINY_GSM_MODEM_SIM7600   // SIM7600E / SIM7600G / SIM7670 / A7680C
// #define TINY_GSM_MODEM_A7670    // A7670C / A7670E
// #define TINY_GSM_MODEM_SIM800   // SIM800L / SIM800C
// #define TINY_GSM_MODEM_EC200U   // Quectel EC200U / EC600N
// #define TINY_GSM_MODEM_BG96     // Quectel BG96
// #define TINY_GSM_MODEM_MC60     // Quectel MC60

#include <ZenoPCBMain.h>

using namespace ZenoPCB;

// Friendly warning if the user forgot the cellular build flag.
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
  #define MODEM_PWR_PIN 4    // Modem PWRKEY (power-on pulse); -1 if not wired
  #define MODEM_RST_PIN -1   // Modem RESET line; -1 = not wired. Set to the
                             // GPIO on the modem RESET pin to let the library
                             // hardware-reset the modem when it hangs / no SIM.
#endif

// ============================================
// Globals — provider must live for the lifetime of the program
// ============================================
Zeno zeno;

#if defined(ESP32) && defined(ZENOPCB_ENABLE_CELLULAR)
  // The cellular provider owns the UART + power-key + reset lines.
  Zeno4GProvider cellProvider(MODEM_TX_PIN, MODEM_RX_PIN,
                              MODEM_PWR_PIN, MODEM_RST_PIN);
#endif

static uint32_t s_beatCount = 0;

#if defined(ESP32) && defined(ZENOPCB_ENABLE_CELLULAR)
// Device -> Cloud: heartbeat every 60 seconds. Slower than WiFi (30 s) to
// keep cellular bills small. Each line also logs signal strength + IP so you
// can see whether the modem is registered on the network.
ZENO_EVERY(60000)
{
    s_beatCount++;
    DEVICE_TO_CLOUD(Z0, (int32_t)s_beatCount);
    Serial.printf("[03_4g_sim7600] heartbeat %lu (signal=%d, IP=%s)\n",
                   (unsigned long)s_beatCount,
                   cellProvider.getSignalQuality(),  // 0 = none, 31 = excellent
                   cellProvider.getLocalIP().c_str());
}
#endif

void setup()
{
    Serial.begin(115200);
    delay(200);                                       // let USB-CDC enumerate
    Serial.println();
    Serial.println(F("[03_4g_sim7600] boot"));

#if defined(ESP32) && defined(ZENOPCB_ENABLE_CELLULAR)
    // Same fluent builder as the WiFi sketch, except the network is provided
    // by `cellProvider` (the 4G modem driver) instead of WiFi credentials.
    zeno.device(DEVICE_ID, DEVICE_TOKEN)
        .setNetworkProvider(&cellProvider)
        .enableZKeys()
        .onConnected([]()
                     { Serial.println(F("[03_4g_sim7600] cloud connected via 4G")); })
        .begin();
#else
    Serial.println(F("[03_4g_sim7600] Cellular not available on this platform."));
    Serial.println(F(" Cellular is ESP32-only in v0.3.0."));
    Serial.println(F(" Use connectivity/01_wifi_basic instead."));
#endif
}

void loop()
{
#if defined(ESP32) && defined(ZENOPCB_ENABLE_CELLULAR)
    zeno.loop();                                      // pumps modem, MQTT, ZENO_EVERY
#else
    delay(1000);                                      // idle on unsupported boards
#endif
}
