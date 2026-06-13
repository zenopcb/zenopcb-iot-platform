#ifndef ZENOPCB_DEBUG_H
#define ZENOPCB_DEBUG_H

/**
 * @file ZenoPCBDebug.h
 * @brief Cấu hình debug duy nhất cho ZenoPCB IoT Library
 *
 * ⭐ ĐÂY LÀ NƠI DUY NHẤT ĐỂ KIỂM SOÁT TOÀN BỘ LOG OUTPUT
 * Hoạt động trên mọi IDE: Arduino IDE, PlatformIO, VS Code...
 *
 * ============================================
 * HƯỚNG DẪN SỬ DỤNG:
 * ============================================
 *
 * [Chế độ Quiet - Production/Deploy]
 *   ZENOPCB_DEBUG         = 1  (bật)
 *   ZENOPCB_DEBUG_VERBOSE = 0  (tắt - mặc định)
 *   → Chỉ hiển thị: WiFi connected, MQTT connected, lỗi quan trọng
 *
 * [Chế độ Verbose - Khi debug vấn đề kết nối]
 *   ZENOPCB_DEBUG         = 1  (bật)
 *   ZENOPCB_DEBUG_VERBOSE = 1  (bật)
 *   → Hiển thị thêm: chi tiết subscribe topics, routing messages,
 *                    publish telemetry, storage init, v.v.
 *
 * [Tắt hoàn toàn - Sản phẩm cuối]
 *   ZENOPCB_DEBUG         = 0  (tắt tất cả)
 *
 * [Bật debug từng module]
 *   ZENOPCB_DEBUG_MODBUS  = 1  (chỉ xem log Modbus)
 *   ZENOPCB_DEBUG_WIFI    = 1  (chỉ xem log WiFi chi tiết)
 * ============================================
 */

// ============================================
// ⭐ CẤU HÌNH CHÍNH - SỬA 2 DÒNG NÀY ĐỂ ĐIỀU CHỈNH LOG
// ============================================
//
// Library standard 2026-06-06: ships QUIET by default. Arduino IDE
// users — who can't easily set -D build flags — get production-grade
// boot output (only critical events: WiFi connect, MQTT connect/fail,
// errors). Developers who want verbose subsystem-init traces opt-in
// with -DZENOPCB_DEBUG=1 (PlatformIO build_flags) or by editing the
// line below to 1 if they're on Arduino IDE.

#ifndef ZENOPCB_DEBUG
#define ZENOPCB_DEBUG 0 // 1 = bật debug (WiFi/MQTT/lỗi), 0 = tắt tất cả
#endif

#ifndef ZENOPCB_DEBUG_VERBOSE
#define ZENOPCB_DEBUG_VERBOSE 0 // 1 = bật log chi tiết (subscribe, publish detail, routing)
#endif

// ============================================
// Module-specific Debug Switches
// (kế thừa từ ZENOPCB_DEBUG mặc định)
// ============================================

#ifndef ZENOPCB_DEBUG_WIFI
#define ZENOPCB_DEBUG_WIFI ZENOPCB_DEBUG
#endif

#ifndef ZENOPCB_DEBUG_MQTT
#define ZENOPCB_DEBUG_MQTT ZENOPCB_DEBUG
#endif

#ifndef ZENOPCB_DEBUG_OTA
#define ZENOPCB_DEBUG_OTA ZENOPCB_DEBUG
#endif

#ifndef ZENOPCB_DEBUG_4G
#define ZENOPCB_DEBUG_4G 0 // AT command dialog — tắt mặc định (rất verbose)
#endif

#ifndef ZENOPCB_DEBUG_CORE
#define ZENOPCB_DEBUG_CORE ZENOPCB_DEBUG
#endif

#ifndef ZENOPCB_DEBUG_MODBUS
#define ZENOPCB_DEBUG_MODBUS 0 // Tắt mặc định - bật khi cần debug Modbus
#endif

#ifndef ZENOPCB_DEBUG_ALARM
#define ZENOPCB_DEBUG_ALARM ZENOPCB_DEBUG // Alarm evaluation logs
#endif

#ifndef ZENOPCB_DEBUG_PROVISIONING
#define ZENOPCB_DEBUG_PROVISIONING 0 // WiFi Provisioning — TẮT mặc định (ẩn credentials/API/tokens)
                                     // Bật bằng: -DZENOPCB_DEBUG_PROVISIONING=1
#endif

