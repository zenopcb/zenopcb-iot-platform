/**
 * @file ZKeyBuffer.cpp
 * @brief Z Key buffer implementation
 *
 * @author ZenoPCB Development Team
 * @version 1.0.0
 */

#include "ZKeyBuffer.h"

namespace ZenoPCB
{
    // ============================================
    // Singleton
    // ============================================

    ZKeyBuffer &ZKeyBuffer::getInstance()
    {
        static ZKeyBuffer instance;
        return instance;
    }

    ZKeyBuffer::ZKeyBuffer()
        : _publishInterval(Z_KEY_DEFAULT_PUBLISH_INTERVAL),
          _instantPublish(false),
          _instantPublishPending(false),
          _lastPublishTime(0),
          _globalCallback(nullptr)
    {
        // Initialize all key callbacks to nullptr
        memset(_keyCallbacks, 0, sizeof(_keyCallbacks));
    }

    // ============================================
    // Set Values
    // ============================================

    void ZKeyBuffer::set(ZKey key, int32_t value)
    {
        uint8_t idx = zKeyIndex(key);
        _values[idx].type = ZValueType::INT;
        _values[idx].intVal = value;
        _values[idx].dirty = true;

        if (_instantPublish)
        {
            _instantPublishPending = true;
        }
    }

    void ZKeyBuffer::set(ZKey key, float value)
    {
        uint8_t idx = zKeyIndex(key);
        _values[idx].type = ZValueType::FLOAT;
        _values[idx].floatVal = value;
        _values[idx].dirty = true;

        if (_instantPublish)
        {
            _instantPublishPending = true;
        }
    }

    void ZKeyBuffer::set(ZKey key, const String &value)
    {
        uint8_t idx = zKeyIndex(key);
        _values[idx].type = ZValueType::STRING;
        _values[idx].strVal = value.substring(0, 64); // Max 64 chars
        _values[idx].dirty = true;

        if (_instantPublish)
        {
            _instantPublishPending = true;
        }
    }

    void ZKeyBuffer::set(ZKey key, const char *value)
    {
        set(key, String(value));
    }

    void ZKeyBuffer::set(ZKey key, bool value)
    {
        uint8_t idx = zKeyIndex(key);
        _values[idx].type = ZValueType::BOOL;
        _values[idx].boolVal = value;
        _values[idx].dirty = true;

        if (_instantPublish)
        {
            _instantPublishPending = true;
        }
    }

    // ============================================
    // Get Values
    // ============================================

    ZValue ZKeyBuffer::get(ZKey key) const
    {
        return _values[zKeyIndex(key)];
    }

    int32_t ZKeyBuffer::getInt(ZKey key, int32_t defaultVal) const
    {
        return _values[zKeyIndex(key)].toInt(defaultVal);
    }

    float ZKeyBuffer::getFloat(ZKey key, float defaultVal) const
    {
        return _values[zKeyIndex(key)].toFloat(defaultVal);
    }

    String ZKeyBuffer::getString(ZKey key, const String &defaultVal) const
    {
        return _values[zKeyIndex(key)].toString(defaultVal);
    }

    bool ZKeyBuffer::getBool(ZKey key, bool defaultVal) const
    {
        return _values[zKeyIndex(key)].toBool(defaultVal);
    }

    ZValueType ZKeyBuffer::getType(ZKey key) const
    {
        return _values[zKeyIndex(key)].type;
    }

    bool ZKeyBuffer::isSet(ZKey key) const
    {
        return _values[zKeyIndex(key)].isSet();
    }

    // ============================================
    // Set from JSON variant (for control handler)
    // ============================================

