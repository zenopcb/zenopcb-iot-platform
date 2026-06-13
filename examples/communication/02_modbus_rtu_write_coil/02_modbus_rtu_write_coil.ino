/**
 * @file 02_modbus_rtu_write_coil.ino
 * @brief Cloud Z0 (bool) -> Modbus RTU writeCoil to a connected slave.
 *
 * @category Communication
 * @level Intermediate
 *
 * @hardware
 * - **ESP32 only.**
 * - RS485 transceiver on UART2, slave device with a writeable coil.
 *
 * @platform_notes
 * **Modbus stack is ESP32-only** — same constraint as
 * 01_modbus_rtu_read_register.ino. On other platforms this sketch becomes
 * a no-op with a Serial notice.
 *
 * @wiring (ESP32)
 * Same as 01_modbus_rtu_read_register.ino — MAX485 on GPIO 16/17/4.
 *
 * @usage
 * 1. Set credentials.
 * 2. Set SLAVE_ID and COIL_ADDR to match your slave's writeable coil.
 * 3. Cloud-write Z0 = true/false; the sketch issues a Modbus writeCoil.
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

static const uint8_t  SLAVE_ID  = 1;
static const uint16_t COIL_ADDR = 0;

Zeno      zeno;
ModbusRTU mb;

ZENO_READ(Z0)
{
    const bool on = param.toBool();
    mb.writeCoil(SLAVE_ID, COIL_ADDR, on, nullptr);
    ZENOPCB_PRINTF("[Modbus] writeCoil[%u] = %s\n",
                   COIL_ADDR, on ? "ON" : "OFF");
}

void setup()
{
    Serial.begin(115200);
    Serial1.begin(9600, SERIAL_8N1, MB_RX_PIN, MB_TX_PIN);
    mb.begin(&Serial1, MB_DE_PIN);
    mb.master();

    zeno.wifi(WIFI_SSID, WIFI_PASS)
        .device(DEVICE_ID, DEVICE_TOKEN)
        .enableZKeys()
        .onZKeyChange(ZKey::Z0, onZ0)
        .begin();
}

void loop()
{
    mb.task();
    zeno.loop();
}

#else   // non-ESP32

void setup()
{
    Serial.begin(115200);
    Serial.println(F("[INFO] Modbus RTU stack is ESP32-only — see sketch header."));
}

void loop() {}

#endif