// ============================================
// Portable printf shim (Phase 7 Plan 07-05)
// ============================================
// ESP32 + ESP8266 Arduino cores expose `Serial.printf` directly. UNO R4
// (Renesas `UART` class) and STM32duino (`HardwareSerial`) do NOT — they
// only have `Serial.print` / `Serial.println`. To keep the central
// `ZENO_LOG_*` surface working across all four platforms without losing
// log output on the new ports, we route through a `zenopcb_printf` shim:
//   - Espressif: forwards directly to `Serial.printf` (zero overhead).
//   - UNO R4 / STM32: `snprintf` into a 256-byte stack buffer + `Serial.print`.
//
// Keeps CLAUDE.md "library code never calls Serial.print* directly — use
// ZENO_LOG_*" invariant intact, and ESP32 baseline byte-identical.
#if defined(ESP32) || defined(ESP8266)
  #define ZENOPCB_PRINTF(fmt, ...) Serial.printf(fmt, ##__VA_ARGS__)
#else
  // Portable shim: snprintf to a stack buffer then Serial.print.
  // Buffer is bounded at 256 bytes — large enough for typical log lines;
  // longer messages are truncated (defensive, matches ESP32 vprintf 256B
  // chunk behavior).
  //
  // Plan 07-06.5 (Area C aftermath) — STM32duino + Renesas headers do
  // not pull <stdarg.h> via <stdio.h> the same way Espressif cores do;
  // include both va_list machinery and <Arduino.h> for Serial declaration
  // BEFORE the shim definition. Espressif arm above does not need this
  // because <Arduino.h> + <stdarg.h> are already in scope at every
  // include site of ZenoPCBDebug.h on those cores.
  #include <stdio.h>
  #include <stdarg.h>
  #include <Arduino.h>
  static inline void zenopcb_printf_shim(const char* fmt, ...)
  {
      char _zbuf[256];
      va_list _zargs;
      va_start(_zargs, fmt);
      vsnprintf(_zbuf, sizeof(_zbuf), fmt, _zargs);
      va_end(_zargs);
      Serial.print(_zbuf);
  }
  #define ZENOPCB_PRINTF(fmt, ...) zenopcb_printf_shim(fmt, ##__VA_ARGS__)
#endif

// ============================================
// Portable strlcpy() shim (Plan 07-06.5)
// ============================================
//
// `strlcpy()` is a BSD extension. ESP32 + ESP8266 newlib expose it via
// <string.h>; STM32duino newlib + Renesas RA libc do NOT. We provide a
// drop-in implementation when the platform doesn't supply one. Inline
// + `static` so multiple TUs get a private copy with no link conflict.
//
// Guard via `__has_include` style of detection: STM32 + Renesas miss it;
// ESP32 / ESP8266 have it natively — exclude there to keep baseline byte-
// identical and avoid ODR weirdness.
#include <string.h>
#if !defined(ESP32) && !defined(ESP8266)
static inline size_t zenopcb_strlcpy(char* dst, const char* src, size_t dsize)
{
    const char* osrc = src;
    size_t nleft = dsize;
    if (nleft != 0) {
        while (--nleft != 0) {
            if ((*dst++ = *src++) == '\0') break;
        }
    }
    if (nleft == 0) {
        if (dsize != 0) *dst = '\0';
        while (*src++) { /* count remaining */ }
    }
    return (size_t)(src - osrc - 1);
}
#define strlcpy(dst, src, dsize) zenopcb_strlcpy((dst), (src), (dsize))
#endif

// ============================================
// Base Macros
// ============================================

#if ZENOPCB_DEBUG
#define ZENO_LOG(tag, fmt, ...) ZENOPCB_PRINTF("[%s] " fmt "\n", tag, ##__VA_ARGS__)
#define ZENO_LOG_RAW(fmt, ...) ZENOPCB_PRINTF(fmt, ##__VA_ARGS__)
#else
#define ZENO_LOG(tag, fmt, ...)
#define ZENO_LOG_RAW(fmt, ...)
#endif

// ============================================
// Module Macros (for important events)
// ============================================

#if ZENOPCB_DEBUG_WIFI
#define ZENO_LOG_WIFI(fmt, ...) ZENO_LOG("WiFi", fmt, ##__VA_ARGS__)
#else
#define ZENO_LOG_WIFI(fmt, ...)
#endif

#if ZENOPCB_DEBUG_MQTT
#define ZENO_LOG_MQTT(fmt, ...) ZENO_LOG("MQTT", fmt, ##__VA_ARGS__)
#else
#define ZENO_LOG_MQTT(fmt, ...)
#endif

#if ZENOPCB_DEBUG_OTA
#define ZENO_LOG_OTA(fmt, ...) ZENO_LOG("OTA", fmt, ##__VA_ARGS__)
#else
#define ZENO_LOG_OTA(fmt, ...)
#endif

#if ZENOPCB_DEBUG_CORE
#define ZENO_LOG_CORE(fmt, ...) ZENO_LOG("ZenoPCB", fmt, ##__VA_ARGS__)
#else
#define ZENO_LOG_CORE(fmt, ...)
#endif

