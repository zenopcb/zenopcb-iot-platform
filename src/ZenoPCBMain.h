/**
 * @file ZenoPCBMain.h
 * @brief Main include file for ZenoPCB IoT Library
 *
 * Include this file to use ZenoPCB IoT Library:
 * #include <ZenoPCBMain.h>
 *
 * @example
 * ZenoPCB zeno;
 * zeno.wifiProvisioning(0, 3000)  // IO-0, hold 3s
 *     .onConnected([]() { Serial.println("Connected!"); })
 *     .begin();
 *
 * void loop() {
 *     zeno.loop();
 * }
 */

#ifndef ZENOPCB_MAIN_H
#define ZENOPCB_MAIN_H

#include "ZenoPCB.h"
#include "core/DeviceCredentials.h"

// ============================================
// Dùng:  ZENO_WRITE(Z0) { /* xử lý param */ }
// Đăng ký: .onZKeyChange(ZKey::Z0, onZ0)
//
// Trong body có sẵn biến:
//   param       - ZValue (dùng param.toFloat(), param.toInt(), param.toString(), param.toBool())
//   param.type  - ZValueType (INT, FLOAT, STRING, BOOL, NONE)
// ============================================
// ============================================
// ZENO_WRITE(key, value) - ghi dữ liệu LÊN server
// ============================================
// Dùng bên trong ZENO_READ_ALL { } hoặc bất kỳ đâu:
//   ZENO_WRITE(Z0, readTemperature());
//   ZENO_WRITE(Z1, 42);
// ============================================
#define ZENO_WRITE(key, value) zeno.set(ZKey::key, value)

// ============================================
// ZENO_READ(key) - nhận dữ liệu TỪ server xuống
// ============================================
// Dùng:  ZENO_READ(Z3) { /* xử lý param */ }
// Đăng ký: .onZKeyChange(ZKey::Z3, onZ3)
//
// Biến sẵn có trong body:
//   param           - ZValue
//   param.toFloat() - lấy số thực
//   param.toInt()   - lấy số nguyên
//   param.toString()- lấy chuỗi
//   param.toBool()  - lấy bool
// ============================================
#define ZENO_READ(key) void on##key(ZKey _zkey, const ZValue &param)

// ============================================
// ZENO_READ_ALL - gọi NGay TRƯỚC mỗi lần publish
// ============================================
// Dùng:  ZENO_READ_ALL { ZENO_WRITE(Z0, sensor()); ... }
// Đăng ký: .onZKeyRead(_zenoReadAll)
// Chỉ gọi khi đến interval publish, không gọi mỗi loop()
// ============================================
#define ZENO_READ_ALL void _zenoReadAll()

#endif // ZENOPCB_MAIN_H
