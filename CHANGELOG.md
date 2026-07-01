# Changelog

Tất cả thay đổi đáng chú ý của ZenoPCB IoT Library được ghi ở đây.
Định dạng dựa trên [Keep a Changelog](https://keepachangelog.com/);
dự án theo [Semantic Versioning](https://semver.org/).

## [0.4.1] - 2026-07-01

### Fixed
- **MQTT `onConnected`**: đăng ký unified callbacks trước `connect()` để lần
  connect đầu không bỏ lỡ callback. _(báo cáo bởi tphp / @NBTPH)_
- **Instant publish**: không xóa `_instantPublishPending` trong
  `markPublishTimer()` nữa, tránh drop publish tức thời ở một số ca biên.
  _(báo cáo bởi tphp / @NBTPH)_
- **4G modem reset**: `Zeno4GProvider::begin()` / `_restartModem()` giờ có drive
  chân RESET khi được khai báo, thay vì chỉ dùng PWRKEY.
- **ESP32 I2C**: các example I2C gọi `Wire.begin(SDA, SCL)` với chân tường minh
  trên ESP32 / ESP8266.

### Added
- **ZKey chunked telemetry**: publish tối đa 255 ZKey qua nhiều message giới hạn
  kích thước (an toàn trên cả 5 nền tảng), kèm guard `overflowed()` và giãn nhịp
  publish trên 4G để tránh tràn TCP buffer modem.
- **CONTRIBUTORS.md** ghi công cộng đồng.
- Liên kết tải thư viện ngoài trong `@lib_deps` của các example dùng lib ngoài.

### Changed
- Chuẩn hóa sentinel chân không dùng: `-1` (`int8_t`) trên toàn dự án.
- Rút gọn bộ example 44 → 31 (gộp trùng lặp), đánh số lại liền mạch.
- README: bỏ cột level + emoji; dọn comment tham chiếu nội bộ trong `src/`.

[0.4.1]: https://github.com/zenopcb/zenopcb-iot-platform/releases/tag/v0.4.1
