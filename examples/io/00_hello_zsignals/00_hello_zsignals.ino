/**
 * @file 00_hello_zsignals.ino
 * @brief Hello-world for ZenoPCB ZSignals — one analog sensor + one digital actuator.
 *
 * @category IO
 * @level Beginner
 *
 * Two ZSignal channels:
 *   Z0 (Float) — Sensor value (0.0..100.0) read from analogRead on SENSOR_PIN.
 *                Published to ZenoPCB Cloud every 5 s.
 *   Z1 (Bool)  — LED state on LED_PIN. Written by the cloud and applied in
 *                the ZENO_READ(Z1) callback below.
 *
 * @hardware
 * - Any supported board (ESP32 / ESP8266 / UNO R4 WiFi / STM32 F1 / STM32 F4).
 * - LED + 220 Ohm resistor on LED_PIN (built-in on most dev boards).
 * - Optional analog sensor on SENSOR_PIN. With nothing wired, analogRead returns
 *   a floating value — still fine for verifying the publish path on its own.
 *
 * @wiring
 * - LED_PIN -> LED anode -> 220 Ohm resistor -> GND (or just rely on the
 *   on-board LED if your board ships with one).
 * - SENSOR_PIN -> potentiometer wiper / analog sensor output (optional).
 *
 * @usage
 * 1. Replace WIFI_SSID, WIFI_PASS, DEVICE_ID, DEVICE_TOKEN below.
 * 2. Flash, open Serial Monitor at 115200 baud.
 * 3. On the ZenoPCB Cloud dashboard, watch Z0 update every 5 s.
 * 4. Toggle Z1 from the dashboard to switch the LED on/off.
 *
 * The broker hostname is built into the library — no broker setup needed.
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
// Hardware pins + ADC resolution per platform
// ============================================
#if defined(ESP32)
  #define SENSOR_PIN 34       // ESP32 GPIO 34 (input-only ADC) — pin float is OK for first-run demo
  #define SENSOR_MAX 4095.0f  // 12-bit ADC (0..4095)
  #define LED_PIN    2        // Built-in LED on most ESP32 dev boards
#elif defined(ESP8266)
  #define SENSOR_PIN A0       // ESP8266 only ADC pin
  #define SENSOR_MAX 1023.0f  // 10-bit ADC (0..1023)
  #define LED_PIN    LED_BUILTIN
#elif defined(ARDUINO_UNOR4_WIFI)
  #define SENSOR_PIN A0       // UNO R4 WiFi A0
  #define SENSOR_MAX 16383.0f // 14-bit ADC by default (0..16383)
  #define LED_PIN    LED_BUILTIN
#elif defined(STM32F4)
  #define SENSOR_PIN PA0      // Nucleo-F429ZI Arduino A0 = PA0
  #define SENSOR_MAX 4095.0f  // 12-bit ADC (0..4095)
  #define LED_PIN    PA5      // Nucleo-F429ZI green LED
#elif defined(STM32F1)
  #define SENSOR_PIN PA0      // Blue Pill F103C8 PA0
  #define SENSOR_MAX 4095.0f  // 12-bit ADC (0..4095)
  #define LED_PIN    PC13     // Blue Pill on-board LED (active-LOW)
#endif

// ============================================
// Globals
// ============================================
Zeno zeno;

// ============================================
// ZENO_READ_ALL — called once before each cloud publish.
// Sample the sensor and stuff the scaled value into Z0.
// ============================================
ZENO_READ_ALL
{
    float raw = (float)analogRead(SENSOR_PIN);
    float scaled = raw / SENSOR_MAX * 100.0f; // per-platform ADC resolution (see #defines above)
    ZENO_WRITE(Z0, scaled);
    ZENOPCB_PRINTF("[Z0] sensor = %.2f\n", scaled);
}

// ============================================
// ZENO_READ(Z1) — invoked when the cloud writes Z1.
// Use param.toBool() to drive the LED.
// ============================================
ZENO_READ(Z1)
{
    bool on = param.toBool();
    // STM32F1 Blue Pill on-board LED (PC13) is active-LOW.
#if defined(STM32F1)
    digitalWrite(LED_PIN, on ? LOW : HIGH);
#else
    digitalWrite(LED_PIN, on ? HIGH : LOW);
#endif
    ZENOPCB_PRINTF("[Z1] LED %s\n", on ? "ON" : "OFF");
}

// ============================================
// Setup
// ============================================
void setup()
{
    Serial.begin(115200);
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, LOW);

    zeno.wifi(WIFI_SSID, WIFI_PASS)
        .device(DEVICE_ID, DEVICE_TOKEN)
        .enableZKeys()
        .setZPublishInterval(5000) // publish every 5 s
        .onZKeyRead(_zenoReadAll)
        .onZKeyChange(ZKey::Z1, onZ1)
        .begin();
}

// ============================================
// Loop
// ============================================
void loop()
{
    zeno.loop();
}
