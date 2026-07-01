#ifndef ZENOPCB_ZKEY_BUFFER_H
#define ZENOPCB_ZKEY_BUFFER_H

/**
 * @file ZKeyBuffer.h
 * @brief Z Key buffer - stores Z0-Z254 values with dirty tracking & callbacks
 *
 * Singleton buffer managing 255 Z key slots. Supports:
 * - Set/Get values with type safety (int, float, String, bool)
 * - Dirty flag tracking for efficient publish (only changed keys)
 * - Per-key onChange callbacks (triggered when cloud sends control messages)
 * - Configurable publish interval with optional instant publish
 * - JSON serialization for MQTT telemetry
 *
 * @author ZenoPCB Development Team
 * @version 1.0.0
 */

#include <Arduino.h>
#include "../ZenoJson.h"  // ArduinoJson API from vendored copy (namespace ZenoJson; see vendor/ArduinoJson/LICENSE.md)
#include <functional>
#include <map>
#include "ZKeyTypes.h"
#include "ZenoPCBDebug.h"

namespace ZenoPCB
{

    class ZKeyBuffer
    {
    public:
        // ============================================
        // Singleton
        // ============================================

        static ZKeyBuffer &getInstance();

        // ============================================
        // Set Values (marks dirty automatically)
        // ============================================

        void set(ZKey key, int32_t value);
        void set(ZKey key, float value);
        void set(ZKey key, const String &value);
        void set(ZKey key, const char *value);
        void set(ZKey key, bool value);

        // ============================================
        // Get Values (type-safe with defaults)
        // ============================================

        ZValue get(ZKey key) const;
        int32_t getInt(ZKey key, int32_t defaultVal = 0) const;
        float getFloat(ZKey key, float defaultVal = 0.0f) const;
        String getString(ZKey key, const String &defaultVal = "") const;
        bool getBool(ZKey key, bool defaultVal = false) const;
        ZValueType getType(ZKey key) const;
        bool isSet(ZKey key) const;

        // ============================================
        // Set from parsed JSON value (for control handler)
        // ============================================

        /**
         * @brief Set value from ArduinoJson variant (auto-detect type)
         * Used by MQTTControlHandler when receiving control messages
         */
        void setFromJson(ZKey key, const JsonVariant &value);

        // ============================================
        // Dirty Tracking & JSON Build
        // ============================================

        /**
         * @brief Check if any Z key has been modified since last publish
         */
        bool hasDirtyKeys() const;

        /**
         * @brief Count of dirty keys
         */
        uint16_t getDirtyCount() const;

        /**
         * @brief Serialize dirty Z keys into existing JsonDocument
         * Merges Z key data into the document: {"Z0": 25.5, "Z1": "ON", ...}
         *
         * @param doc JsonDocument to merge into (can already contain Modbus data)
         * @return Number of keys added
         */
        uint16_t mergeIntoJson(JsonDocument &doc) const;

        /**
         * @brief Serialize ALL set Z keys into existing JsonDocument (ignores dirty flag)
         * Used by get_all command to return current snapshot of all keys ever set.
         *
         * @param doc JsonDocument to merge into
         * @return Number of keys added
         */
        uint16_t mergeAllIntoJson(JsonDocument &doc) const;

        /**
         * @brief Merge a size-bounded chunk of dirty keys into a JsonDocument.
         *
         * Scans forward from *cursor, adding dirty keys until the estimated
         * serialized size would exceed maxBytes (or the buffer is exhausted).
         * The first dirty key is always added. Advances cursor past the keys
         * consumed so repeated calls walk the whole buffer in bounded chunks
         * without ever materialising the full 255-key document.
         *
         * @param doc      JsonDocument to merge into
         * @param cursor   In/out scan position (start at 0; carried across calls)
         * @param maxBytes Soft cap on the chunk's serialized size
         * @return Number of keys added in this chunk (0 = no dirty keys left)
         */
        uint16_t mergeDirtyChunk(JsonDocument &doc, uint16_t &cursor, size_t maxBytes) const;

