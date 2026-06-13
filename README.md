<div align="center">

# ZenoPCB IoT Platform

**Thư viện Arduino chính thức cho nền tảng IoT ZenoPCB**

WiFi captive-portal provisioning · MQTT · OTA · Modbus · ZSignals · Alarms · Schedules · Irrigation · Diagnostics

[![Version](https://img.shields.io/badge/version-0.3.0-blue.svg)](library.properties)
[![Platforms](https://img.shields.io/badge/platforms-ESP32%20%7C%20ESP8266%20%7C%20UNO%20R4%20%7C%20STM32-orange.svg)](#nền-tảng-hỗ-trợ)
[![License](https://img.shields.io/badge/license-MIT-green.svg)](LICENSE)
[![Arduino](https://img.shields.io/badge/Arduino-Library%20Manager-00979D.svg?logo=arduino)](https://www.arduino.cc/reference/en/libraries/)
[![PlatformIO](https://img.shields.io/badge/PlatformIO-Registry-FF7F00.svg?logo=platformio)](https://platformio.org/lib)
[![Examples](https://img.shields.io/badge/examples-44-success.svg)](#bộ-44-examples)

### Tải app điều khiển ZenoPCB

<a href="https://apps.apple.com/vn/app/zenopcb/id6759211734?l=vi">
  <img alt="Download on the App Store" src="https://developer.apple.com/assets/elements/badges/download-on-the-app-store.svg" height="54">
</a>&nbsp;
<a href="https://play.google.com/store/apps/details?id=com.zenopcb.iot&hl=vi">
  <img alt="Get it on Google Play" src="https://play.google.com/intl/en_us/badges/static/images/badges/en_badge_web_generic.png" height="54">
</a>

</div>

---

## Tổng quan

`ZenoPCB::Zeno` là một điểm vào fluent duy nhất cho toàn bộ vòng đời thiết bị IoT trên ZenoPCB Cloud:

```cpp
#include <ZenoPCBMain.h>
using namespace ZenoPCB;

Zeno zeno;

void setup() {
    zeno.wifi("SSID", "PASSWORD")
        .device("DEVICE_ID", "DEVICE_TOKEN")
        .enableZKeys()
        .setZPublishInterval(5000)
        .begin();
}

void loop() { zeno.loop(); }
```

- **Single include** — `#include <ZenoPCBMain.h>` kéo theo cả `ZenoPCB.h` và macros `ZENO_WRITE` / `ZENO_READ` / `ZENO_READ_ALL`.
- **Broker built-in** — không cần khai báo `.mqtt(...)` cho luồng cơ bản; thư viện kết nối thẳng tới ZenoPCB Cloud broker. Tự khai broker chỉ cần thiết khi self-host.
- **Zero external deps** — ArduinoJson, PubSubClient, TinyGSM, Preferences, FlashStorage, StreamDebugger đều đã vendor trong [src/vendor/](src/vendor/).
- **HAL layer** — mọi truy cập phần cứng đi qua [`IZenoHal`](src/hal/IZenoHal.h). Port nền tảng mới chỉ cần hiện thực 5 interface con.
- **44 examples sẵn sàng chạy** — xem [bộ examples](#bộ-44-examples) bên dưới.

## Nền tảng hỗ trợ

| Nền tảng | Macro phát hiện | HAL | Module ESP32-only bị tắt |
|---|---|---|---|
| ESP32 | `ESP32` | [Esp32Hal](src/hal/esp32/) | — (đầy đủ tính năng) |
| ESP8266 | `ESP8266` | [Esp8266Hal](src/hal/esp8266/) | `modbus/`, `network/`, `irrigation/` |
| Arduino UNO R4 WiFi | `ARDUINO_UNOR4_WIFI` | [UnoR4Hal](src/hal/unor4/) | `modbus/`, `network/`, `irrigation/` |
| STM32 Nucleo-F429ZI | `STM32F4` | [Stm32Hal](src/hal/stm32/) | `modbus/`, `network/`, `irrigation/` |
| STM32 Blue Pill F103 | `STM32F1` | [Stm32Hal](src/hal/stm32/) | `modbus/`, `network/`, `irrigation/`, `alarm/`, `schedule/` (profile MICRO_BASIC) |

> **Capability probe:** `IZenoHal::capabilities()` trả bitmask cho từng tính năng (OTA, captive portal, TLS…). Các hàm có khả năng vắng mặt trên một số nền tảng (vd. `Zeno::ota()`, `Zeno::wifiProvisioning(apSsid, apPwd)`) trả `ZenoCapability::OK / Unavailable / Error` để caller có thể fail-safe. Xem [examples/patterns/04_capability_matrix/](examples/patterns/04_capability_matrix/).

## Cài đặt

### Arduino IDE — Library Manager
**Sketch → Include Library → Manage Libraries…** tìm `ZenoPCB`.

### Arduino IDE — ZIP
1. Tải repo dạng `.zip`.
2. **Sketch → Include Library → Add .ZIP Library…**

### PlatformIO
```ini
lib_deps =
    https://github.com/zenopcb/zenopcb-iot-platform.git
```

## Hello, ZSignals

Lấy thẳng từ [examples/io/00_hello_zsignals/](examples/io/00_hello_zsignals/00_hello_zsignals.ino):

```cpp
#include <ZenoPCBMain.h>
using namespace ZenoPCB;

#define WIFI_SSID    "REPLACE_ME"
#define WIFI_PASS    "REPLACE_ME"
#define DEVICE_ID    "REPLACE_ME"
#define DEVICE_TOKEN "REPLACE_AT_PROVISIONING"

Zeno zeno;

// Z1: cloud → device  (LED control)
ZENO_READ(Z1) {
    digitalWrite(LED_PIN, param.toBool() ? HIGH : LOW);
}

// Đọc tất cả sensor ngay trước mỗi publish cycle
ZENO_READ_ALL {
    ZENO_WRITE(Z0, analogRead(SENSOR_PIN) * (100.0f / ADC_FULL));
}

void setup() {
    Serial.begin(115200);
    pinMode(LED_PIN, OUTPUT);

    zeno.wifi(WIFI_SSID, WIFI_PASS)
        .device(DEVICE_ID, DEVICE_TOKEN)
        .enableZKeys()
        .setZPublishInterval(5000)
        .onZKeyRead(_zenoReadAll)
        .onZKeyChange(ZKey::Z1, onZ1)
        .begin();
}

void loop() { zeno.loop(); }
```

## Bộ 44 examples

44 sketch chạy được trên tất cả 5 nền tảng (trừ khi note rõ), chia thành 10 nhóm theo level:
🟢 Beginner · 🟡 Intermediate · 🔴 Advanced

### 🔌 IO — [examples/io/](examples/io/)
| # | Sketch | Level | Mô tả |
|---|---|---|---|
| 00 | [hello_zsignals](examples/io/00_hello_zsignals/) | 🟢 | Hello-world: 1 analog sensor + 1 digital actuator qua ZSignal Z0/Z1 |
| 01 | [blink_led](examples/io/01_blink_led/) | 🟢 | Nháy LED built-in + publish trạng thái lên Z0 |
| 02 | [button_read](examples/io/02_button_read/) | 🟢 | Đọc nút bấm + publish lên Z0 |
| 03 | [button_debounce](examples/io/03_button_debounce/) | 🟡 | Chống dội nút bấm (software debounce) |
| 04 | [digital_input_output](examples/io/04_digital_input_output/) | 🟢 | Map nhiều digital IO sang ZSignals |
| 05 | [analog_read](examples/io/05_analog_read/) | 🟢 | Đọc ADC chuẩn hoá theo bit-depth từng board |
| 06 | [pwm_output](examples/io/06_pwm_output/) | 🟡 | Điều khiển PWM (LED dimming, fan speed) từ cloud |
| 07 | [multi_button_state](examples/io/07_multi_button_state/) | 🟡 | State machine với nhiều nút bấm |

### 📡 Sensors — [examples/sensors/](examples/sensors/)
| # | Sketch | Level | Mô tả |
|---|---|---|---|
| 01 | [dht22_temp_humidity](examples/sensors/01_dht22_temp_humidity/) | 🟢 | Nhiệt độ + độ ẩm DHT22 → Z0/Z1 |
| 02 | [pir_motion_detect](examples/sensors/02_pir_motion_detect/) | 🟢 | Phát hiện chuyển động PIR + cloud alert |
| 03 | [ultrasonic_distance](examples/sensors/03_ultrasonic_distance/) | 🟡 | Đo khoảng cách HC-SR04 |
| 04 | [ldr_light_sensor](examples/sensors/04_ldr_light_sensor/) | 🟢 | Cảm biến ánh sáng LDR |
| 05 | [dallas_ds18b20](examples/sensors/05_dallas_ds18b20/) | 🟡 | DS18B20 OneWire, hỗ trợ nhiều cảm biến trên 1 bus |
| 06 | [soil_moisture](examples/sensors/06_soil_moisture/) | 🟢 | Cảm biến độ ẩm đất (capacitive/resistive) |

### ⚙️ Actuation — [examples/actuation/](examples/actuation/)
| # | Sketch | Level | Mô tả |
|---|---|---|---|
| 01 | [relay_single](examples/actuation/01_relay_single/) | 🟢 | Relay 1 kênh điều khiển từ cloud |
| 02 | [relay_4ch](examples/actuation/02_relay_4ch/) | 🟡 | Module relay 4 kênh trên Z0..Z3 |
| 03 | [servo_position](examples/actuation/03_servo_position/) | 🟡 | Servo PWM, vị trí 0-180° từ Z0 |
| 04 | [dc_motor](examples/actuation/04_dc_motor/) | 🟡 | Driver L298N, tốc độ + chiều quay |
| 05 | [solenoid_valve](examples/actuation/05_solenoid_valve/) | 🟡 | Van điện từ thuỷ lực + back-EMF protection |

### 🌐 Connectivity — [examples/connectivity/](examples/connectivity/)
| # | Sketch | Level | Mô tả |
|---|---|---|---|
| 01 | [wifi_basic](examples/connectivity/01_wifi_basic/) | 🟢 | Kết nối WiFi tĩnh (không captive-portal) |
| 02 | [ethernet_w5500](examples/connectivity/02_ethernet_w5500/) | 🟡 | W5500 SPI (ESP32) + Nucleo-F429 RMII (STM32). Cần `-DZENOPCB_ENABLE_ETHERNET` |
| 03 | [4g_sim7600](examples/connectivity/03_4g_sim7600/) | 🔴 | SIM7600 4G/LTE qua TinyGSM. Cần `-DZENOPCB_ENABLE_CELLULAR` |
| 04 | [multi_failover](examples/connectivity/04_multi_failover/) | 🔴 | Failover tự động WiFi → Ethernet → 4G |

### 📞 Communication — [examples/communication/](examples/communication/)
| # | Sketch | Level | Mô tả |
|---|---|---|---|
| 01 | [modbus_rtu_read_register](examples/communication/01_modbus_rtu_read_register/) | 🟡 | Modbus RTU master, đọc holding register |
| 02 | [modbus_rtu_write_coil](examples/communication/02_modbus_rtu_write_coil/) | 🟡 | Modbus RTU master, ghi coil |
| 03 | [i2c_device_scanner](examples/communication/03_i2c_device_scanner/) | 🟢 | Quét địa chỉ I2C, publish danh sách lên Z0 |
| 04 | [serial_passthrough](examples/communication/04_serial_passthrough/) | 🟡 | Cầu Serial bridging cho debug peripheral |

### 📺 Display — [examples/display/](examples/display/)
| # | Sketch | Level | Mô tả |
|---|---|---|---|
| 01 | [lcd_i2c_16x2](examples/display/01_lcd_i2c_16x2/) | 🟢 | LCD 16×2 qua PCF8574 I2C |
| 02 | [oled_ssd1306](examples/display/02_oled_ssd1306/) | 🟡 | OLED 0.96" SSD1306, hiển thị ZSignal real-time |
| 03 | [seven_segment_4digit](examples/display/03_seven_segment_4digit/) | 🟡 | 7-segment TM1637 4 digit |

### 🚨 Alarm — [examples/alarm/](examples/alarm/)
> Yêu cầu Alarm Engine (không có trên STM32F103 MICRO_BASIC).

| # | Sketch | Level | Mô tả |
|---|---|---|---|
| 01 | [threshold_high](examples/alarm/01_threshold_high/) | 🟢 | Cloud rule `Z2 > 80` → LED on |
| 02 | [threshold_range](examples/alarm/02_threshold_range/) | 🟡 | Khoảng hợp lệ `20 ≤ Z2 ≤ 80` |
| 03 | [buzzer_alarm](examples/alarm/03_buzzer_alarm/) | 🟡 | Buzzer cảnh báo với pattern beep |
| 04 | [alarm_cooldown](examples/alarm/04_alarm_cooldown/) | 🟡 | Per-rule cooldown chống spam notification |

### ⏰ Scheduling — [examples/scheduling/](examples/scheduling/)
> Yêu cầu Schedule subsystem + NTP (không có trên STM32F103 MICRO_BASIC).

| # | Sketch | Level | Mô tả |
|---|---|---|---|
| 01 | [simple_timer](examples/scheduling/01_simple_timer/) | 🟢 | Timer millis-based, hoạt động trên F103 |
| 02 | [daily_schedule](examples/scheduling/02_daily_schedule/) | 🟡 | Relay ON 06:00, OFF 18:00 mỗi ngày (cloud-synced) |
| 03 | [cron_pattern](examples/scheduling/03_cron_pattern/) | 🔴 | Lịch dạng cron cho recurring task |
| 04 | [countdown_action](examples/scheduling/04_countdown_action/) | 🟡 | Đếm ngược kích hoạt action (vd. tắt thiết bị sau N phút) |

### 🔧 Maintenance — [examples/maintenance/](examples/maintenance/)
| # | Sketch | Level | Mô tả |
|---|---|---|---|
| 01 | [ota_basic](examples/maintenance/01_ota_basic/) | 🟡 | OTA firmware update qua MQTT trigger (ESP32; opt-in cho UNO R4 và STM32) |
| 02 | [diagnostics](examples/maintenance/02_diagnostics/) | 🟡 | Báo cáo RSSI / heap / uptime mỗi 10 phút |

### 🧠 Patterns — [examples/patterns/](examples/patterns/)
| # | Sketch | Level | Mô tả |
|---|---|---|---|
| 01 | [state_machine_led](examples/patterns/01_state_machine_led/) | 🟡 | State machine FSM điều khiển LED multi-state |
| 02 | [multi_zsignal_aggregator](examples/patterns/02_multi_zsignal_aggregator/) | 🟡 | 5 kênh ADC → Z0..Z4 + JSON tổng hợp Z5 |
| 03 | [debounced_button_zsignal](examples/patterns/03_debounced_button_zsignal/) | 🟡 | Nút bấm debounce → ZSignal toggle |
| 04 | [capability_matrix](examples/patterns/04_capability_matrix/) | 🔴 | Runtime probe `hal().capabilities()`, kiểm tra tính năng cho từng port |

## ZSignals — kênh dữ liệu tuỳ biến (Z0–Z254)

ZSignals (kiểu dữ liệu C++: `ZKey`) là kênh telemetry/control 2 chiều với tới 255 slot. Mỗi slot có thể mang `int32_t`, `float`, `String`, hay `bool` — type được track tại runtime trong `ZValue`.

```cpp
ZENO_READ(Z1) { /* cloud → device. param là ZValue */ }
ZENO_READ_ALL { /* gọi trước mỗi publish cycle */ }

zeno.enableZKeys()
    .setZPublishInterval(5000)        // publish định kỳ
    .setZInstantPublish(true)         // publish ngay khi đổi giá trị
    .onZKeyChange(ZKey::Z1, onZ1)
    .onAnyZKeyChange([](ZKey k, const ZValue& v) { /* ... */ })
    .onZKeyRead(_zenoReadAll);
```

Helper overloads cho callback:
```cpp
zeno.onZKeyChange(ZKey::Z0, [](float v)        { /* ... */ });
zeno.onZKeyChange(ZKey::Z1, [](int32_t v)      { /* ... */ });
zeno.onZKeyChange(ZKey::Z2, [](const String& v){ /* ... */ });
zeno.onZKeyChange(ZKey::Z3, [](bool v)         { /* ... */ });
```

## Tính năng tuỳ chọn (build flags)

Khai báo trước khi `#include <ZenoPCB.h>` — trong `platformio.ini` (`build_flags`) hoặc Arduino IDE custom build options.

| Flag | Tác dụng | Ghi chú |
|---|---|---|
| `ZENOPCB_ENABLE_TLS` | Bật `enableTLS()` / `mqttTLS()` / `setRootCA()` | +~150 KB Flash. Không pin root-CA → ESP32 default `setInsecure()` (dev mode). |
| `ZENOPCB_ENABLE_ETHERNET` | Bật `ZenoEthernetProvider` (W5500 native ETH) | ESP32. Bắt buộc cho `examples/connectivity/02_ethernet_w5500/`. |
| `ZENOPCB_ENABLE_CELLULAR` | Bật `Zeno4GProvider` (TinyGSM) | ESP32. Bắt buộc cho `examples/connectivity/03_4g_sim7600/`. |
| `ZENOPCB_ENABLE_UNOR4_OTA` | Bật OTA opt-in trên UNO R4 WiFi | UNO R4 only. |
| `ZENOPCB_NET=esp-at` | STM32F4: dùng WiFiEspAT thay vì Ethernet mặc định | STM32F4 only. |

## Cấu trúc repo

```
zenopcb-iot-platform/
├── library.properties              # Metadata Arduino IDE 1.5+
├── library.json                    # Metadata PlatformIO
├── keywords.txt                    # Tô màu cú pháp (TAB-separated)
├── src/
│   ├── ZenoPCB.h                   # Điểm vào chính (namespace ZenoPCB, class Zeno)
│   ├── ZenoPCBMain.h               # Include tiện lợi + macros ZENO_WRITE/READ/READ_ALL
│   ├── ZenoPCB.cpp                 # Implementation
│   ├── zenopcb_partition*.csv      # ESP32 partition tables (4/8/16 MB Flash)
│   ├── core/                       # Types, debug, time, Z-Key buffer, network provider interface
│   ├── hal/                        # IZenoHal + concrete HAL (esp32/, esp8266/, unor4/, stm32/)
│   ├── wifi/                       # WiFiProvisioning (captive portal)
│   ├── mqtt/                       # ZenoPCBMQTT + PubSubClient
│   ├── network/                    # Ethernet / 4G / multi-connect (ESP32 only)
│   ├── modbus/                     # Modbus RTU/TCP + register polling (ESP32 only)
│   ├── storage/                    # LittleFS connection & data-monitor config
│   ├── schedule/                   # Schedule executor + MQTT handler
│   ├── alarm/                      # Edge alarm engine
│   ├── irrigation/                 # Irrigation scenarios + scheduler (ESP32 only)
│   ├── diagnostics/                # Periodic diagnostics reporting
│   ├── ota/                        # HTTP OTA + rollback
│   └── vendor/                     # ArduinoJson, PubSubClient, TinyGSM, Preferences, FlashStorage, StreamDebugger
├── examples/                       # 44 sketches, 10 nhóm
│   ├── io/                         # 8 sketches (00..07)
│   ├── sensors/                    # 6 sketches
│   ├── actuation/                  # 5 sketches
│   ├── connectivity/               # 4 sketches
│   ├── communication/              # 4 sketches
│   ├── display/                    # 3 sketches
│   ├── alarm/                      # 4 sketches
│   ├── scheduling/                 # 4 sketches
│   ├── maintenance/                # 2 sketches
│   └── patterns/                   # 4 sketches
├── LICENSE
└── README.md
```

## Phát triển

- **Versioning** — SemVer trong [library.properties](library.properties) và [library.json](library.json).
- **Keywords** — thêm class/hàm/hằng public mới vào [keywords.txt](keywords.txt) (**bắt buộc TAB**).
- **Examples mới** — `examples/<category>/<NN_name>/<NN_name>.ino`. Mỗi sketch nên có metadata block (`@category`, `@level`, `@hardware`, `@usage`) như các sketch hiện có.
- **Thêm nền tảng** — hiện thực `IZenoHal` (kèm `IZenoNVS`, `IZenoStorage`, `IZenoOTA`, `IZenoSystem`, `IZenoTime`) trong `src/hal/<platform>/`, rồi thêm nhánh `#elif defined(<MACRO>)` tại [src/ZenoPCB.h:48-63](src/ZenoPCB.h#L48-L63).
- **Vendor** — lib bên thứ ba ở `src/vendor/` để tránh xung đột với Library Manager. ArduinoJson đã rename namespace thành `ZenoJson`.

## Giấy phép

Xem [LICENSE](LICENSE). Mỗi vendor lib trong `src/vendor/` giữ giấy phép gốc của tác giả.

## Nhà phát triển

**ZENOPCB ELECTRONICS TECHNOLOGY CO., LTD**<br>
350/2 đường Chiến Lược, Phường Bình Trị Đông, TP Hồ Chí Minh, Việt Nam<br>
Email: [infor@zenopcb.vn](mailto:infor@zenopcb.vn)<br>
Website: <https://zenopcb.vn><br>
Repo: <https://github.com/zenopcb/zenopcb-iot-platform>

<div align="center">

📱 **Tải app điều khiển ngay**

<a href="https://apps.apple.com/vn/app/zenopcb/id6759211734?l=vi">
  <img alt="Download on the App Store" src="https://developer.apple.com/assets/elements/badges/download-on-the-app-store.svg" height="48">
</a>&nbsp;
<a href="https://play.google.com/store/apps/details?id=com.zenopcb.iot&hl=vi">
  <img alt="Get it on Google Play" src="https://play.google.com/intl/en_us/badges/static/images/badges/en_badge_web_generic.png" height="48">
</a>

</div>
