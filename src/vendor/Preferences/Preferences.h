// SPDX-License-Identifier: MIT
//
// vshymanskyy/Preferences — vendored from upstream v2.2.2 into ZenoPCB IoT Library
// (https://github.com/vshymanskyy/Preferences)
//
// Original Copyright (c) Volodymyr Shymanskyy.
// Licensed under MIT — full text in lib/ZenoPCB/src/vendor/Preferences/LICENSE.

#ifndef _PREFERENCES_H_
#define _PREFERENCES_H_

// ============================================================================
// ZenoPCB Plan 06-2.5d — Pattern B/Pitfall 7 TU guard at vendor surface.
//
// The vendored vshymanskyy/Preferences backport is an ESP8266-only NVS impl
// (the upstream Preferences_setup.h emits `#error "For ESP32 devices, please
// use the native Preferences library"` when defined(ESP32)). Esp8266NVS is
// the sole consumer, and it is already guarded `#if defined(ESP8266)` at its
// own header (Plan 06-2.5a). However, PIO's library scanner compiles every
// `.cpp` and indexes every `.h` under `lib/ZenoPCB/src/` on every env —
// including ESP32 envs — which surfaces the vendor's hard `#error` plus the
// implementation-detail FS function declarations missing on non-ESP8266.
//
// Fix: wrap the vendored Preferences.{h,cpp} bodies in `#if defined(ESP8266)`
// so ESP32 envs see an empty translation unit at the vendor surface. ESP8266
// build behaviour unchanged. No upstream semantics modified — only platform
// gating consistent with how this vendored copy is actually used (sole
// consumer Esp8266NVS is already ESP8266-only).
//
// Logged in 06-2.5d-SUMMARY as Rule 3 (blocking issue auto-fix): without
// this lift, the ESP32 12-env regression cannot be restored even after the
// 5 Esp8266*.h files are TU-guarded.
// ============================================================================
#if defined(ESP8266)

#if defined(PARTICLE)
#  include "Particle.h"
#elif defined(ARDUINO)
#  include "Arduino.h"
#endif

#include "Preferences_setup.h"

#include <math.h>

typedef enum {
    PT_I8, PT_U8, PT_I16, PT_U16, PT_I32, PT_U32, PT_I64, PT_U64, PT_STR, PT_BLOB, PT_INVALID
} PreferenceType;

class ZenoPreferences
{
    typedef float float_t;
    typedef double double_t;

    protected:
#if defined(NVS_USE_DCT)
        dct_handle_t _handle;
#else
        String _path;
#endif
        bool _started;
        bool _readOnly;
    public:
        ZenoPreferences();
        ~ZenoPreferences();

        bool begin(const char * name, bool readOnly=false);
        void end();

        bool clear();
        bool remove(const char * key);

        size_t putChar(const char* key, int8_t value);
        size_t putUChar(const char* key, uint8_t value);
        size_t putShort(const char* key, int16_t value);
        size_t putUShort(const char* key, uint16_t value);
        size_t putInt(const char* key, int32_t value);
        size_t putUInt(const char* key, uint32_t value);
        size_t putLong(const char* key, int32_t value);
        size_t putULong(const char* key, uint32_t value);
        size_t putLong64(const char* key, int64_t value);
        size_t putULong64(const char* key, uint64_t value);
        size_t putFloat(const char* key, float_t value);
        size_t putDouble(const char* key, double_t value);
        size_t putBool(const char* key, bool value);
        size_t putString(const char* key, const char* value);
        size_t putString(const char* key, String value);
        size_t putBytes(const char* key, const void* buf, size_t len);

        bool isKey(const char* key);
        PreferenceType getType(const char* key);
        int8_t getChar(const char* key, int8_t defaultValue = 0);
        uint8_t getUChar(const char* key, uint8_t defaultValue = 0);
        int16_t getShort(const char* key, int16_t defaultValue = 0);
        uint16_t getUShort(const char* key, uint16_t defaultValue = 0);
        int32_t getInt(const char* key, int32_t defaultValue = 0);
        uint32_t getUInt(const char* key, uint32_t defaultValue = 0);
        int32_t getLong(const char* key, int32_t defaultValue = 0);
        uint32_t getULong(const char* key, uint32_t defaultValue = 0);
        int64_t getLong64(const char* key, int64_t defaultValue = 0);
        uint64_t getULong64(const char* key, uint64_t defaultValue = 0);
        float_t getFloat(const char* key, float_t defaultValue = NAN);
        double_t getDouble(const char* key, double_t defaultValue = NAN);
        bool getBool(const char* key, bool defaultValue = false);
        size_t getString(const char* key, char* value, size_t maxLen);
        String getString(const char* key, String defaultValue = String());
        size_t getBytesLength(const char* key);
        size_t getBytes(const char* key, void * buf, size_t maxLen);
        size_t freeEntries();
};

#endif  // defined(ESP8266)

#endif  // _PREFERENCES_H_
