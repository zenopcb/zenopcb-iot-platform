/**
 * @file 04_capability_matrix.ino
 * @brief Runtime capability probe + Pattern G assertion sketch.
 *
 * @category Patterns
 * @level Advanced
 *
 * @hardware
 * - Any board the library supports (ESP32, ESP8266, UNO R4 WiFi,
 *   STM32 Nucleo-F429ZI, STM32 Blue Pill F103). The probe never connects
 *   to the network and never needs credentials.
 *
 * @usage
 * 1. Flash the sketch (no credentials needed — the probe never connects).
 * 2. Open Serial Monitor at 115200 baud.
 * 3. Read the capability table and the OK / FAIL lines.
 *
 * Companion sketch for the README capability matrix (GOV-04). When the CI
 * matrix unpauses, this sketch becomes the executable check that the
 * hand-authored matrix table matches the library's runtime `capabilities()`
 * bitmask on each platform.
 *
 * `Serial.printf` is intentionally not used — STM32duino's HardwareSerial
 * does not provide it. The print pattern below (`Serial.print` + `Serial.println`)
 * is portable across all five supported platforms.
 */

#include <ZenoPCBMain.h>

using namespace ZenoPCB;

// ============================================
// Globals
// ============================================
Zeno zeno;

// Resolve the platform HAL singleton directly. The example does NOT need a
// public `Zeno::hal()` accessor — the HAL factory is exposed by every port
// (`getEsp32Hal()` / `getEsp8266Hal()` / `getUnoR4Hal()` / `getStm32Hal()`)
// and `ZENOPCB_DEFAULT_HAL()` selects the right one at compile time.
static IZenoHal &hal = ZENOPCB_DEFAULT_HAL();

// ============================================
// Helper — print a single capability row.
// ============================================
static void printCapRow(const __FlashStringHelper *name, uint32_t caps, uint32_t bit)
{
    Serial.print(name);
    Serial.print(F(" : "));
    Serial.println((caps & bit) ? F("1") : F("0"));
}

// ============================================
// Setup — runs the probe once.
// ============================================
void setup()
{
    Serial.begin(115200);
    delay(100);
    Serial.println();
    Serial.println(F("[ZenoPCB 07_CapabilityMatrix] runtime capability probe"));

    uint32_t caps = hal.capabilities();

    // ---- Bitmask dump (matches IZenoHal::Capability enum, Phase 7 D-09/D-10) --
    printCapRow(F("CAP_FS_FILES      "), caps, IZenoHal::CAP_FS_FILES);
    printCapRow(F("CAP_OTA           "), caps, IZenoHal::CAP_OTA);
    printCapRow(F("CAP_NVS           "), caps, IZenoHal::CAP_NVS);
    printCapRow(F("CAP_NTP           "), caps, IZenoHal::CAP_NTP);
    printCapRow(F("CAP_WATCHDOG      "), caps, IZenoHal::CAP_WATCHDOG);
    printCapRow(F("CAP_CAPTIVE_PORTAL"), caps, IZenoHal::CAP_CAPTIVE_PORTAL);
    printCapRow(F("CAP_TLS           "), caps, IZenoHal::CAP_TLS);
    printCapRow(F("CAP_DIAGNOSTICS   "), caps, IZenoHal::CAP_DIAGNOSTICS);

    // ---- D-08 + D-21 CI assertion sketch --------------------------------
    // When the capability bit is 0, the Pattern G surface MUST return
    // ZenoCapability::Unavailable. Anything else is a library bug.

    if (!(caps & IZenoHal::CAP_OTA))
    {
        ZenoCapability result = zeno.ota("https://placeholder.invalid/fw.bin");
        if (result != ZenoCapability::Unavailable)
        {
            Serial.println(F("FAIL: zeno.ota expected Unavailable when CAP_OTA=0"));
        }
        else
        {
            Serial.println(F("OK: zeno.ota returned Unavailable as expected"));
        }
    }
    else
    {
        Serial.println(F("SKIP: zeno.ota assertion (CAP_OTA=1 — not tested by this probe)"));
    }

    if (!(caps & IZenoHal::CAP_CAPTIVE_PORTAL))
    {
        ZenoCapability result = zeno.wifiProvisioning("zeno-test-ap", "");
        if (result != ZenoCapability::Unavailable)
        {
            Serial.println(F("FAIL: zeno.wifiProvisioning expected Unavailable when CAP_CAPTIVE_PORTAL=0"));
        }
        else
        {
            Serial.println(F("OK: zeno.wifiProvisioning returned Unavailable as expected"));
        }
    }
    else
    {
        Serial.println(F("SKIP: zeno.wifiProvisioning assertion (CAP_CAPTIVE_PORTAL=1 — not tested by this probe)"));
    }

    Serial.println(F("[07_CapabilityMatrix] probe complete"));
}

// ============================================
// Loop — keep the cooperative scheduler ticking.
// ============================================
void loop()
{
    zeno.loop();
}
