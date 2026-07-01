# Đóng góp cho ZenoPCB IoT Library

Cảm ơn bạn quan tâm đến dự án. Tài liệu này mô tả quy trình đóng góp code, báo lỗi, và đề xuất tính năng.

## Cách nhanh nhất để giúp dự án

- **Báo lỗi:** mở [GitHub Issue](https://github.com/zenopcb/zenopcb-iot-platform/issues/new) với template "Bug report"
- **Đề xuất tính năng:** mở Issue với template "Feature request"
- **Cải thiện docs:** PR vào `README.md` hoặc bất kỳ `.md` nào trong repo
- **Thêm example sketch:** xem [Thêm example mới](#thêm-example-mới) bên dưới

## Trước khi mở Pull Request

1. **Mở issue trước** với change ≥ 50 dòng hoặc chạm vào `src/hal/`, `src/vendor/`, hay `library.properties`. Việc này tránh PR bị reject vì lệch hướng.
2. **Fork** repo về account của bạn.
3. **Branch** từ `main`: `git checkout -b feat/<tên-ngắn-gọn>` hoặc `fix/<tên-ngắn-gọn>`.
4. Implement thay đổi (xem [Code style](#code-style) bên dưới).
5. **Compile thử** trên ít nhất 1 board mỗi nền tảng bạn có. Nếu không có hết board, ghi rõ trong PR description.

## Code style

### C++ (thư viện chính)

- 4 space indent, không dùng tab.
- Class name: `PascalCase` (ví dụ `Zeno`, `ZenoPCBMQTT`).
- Method/biến: `camelCase` (`begin`, `wifiProvisioning`, `_initialized`).
- Private member: prefix `_` (`_hal`, `_wifiClient`).
- Hằng số/macro: `UPPER_SNAKE` (`ZENOPCB_VERSION`, `CONN_WIFI`).
- Không dùng `using namespace std;` ở header. Trong code thư viện, tránh polluting global namespace.
- File header: 80 cột mềm, không cứng.

### Header guard

Dùng `#ifndef ... #define ... #endif` thay vì `#pragma once` để đảm bảo portable trên toolchain cũ (vd. STM32F1 g++).

```cpp
#ifndef ZENOPCB_FEATURE_H
#define ZENOPCB_FEATURE_H
// ...
#endif // ZENOPCB_FEATURE_H
```

### Platform gating

Module chỉ chạy được trên một số platform → bọc bằng `#if defined(<MACRO>)`:

```cpp
#if defined(ESP32)
  #include "network/ZenoEthernetProvider.h"
#endif
```

Method công khai trên `Zeno` mà có thể vắng mặt runtime → trả `ZenoCapability` enum (`OK / Unavailable / Error`) thay vì `bool`. Xem [`src/ZenoPCB.h`](src/ZenoPCB.h) — `wifiProvisioning(apSsid, apPwd)` và `ota(url)` là ví dụ canonical.

### Vendor libraries

`src/vendor/` chứa lib bên thứ ba **đã vendor**. **Đừng sửa code vendor trực tiếp.** Nếu cần cập nhật:

1. Tải bản upstream mới nhất.
2. Diff với bản hiện tại, ghi lại các tuỳ biến nội bộ vào file `VENDORED.md` cùng thư mục.
3. Re-apply tuỳ biến (vd. ArduinoJson đã rename namespace thành `ZenoJson`).
4. Chạy full release checklist trong [`doc/RELEASE.md`](doc/RELEASE.md).

### Comment

Mặc định không viết comment. Chỉ viết khi:
- WHY non-obvious (vd. workaround cho bug toolchain).
- Invariant ẩn (vd. "_initialized phải set TRƯỚC khi gọi onConnected để callback không re-entrant ").
- Doxygen-style comment cho public API (xem các method hiện có trong [`src/ZenoPCB.h`](src/ZenoPCB.h)).

## Thêm example mới

1. Tạo thư mục: `examples/<category>/<NN_name>/<NN_name>.ino`.
2. `NN` là 2 chữ số (`00`, `01`, …) cho thứ tự trong category. Tăng số nếu category đã có sketch.
3. Comment header bắt buộc với các tag sau:

   ```cpp
   /**
    * @file <NN>_<name>.ino
    * @brief <tóm tắt 1 dòng>.
    *
    * @category <IO|Sensors|Actuation|Connectivity|Communication|Display|Alarm|Scheduling|Maintenance|Patterns>
    * @level <Beginner|Intermediate|Advanced>
    *
    * @hardware
    * - <list board / module cần>
    *
    * @wiring
    * - <pin mapping>
    *
    * @usage
    * 1. <step 1>
    * 2. <step 2>
    * 3. ...
    */
   ```

4. Cross-platform: dùng `#if defined(...) ... #elif ... #endif` cho pin mapping và ADC bit-depth (xem [`examples/io/02_analog_read/`](examples/io/02_analog_read/) làm template).
5. Nếu sketch yêu cầu lib bên ngoài: thêm comment `@lib_deps` với cú pháp PlatformIO và hướng dẫn Arduino IDE Library Manager.
6. Cập nhật bảng examples trong [`README.md`](README.md) (section "Bộ 31 examples").
7. Compile thử trên các nền tảng sketch hỗ trợ trước khi mở PR.

## Báo lỗi (Bug report)

Issue Bug report cần có:

- **Phiên bản** thư viện (xem `library.properties` hoặc `ZENOPCB_VERSION`).
- **Board** (FQBN nếu dùng Arduino IDE, hoặc `board` trong `platformio.ini`).
- **Core version** (vd. Arduino ESP32 core 3.0.5).
- **Steps to reproduce** — sketch tối thiểu (MRE).
- **Behaviour quan sát được** và **behaviour mong đợi**.
- **Logs** (Serial output, build error) trong code block.

## Commit message

Theo Conventional Commits đơn giản:

| Prefix | Khi dùng |
|---|---|
| `feat:` | Thêm tính năng public mới |
| `fix:` | Sửa bug |
| `docs:` | Chỉ thay đổi documentation |
| `refactor:` | Refactor không đổi behaviour |
| `chore:` | Build/tooling/CI changes |
| `release:` | Version bump cho release |
| `ci:` | Thay đổi `.github/workflows` |

Ví dụ: `fix: prevent MQTT reconnect loop on TLS handshake failure (#42)`

## Process review

- CI compile-sketches phải pass trên tất cả 5 board.
- Branch protection chặn force-push, deletion.
- PR sẽ được review trong vòng 7 ngày. Nếu lâu hơn, ping `@TrongThan` trong PR.

## Quy tắc Release

Chi tiết trong [`doc/RELEASE.md`](doc/RELEASE.md) (nội bộ, không public). Maintainer chịu trách nhiệm release.

## Giấy phép

Đóng góp của bạn được publish dưới MIT License giống thư viện (xem [`LICENSE`](LICENSE)).

## Liên hệ

- GitHub Issues cho mọi câu hỏi technical
- Email: [infor@zenopcb.vn](mailto:infor@zenopcb.vn) cho liên hệ business
