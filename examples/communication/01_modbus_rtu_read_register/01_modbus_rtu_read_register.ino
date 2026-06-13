/**
 * @file 01_modbus_rtu_read_register.ino
 * @brief Read one Modbus RTU holding register over RS485 -> publish to Z0.
 *
 * @category Communication
 * @level Intermediate
 *
 * @hardware
 * - **ESP32 only** (see @platform_notes).
 * - RS485 transceiver (MAX485 / SP485) wired to UART2.
 * - Any Modbus RTU slave device (PLC, sensor, energy meter, etc.) on
 *   the RS485 bus.
 *
 * @platform_notes
 * **Modbus stack is ESP32-only.** Phase 6 + 7 guard the entire Modbus
 * subsystem behind `#if defined(ESP32)` because the underlying RTU driver
 * uses ESP32-specific UART APIs. On ESP8266 / UNO R4 / STM32 this sketch
 * compiles to an empty loop with a Serial.println explanation — use a
 * third-party Modbus library on those targets (e.g. ArduinoModbus).
 *
 * @wiring (ESP32)
 * - MAX485 RO -> GPIO 16 (Serial1 RX).
 * - MAX485 DI -> GPIO 17 (Serial1 TX).
 * - MAX485 DE+RE -> GPIO 4 (driver-enable, tied together).
 * - MAX485 VCC / GND -> 5 V / GND.
 * - A/B differential pair -> slave A/B.
 * - 120 Ohm termination at each end of the RS485 bus.
 *
 * @usage
 * 1. Set credentials.
 * 2. Adjust SLAVE_ID and REGISTER_ADDR to match your slave device.
 * 3. The sketch polls one holding register every 2 s and publishes the
 *    raw uint16 value to Z0.
 */

#include <ZenoPCBMain.h>

using namespace ZenoPCB;

#define WIFI_SSID "REPLACE_ME"
#define WIFI_PASS "REPLACE_ME"
#define DEVICE_ID "REPLACE_ME"
#define DEVICE_TOKEN "REPLACE_AT_PROVISIONING"

#if defined(ESP32)

#include <ModbusRTU.h>

#define MB_RX_PIN 16
#define MB_TX_PIN 17
#define MB_DE_PIN 4

static const uint8_t  SLAVE_ID      = 1;
static const uint16_t REGISTER_ADDR = 0;     // change to match your slave

Zeno      zeno;
ModbusRTU mb;
static uint32_t s_lastPollMs = 0;
static const uint32_t POLL_MS = 2000;
static uint16_t s_lastValue = 0;

void setup()
{
    Serial.begin(115200);
    Serial1.begin(9600, SERIAL_8N1, MB_RX_PIN, MB_TX_PIN);
    mb.begin(&Serial1, MB_DE_PIN);
    mb.master();

    zeno.wifi(WIFI_SSID, WIFI_PASS)
        .device(DEVICE_ID, DEVICE_TOKEN)
        .enableZKeys()
        .setZPublishInterval(5000)
        .begin();
}

void loop()
{
    mb.task();
    const uint32_t now = millis();
    if (!mb.slave() && (now - s_lastPollMs >= POLL_MS))
    {
        s_lastPollMs = now;
        uint16_t buf = 0;
        mb.readHreg(SLAVE_ID, REGISTER_ADDR, &buf, 1,
            [](Modbus::ResultCode event, uint16_t /*transactionId*/, void* /*data*/) -> bool
            {
                if (event == Modbus::EX_SUCCESS)
                {
                    ZENO_WRITE(Z0, (int32_t)s_lastValue);
                    ZENOPCB_PRINTF("[Modbus] hreg[%u] = %u\n",
                                   REGISTER_ADDR, s_lastValue);
                }
                else
                {
                    ZENOPCB_PRINTF("[Modbus] read err = 0x%02X\n", event);
                }
                return true;
            });
        s_lastValue = buf;
    }
    zeno.loop();
}

#else   // non-ESP32

void setup()
{
    Serial.begin(115200);
    Serial.println(F("[INFO] Modbus RTU stack is ESP32-only — see sketch header."));
}

void loop()
{
    // no-op
}

#endif  // ESP32
