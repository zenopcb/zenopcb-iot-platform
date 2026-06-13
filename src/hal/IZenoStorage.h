#ifndef ZENOPCB_IZENOSTORAGE_H
#define ZENOPCB_IZENOSTORAGE_H

/**
 * @file IZenoStorage.h
 * @brief Pure-virtual interface for file-system / blob storage.
 *
 * Part of the ZenoPCB Hardware Abstraction Layer (HAL). Concrete impls
 * live under hal/<platform>/ (e.g. hal/esp32/Esp32Storage.{h,cpp}). On
 * ESP32 the impl wraps LittleFS; on Phase 6 ESP8266 it wraps LittleFS
 * as well; on Phase 7 STM32 it can wrap InternalStorage or SPI flash.
 *
 * Design notes:
 *  - All path / data parameters are `const char*` and bounded buffers
 *    (no Arduino heap-allocated text type) per CLAUDE.md memory rule.
 *  - readFile / writeFile are "atomic read all / write all" — they hide
 *    file-handle lifetime entirely so callers cannot leak descriptors.
 *  - listFiles uses a callback invoked synchronously per matching file.
 *    The callback MUST NOT store the path pointer beyond the call;
 *    implementations may reuse an internal buffer between callbacks.
 *  - writeFile returns the number of bytes written (not bool) so the
 *    caller can detect a short write (disk full) by comparing to `len`.
 *
 * No exceptions — fallible methods return bool / size_t. Callers must
 * check return values.
 */

#include <stddef.h>
#include <stdint.h>
#include <functional>

namespace ZenoPCB {

struct IZenoStorage {
    virtual ~IZenoStorage() = default;

    /**
     * Initialise the underlying filesystem (e.g. mount LittleFS, format
     * on first-boot failure). Returns true on success.
     */
    virtual bool begin() = 0;

    /**
     * Returns true if `path` exists in the filesystem.
     */
    virtual bool exists(const char *path) = 0;

    /**
     * Read the entire file at `path` into `out` (up to `maxLen-1` bytes,
     * NUL-terminated). Returns bytes written excluding the NUL, or 0
     * on failure / missing file.
     */
    virtual size_t readFile(const char *path, char *out, size_t maxLen) = 0;

    /**
     * Write exactly `len` bytes from `data` to `path` (overwrites).
     * Returns the number of bytes actually written. Caller compares
     * to `len` to detect short writes (disk full).
     */
    virtual size_t writeFile(const char *path, const char *data, size_t len) = 0;

    /**
     * Delete the file at `path`. Returns true if removed (or did not
     * exist on platforms whose unlink is idempotent — impl-defined).
     */
    virtual bool deleteFile(const char *path) = 0;

    /**
     * Create a directory at `path`. Required by ScheduleStorage which
     * calls LittleFS.mkdir(SCHEDULE_DIR) to ensure the schedules dir
     * exists before writing per-id files.
     */
    virtual bool mkdir(const char *path) = 0;

    /**
     * Iterate files whose path begins with `prefix`. The callback runs
     * synchronously per file. Do not store the path pointer beyond the
     * callback — implementations may reuse the buffer.
     */
    virtual void listFiles(const char *prefix,
                           std::function<void(const char *)> callback) = 0;
};

}  // namespace ZenoPCB

#endif  // ZENOPCB_IZENOSTORAGE_H
