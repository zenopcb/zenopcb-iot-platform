// SPDX-License-Identifier: MIT
//
// vshymanskyy/Preferences — vendored from upstream v2.2.2 into ZenoPCB IoT Library
// (https://github.com/vshymanskyy/Preferences)
//
// Original Copyright (c) Volodymyr Shymanskyy.
// Licensed under MIT — full text in lib/ZenoPCB/src/vendor/Preferences/LICENSE.

#include "Preferences.h"

// Pattern B/Pitfall 7 TU guard at vendor .cpp surface (Plan 06-2.5d). The
// vendored Preferences is an ESP8266-only NVS impl whose sole consumer
// (Esp8266NVS) is already `#if defined(ESP8266)` guarded. On ESP32 envs PIO
// still compiles this translation unit, surfacing the Preferences_setup.h
// hard `#error` and undefined `_fs_*` references. Wrapping the body in the
// platform guard turns this into an empty TU on ESP32 — matching how the
// vendored copy is actually used. No upstream semantics modified.
#if defined(ESP8266)

//#define NVS_LOG

#define NVS_LOG_NAME "prefs"

#if defined(NVS_LOG) && defined(PARTICLE)
  static Logger prefsLog(NVS_LOG_NAME);

  #define LOG_D(...)        prefsLog.trace(__VA_ARGS__)
  #define LOG_I(...)        prefsLog.info(__VA_ARGS__)
  #define LOG_W(...)        prefsLog.warn(__VA_ARGS__)
  #define LOG_E(...)        prefsLog.error(__VA_ARGS__)
#elif defined(NVS_LOG) && defined(ARDUINO)
  #define LOG_D(fmt, ...)   { Serial.printf("%6lu [%s] DEBUG: ", millis(),  NVS_LOG_NAME); Serial.printf(fmt "\n", ##__VA_ARGS__); }
  #define LOG_I(fmt, ...)   { Serial.printf("%6lu [%s] INFO: ",  millis(),  NVS_LOG_NAME); Serial.printf(fmt "\n", ##__VA_ARGS__); }
  #define LOG_W(fmt, ...)   { Serial.printf("%6lu [%s] WARN: ",  millis(),  NVS_LOG_NAME); Serial.printf(fmt "\n", ##__VA_ARGS__); }
  #define LOG_E(fmt, ...)   { Serial.printf("%6lu [%s] ERROR: ", millis(),  NVS_LOG_NAME); Serial.printf(fmt "\n", ##__VA_ARGS__); }
#else
  #define LOG_D(fmt, ...)
  #define LOG_I(fmt, ...)
  #define LOG_W(fmt, ...)
  #define LOG_E(fmt, ...)
#endif

#if defined(NVS_USE_DCT)
  #include "Preferences_impl_dct.h"
#else
  #include "Preferences_impl_fs.h"
#endif

// Plan 06-03 Rule 1 (auto-fix bug discovered when ESP8266 first compiled
// vendored Preferences) — Plan 06-2.5c renamed the class to
// ZenoPreferences but left the ctor/dtor spelled Preferences(), which
// only compiles for ESP32 because no ESP8266 env exercised this code
// path before. Fix: rename ctor/dtor to match class name per ISO C++.
ZenoPreferences::ZenoPreferences()
    : _started(false)
    , _readOnly(false)
{}

ZenoPreferences::~ZenoPreferences(){
    end();
}

/*
 * Put a key value
 * */

size_t ZenoPreferences::putChar(const char* key, int8_t value){
    return putBytes(key, (void*)&value, sizeof(value));
}

size_t ZenoPreferences::putUChar(const char* key, uint8_t value){
    return putBytes(key, (void*)&value, sizeof(value));
}

size_t ZenoPreferences::putShort(const char* key, int16_t value){
    return putBytes(key, (void*)&value, sizeof(value));
}

size_t ZenoPreferences::putUShort(const char* key, uint16_t value){
    return putBytes(key, (void*)&value, sizeof(value));
}

size_t ZenoPreferences::putInt(const char* key, int32_t value){
    return putBytes(key, (void*)&value, sizeof(value));
}

