#ifndef ZENOPCB_UNOR4_NVS_H
#define ZENOPCB_UNOR4_NVS_H

/**
 * @file UnoR4NVS.h
 * @brief Arduino UNO R4 WiFi (Renesas RA4M1) concrete impl of IZenoNVS 
 *        wraps the Arduino built-in `<EEPROM.h>` (Renesas FSP EEPROM
 *        emulation, ~8 KB on RA4M1).
 *
 * Mechanical mirror of Esp8266NVS.{h,cpp}.
 * See.planning/phases/07-uno-r4-stm32-ports-capability-matrix/
 * "UnoR4NVS" (lines 351-415).
 *
 * + RESEARCH Architectural Responsibility Map line 110: UNO R4
 * exposes the standard Arduino `<EEPROM.h>` API backed by RA4M1 silicon-
 * level wear-leveled flash emulation (~8 KB total). No vendored
 * Preferences backport is needed  we hand-roll a small length-prefixed
 * KV walker over the EEPROM byte array. Layout per record:
 *
 *   [u8 nsHash][u8 keyLen][u8 valLen][key bytes ...][val bytes ...]
 *
 *   - `nsHash`  : 1-byte FNV-1a hash of the namespace name (collision
 *                 acceptable: caller surface is `begin(ns)` then keyed
 *                 reads, and namespaces are static literals not user
 *                 input). 0x00 marks an empty slot / end-of-store.
 *   - `keyLen`  : 1..254  bytes in the key (raw, no NUL).
 *                 0xFF reserved for "deleted record" tombstone.
 *   - `valLen`  : 0..254  bytes in the value (raw, no NUL).
 *
 * RA4M1 EEPROM has silicon-level wear leveling (RESEARCH Don't Hand-Roll),
 * so no software wear management is required at the KV layer.
 *
 * Holds only the namespace identifier + open/readOnly flags; no
 * heap-allocated state. Stack arrays for KV record scan kept < 256 B per
 * CLAUDE.md memory rule.
 *
 * Deleted copy semantics per (single underlying EEPROM byte
 * array is process-global state; duplicating the wrapper would let two
 * `_open` namespaces race against each other on commit / clear).
 */

#include "../IZenoNVS.h"

// / lifted to.h surface (carry-forward)
// `<EEPROM.h>` resolves to a different concrete class on AVR Arduino /
// ESP32 / ESP8266 / Renesas RA4M1 cores. PIO's library scanner indexes
// this header on every env. Guarding at the header surface keeps ESP32 /
// ESP8266 envs from pulling in the Renesas-specific EEPROM emulation
// during library indexing. The IZenoNVS abstract interface include stays
// OUTSIDE the guard because it is the cross-platform contract type.
#if defined(ARDUINO_UNOR4_WIFI)

#include <EEPROM.h>

namespace ZenoPCB {

class UnoR4NVS : public IZenoNVS {
public:
    UnoR4NVS() = default;
    ~UnoR4NVS() override {
        // Best-effort end() clear the open flag so a destructor-without-end
        // path does not leave the wrapper in an inconsistent state. The
        // EEPROM byte array itself is process-global; nothing to release.
        _open = false;
    }

    // Deleted copy semantics (EEPROM byte array is process-
    // global state; duplicating the wrapper would let two `_open`
    // namespaces race on commit / clear).
    UnoR4NVS(const UnoR4NVS&) = delete;
    UnoR4NVS& operator=(const UnoR4NVS&) = delete;

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
    // Length-prefixed KV records walked by linear scan within the
    // namespace partition. Layout per the file-header comment.
    char _namespace[16] = {0};
    uint8_t _nsHash = 0;
    bool _open = false;
    bool _readOnly = false;
};

}  // namespace ZenoPCB

#endif  // defined(ARDUINO_UNOR4_WIFI)

#endif  // ZENOPCB_UNOR4_NVS_H
