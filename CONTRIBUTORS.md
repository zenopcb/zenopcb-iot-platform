# Contributors

Cảm ơn mọi người đã giúp cải thiện ZenoPCB IoT Library.

## Maintainer

- **ZENOPCB ELECTRONICS TECHNOLOGY CO., LTD** — <infor@zenopcb.vn>

## Community contributors

- **tphp** ([@NBTPH](https://github.com/NBTPH)) — báo cáo và đề xuất fix cho hai lỗi trong đường telemetry ZKey:
  - Race condition ở MQTT `onConnected`: callback được đăng ký sau khi
    `connect()` chạy, khiến lần connect đầu có thể bỏ lỡ callback.
  - Timing của instant-publish: `_instantPublishPending` bị clear quá sớm
    trong `markPublishTimer()`, dẫn tới quyết định publish sai ở một số ca biên.

<!-- Cách đóng góp: xem CONTRIBUTING.md. Đóng góp được ghi công tại đây và
     qua trailer `Co-authored-by` trên commit tương ứng. -->
