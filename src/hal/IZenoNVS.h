#ifndef ZENOPCB_IZENONVS_H
#define ZENOPCB_IZENONVS_H

/**
 * @file IZenoNVS.h
 * @brief Pure-virtual interface for namespaced non-volatile key/value storage.
 *
 * Surface mirrors Arduino-ESP32 `Preferences` exactly so refactored
 * consumers preserve byte-for-byte NVS storage compatibility — devices
 * keep their saved Wi-Fi config across the refactor (per 04-RESEARCH.md
 * Pitfall 6 / A9).
 *
 * Lifecycle:
 *  - begin(ns, readOnly) opens a namespace handle.
 *  - end() releases the handle. ESP32 NVS supports ~4 simultaneously
 *    open namespaces — every begin() must be paired with an end() to
 *    prevent handle table exhaustion (per Pitfall 1).
 *  - Multiple sequential namespaces are fine: begin("a")...end(),
 *    begin("b")...end().
 *
 * The full audited Preferences surface from WiFiProvisioning.cpp is
 * exposed here: putBool/getBool, putUChar/getUChar, putString/getString,
 * putULong/getULong, remove, clear. Plan 04-03 will refactor consumers
 * to this surface; signature parity matters.
 *
 * No Arduino heap-allocated text type in signatures — char buffers only
 * (CLAUDE.md memory rule). No exceptions — fallible methods return bool.
 * Callers check.
 */

#include <stddef.h>
#include <stdint.h>

namespace ZenoPCB {

struct IZenoNVS {
    virtual ~IZenoNVS() = default;

    /**
     * Open `namespaceName` for read/write (default) or read-only.
     * Returns true on success. Must be paired with end().
     */
    virtual bool begin(const char *namespaceName, bool readOnly = false) = 0;

    /**
     * Release the currently open namespace handle. Safe to call when
     * no namespace is open (no-op).
     */
    virtual void end() = 0;

    // --- Text -----------------------------------------------------------

    /**
     * Store a NUL-terminated text value under `key`. Returns true on success.
     */
    virtual bool putString(const char *key, const char *value) = 0;

    /**
     * Read text at `key` into `out` (up to `maxLen-1` bytes, NUL-term).
     * If the key is missing or empty, copies `defaultValue` instead.
     * Returns bytes written excluding NUL.
     */
    virtual size_t getString(const char *key, char *out, size_t maxLen,
                             const char *defaultValue) = 0;

    // --- 32-bit unsigned -----------------------------------------------

    virtual bool putULong(const char *key, uint32_t value) = 0;
    virtual uint32_t getULong(const char *key, uint32_t defaultValue) = 0;

    // --- Bool -----------------------------------------------------------
    // Required by WiFiProvisioning.cpp (lines 136, 1732, 1748, 1760, 1775,
    // 1789, 1801) — without this consumers cannot preserve byte-compat.

    virtual bool putBool(const char *key, bool value) = 0;
    virtual bool getBool(const char *key, bool defaultValue) = 0;

    // --- 8-bit unsigned -------------------------------------------------
    // Required by WiFiProvisioning.cpp lines 1737, 1778.

    virtual bool putUChar(const char *key, uint8_t value) = 0;
    virtual uint8_t getUChar(const char *key, uint8_t defaultValue) = 0;

    // --- Maintenance ----------------------------------------------------

    /**
     * Remove a single key from the currently open namespace.
     */
    virtual bool remove(const char *key) = 0;

    /**
     * Wipe every key in the currently open namespace.
     */
    virtual bool clear() = 0;
};

}  // namespace ZenoPCB

#endif  // ZENOPCB_IZENONVS_H