#if ZENOPCB_DEBUG_MODBUS
#define ZENO_LOG_MODBUS(fmt, ...) ZENO_LOG("Modbus", fmt, ##__VA_ARGS__)
#else
#define ZENO_LOG_MODBUS(fmt, ...)
#endif

#if ZENOPCB_DEBUG_ALARM
#define ZENO_LOG_ALARM_H(fmt, ...) ZENO_LOG("Alarm", fmt, ##__VA_ARGS__)
#else
#define ZENO_LOG_ALARM_H(fmt, ...)
#endif

// ============================================
// PROVISIONING Macros (ẩn toàn bộ theo mặc định)
// Tắt khi ZENOPCB_DEBUG_PROVISIONING=0 (mặc định):
//   - Credentials, userId, deviceId, token
//   - API endpoint calls, response body
//   - WiFi SSID, IP, APN, Ethernet config
//   - Button hold logs, AP mode startup
// Bật khi debug firmware: -DZENOPCB_DEBUG_PROVISIONING=1
// ============================================

#if ZENOPCB_DEBUG_PROVISIONING
#define ZENO_LOG_PROV(fmt, ...) ZENO_LOG("Prov", fmt, ##__VA_ARGS__)
#define ZENO_LOG_PROV_RAW(fmt, ...) ZENOPCB_PRINTF(fmt, ##__VA_ARGS__)
#else
#define ZENO_LOG_PROV(fmt, ...)
#define ZENO_LOG_PROV_RAW(fmt, ...)
#endif

// ============================================
// VERBOSE Macro (for noisy detail logs)
// Dùng cho: subscribe topics, publish confirmations,
//           message routing, storage init, v.v.
// ============================================

#if ZENOPCB_DEBUG_VERBOSE
#define ZENO_LOG_VERBOSE(fmt, ...) ZENO_LOG("ZenoPCB", fmt, ##__VA_ARGS__)
#else
#define ZENO_LOG_VERBOSE(fmt, ...)
#endif

// ============================================
// Security Helpers — mask token in topics/credentials
// Topic format: v1/devices/{token}/suffix → v1/devices/****/suffix
// ============================================

#include <Arduino.h>

inline String maskTopic(const String& topic) {
    int idx = topic.indexOf("devices/");
    if (idx < 0) return topic;
    int tokenStart = idx + 8; // after "devices/"
    int tokenEnd = topic.indexOf('/', tokenStart);
    if (tokenEnd < 0) return topic.substring(0, tokenStart) + "****";
    return topic.substring(0, tokenStart) + "****" + topic.substring(tokenEnd);
}

inline String maskToken(const String& token) {
    if (token.length() == 0) return "(none)";
    if (token.length() <= 4) return "****";
    return token.substring(0, 4) + "..." + token.substring(token.length() - 4);
}

/**
 * @brief Mask sensitive query params (token=xxx) in URL
 * e.g. "http://host/path?token=abc-123-def" → "http://host/path?token=abc-...def"
 */
inline String maskUrl(const String& url) {
    // Mask token= query parameter
    int tokenIdx = url.indexOf("token=");
    if (tokenIdx < 0) return url;
    int valueStart = tokenIdx + 6; // after "token="
    int valueEnd = url.indexOf('&', valueStart);
    if (valueEnd < 0) valueEnd = url.length();
    int tokenLen = valueEnd - valueStart;
    if (tokenLen <= 8) {
        return url.substring(0, valueStart) + "****" + url.substring(valueEnd);
    }
    return url.substring(0, valueStart) + url.substring(valueStart, valueStart + 4) + "..." + url.substring(valueEnd - 4, valueEnd) + url.substring(valueEnd);
}

/**
 * @brief Mask sensitive fields in JSON payload (url, token)
 * Truncates payload and masks token values
 */
inline String maskPayload(const String& payload, int maxLen = 80) {
    String masked = payload;
    // Mask token= in URLs within JSON
    int tokenIdx = masked.indexOf("token=");
    if (tokenIdx >= 0) {
        int valueStart = tokenIdx + 6;
        int valueEnd = masked.indexOf('"', valueStart);
        if (valueEnd < 0) valueEnd = masked.indexOf('&', valueStart);
        if (valueEnd < 0) valueEnd = masked.length();
        int tokenLen = valueEnd - valueStart;
        if (tokenLen > 8) {
            masked = masked.substring(0, valueStart) + masked.substring(valueStart, valueStart + 4) + "..." + masked.substring(valueEnd - 4, valueEnd) + masked.substring(valueEnd);
        } else if (tokenLen > 0) {
            masked = masked.substring(0, valueStart) + "****" + masked.substring(valueEnd);
        }
    }
    if (masked.length() > (unsigned int)maxLen) {
        masked = masked.substring(0, maxLen) + "...";
    }
    return masked;
}

#endif // ZENOPCB_DEBUG_H
