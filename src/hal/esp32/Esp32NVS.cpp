#include "Esp32NVS.h"

// (symmetric to Esp8266 mirror)
// TU guard so PIO's library scanner compiles to an empty translation
// unit on non-ESP32 envs even when build_src_filter does not exclude
// the file.
#if defined(ESP32)

#include <string.h>

#include "../../core/ZenoPCBDebug.h"

namespace ZenoPCB {

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

bool Esp32NVS::begin(const char *namespaceName, bool readOnly) {
    if (!namespaceName) return false;
    if (_open) {
        // pair every open with a close. If a caller forgot to
        // close the previous namespace, surface it and recover.
        ZENO_LOG_CORE("Esp32NVS::begin: handle already open, closing previous");
        _prefs.end();
        _open = false;
    }
    _open = _prefs.begin(namespaceName, readOnly);
    return _open;
}

void Esp32NVS::end() {
    if (_open) {
        _prefs.end();
        _open = false;
    }
}

bool Esp32NVS::putString(const char *key, const char *value) {
    if (!_open || !key || !value) return false;
    return _prefs.putString(key, value) > 0;
}

size_t Esp32NVS::getString(const char *key, char *out, size_t maxLen,
                           const char *defaultValue) {
    if (!out || maxLen == 0) return 0;
    if (!_open || !key) {
        return writeDefault(out, maxLen, defaultValue);
    }

    if (!_prefs.isKey(key)) {
        return writeDefault(out, maxLen, defaultValue);
    }

    // explicitly call the char-buffer overload (not the
    // String-returning one) to avoid heap allocation. Returns bytes written
    // including the NUL on success per arduino-esp32 docs; we re-derive the
    // length via strnlen for safety.
    _prefs.getString(key, out, maxLen);
    out[maxLen - 1] = '\0';
    return strnlen(out, maxLen);
}

bool Esp32NVS::putULong(const char *key, uint32_t value) {
    if (!_open || !key) return false;
    return _prefs.putULong(key, value) > 0;
}

uint32_t Esp32NVS::getULong(const char *key, uint32_t defaultValue) {
    if (!_open || !key) return defaultValue;
    return _prefs.getULong(key, defaultValue);
}

bool Esp32NVS::putBool(const char *key, bool value) {
    if (!_open || !key) return false;
    return _prefs.putBool(key, value) > 0;
}

bool Esp32NVS::getBool(const char *key, bool defaultValue) {
    if (!_open || !key) return defaultValue;
    return _prefs.getBool(key, defaultValue);
}

bool Esp32NVS::putUChar(const char *key, uint8_t value) {
    if (!_open || !key) return false;
    return _prefs.putUChar(key, value) > 0;
}

uint8_t Esp32NVS::getUChar(const char *key, uint8_t defaultValue) {
    if (!_open || !key) return defaultValue;
    return _prefs.getUChar(key, defaultValue);
}

bool Esp32NVS::remove(const char *key) {
    if (!_open || !key) return false;
    return _prefs.remove(key);
}

bool Esp32NVS::clear() {
    if (!_open) return false;
    return _prefs.clear();
}

}  // namespace ZenoPCB

#endif  // defined(ESP32)
