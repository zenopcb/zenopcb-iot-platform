/**
 * @file 02_multi_zsignal_aggregator.ino
 * @brief Aggregate 5 analog channels into 5 ZSignals + one JSON summary.
 *
 * @category Patterns
 * @level Intermediate
 *
 * @hardware
 * - Any supported board.
 * - Up to 5 analog inputs on A0..A4 (or equivalents).
 *
 * @platform_notes
 * Pure ZSignals — works on every supported port including F103.
 *
 * @usage
 * 1. Set credentials.
 * 2. Each loop iteration reads 5 analog channels and writes them to
 *    Z0..Z4 (raw 0..100% normalised across each board's ADC bit depth).
 * 3. A combined JSON summary is also written to Z5 once every 10 s, so
 *    the cloud receives a single packet with all readings — useful when
 *    downstream consumers prefer one event over five.
 */

#include <ZenoPCBMain.h>

using namespace ZenoPCB;

#define WIFI_SSID "REPLACE_ME"
#define WIFI_PASS "REPLACE_ME"
#define DEVICE_ID "REPLACE_ME"
#define DEVICE_TOKEN "REPLACE_AT_PROVISIONING"

#if defined(ESP32)
  static const int   ADC_PINS[5] = { 32, 33, 34, 35, 36 };
  #define ADC_FULL 4095.0f
#elif defined(ESP8266)
  static const int   ADC_PINS[5] = { A0, A0, A0, A0, A0 }; // ESP8266 has only A0; same pin sampled 5x for demo
  #define ADC_FULL 1023.0f
#elif defined(ARDUINO_UNOR4_WIFI)
  static const int   ADC_PINS[5] = { A0, A1, A2, A3, A4 };
  #define ADC_FULL 16383.0f
#elif defined(STM32F4)
  static const int   ADC_PINS[5] = { PA0, PA1, PA2, PA3, PA4 };
  #define ADC_FULL 4095.0f
#elif defined(STM32F1)
  static const int   ADC_PINS[5] = { PA0, PA1, PA2, PA3, PA4 };
  #define ADC_FULL 4095.0f
#endif

Zeno zeno;
static uint32_t s_lastSampleMs = 0;
static uint32_t s_lastSummaryMs = 0;
static const uint32_t SAMPLE_MS  = 2000;
static const uint32_t SUMMARY_MS = 10000;

static float s_pct[5] = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };

void setup()
{
    Serial.begin(115200);

    zeno.wifi(WIFI_SSID, WIFI_PASS)
        .device(DEVICE_ID, DEVICE_TOKEN)
        .enableZKeys()
        .setZPublishInterval(SUMMARY_MS)
        .begin();
}

void loop()
{
    const uint32_t now = millis();

    if (now - s_lastSampleMs >= SAMPLE_MS)
    {
        s_lastSampleMs = now;
        for (int i = 0; i < 5; ++i)
        {
            s_pct[i] = (float)analogRead(ADC_PINS[i]) / ADC_FULL * 100.0f;
        }
        ZENO_WRITE(Z0, s_pct[0]);
        ZENO_WRITE(Z1, s_pct[1]);
        ZENO_WRITE(Z2, s_pct[2]);
        ZENO_WRITE(Z3, s_pct[3]);
        ZENO_WRITE(Z4, s_pct[4]);
    }

    if (now - s_lastSummaryMs >= SUMMARY_MS)
    {
        s_lastSummaryMs = now;
        String json = "{\"ch\":[";
        for (int i = 0; i < 5; ++i)
        {
            if (i > 0) json += ",";
            json += String(s_pct[i], 1);
        }
        json += "]}";
        ZENO_WRITE(Z5, json);
        ZENOPCB_PRINTF("[Aggregator] %s\n", json.c_str());
    }

    zeno.loop();
}
