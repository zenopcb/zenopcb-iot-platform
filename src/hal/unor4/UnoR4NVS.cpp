#include "UnoR4NVS.h"

#if defined(ARDUINO_UNOR4_WIFI)

#include <EEPROM.h>
#include <string.h>

#include "../../core/ZenoPCBDebug.h"

namespace ZenoPCB {

namespace {

// FNV-1a 8-bit reduction. Collision probability is acceptable: caller
// surface is `begin(ns)` then keyed reads, and namespaces are static
// literal strings ("gw_net", "zeno_creds", ...) not user input. If a
// future namespace literal collides, planner picks a different literal
// (the namespace strings live in the consumer code, not in NVS bytes).
uint8_t fnv1a8(const char *s) {
    uint32_t h = 2166136261u;
    if (!s) return 0;
    while (*s) {
        h ^= static_cast<uint8_t>(*s++);
        h *= 16777619u;
    }
    // XOR-fold 32 → 8 to spread entropy into the byte we keep.
    uint8_t b = static_cast<uint8_t>((h >> 24) ^ (h >> 16) ^ (h >> 8) ^ h);
    // Reserve 0x00 as the empty-slot / end-of-store marker.
    return (b == 0) ? 1u : b;
}

// Copy `defaultValue` (or "" if null) into `out` with NUL termination, and
// return the number of bytes written excluding the NUL. Used by getString
// when the NVS lookup cannot run (handle closed, bad key) or returned no
// match in the EEPROM scan.
size_t writeDefault(char *out, size_t maxLen, const char *defaultValue) {
    if (!out || maxLen == 0) return 0;
    if (!defaultValue) {
        out[0] = '\0';
        return 0;
    }
    size_t copyLen = strnlen(defaultValue, maxLen - 1);
    memcpy(out, defaultValue, copyLen);
    out[copyLen] = '\0';
    return copyLen;
}

// Maximum bytes we walk in EEPROM. RA4M1 emulation is ~8 KB; we conserve a
// few bytes for future header metadata.
constexpr size_t kEepromMax = 8000;

// Tombstone marker for `keyLen` field of a deleted record.
constexpr uint8_t kTombKeyLen = 0xFF;

// Walk the EEPROM byte array looking for a record matching (_nsHash, key).
// Returns the absolute offset of the record header byte (the `nsHash` byte)
// on match, or `kEepromMax` (out-of-range) on miss. Skips tombstones.
size_t findRecord(uint8_t nsHash, const char *key, size_t keyLen) {
    if (keyLen == 0 || keyLen > 254) return kEepromMax;
    size_t i = 0;
    while (i + 3 <= kEepromMax) {
        uint8_t recNs  = EEPROM.read(i);
        if (recNs == 0) return kEepromMax;   // empty slot — end of store
        uint8_t recKL  = EEPROM.read(i + 1);
        uint8_t recVL  = EEPROM.read(i + 2);
        if (recKL == kTombKeyLen) {
            // Tombstone — value length still tracks the slot footprint so
            // we can skip past it without leaking the original key bytes.
            i += 3 + recVL;
            continue;
        }
        if (recNs == nsHash && recKL == keyLen) {
            // Compare key bytes.
            bool match = true;
            for (size_t k = 0; k < keyLen; ++k) {
                if (EEPROM.read(i + 3 + k) != static_cast<uint8_t>(key[k])) {
                    match = false;
                    break;
                }
            }
            if (match) return i;
        }
        i += 3 + recKL + recVL;
    }
    return kEepromMax;
}

// Find the first empty slot suitable for a new record of (keyLen + valLen)
// bytes. Returns `kEepromMax` if the store cannot hold one more record.
size_t findFreeSlot(size_t keyLen, size_t valLen) {
    if (keyLen == 0 || keyLen > 254 || valLen > 254) return kEepromMax;
    size_t needed = 3 + keyLen + valLen;
    size_t i = 0;
    while (i + 3 <= kEepromMax) {
        uint8_t recNs = EEPROM.read(i);
        if (recNs == 0) {
            // Empty slot — check we have room.
            if (i + needed > kEepromMax) return kEepromMax;
            return i;
        }
        uint8_t recKL = EEPROM.read(i + 1);
        uint8_t recVL = EEPROM.read(i + 2);
        // Treat tombstones as occupied during forward scan (no compaction
        // pass in v1.0 — see TODO note in plan SUMMARY for compaction
        // follow-up if RA4M1 EEPROM emulation reports byte exhaustion in
        // long-running deployments).
        if (recKL == kTombKeyLen) {
            i += 3 + recVL;
        } else {
            i += 3 + recKL + recVL;
        }
    }
    return kEepromMax;
}

// Write a fresh record at absolute offset `off`. Returns true if all bytes
// landed within range; caller is responsible for slot-fit checking.
bool writeRecord(size_t off, uint8_t nsHash, const char *key, size_t keyLen,
                 const uint8_t *val, size_t valLen) {
    if (off + 3 + keyLen + valLen > kEepromMax) return false;
    EEPROM.write(off + 0, nsHash);
    EEPROM.write(off + 1, static_cast<uint8_t>(keyLen));
    EEPROM.write(off + 2, static_cast<uint8_t>(valLen));
    for (size_t k = 0; k < keyLen; ++k) {
        EEPROM.write(off + 3 + k, static_cast<uint8_t>(key[k]));
    }
    for (size_t v = 0; v < valLen; ++v) {
        EEPROM.write(off + 3 + keyLen + v, val[v]);
    }
    return true;
}

}  // namespace

bool UnoR4NVS::begin(const char *namespaceName, bool readOnly) {
    if (!namespaceName) return false;
    if (_open) {
        ZENO_LOG_CORE("UnoR4NVS::begin: handle already open, closing previous");
        _open = false;
    }
    size_t nsLen = strnlen(namespaceName, sizeof(_namespace) - 1);
    memcpy(_namespace, namespaceName, nsLen);
    _namespace[nsLen] = '\0';
    _nsHash = fnv1a8(_namespace);
    _readOnly = readOnly;
    _open = true;
    return true;
}

void UnoR4NVS::end() {
    _open = false;
    _readOnly = false;
    _nsHash = 0;
    _namespace[0] = '\0';
}

bool UnoR4NVS::putString(const char *key, const char *value) {
    if (!_open || _readOnly || !key || !value) return false;
    size_t keyLen = strnlen(key, 255);
    if (keyLen == 0 || keyLen > 254) return false;
    size_t valLen = strnlen(value, 255);
    if (valLen > 254) return false;

    // If the key exists, tombstone the old record before writing the new
    // one (simple O(n) update — no in-place value replacement). Per the
    // CAP_NVS contract we keep the surface minimal; compaction is a v1.1
    // follow-up.
    size_t existing = findRecord(_nsHash, key, keyLen);
    if (existing != kEepromMax) {
        uint8_t recVL = EEPROM.read(existing + 2);
        EEPROM.write(existing + 1, kTombKeyLen);
        EEPROM.write(existing + 2, recVL);
    }

    size_t slot = findFreeSlot(keyLen, valLen);
    if (slot == kEepromMax) return false;
    return writeRecord(slot, _nsHash, key, keyLen,
                       reinterpret_cast<const uint8_t *>(value), valLen);
}

size_t UnoR4NVS::getString(const char *key, char *out, size_t maxLen,
                           const char *defaultValue) {
    if (!out || maxLen == 0) return 0;
    if (!_open || !key) {
        return writeDefault(out, maxLen, defaultValue);
    }
    size_t keyLen = strnlen(key, 255);
    if (keyLen == 0 || keyLen > 254) {
        return writeDefault(out, maxLen, defaultValue);
    }

    size_t rec = findRecord(_nsHash, key, keyLen);
    if (rec == kEepromMax) {
        return writeDefault(out, maxLen, defaultValue);
    }

    uint8_t valLen = EEPROM.read(rec + 2);
    size_t toCopy = valLen;
    if (toCopy > maxLen - 1) toCopy = maxLen - 1;
    for (size_t v = 0; v < toCopy; ++v) {
        out[v] = static_cast<char>(EEPROM.read(rec + 3 + keyLen + v));
    }
    out[toCopy] = '\0';
    return toCopy;
}

bool UnoR4NVS::putULong(const char *key, uint32_t value) {
    if (!_open || _readOnly || !key) return false;
    size_t keyLen = strnlen(key, 255);
    if (keyLen == 0 || keyLen > 254) return false;

    size_t existing = findRecord(_nsHash, key, keyLen);
    if (existing != kEepromMax) {
        uint8_t recVL = EEPROM.read(existing + 2);
        EEPROM.write(existing + 1, kTombKeyLen);
        EEPROM.write(existing + 2, recVL);
    }

    uint8_t buf[4];
    buf[0] = static_cast<uint8_t>(value & 0xFFu);
    buf[1] = static_cast<uint8_t>((value >> 8) & 0xFFu);
    buf[2] = static_cast<uint8_t>((value >> 16) & 0xFFu);
    buf[3] = static_cast<uint8_t>((value >> 24) & 0xFFu);

    size_t slot = findFreeSlot(keyLen, sizeof(buf));
    if (slot == kEepromMax) return false;
    return writeRecord(slot, _nsHash, key, keyLen, buf, sizeof(buf));
}

uint32_t UnoR4NVS::getULong(const char *key, uint32_t defaultValue) {
    if (!_open || !key) return defaultValue;
    size_t keyLen = strnlen(key, 255);
    if (keyLen == 0 || keyLen > 254) return defaultValue;
    size_t rec = findRecord(_nsHash, key, keyLen);
    if (rec == kEepromMax) return defaultValue;
    uint8_t valLen = EEPROM.read(rec + 2);
    if (valLen != 4) return defaultValue;
    uint32_t v = 0;
    v |= static_cast<uint32_t>(EEPROM.read(rec + 3 + keyLen + 0));
    v |= static_cast<uint32_t>(EEPROM.read(rec + 3 + keyLen + 1)) << 8;
    v |= static_cast<uint32_t>(EEPROM.read(rec + 3 + keyLen + 2)) << 16;
    v |= static_cast<uint32_t>(EEPROM.read(rec + 3 + keyLen + 3)) << 24;
    return v;
}

bool UnoR4NVS::putBool(const char *key, bool value) {
    return putUChar(key, value ? 1u : 0u);
}

bool UnoR4NVS::getBool(const char *key, bool defaultValue) {
    uint8_t v = getUChar(key, defaultValue ? 1u : 0u);
    return v != 0;
}

bool UnoR4NVS::putUChar(const char *key, uint8_t value) {
    if (!_open || _readOnly || !key) return false;
    size_t keyLen = strnlen(key, 255);
    if (keyLen == 0 || keyLen > 254) return false;

    size_t existing = findRecord(_nsHash, key, keyLen);
    if (existing != kEepromMax) {
        uint8_t recVL = EEPROM.read(existing + 2);
        EEPROM.write(existing + 1, kTombKeyLen);
        EEPROM.write(existing + 2, recVL);
    }

    size_t slot = findFreeSlot(keyLen, 1);
    if (slot == kEepromMax) return false;
    uint8_t buf[1] = {value};
    return writeRecord(slot, _nsHash, key, keyLen, buf, 1);
}

uint8_t UnoR4NVS::getUChar(const char *key, uint8_t defaultValue) {
    if (!_open || !key) return defaultValue;
    size_t keyLen = strnlen(key, 255);
    if (keyLen == 0 || keyLen > 254) return defaultValue;
    size_t rec = findRecord(_nsHash, key, keyLen);
    if (rec == kEepromMax) return defaultValue;
    uint8_t valLen = EEPROM.read(rec + 2);
    if (valLen != 1) return defaultValue;
    return EEPROM.read(rec + 3 + keyLen);
}

bool UnoR4NVS::remove(const char *key) {
    if (!_open || _readOnly || !key) return false;
    size_t keyLen = strnlen(key, 255);
    if (keyLen == 0 || keyLen > 254) return false;
    size_t rec = findRecord(_nsHash, key, keyLen);
    if (rec == kEepromMax) return false;
    uint8_t recVL = EEPROM.read(rec + 2);
    EEPROM.write(rec + 1, kTombKeyLen);
    EEPROM.write(rec + 2, recVL);
    return true;
}

bool UnoR4NVS::clear() {
    if (!_open || _readOnly) return false;
    // Walk the store and tombstone every record matching this namespace.
    size_t i = 0;
    while (i + 3 <= kEepromMax) {
        uint8_t recNs = EEPROM.read(i);
        if (recNs == 0) break;
        uint8_t recKL = EEPROM.read(i + 1);
        uint8_t recVL = EEPROM.read(i + 2);
        if (recKL == kTombKeyLen) {
            i += 3 + recVL;
            continue;
        }
        if (recNs == _nsHash) {
            EEPROM.write(i + 1, kTombKeyLen);
            // recVL byte preserves the slot footprint for forward walks.
        }
        i += 3 + recKL + recVL;
    }
    return true;
}

}  // namespace ZenoPCB

#endif  // defined(ARDUINO_UNOR4_WIFI)
