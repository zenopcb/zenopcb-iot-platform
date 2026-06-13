#include "Stm32NVS.h"

#if defined(STM32F1) || defined(STM32F4)

#include <string.h>

#include "../../core/ZenoPCBDebug.h"

namespace ZenoPCB {

// ============================================================================
// Per-family partition byte budget (07-RESEARCH §Pattern 2 lines 476-480).
// `kPartitionBytes` is the upper bound of the linear walker scan; it does
// NOT change the underlying ZenoFlashStorage `length()` (which is board-
// defined via E2END). Clamping below `length()` keeps NVS usage within
// the per-family RAM/Flash budget per D-12 MICRO profile.
// ============================================================================
#if defined(STM32F4)
static constexpr size_t kPartitionBytes = 8u * 1024u;   // F4 NVS budget.
#elif defined(STM32F1)
static constexpr size_t kPartitionBytes = 2u * 1024u;   // F1 MICRO budget per D-12.
#endif

// Sentinel byte values for the nsHash slot.
static constexpr uint8_t kSlotEmpty     = 0x00;
static constexpr uint8_t kSlotTombstone = 0xFF;

namespace {

// Copy `defaultValue` (or "" if null) into `out` with NUL termination, and
// return the number of bytes written excluding the NUL. Used by getString
// when the NVS lookup cannot run (handle closed, bad key) or returned 0.
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

}  // namespace

uint8_t Stm32NVS::_hashNs(const char *ns) {
    if (!ns) return 0x01;
    // 8-bit djb2 — folds 32-bit djb2 output to a single byte via XOR.
    uint32_t h = 5381u;
    for (const char *p = ns; *p; ++p) {
        h = ((h << 5) + h) + static_cast<uint8_t>(*p);   // h*33 + c
    }
    uint8_t out = static_cast<uint8_t>((h ^ (h >> 8) ^ (h >> 16) ^ (h >> 24)) & 0xFFu);
    // Clamp 0x00 and 0xFF away from the sentinel slots.
    if (out == kSlotEmpty)     out = 0x01;
    if (out == kSlotTombstone) out = 0xFE;
    return out;
}

void Stm32NVS::_flushIfDirty() {
    if (_flash) {
        // commit() is a no-op if the dirty flag is not set; safe to call
        // unconditionally at end().
        _flash->commit();
    }
}

bool Stm32NVS::_findRecord(const char *key, size_t &recordAddr,
                           size_t &valAddr, uint8_t &valLen) {
    if (!_flash || !key) return false;
    const uint8_t keyLen = static_cast<uint8_t>(strnlen(key, 255));
    if (keyLen == 0) return false;

    size_t addr = 0;
    while (addr + 3 < kPartitionBytes) {
        const uint8_t ns = _flash->read(static_cast<int>(addr));
        if (ns == kSlotEmpty) {
            // Empty terminator — no more records past this point.
            return false;
        }
        const uint8_t kLen = _flash->read(static_cast<int>(addr + 1));
        const uint8_t vLen = _flash->read(static_cast<int>(addr + 2));
        const size_t recordSize = 3u + kLen + vLen;
        // Skip malformed records (length walks past partition).
        if (addr + recordSize > kPartitionBytes) {
            return false;
        }
        if (ns == _nsHash && kLen == keyLen) {
            // Compare key bytes.
            bool match = true;
            for (uint8_t i = 0; i < kLen; ++i) {
                if (_flash->read(static_cast<int>(addr + 3u + i)) != static_cast<uint8_t>(key[i])) {
                    match = false;
                    break;
                }
            }
            if (match) {
                recordAddr = addr;
                valAddr = addr + 3u + kLen;
                valLen = vLen;
                return true;
            }
        }
        // Skip tombstones + non-matches.
        addr += recordSize;
    }
    return false;
}

bool Stm32NVS::_appendRecord(const char *key, const uint8_t *val, uint8_t valLen) {
    if (!_flash || !key || !val) return false;
    if (_readOnly) return false;
    const uint8_t keyLen = static_cast<uint8_t>(strnlen(key, 255));
    if (keyLen == 0) return false;

    // Walk to first empty slot (nsHash == kSlotEmpty), tombstoning any
    // prior record for the same key along the way.
    size_t addr = 0;
    while (addr + 3 < kPartitionBytes) {
        const uint8_t ns = _flash->read(static_cast<int>(addr));
        if (ns == kSlotEmpty) {
            // Found the first empty slot — write here.
            break;
        }
        const uint8_t kLen = _flash->read(static_cast<int>(addr + 1));
        const uint8_t vLen = _flash->read(static_cast<int>(addr + 2));
        const size_t recordSize = 3u + kLen + vLen;
        if (addr + recordSize > kPartitionBytes) {
            // Malformed — bail.
            return false;
        }
        if (ns == _nsHash && kLen == keyLen) {
            // Check for prior record with same key → tombstone it.
            bool match = true;
            for (uint8_t i = 0; i < kLen; ++i) {
                if (_flash->read(static_cast<int>(addr + 3u + i)) != static_cast<uint8_t>(key[i])) {
                    match = false;
                    break;
                }
            }
            if (match) {
                _flash->update(static_cast<int>(addr), kSlotTombstone);
            }
        }
        addr += recordSize;
    }

    // Verify the new record fits in the remaining partition window.
    const size_t needed = 3u + keyLen + valLen;
    if (addr + needed > kPartitionBytes) {
        return false;   // partition full
    }

    // Write the new record. Order matters: write payload BEFORE the nsHash
    // sentinel so a torn write does not leave a half-formed record visible.
    _flash->update(static_cast<int>(addr + 1), keyLen);
    _flash->update(static_cast<int>(addr + 2), valLen);
    for (uint8_t i = 0; i < keyLen; ++i) {
        _flash->update(static_cast<int>(addr + 3u + i), static_cast<uint8_t>(key[i]));
    }
    for (uint8_t i = 0; i < valLen; ++i) {
        _flash->update(static_cast<int>(addr + 3u + keyLen + i), val[i]);
    }
    // Last: stamp the nsHash to "publish" the record.
    _flash->update(static_cast<int>(addr), _nsHash);

    return true;
}

bool Stm32NVS::begin(const char *namespaceName, bool readOnly) {
    if (!namespaceName) return false;
    if (_open) {
        ZENO_LOG_CORE("Stm32NVS::begin: handle already open, closing previous");
        _flushIfDirty();
        _open = false;
    }
    // Copy namespace name (bounded).
    size_t nsLen = strnlen(namespaceName, sizeof(_namespace) - 1);
    memcpy(_namespace, namespaceName, nsLen);
    _namespace[nsLen] = '\0';
    _nsHash = _hashNs(_namespace);
    _readOnly = readOnly;
    _flash = &ZenoEEPROM;
    // Disable auto-commit-on-put — batch writes per begin/end pair.
    _flash->setCommitASAP(false);
    _open = true;
    return _open;
}

void Stm32NVS::end() {
    if (_open) {
        _flushIfDirty();
        _open = false;
        _flash = nullptr;
    }
}

bool Stm32NVS::putString(const char *key, const char *value) {
    if (!_open || !key || !value) return false;
    const size_t vLen = strnlen(value, 255);
    return _appendRecord(key, reinterpret_cast<const uint8_t *>(value),
                         static_cast<uint8_t>(vLen));
}

size_t Stm32NVS::getString(const char *key, char *out, size_t maxLen,
                           const char *defaultValue) {
    if (!out || maxLen == 0) return 0;
    if (!_open || !key) {
        return writeDefault(out, maxLen, defaultValue);
    }
    size_t recordAddr = 0;
    size_t valAddr = 0;
    uint8_t valLen = 0;
    if (!_findRecord(key, recordAddr, valAddr, valLen)) {
        return writeDefault(out, maxLen, defaultValue);
    }
    size_t toCopy = valLen;
    if (toCopy > maxLen - 1) toCopy = maxLen - 1;
    for (size_t i = 0; i < toCopy; ++i) {
        out[i] = static_cast<char>(_flash->read(static_cast<int>(valAddr + i)));
    }
    out[toCopy] = '\0';
    return toCopy;
}

bool Stm32NVS::putULong(const char *key, uint32_t value) {
    if (!_open || !key) return false;
    // Store little-endian 4-byte representation.
    uint8_t buf[4];
    buf[0] = static_cast<uint8_t>(value & 0xFFu);
    buf[1] = static_cast<uint8_t>((value >> 8) & 0xFFu);
    buf[2] = static_cast<uint8_t>((value >> 16) & 0xFFu);
    buf[3] = static_cast<uint8_t>((value >> 24) & 0xFFu);
    return _appendRecord(key, buf, 4u);
}

uint32_t Stm32NVS::getULong(const char *key, uint32_t defaultValue) {
    if (!_open || !key) return defaultValue;
    size_t recordAddr = 0;
    size_t valAddr = 0;
    uint8_t valLen = 0;
    if (!_findRecord(key, recordAddr, valAddr, valLen)) {
        return defaultValue;
    }
    if (valLen != 4) return defaultValue;
    uint32_t v = 0;
    v |= static_cast<uint32_t>(_flash->read(static_cast<int>(valAddr + 0)));
    v |= static_cast<uint32_t>(_flash->read(static_cast<int>(valAddr + 1))) << 8;
    v |= static_cast<uint32_t>(_flash->read(static_cast<int>(valAddr + 2))) << 16;
    v |= static_cast<uint32_t>(_flash->read(static_cast<int>(valAddr + 3))) << 24;
    return v;
}

bool Stm32NVS::putBool(const char *key, bool value) {
    if (!_open || !key) return false;
    const uint8_t b = value ? 0x01u : 0x00u;
    return _appendRecord(key, &b, 1u);
}

bool Stm32NVS::getBool(const char *key, bool defaultValue) {
    if (!_open || !key) return defaultValue;
    size_t recordAddr = 0;
    size_t valAddr = 0;
    uint8_t valLen = 0;
    if (!_findRecord(key, recordAddr, valAddr, valLen)) {
        return defaultValue;
    }
    if (valLen != 1) return defaultValue;
    return _flash->read(static_cast<int>(valAddr)) != 0u;
}

// ============================================================================
// PLACEHOLDER — Plan 07-04 W-5 fix narrows working-body scope to 8 core
// methods; the 4 below are deferred to Plan 07-09 hardware UAT per the plan.
// Each returns the IZenoNVS-contract default on failure (false / defaultValue).
// ============================================================================

bool Stm32NVS::putUChar(const char *, uint8_t) {
    // TODO Plan 07-09 hardware UAT: route through _appendRecord with 1-byte
    // value, mirroring putBool. Trivial body — held back from Task 2 only
    // to keep the W-5 scope mitigation honest (working bodies for the 8
    // most-used IZenoNVS surfaces; deferred for 4 less-used).
    return false;
}

uint8_t Stm32NVS::getUChar(const char *, uint8_t defaultValue) {
    // TODO Plan 07-09 hardware UAT: route through _findRecord, expect valLen==1.
    return defaultValue;
}

bool Stm32NVS::remove(const char *) {
    // TODO Plan 07-09 hardware UAT: tombstone the record (nsHash =
    // kSlotTombstone) via _flash->update; require _readOnly == false.
    return false;
}

bool Stm32NVS::clear() {
    // TODO Plan 07-09 hardware UAT: walk the partition and tombstone every
    // record belonging to _nsHash, then commit. Alternative full-wipe
    // (write 0x00 across [0, kPartitionBytes)) destroys data in other
    // namespaces — not what IZenoNVS::clear() promises.
    return false;
}

}  // namespace ZenoPCB

#endif  // defined(STM32F1) || defined(STM32F4)