        /**
         * @brief Build standalone JSON string with only Z keys
         * @return JSON string like {"Z0": 25.5, "Z1": "ON"}
         */
        String buildJson() const;

        /**
         * @brief Clear all dirty flags (call after successful publish)
         */
        void clearDirtyFlags();

        // ============================================
        // Publish Configuration
        // ============================================

        /**
         * @brief Set publish interval in milliseconds
         * @param intervalMs Publish interval (min: 1000ms, default: 5000ms)
         */
        void setPublishInterval(uint32_t intervalMs);

        /**
         * @brief Get current publish interval
         */
        uint32_t getPublishInterval() const { return _publishInterval; }

        /**
         * @brief Enable instant publish mode
         * When enabled, set() will trigger immediate publish (throttled by min interval)
         */
        void setInstantPublish(bool enable);

        /**
         * @brief Check if instant publish is enabled
         */
        bool isInstantPublish() const { return _instantPublish; }

        /**
         * @brief Check if it's time to publish based on interval
         */
        bool isPublishDue() const;

        /**
         * @brief Check if instant publish was requested (by set() call)
         */
        bool isInstantPublishPending() const { return _instantPublishPending; }

        /**
         * @brief Explicitly request an instant publish on next loop cycle
         */
        void requestInstantPublish() { _instantPublishPending = true; }

        /**
         * @brief Mark publish as done.
         */
        void markPublished();

        /**
         * @brief Reset the publish-due timer.
         */
        void markPublishTimer();

        // ============================================
        // Callbacks (triggered by cloud device control)
        // ============================================

        /**
         * @brief Register callback for specific Z key change (from cloud control)
         * @param key Z key to watch
         * @param callback Function called with new value
         */
        void onChange(ZKey key, ZKeyChangeCallback callback);

        /**
         * @brief Register global callback for any Z key change (from cloud control)
         * @param callback Function called with key and new value
         */
        void onAnyChange(ZKeyChangeCallback callback);

        /**
         * @brief Manually trigger onChange callbacks for a key
         * (Use after internal set() calls, e.g. from ScheduleExecutor)
         * @param key Z key to notify
         */
        void notifyChange(ZKey key);

        /**
         * @brief Install all pending CLOUD_TO_DEVICE handlers queued at
         *        static-init time. Called by Zeno::begin().
         *
         * The CLOUD_TO_DEVICE macro generates a global ZKeyHandlerRegistrar
         * whose ctor pushes the (key, callback) pair into a module-local
         * queue (see ZenoTimer.cpp). begin() then drains that queue into
         * _keyCallbacks[] via this method.
         */
        void commitPendingHandlers();

        // ============================================
        // Stats & Debug
        // ============================================

        /**
         * @brief Get count of keys that have been set (type != NONE)
         */
        uint16_t getActiveKeyCount() const;

        /**
         * @brief Reset a specific key to NONE
         */
        void reset(ZKey key);

        /**
         * @brief Reset all keys to NONE
         */
        void resetAll();

    private:
        ZKeyBuffer();
        ~ZKeyBuffer() = default;
        ZKeyBuffer(const ZKeyBuffer &) = delete;
        ZKeyBuffer &operator=(const ZKeyBuffer &) = delete;

        // Storage: 255 slots
        ZValue _values[Z_KEY_COUNT];

        // Publish config
        uint32_t _publishInterval;
        bool _instantPublish;
        bool _instantPublishPending;
        unsigned long _lastPublishTime;

        // Callbacks: per-key and global
        ZKeyChangeCallback _keyCallbacks[Z_KEY_COUNT]; // Per-key callbacks (null if not set)
        ZKeyChangeCallback _globalCallback;            // Global callback

        // Internal: trigger onChange callbacks (only for cloud-initiated changes)
        void _triggerCallbacks(ZKey key, const ZValue &value);
    };

} // namespace ZenoPCB

#endif // ZENOPCB_ZKEY_BUFFER_H