    void ZKeyBuffer::setFromJson(ZKey key, const JsonVariant &value)
    {
        if (value.is<bool>())
        {
            set(key, value.as<bool>());
        }
        else if (value.is<int32_t>())
        {
            set(key, value.as<int32_t>());
        }
        else if (value.is<float>() || value.is<double>())
        {
            set(key, value.as<float>());
        }
        else if (value.is<const char *>())
        {
            set(key, String(value.as<const char *>()));
        }
        else
        {
            // Fallback: convert to string
            String str;
            serializeJson(value, str);
            set(key, str);
        }

        // Trigger callbacks for cloud-initiated changes
        _triggerCallbacks(key, _values[zKeyIndex(key)]);
    }

    void ZKeyBuffer::notifyChange(ZKey key)
    {
        _triggerCallbacks(key, _values[zKeyIndex(key)]);
    }

    // ============================================
    // Dirty Tracking & JSON Build
    // ============================================

    bool ZKeyBuffer::hasDirtyKeys() const
    {
        for (uint16_t i = 0; i < Z_KEY_COUNT; i++)
        {
            if (_values[i].dirty)
                return true;
        }
        return false;
    }

    uint16_t ZKeyBuffer::getDirtyCount() const
    {
        uint16_t count = 0;
        for (uint16_t i = 0; i < Z_KEY_COUNT; i++)
        {
            if (_values[i].dirty)
                count++;
        }
        return count;
    }

    uint16_t ZKeyBuffer::mergeIntoJson(JsonDocument &doc) const
    {
        uint16_t count = 0;
        for (uint16_t i = 0; i < Z_KEY_COUNT; i++)
        {
            if (!_values[i].dirty || _values[i].type == ZValueType::NONE)
                continue;

            // Build "Z<n>" key into a stack buffer. Previously this used
            // String concatenation, which incurred 2x heap allocations per
            // dirty key and up to 255x per merge cycle.
            // Buffer size 8 = "Z" + up to 3 digits + NUL = 5 bytes, with safe
            // headroom — matches the sister-file convention at
            // ZKeyTypes.h:430 (char check[8]; snprintf(check, sizeof check,
            // "Z%d", val)). %d, not %u, to match that existing convention.
            char keyBuf[8];
            snprintf(keyBuf, sizeof(keyBuf), "Z%d", (int)i);

            // ArduinoJson v7 quirk (RESEARCH.md Pitfall #1): doc[const char*]
            // stores the pointer by reference, NOT a copy. keyBuf goes out of
            // scope on the next loop iteration — passing it as a raw char*
            // would dangle when serializeJson runs later.
            //
            // Forcing the key through String(keyBuf) makes ArduinoJson v7
            // copy the key at assignment time. Net cost: 1 short heap alloc
            // per dirty key (was: 2) — ~50% reduction in merge-cycle heap
            // traffic.
            switch (_values[i].type)
            {
            case ZValueType::INT:
                doc[String(keyBuf)] = _values[i].intVal;
                count++;
                break;
            case ZValueType::FLOAT:
                doc[String(keyBuf)] = _values[i].floatVal;
                count++;
                break;
            case ZValueType::STRING:
                doc[String(keyBuf)] = _values[i].strVal;
                count++;
                break;
            case ZValueType::BOOL:
                doc[String(keyBuf)] = _values[i].boolVal;
                count++;
                break;
            default:
                break;
            }
        }
        return count;
    }

    uint16_t ZKeyBuffer::mergeAllIntoJson(JsonDocument &doc) const
    {
        uint16_t count = 0;
        for (uint16_t i = 0; i < Z_KEY_COUNT; i++)
        {
            if (_values[i].type == ZValueType::NONE)
                continue; // Key never set

            // Same refactor as mergeIntoJson above — char keyBuf[8] + snprintf
            // + doc[String(keyBuf)] forces ArduinoJson v7 to copy the key so
            // the loop-local buffer never dangles past the iteration.
            // Sister method shares the allocator pattern; verify gate covers
            // the whole file.
            char keyBuf[8];
            snprintf(keyBuf, sizeof(keyBuf), "Z%d", (int)i);

            switch (_values[i].type)
            {
            case ZValueType::INT:
                doc[String(keyBuf)] = _values[i].intVal;
                count++;
                break;
            case ZValueType::FLOAT:
                doc[String(keyBuf)] = _values[i].floatVal;
                count++;
                break;
            case ZValueType::STRING:
                doc[String(keyBuf)] = _values[i].strVal;
                count++;
                break;
            case ZValueType::BOOL:
                doc[String(keyBuf)] = _values[i].boolVal;
                count++;
                break;
            default:
                break;
            }
        }
        return count;
    }

