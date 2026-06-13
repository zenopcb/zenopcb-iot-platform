/**
 * @file 04_serial_passthrough.ino
 * @brief Z0 -> Serial.print + Serial.read() -> Z1 (bidirectional bridge).
 *
 * @category Communication
 * @level Intermediate
 *
 * @hardware
 * - Any supported board.
 * - Another serial device connected to UART2 (or UART1 on F1/F4 — see pin
 *   guards).
 *
 * @wiring
 * - DEV_RX_PIN -> external device TX.
 * - DEV_TX_PIN -> external device RX.
 * - Common GND.
 *
 * @usage
 * 1. Set credentials.
 * 2. Cloud-write a String to Z0 — sketch writes it verbatim out the UART.
 * 3. Sketch collects whatever the external device sends back into a 128-byte
 *    buffer until it hits '\n' or fills the buffer, then publishes that
 *    line as a String to Z1.
 */

#include <ZenoPCBMain.h>

using namespace ZenoPCB;

#define WIFI_SSID "REPLACE_ME"
#define WIFI_PASS "REPLACE_ME"
#define DEVICE_ID "REPLACE_ME"
#define DEVICE_TOKEN "REPLACE_AT_PROVISIONING"

// Choose the second UART available on each board (USB CDC keeps the
// debug Serial usable).
#if defined(ESP32)
  #define DEV_SERIAL Serial2
  #define DEV_RX_PIN 16
  #define DEV_TX_PIN 17
#elif defined(ESP8266)
  // ESP8266 has only Serial (UART0) and Serial1 (TX only); use Serial1 TX +
  // SoftwareSerial RX externally if you need full duplex. For simplicity:
  #define DEV_SERIAL Serial1
  #define DEV_RX_PIN 0  // unused on ESP8266 Serial1
  #define DEV_TX_PIN 2
#elif defined(ARDUINO_UNOR4_WIFI)
  #define DEV_SERIAL Serial1
  #define DEV_RX_PIN 0
  #define DEV_TX_PIN 1
#elif defined(STM32F4)
  #define DEV_SERIAL Serial2
  #define DEV_RX_PIN PA3
  #define DEV_TX_PIN PA2
#elif defined(STM32F1)
  #define DEV_SERIAL Serial2
  #define DEV_RX_PIN PA3
  #define DEV_TX_PIN PA2
#endif

Zeno zeno;
static char     s_rxBuf[128];
static uint16_t s_rxLen = 0;

ZENO_READ(Z0)
{
    const String out = param.toString();
    DEV_SERIAL.print(out);
    ZENOPCB_PRINTF("[Serial] tx (%u bytes)\n", (unsigned)out.length());
}

void setup()
{
    Serial.begin(115200);

#if defined(ESP32)
    DEV_SERIAL.begin(9600, SERIAL_8N1, DEV_RX_PIN, DEV_TX_PIN);
#else
    DEV_SERIAL.begin(9600);
#endif

    zeno.wifi(WIFI_SSID, WIFI_PASS)
        .device(DEVICE_ID, DEVICE_TOKEN)
        .enableZKeys()
        .onZKeyChange(ZKey::Z0, onZ0)
        .begin();
}

void loop()
{
    while (DEV_SERIAL.available() > 0 && s_rxLen < sizeof(s_rxBuf) - 1)
    {
        const char c = (char)DEV_SERIAL.read();
        if (c == '\n')
        {
            s_rxBuf[s_rxLen] = '\0';
            ZENO_WRITE(Z1, String(s_rxBuf));
            ZENOPCB_PRINTF("[Serial] rx -> Z1: %s\n", s_rxBuf);
            s_rxLen = 0;
        }
        else if (c != '\r')
        {
            s_rxBuf[s_rxLen++] = c;
        }
    }
    if (s_rxLen >= sizeof(s_rxBuf) - 1)
    {
        s_rxBuf[s_rxLen] = '\0';
        ZENO_WRITE(Z1, String(s_rxBuf));
        s_rxLen = 0;
    }
    zeno.loop();
}
