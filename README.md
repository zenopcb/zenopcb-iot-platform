<div align="center">

# ZenoPCB IoT Platform

**Thư viện Arduino chính thức cho nền tảng IoT ZenoPCB**

WiFi captive-portal provisioning · MQTT · OTA · Modbus · ZSignals · Alarms · Schedules · Irrigation · Diagnostics

[![Version](https://img.shields.io/badge/version-0.4.0-blue.svg)](library.properties)
[![Platforms](https://img.shields.io/badge/platforms-ESP32%20%7C%20ESP8266%20%7C%20UNO%20R4%20%7C%20STM32-orange.svg)](#nền-tảng-hỗ-trợ)
[![License](https://img.shields.io/badge/license-MIT-green.svg)](LICENSE)
[![Arduino](https://img.shields.io/badge/Arduino-Library%20Manager-00979D.svg?logo=arduino)](https://www.arduino.cc/reference/en/libraries/)
[![PlatformIO](https://img.shields.io/badge/PlatformIO-Registry-FF7F00.svg?logo=platformio)](https://platformio.org/lib)
[![Examples](https://img.shields.io/badge/examples-31-success.svg)](#bộ-31-examples)

### Tải app điều khiển ZenoPCB

<a href="https://apps.apple.com/vn/app/zenopcb/id6759211734?l=vi"><img alt="Download on the App Store" src="https://developer.apple.com/assets/elements/badges/download-on-the-app-store.svg" height="48" align="middle"></a>&nbsp;&nbsp;<a href="https://play.google.com/store/apps/details?id=com.zenopcb.iot&hl=vi"><img alt="Get it on Google Play" src="https://play.google.com/intl/en_us/badges/static/images/badges/en_badge_web_generic.png" height="58" align="middle"></a>

</div>

---

## Tổng quan

`ZenoPCB::Zeno` là điểm vào duy nhất cho toàn bộ vòng đời thiết bị IoT trên ZenoPCB Cloud. **v0.4.0** đổi sang API **declarative** — chỉ cần khai báo block ở file scope, library tự register, `setup()` còn 1 dòng:

```cpp
#include <ZenoPCBMain.h>
using namespace ZenoPCB;

Zeno zeno;

// Cloud → Device: tự register, không cần .onZKeyChange()
CLOUD_TO_DEVICE(Z0) {
    digitalWrite(LED_BUILTIN, param.toBool() ? HIGH : LOW);
    DEVICE_TO_CLOUD(Z0, param.toBool());     // echo state
}

// Device → Cloud định kỳ: tự register, không cần .onZKeyRead()
ZENO_EVERY(5000) {
    DEVICE_TO_CLOUD(Z1, analogRead(A0) * 0.1f);
}

void setup() {
    Serial.begin(115200);
    zeno.wifi("SSID", "PASS").device("ID", "TOKEN").enableZKeys().begin();
}

void loop() { zeno.loop(); }
```

- **Single include** — `#include <ZenoPCBMain.h>` kéo theo cả `ZenoPCB.h` và 3 macro `DEVICE_TO_CLOUD` / `CLOUD_TO_DEVICE` / `ZENO_EVERY`.
- **Broker built-in** — không cần khai báo `.mqtt(...)` cho luồng cơ bản; thư viện kết nối thẳng tới ZenoPCB Cloud broker. Tự khai broker chỉ cần thiết khi self-host.
- **Zero external deps** — ArduinoJson, PubSubClient, TinyGSM, Preferences, FlashStorage, StreamDebugger đều đã vendor trong [src/vendor/](src/vendor/).
- **HAL layer** — mọi truy cập phần cứng đi qua [`IZenoHal`](src/hal/IZenoHal.h). Port nền tảng mới chỉ cần hiện thực 5 interface con.
- **31 examples sẵn sàng chạy** — xem [bộ examples](#bộ-31-examples) bên dưới.

## Nền tảng hỗ trợ

| Nền tảng | Macro phát hiện | HAL | Module ESP32-only bị tắt |
|---|---|---|---|
| ESP32 | `ESP32` | [Esp32Hal](src/hal/esp32/) | — (đầy đủ tính năng) |
| ESP8266 | `ESP8266` | [Esp8266Hal](src/hal/esp8266/) | `modbus/`, `network/`, `irrigation/` |
| Arduino UNO R4 WiFi | `ARDUINO_UNOR4_WIFI` | [UnoR4Hal](src/hal/unor4/) | `modbus/`, `network/`, `irrigation/` |
| STM32 Nucleo-F429ZI | `STM32F4` | [Stm32Hal](src/hal/stm32/) | `modbus/`, `network/`, `irrigation/` |
| STM32 Blue Pill F103 | `STM32F1` | [Stm32Hal](src/hal/stm32/) | `modbus/`, `network/`, `irrigation/`, `alarm/`, `schedule/` (profile MICRO_BASIC) |

> **Capability probe:** `IZenoHal::capabilities()` trả bitmask cho từng tính năng (OTA, captive portal, TLS…). Các hàm có khả năng vắng mặt trên một số nền tảng (vd. `Zeno::ota()`, `Zeno::wifiProvisioning(apSsid, apPwd)`) trả `ZenoCapability::OK / Unavailable / Error` để caller có thể fail-safe.

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

### ⚠️ ESP32 — bắt buộc chọn Partition Scheme ≥ 1.9 MB APP

Thư viện full ~1.3 MB, partition **Default** của Arduino IDE chỉ có 1.2 MB APP slot → linker fail `text section exceeds available space`. Hint này tự xuất hiện trong build log ở mọi compile ESP32 ([src/ZenoPCB.h:55-58](src/ZenoPCB.h#L55-L58)).

| Tool | Cách set |
|---|---|
| **Arduino IDE** | Tools → Partition Scheme → `Minimal SPIFFS (1.9MB APP with OTA)` hoặc `Huge APP (3MB No OTA)` |
| **PlatformIO** | Thêm `board_build.partitions = min_spiffs.csv` vào `[env:esp32dev]` |

Tiết kiệm thêm ~126 KB bằng cách disable các module không dùng (thêm vào `build_flags` PIO hoặc Arduino IDE custom flags):
```
-DZENOPCB_DISABLE_SCHEDULE
-DZENOPCB_DISABLE_ALARM
-DZENOPCB_DISABLE_DIAGNOSTICS
-DZENOPCB_DISABLE_OTA
-DZENOPCB_DISABLE_PROVISIONING
```

Silence hint sau khi đã config xong: `-DZENOPCB_SILENCE_HINTS`.

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

// Z1: cloud → device  (LED control). Self-register tự động.
CLOUD_TO_DEVICE(Z1) {
    digitalWrite(LED_PIN, param.toBool() ? HIGH : LOW);
}

// Z0: device → cloud mỗi 5 giây. Self-register tự động.
ZENO_EVERY(5000) {
    DEVICE_TO_CLOUD(Z0, analogRead(SENSOR_PIN) * (100.0f / ADC_FULL));
}

void setup() {
    Serial.begin(115200);
    pinMode(LED_PIN, OUTPUT);

    // Setup chỉ còn 1 dòng — không cần .onZKeyChange / .onZKeyRead / .setZPublishInterval
    zeno.wifi(WIFI_SSID, WIFI_PASS)
        .device(DEVICE_ID, DEVICE_TOKEN)
        .enableZKeys()
        .begin();
}

void loop() { zeno.loop(); }
```

## Bộ 31 examples

31 sketch chạy được trên ESP32 / ESP8266 / STM32 (F429 + F103), chia thành 10 nhóm.

> **Lưu ý UNO R4 WiFi:** phần lớn example log bằng `Serial.printf()`, mà core Renesas của UNO R4 không cung cấp (`class UART` không có `printf`) — nên các sketch này **không compile được trên UNO R4 as-is**. Đây chỉ là giới hạn logging của example; core thư viện vẫn hỗ trợ UNO R4 đầy đủ. Muốn chạy trên UNO R4: đổi `Serial.printf(...)` sang `Serial.print(...)` / `Serial.println(...)`. Hai example compile sẵn trên UNO R4: [io/00_hello_zsignals](examples/io/00_hello_zsignals/) và [maintenance/02_diagnostics](examples/maintenance/02_diagnostics/).

### IO — [examples/io/](examples/io/)
| # | Sketch | Mô tả |
|---|---|---|
| 00 | [hello_zsignals](examples/io/00_hello_zsignals/) | Hello-world: 1 analog sensor + 1 digital actuator qua ZSignal Z0/Z1 |
| 01 | [digital_input_output](examples/io/01_digital_input_output/) | Map nhiều digital IO sang ZSignals |
| 02 | [analog_read](examples/io/02_analog_read/) | Đọc ADC chuẩn hoá theo bit-depth từng board |
| 03 | [pwm_output](examples/io/03_pwm_output/) | Điều khiển PWM (LED dimming, fan speed) từ cloud |

### Sensors — [examples/sensors/](examples/sensors/)
| # | Sketch | Mô tả |
|---|---|---|
| 01 | [dht22_temp_humidity](examples/sensors/01_dht22_temp_humidity/) | Nhiệt độ + độ ẩm DHT22 → Z0/Z1 |
| 02 | [pir_motion_detect](examples/sensors/02_pir_motion_detect/) | Phát hiện chuyển động PIR + cloud alert |
| 03 | [ultrasonic_distance](examples/sensors/03_ultrasonic_distance/) | Đo khoảng cách HC-SR04 |
| 04 | [dallas_ds18b20](examples/sensors/04_dallas_ds18b20/) | DS18B20 OneWire, hỗ trợ nhiều cảm biến trên 1 bus |
| 05 | [soil_moisture](examples/sensors/05_soil_moisture/) | Cảm biến độ ẩm đất (capacitive/resistive) |

### Actuation — [examples/actuation/](examples/actuation/)
| # | Sketch | Mô tả |
|---|---|---|
| 01 | [relay_single](examples/actuation/01_relay_single/) | Relay 1 kênh điều khiển từ cloud |
| 02 | [servo_position](examples/actuation/02_servo_position/) | Servo PWM, vị trí 0-180° từ Z0 |
| 03 | [dc_motor](examples/actuation/03_dc_motor/) | Driver L298N, tốc độ + chiều quay |

### Connectivity — [examples/connectivity/](examples/connectivity/)
| # | Sketch | Mô tả |
|---|---|---|
| 01 | [wifi_basic](examples/connectivity/01_wifi_basic/) | Kết nối WiFi tĩnh (không captive-portal) |
| 02 | [ethernet_w5500](examples/connectivity/02_ethernet_w5500/) | W5500 SPI (ESP32) + Nucleo-F429 RMII (STM32). Cần `-DZENOPCB_ENABLE_ETHERNET` |
| 03 | [4g_sim7600](examples/connectivity/03_4g_sim7600/) | SIM7600 4G/LTE qua TinyGSM. Cần `-DZENOPCB_ENABLE_CELLULAR` |
| 04 | [multi_failover](examples/connectivity/04_multi_failover/) | Failover tự động WiFi → Ethernet → 4G |

### Communication — [examples/communication/](examples/communication/)
| # | Sketch | Mô tả |
|---|---|---|
| 01 | [modbus_rtu_read_register](examples/communication/01_modbus_rtu_read_register/) | Modbus RTU master, đọc holding register |
| 02 | [modbus_rtu_write_coil](examples/communication/02_modbus_rtu_write_coil/) | Modbus RTU master, ghi coil |
| 03 | [i2c_device_scanner](examples/communication/03_i2c_device_scanner/) | Quét địa chỉ I2C, publish danh sách lên Z0 |
| 04 | [serial_passthrough](examples/communication/04_serial_passthrough/) | Cầu Serial bridging cho debug peripheral |

### Display — [examples/display/](examples/display/)
| # | Sketch | Mô tả |
|---|---|---|
| 01 | [lcd_i2c_16x2](examples/display/01_lcd_i2c_16x2/) | LCD 16×2 qua PCF8574 I2C |
| 02 | [oled_ssd1306](examples/display/02_oled_ssd1306/) | OLED 0.96" SSD1306, hiển thị ZSignal real-time |

### Alarm — [examples/alarm/](examples/alarm/)
> Yêu cầu Alarm Engine (không có trên STM32F103 MICRO_BASIC).

| # | Sketch | Mô tả |
|---|---|---|
| 01 | [threshold_high](examples/alarm/01_threshold_high/) | Cloud rule `Z2 > 80` → LED on |
| 02 | [alarm_cooldown](examples/alarm/02_alarm_cooldown/) | Per-rule cooldown chống spam notification |

### Scheduling — [examples/scheduling/](examples/scheduling/)
> Yêu cầu Schedule subsystem + NTP (không có trên STM32F103 MICRO_BASIC).

| # | Sketch | Mô tả |
|---|---|---|
| 01 | [simple_timer](examples/scheduling/01_simple_timer/) | Timer millis-based, hoạt động trên F103 |
| 02 | [daily_schedule](examples/scheduling/02_daily_schedule/) | Relay ON 06:00, OFF 18:00 mỗi ngày (cloud-synced) |
| 03 | [countdown_action](examples/scheduling/03_countdown_action/) | Đếm ngược kích hoạt action (vd. tắt thiết bị sau N phút) |

### Maintenance — [examples/maintenance/](examples/maintenance/)
| # | Sketch | Mô tả |
|---|---|---|
| 01 | [ota_basic](examples/maintenance/01_ota_basic/) | OTA firmware update qua MQTT trigger (ESP32; opt-in cho UNO R4 và STM32) |
| 02 | [diagnostics](examples/maintenance/02_diagnostics/) | Báo cáo RSSI / heap / uptime mỗi 10 phút |

### Patterns — [examples/patterns/](examples/patterns/)
| # | Sketch | Mô tả |
|---|---|---|
| 01 | [state_machine_led](examples/patterns/01_state_machine_led/) | State machine FSM điều khiển LED multi-state |
| 02 | [debounced_button_zsignal](examples/patterns/02_debounced_button_zsignal/) | Nút bấm debounce → ZSignal toggle |

## ZSignals — kênh dữ liệu tuỳ biến (Z0–Z254)

ZSignals (kiểu dữ liệu C++: `ZKey`) là kênh telemetry/control 2 chiều với tới 255 slot. Mỗi slot có thể mang `int32_t`, `float`, `String`, hay `bool` — type được track tại runtime trong `ZValue`.

### 3 macro chính (v0.4.0+)

| Macro | Hướng | Khi nào dùng |
|---|---|---|
| `DEVICE_TO_CLOUD(Zx, value)` | Device → Cloud (1 dòng) | Bất kỳ đâu cần publish giá trị mới |
| `CLOUD_TO_DEVICE(Zx) { … }` | Cloud → Device (block) | File scope — nhận lệnh từ dashboard, điều khiển hardware |
| `ZENO_EVERY(intervalMs) { … }` | Device → Cloud (định kỳ) | File scope — auto-publish telemetry mỗi N ms |

### Pattern hoàn chỉnh

```cpp
// Nhận lệnh từ cloud → bật/tắt relay
CLOUD_TO_DEVICE(Z0) {
    bool on = param.toBool();
    digitalWrite(RELAY, on ? HIGH : LOW);
    DEVICE_TO_CLOUD(Z0, on);   // echo state để server xác nhận
}

// Gửi nhiệt độ mỗi 5 giây
ZENO_EVERY(5000) {
    DEVICE_TO_CLOUD(Z1, dht.readTemperature());
}

// Gửi độ ẩm mỗi 30 giây
ZENO_EVERY(30000) {
    DEVICE_TO_CLOUD(Z2, dht.readHumidity());
}

void setup() {
    zeno.wifi(SSID, PASS).device(ID, TOKEN).enableZKeys().begin();
}
```

### Các quy tắc quan trọng

- **`CLOUD_TO_DEVICE` và `ZENO_EVERY` phải ở FILE SCOPE** (ngoài mọi hàm). Compiler tự chặn nếu đặt trong `loop()`/`setup()` (anonymous namespace error).
- **Interval auto-clamp về 1000ms** — viết `ZENO_EVERY(500)` cũng chạy 1s/lần (bảo vệ MQTT broker khỏi spam).
- **GET_ALL auto-refresh** — khi server hỏi state, library tự gọi MỌI `ZENO_EVERY` block ngay → response chứa snapshot mới nhất, server không phải poll loop.
- **Backward compat** — `ZENO_WRITE(Zx, value)` vẫn alias `DEVICE_TO_CLOUD` cho user upgrade dần. `ZENO_READ` và `ZENO_READ_ALL` đã **xoá**.

### Tùy chọn nâng cao

```cpp
zeno.enableZKeys()
    .onAnyZKeyChange([](ZKey k, const ZValue& v) { /* watch any key */ });
```

### Migration v0.3.x → v0.4.0

| v0.3.x | v0.4.0 |
|---|---|
| `ZENO_READ(Z1) { … }` + `.onZKeyChange(ZKey::Z1, onZ1)` | `CLOUD_TO_DEVICE(Z1) { … }` |
| `ZENO_READ_ALL { … }` + `.onZKeyRead(_zenoReadAll)` + `.setZPublishInterval(5000)` | `ZENO_EVERY(5000) { … }` |
| `ZENO_WRITE(Z0, x)` | `DEVICE_TO_CLOUD(Z0, x)` *(alias vẫn dùng được)* |

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
├── examples/                       # 31 sketches, 10 nhóm
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

## Đóng góp

Cảm ơn cộng đồng đã báo lỗi và đề xuất cải tiến — xem [CONTRIBUTORS.md](CONTRIBUTORS.md). Quy trình đóng góp ở [CONTRIBUTING.md](CONTRIBUTING.md).

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

<a href="https://apps.apple.com/vn/app/zenopcb/id6759211734?l=vi"><img alt="Download on the App Store" src="https://developer.apple.com/assets/elements/badges/download-on-the-app-store.svg" height="48" align="middle"></a>&nbsp;&nbsp;<a href="https://play.google.com/store/apps/details?id=com.zenopcb.iot&hl=vi"><img alt="Get it on Google Play" src="https://play.google.com/intl/en_us/badges/static/images/badges/en_badge_web_generic.png" height="58" align="middle"></a>

</div>