    String ZKeyBuffer::buildJson() const
    {
        JsonDocument doc;
        mergeIntoJson(doc);
        String output;
        serializeJson(doc, output);
        return output;
    }

    void ZKeyBuffer::clearDirtyFlags()
    {
        for (uint16_t i = 0; i < Z_KEY_COUNT; i++)
        {
            _values[i].dirty = false;
        }
        _instantPublishPending = false;
    }

    // ============================================
    // Publish Configuration
    // ============================================

    void ZKeyBuffer::setPublishInterval(uint32_t intervalMs)
    {
        // 0 = disable auto-publish (push only on demand)
        if (intervalMs == 0)
            _publishInterval = 0;
        else
            _publishInterval = max(intervalMs, Z_KEY_MIN_PUBLISH_INTERVAL);
    }

    void ZKeyBuffer::setInstantPublish(bool enable)
    {
        _instantPublish = enable;
    }

    bool ZKeyBuffer::isPublishDue() const
    {
        // Instant publish bypasses timer — publish on very next loop()
        if (_instantPublishPending)
            return true;
        if (_publishInterval == 0)
            return false; // disabled
        return (millis() - _lastPublishTime) >= _publishInterval;
    }

    void ZKeyBuffer::markPublished()
    {
        _lastPublishTime = millis();
        _instantPublishPending = false;
        clearDirtyFlags(); // Also clear dirty flags to prevent re-publish on next loop
    }

    void ZKeyBuffer::markPublishTimer()
    {
        _lastPublishTime = millis();
        _instantPublishPending = false;
        // Intentionally NOT calling clearDirtyFlags() — keeps values set from
        // loop() (e.g. `ZENO_WRITE(Z0, x)` outside ZENO_READ_ALL) alive so
        // _publishZKeyTelemetry() picks them up on this cycle.
    }

    // ============================================
    // Callbacks
    // ============================================

    void ZKeyBuffer::onChange(ZKey key, ZKeyChangeCallback callback)
    {
        _keyCallbacks[zKeyIndex(key)] = callback;
    }

    void ZKeyBuffer::onAnyChange(ZKeyChangeCallback callback)
    {
        _globalCallback = callback;
    }

    void ZKeyBuffer::_triggerCallbacks(ZKey key, const ZValue &value)
    {
        uint8_t idx = zKeyIndex(key);

        // Per-key callback
        if (_keyCallbacks[idx])
        {
            _keyCallbacks[idx](key, value);
        }

        // Global callback
        if (_globalCallback)
        {
            _globalCallback(key, value);
        }
    }

    // ============================================
    // Stats & Debug
    // ============================================

    uint16_t ZKeyBuffer::getActiveKeyCount() const
    {
        uint16_t count = 0;
        for (uint16_t i = 0; i < Z_KEY_COUNT; i++)
        {
            if (_values[i].type != ZValueType::NONE)
                count++;
        }
        return count;
    }

    void ZKeyBuffer::reset(ZKey key)
    {
        uint8_t idx = zKeyIndex(key);
        _values[idx] = ZValue();
    }

    void ZKeyBuffer::resetAll()
    {
        for (uint16_t i = 0; i < Z_KEY_COUNT; i++)
        {
            _values[i] = ZValue();
        }
        _instantPublishPending = false;
    }

} // namespace ZenoPCB