size_t ZenoPreferences::putUInt(const char* key, uint32_t value){
    return putBytes(key, (void*)&value, sizeof(value));
}

size_t ZenoPreferences::putLong(const char* key, int32_t value){
    return putInt(key, value);
}

size_t ZenoPreferences::putULong(const char* key, uint32_t value){
    return putUInt(key, value);
}

size_t ZenoPreferences::putLong64(const char* key, int64_t value){
    return putBytes(key, (void*)&value, sizeof(value));
}

size_t ZenoPreferences::putULong64(const char* key, uint64_t value){
    return putBytes(key, (void*)&value, sizeof(value));
}

size_t ZenoPreferences::putFloat(const char* key, const float_t value){
    return putBytes(key, (void*)&value, sizeof(value));
}

size_t ZenoPreferences::putDouble(const char* key, const double_t value){
    return putBytes(key, (void*)&value, sizeof(value));
}

size_t ZenoPreferences::putBool(const char* key, const bool value){
    return putUChar(key, (uint8_t) (value ? 1 : 0));
}

size_t ZenoPreferences::putString(const char* key, const char* value){
    return putBytes(key, (void*)value, strlen(value));
}

size_t ZenoPreferences::putString(const char* key, const String value){
    return putBytes(key, value.c_str(), value.length());
}

PreferenceType ZenoPreferences::getType(const char* key) {
    (void)key;
    return PT_INVALID;
}

/*
 * Get a key value
 * */

int8_t ZenoPreferences::getChar(const char* key, const int8_t defaultValue){
    int8_t value = defaultValue;
    getBytes(key, (void*) &value, sizeof(value));
    return value;
}

uint8_t ZenoPreferences::getUChar(const char* key, const uint8_t defaultValue){
    uint8_t value = defaultValue;
    getBytes(key, (void*) &value, sizeof(value));
    return value;
}

int16_t ZenoPreferences::getShort(const char* key, const int16_t defaultValue){
    int16_t value = defaultValue;
    getBytes(key, (void*) &value, sizeof(value));
    return value;
}

uint16_t ZenoPreferences::getUShort(const char* key, const uint16_t defaultValue){
    uint16_t value = defaultValue;
    getBytes(key, (void*) &value, sizeof(value));
    return value;
}

int32_t ZenoPreferences::getInt(const char* key, const int32_t defaultValue){
    int32_t value = defaultValue;
    getBytes(key, (void*) &value, sizeof(value));
    return value;
}

uint32_t ZenoPreferences::getUInt(const char* key, const uint32_t defaultValue){
    uint32_t value = defaultValue;
    getBytes(key, (void*) &value, sizeof(value));
    return value;
}

int32_t ZenoPreferences::getLong(const char* key, const int32_t defaultValue){
    return getInt(key, defaultValue);
}

uint32_t ZenoPreferences::getULong(const char* key, const uint32_t defaultValue){
    return getUInt(key, defaultValue);
}

int64_t ZenoPreferences::getLong64(const char* key, const int64_t defaultValue){
    int64_t value = defaultValue;
    getBytes(key, (void*) &value, sizeof(value));
    return value;
}

uint64_t ZenoPreferences::getULong64(const char* key, const uint64_t defaultValue){
    uint64_t value = defaultValue;
    getBytes(key, (void*) &value, sizeof(value));
    return value;
}

float_t ZenoPreferences::getFloat(const char* key, const float_t defaultValue) {
    float_t value = defaultValue;
    getBytes(key, (void*) &value, sizeof(value));
    return value;
}

double_t ZenoPreferences::getDouble(const char* key, const double_t defaultValue) {
    double_t value = defaultValue;
    getBytes(key, (void*) &value, sizeof(value));
    return value;
}

bool ZenoPreferences::getBool(const char* key, const bool defaultValue) {
    return getUChar(key, defaultValue ? 1 : 0) == 1;
}

size_t ZenoPreferences::getString(const char* key, char* value, const size_t maxLen){
    if(!_started || !key || !value || !maxLen){
        return 0;
    }
    size_t len = getBytes(key, value, maxLen-1);
    value[len] = '\0';
    return len;
}

#endif  // defined(ESP8266)
