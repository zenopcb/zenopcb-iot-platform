#ifndef ZENOPCB_IZENOOTA_H
#define ZENOPCB_IZENOOTA_H

/**
 * @file IZenoOTA.h
 * @brief Pure-virtual interface for firmware over-the-air updates.
 *
 * Lifecycle: begin(size, md5?) -> write(...) [N times] -> end() | abort().
 *
 * `end()` only commits the new partition as bootable; the caller must
 * explicitly call `IZenoSystem.restart` afterwards. This split lets
 * Phase 7 STM32 ship without Update.h while keeping restart functional.
 *
 * On ESP32 the impl forwards to the global `Update` singleton (Update.h).
 * On Phase 6 ESP8266 it forwards to `Updater`. On Phase 7 STM32 it can
 * forward to a custom bootloader / IAP flash writer.
 *
 * Optional MD5: pass via begin(); impl forwards to Update.setMD5 before
 * Update.begin (per RESEARCH Pitfall 2 — Option b, single call).
 *
 * Rollback: canRollBack()/rollBack() let the device revert to the previous
 * bootable partition on ESP32 (Update.canRollBack / Update.rollBack).
 *
 * No Arduino heap-allocated text type in signatures — char buffers only.
 * No exceptions — fallible methods return bool / size_t. Callers check.
 */

#include <stddef.h>
#include <stdint.h>

namespace ZenoPCB {

struct IZenoOTA {
    virtual ~IZenoOTA() = default;

    /**
     * Begin an OTA session. `expectedSize` is the total firmware size
     * in bytes. `expectedMd5` is an optional NUL-terminated hex MD5;
     * pass nullptr to skip MD5 verification.
     * Returns true on success.
     */
    virtual bool begin(size_t expectedSize, const char *expectedMd5 = nullptr) = 0;

    /**
     * Write a chunk of firmware bytes. Returns the number of bytes
     * actually written. Caller compares to `len` to detect a short
     * write (partition full / sector erase failed).
     */
    virtual size_t write(const uint8_t *data, size_t len) = 0;

    /**
     * Commit the new partition as bootable. Does NOT reboot. Caller is
     * responsible for calling IZenoSystem.restart afterwards.
     * Returns true on success.
     */
    virtual bool end() = 0;

    /**
     * Abort the OTA session and discard any written data.
     */
    virtual void abort() = 0;

    /**
     * Returns a NUL-terminated human-readable error description for the
     * last failure. Pointer remains valid until the next OTA call.
     */
    virtual const char *errorString() = 0;

    /**
     * Returns true if the platform supports rolling back to the previous
     * partition (ESP32: Update.canRollBack()).
     */
    virtual bool canRollBack() = 0;

    /**
     * Roll back to the previous bootable partition. Returns true on
     * success; caller still must call IZenoSystem.restart to boot it.
     */
    virtual bool rollBack() = 0;
};

}  // namespace ZenoPCB

#endif  // ZENOPCB_IZENOOTA_H
