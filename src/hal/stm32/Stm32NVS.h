#ifndef ZENOPCB_STM32_NVS_H
#define ZENOPCB_STM32_NVS_H

/**
 * @file Stm32NVS.h
 * @brief STM32 (F1 + F4) concrete impl of IZenoNVS  wraps the vendored
 * ZenoFlashStorage (MIT,) as a length-prefixed KV walker.
 *
 * Mechanical mirror of `Esp8266NVS.{h,cpp}` see
 * "Stm32NVS". Where Esp8266NVS delegates to the vendored
 * ZenoPreferences (Arduino-ESP32-style namespace KV API), Stm32NVS wraps
 * the upstream `ZenoFlashStorage` from `lib/ZenoPCB/src/vendor/FlashStorage/`
 * (renamed in per). The underlying emulated-EEPROM surface
 * is byte-addressable (`read(addr)` / `update(addr, val)` / `get<T>(addr, T&)`
 * / `put<T>(addr, T&)` / `commit()`) so we build the namespace + key-string
 * semantics on top via a linear length-prefixed record walker.
 *
 * Per-family partition byte budget (lines 476-480)
 * STM32F4: 8 KB walk window (hardware UAT validates)
 * STM32F1: 2 KB walk window per MICRO profile (CAP_DIAGNOSTICS off)
 *
 * Record layout (linearly scanned within the walk window):
 *   `[u8 nsHash][u8 keyLen][u8 valLen][key bytes][val bytes]`
 *   - `nsHash`  = 8-bit djb2 hash of the namespace string (`begin()` arg);
 *                 0x00 = empty slot, 0xFF = tombstone (deleted record).
 *   - `keyLen`  = key length excluding NUL (1-255).
 *   - `valLen`  = value length excluding NUL (1-255).
 *   - `key`     = raw bytes; NOT NUL-terminated.
 *   - `val`     = raw bytes; NOT NUL-terminated.
 *
 * W-5 fix: 8 core methods (begin/end/putString/getString/
 * putULong/getULong/putBool/getBool) MUST have working bodies using the
 * walker. PLACEHOLDER returns are allowed ONLY for the 4 lower-priority
 * methods (putUChar/getUChar/remove/clear) with explicit TODO comments
 * pointing at hardware UAT.
 *
 * deleted copy semantics (ZenoFlashStorage wraps a single
 * underlying RAM mirror via `static ZenoFlashStorage ZenoEEPROM;` in
 * vendor/FlashStorage/FlashStorage_STM32.hpp:211; duplicating the wrapper
 * duplicates the handle reference).
 */

#include "../IZenoNVS.h"

// / TU guard at header surface. The vendored
// `FlashStorage_STM32.h` emits a hard `#error` on non-STM32 targets
// (vendor/FlashStorage/FlashStorage_STM32.h:55-58); guarding the include
// + class body at the header surface ensures ESP32/ESP8266/UNO R4 envs
// produce an empty translation unit for this file during library scanning.
#if defined(STM32F1xx) || defined(STM32F4xx)

// Relative include into the vendored ZenoFlashStorage (, MIT
// renamed from upstream EEPROMClass ZenoFlashStorage per). The
// vendored header itself has a defense-in-depth STM32-family `#error`
// (see FlashStorage_STM32.h:55-58) so the TU guard above is checked twice.
#include "../../vendor/FlashStorage/FlashStorage_STM32.hpp"

namespace ZenoPCB {

class Stm32NVS : public IZenoNVS {
public:
    Stm32NVS() = default;
    ~Stm32NVS() override {
        // Best-effort: flush any pending RAM-mirror writes if the user
        // forgot to call end() before destruction.
        if (_open) {
            _flushIfDirty();
            _open = false;
        }
    }

    // Deleted copy semantics (ZenoFlashStorage wraps a single
    // global RAM mirror via the `ZenoEEPROM` static; duplicating the
    // wrapper duplicates the reference).
    Stm32NVS(const Stm32NVS&) = delete;
    Stm32NVS& operator=(const Stm32NVS&) = delete;

    bool begin(const char *namespaceName, bool readOnly = false) override;
    void end() override;

    bool putString(const char *key, const char *value) override;
    size_t getString(const char *key, char *out, size_t maxLen,
                     const char *defaultValue) override;

    bool putULong(const char *key, uint32_t value) override;
    uint32_t getULong(const char *key, uint32_t defaultValue) override;

    bool putBool(const char *key, bool value) override;
    bool getBool(const char *key, bool defaultValue) override;

    bool putUChar(const char *key, uint8_t value) override;
    uint8_t getUChar(const char *key, uint8_t defaultValue) override;

    bool remove(const char *key) override;
    bool clear() override;

private:
    // Internal helpers walker primitives below the IZenoNVS surface.
    //
    // `_findRecord(key, recordAddr, valAddr, valLen)` walks the partition
    // and returns true if a non-tombstone record exists for the currently-
    // open `_nsHash` + `key`. `_recordAddr` is the address of the record's
    // first byte (nsHash); `_valAddr` is the address of the first value
    // byte; `_valLen` is the value byte length.
    bool _findRecord(const char *key, size_t &recordAddr,
                     size_t &valAddr, uint8_t &valLen);

    // Append a new record at the first empty slot. Returns false if the
    // partition is full. Auto-tombstones any prior record for the same
    // key (write-once semantics behave as overwrite from the caller).
    bool _appendRecord(const char *key, const uint8_t *val, uint8_t valLen);

    // 8-bit djb2 hash of a NUL-terminated string. 0x00 reserved for
    // empty slots; 0xFF reserved for tombstone. If the natural djb2
    // result is 0x00 or 0xFF, clamp to 0x01 / 0xFE respectively.
    static uint8_t _hashNs(const char *ns);

    void _flushIfDirty();

    ZenoFlashStorage *_flash = nullptr;
    char _namespace[16] = {0};
    uint8_t _nsHash = 0;
    bool _open = false;
    bool _readOnly = false;
};

}  // namespace ZenoPCB

#endif  // defined(STM32F1xx) || defined(STM32F4xx)

#endif  // ZENOPCB_STM32_NVS_H
