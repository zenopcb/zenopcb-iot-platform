#ifndef ZENOPCB_IZENOSYSTEM_H
#define ZENOPCB_IZENOSYSTEM_H

/**
 * @file IZenoSystem.h
 * @brief Pure-virtual interface for chip-level system services:
 *        restart, heap stats, unique chip ID, uptime, watchdog feed.
 *
 * Wraps `ESP.restart()`, `ESP.getFreeHeap()`, `ESP.getHeapSize()`,
 * `ESP.getMaxAllocHeap()`, `ESP.getEfuseMac()`, `millis()` and
 * `esp_task_wdt_reset` on ESP32. ports forward to their
 * platform equivalents.
 *
 * `restart()` is `[[noreturn]]`  execution does not return; the chip
 * resets within ~1 ms. Callers should not write code after the call
 * (per). If the toolchain rejects `[[noreturn]]` on a pure
 * virtual, the attribute will be dropped and behaviour remains the
 * same see action note in Task 2 (A8 MEDIUM risk).
 *
 * `getTotalHeap()` is required by DiagnosticsCollector which computes
 * used memory as `getTotalHeap() - getFreeHeap()`. Matches existing
 * ESP.getHeapSize() semantics exactly (Esp32 refinement).
 *
 * `getUniqueId(out, outSize)` writes a NUL-terminated hex string of
 * the chip's unique ID. Implementations return at least 8 hex chars
 * (ESP32 truncates the 48-bit MAC to the lower 32 bits => 8 hex).
 * Caller buffer must be at least 13 bytes for ESP32 (8 hex + NUL with
 * a small margin). Returns the number of bytes written excluding NUL.
 *
 * No exceptions  fallible methods return bool. Callers check.
 */

#include <stddef.h>
#include <stdint.h>

namespace ZenoPCB {

struct IZenoSystem {
    virtual ~IZenoSystem() = default;

    /**
     * Reset the chip. Does not return.
     */
    [[noreturn]] virtual void restart() = 0;

    /**
     * Free heap in bytes (smallest free block + free list total 
     * platform-defined; on ESP32 = ESP.getFreeHeap()).
     */
    virtual uint32_t getFreeHeap() = 0;

    /**
     * Largest contiguous free block in bytes (ESP32 = ESP.getMaxAllocHeap()).
     */
    virtual uint32_t getMaxAllocHeap() = 0;

    /**
     * Total heap capacity in bytes (ESP32 = ESP.getHeapSize()).
     * Required by DiagnosticsCollector which computes used heap as
     * getTotalHeap - getFreeHeap. ESP8266 may return a
     * static MMU-defined total; STM32 returns the linker
     * `_end` to `_estack` window.
     */
    virtual uint32_t getTotalHeap() = 0;

    /**
     * Write a NUL-terminated hex representation of the unique chip ID
     * into `out`. Returns bytes written excluding NUL.
     * ESP32: at least 8 hex chars; caller buffer must be >= 13 bytes.
     */
    virtual size_t getUniqueId(char *out, size_t outSize) = 0;

    /**
     * Milliseconds since boot (wraps approximately every 49.7 days).
     */
    virtual uint32_t uptimeMs() = 0;

    /**
     * Feed the task watchdog. No-op on platforms without WDT
     * (capability bit CAP_WATCHDOG signals presence).
     */
    virtual void feedWatchdog() = 0;
};

}  // namespace ZenoPCB

#endif  // ZENOPCB_IZENOSYSTEM_H
