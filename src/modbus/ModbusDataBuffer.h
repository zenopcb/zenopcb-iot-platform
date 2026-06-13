#ifndef ZENOPCB_MODBUS_DATA_BUFFER_H
#define ZENOPCB_MODBUS_DATA_BUFFER_H

#include "../storage/DataMonitorConfig.h"
#include <map>
#include <vector>
#include <memory>
#include <functional>

namespace ZenoPCB
{

    /**
     * @brief Represents a single register value with metadata
     *
     * v2.2: Added 64-bit types (i64, u64, f64)
     */
    struct RegisterValue
    {
        union
        {
            // 16-bit values (1 register)
            uint16_t u16Value;
            int16_t i16Value;

            // 32-bit values (2 registers)
            uint32_t u32Value;
            int32_t i32Value;
            float floatValue;

            // 64-bit values (4 registers) - NEW v2.2
            uint64_t u64Value;
            int64_t i64Value;
            double doubleValue;
        };
        bool boolValue;

        enum ValueStatus
        {
            UNKNOWN,        // Never read
            VALID,          // Last read successful
            TIMEOUT,        // Read timeout
            ERROR,          // Read error
            CONNECTION_LOST // Connection lost
        };

        ValueStatus status;
        uint32_t lastUpdateTime; // Timestamp of last successful read
        uint32_t lastErrorTime;  // Timestamp of last error
        uint8_t retryCount;      // Current retry count
        String lastError;
        uint8_t connectionErrorCode; // Modbus connection error code (0x00=success, 0xE4=timeout, etc.)

        // Constructor
        RegisterValue()
            : status(UNKNOWN), lastUpdateTime(0), lastErrorTime(0),
              retryCount(0), u64Value(0), boolValue(false), connectionErrorCode(0)
        {
        }

        /**
         * @brief Apply scaling and offset to raw value (v2.2)
         *
         * Returns double for better precision with 64-bit values.
         * 32-bit float has only ~7 significant digits, while double has ~15.
         *
         * WARNING: For i64/u64 values > 2^53 (9,007,199,254,740,992),
         * even double will lose precision. Use getRawI64/getRawU64 for exact values.
         */
        double getScaledValue(const DataMonitorConfig &config) const
        {
            double value = 0.0;

            // Get base value based on dataType
            switch (config.dataType)
            {
            case DataType::SIGNED_INT16:
                value = (double)i16Value;
                break;
            case DataType::UNSIGNED_INT16:
                value = (double)u16Value;
                break;
            case DataType::SIGNED_INT32:
                value = (double)i32Value;
                break;
            case DataType::UNSIGNED_INT32:
                value = (double)u32Value;
                break;
            case DataType::FLOAT32:
                value = (double)floatValue;
                break;
            case DataType::SIGNED_INT64:
                value = (double)i64Value;
                break;
            case DataType::UNSIGNED_INT64:
                value = (double)u64Value;
                break;
            case DataType::FLOAT64:
                value = doubleValue;
                break;
            case DataType::BOOLEAN:
                value = boolValue ? 1.0 : 0.0;
                break;
            default:
                value = (double)u16Value;
            }

            // Apply scaling (v2.1)
            if (config.scaleEnabled)
            {
                if (config.scaleOperation == ScaleOperation::MULTIPLY)
                {
                    value *= (double)config.scaleValue;
                }
                else if (config.scaleOperation == ScaleOperation::DIVIDE)
                {
                    if (config.scaleValue != 0.0f)
                    {
                        value /= (double)config.scaleValue;
                    }
                }
            }

            // Apply offset (v2.1)
            if (config.offsetValue != 0.0f)
            {
                if (config.offsetOperation == OffsetOperation::ADD)
                {
                    value += (double)config.offsetValue;
                }
                else if (config.offsetOperation == OffsetOperation::SUBTRACT)
                {
                    value -= (double)config.offsetValue;
                }
            }

            return value;
        }

        /**
         * @brief Check if dataType is 64-bit (v2.2)
         * Used for JSON serialization precision handling
         */
        static bool is64BitType(DataType type)
        {
            return type == DataType::SIGNED_INT64 ||
                   type == DataType::UNSIGNED_INT64 ||
                   type == DataType::FLOAT64;
        }

        /**
         * @brief Check if dataType is integer (v2.2)
         * Used for JSON serialization
         */
        static bool isIntegerType(DataType type)
        {
            return type == DataType::SIGNED_INT16 ||
                   type == DataType::UNSIGNED_INT16 ||
                   type == DataType::SIGNED_INT32 ||
                   type == DataType::UNSIGNED_INT32 ||
                   type == DataType::SIGNED_INT64 ||
                   type == DataType::UNSIGNED_INT64;
        }

        // Get JSON representation
        String toJson() const
        {
            JsonDocument doc;
            doc["v"] = u16Value; // Raw value
            doc["s"] = (int)status;
            doc["t"] = lastUpdateTime;
            doc["e"] = lastError;
            return String(doc.as<String>());
        }
    };

    /**
     * @brief Buffer to store all register values
     * Maps mqttKey -> RegisterValue
     */
    class ModbusDataBuffer
    {
    public:
        static ModbusDataBuffer &getInstance();

        // Initialization
        bool begin();

        // Register management
        bool addRegister(const DataMonitorConfig &config);
        bool updateRegisterConfig(const DataMonitorConfig &config); // ⭐ Update existing config (enabled, etc.)
        bool removeRegister(const String &mqttKey);
        bool hasRegister(const String &mqttKey) const;
        std::vector<String> listRegisters() const;

        // Update values (16-bit)
        void updateRegisterValue(const String &mqttKey, uint16_t u16Value, RegisterValue::ValueStatus status = RegisterValue::VALID);
        void updateRegisterValue(const String &mqttKey, int16_t i16Value, RegisterValue::ValueStatus status = RegisterValue::VALID);

        // Update values (32-bit)
        void updateRegisterValue(const String &mqttKey, uint32_t u32Value, RegisterValue::ValueStatus status = RegisterValue::VALID);
        void updateRegisterValue(const String &mqttKey, int32_t i32Value, RegisterValue::ValueStatus status = RegisterValue::VALID);
        void updateRegisterValue(const String &mqttKey, float floatValue, RegisterValue::ValueStatus status = RegisterValue::VALID);

        // Update values (64-bit) - NEW v2.2
        void updateRegisterValue(const String &mqttKey, uint64_t u64Value, RegisterValue::ValueStatus status = RegisterValue::VALID);
        void updateRegisterValue(const String &mqttKey, int64_t i64Value, RegisterValue::ValueStatus status = RegisterValue::VALID);
        void updateRegisterValue(const String &mqttKey, double doubleValue, RegisterValue::ValueStatus status = RegisterValue::VALID);

        // Update values (boolean)
        void updateRegisterValue(const String &mqttKey, bool boolValue, RegisterValue::ValueStatus status = RegisterValue::VALID);
        void setError(const String &mqttKey, const String &error, uint8_t connectionErrorCode = 0);

        // Read values
        RegisterValue getValue(const String &mqttKey) const;
        double getScaledValue(const String &mqttKey) const; // v2.2: returns double for 64-bit precision
        bool isValid(const String &mqttKey) const;

        // Batch operations
        size_t getRegisterCount() const { return _registerValues.size(); }
        size_t getValidCount() const;
        size_t getErrorCount() const;

        // Returns true if at least one enabled register has a VALID value or a fresh last-known-good.
        // Used to suppress periodic telemetry when all values are null/error.
        bool hasPublishableValues() const;

        // Configuration access
        const DataMonitorConfig *getRegisterConfig(const String &mqttKey) const;

        // Telemetry building
        // includeNulls=true: emit null for stale-expired registers (used by get_all)
        // includeNulls=false: omit stale-expired registers entirely (used by periodic telemetry)
        String buildTelemetryJson(size_t maxSize = 4096, bool includeNulls = false);
        String buildRegisterJson(const String &mqttKey); // Single register JSON

        // Instant publish flag — set after write to force immediate Modbus telemetry
        void requestInstantPublish() { _instantPublishPending = true; }
        bool isInstantPublishPending() const { return _instantPublishPending; }
        void clearInstantPublish() { _instantPublishPending = false; }

        // Update buffer with write value, respecting config dataType (raw register value)
        // Ensures getScaledValue() returns correct result after a write command
        void updateFromWriteValue(const String &mqttKey, double rawValue);

        // Write-hold: after a successful write, block poll read-back from overwriting
        // the buffer for the specified duration. Prevents UI "bounce-back" on slow devices.
        void setWriteHold(const String &mqttKey, uint32_t durationMs);
        bool isWriteHeld(const String &mqttKey) const;

        // Write-hold default duration (ms)
        static constexpr uint32_t WRITE_HOLD_MS = 3000;

        // Max age of last-known-good value when register is in error state.
        // After this duration without a successful read, the stale value is
        // no longer published — prevents app from showing permanently-stale data.
        static constexpr uint32_t STALE_VALUE_MAX_AGE_MS = 60000; // 60 seconds

        // Number of times to send null in periodic telemetry for a stale-expired register
        // before stopping. Ensures app receives at least N null signals.
        static constexpr uint8_t NULL_PERIODIC_SEND_COUNT = 2;

        // ⭐ Callback to check if connection is enabled (for filtering disabled connections)
        using ConnectionEnabledCallback = std::function<bool(const String &connectionId)>;
        void setConnectionEnabledCallback(ConnectionEnabledCallback callback) { _connectionEnabledCallback = callback; }

        // Clear all
        void clear();

    private:
        ModbusDataBuffer() = default;
        ~ModbusDataBuffer();

        // Prevent copy/move
        ModbusDataBuffer(const ModbusDataBuffer &) = delete;
        ModbusDataBuffer &operator=(const ModbusDataBuffer &) = delete;

        // Storage
        std::map<String, RegisterValue> _registerValues;
        std::map<String, DataMonitorConfig> _registerConfigs;

        // Instant publish flag
        bool _instantPublishPending = false;

        // Write-hold: mqttKey -> millis() expiry time
        std::map<String, uint32_t> _writeHoldUntil;

        // Track how many times null has been sent in periodic telemetry per register
        std::map<String, uint8_t> _nullSentCount;

        // ⭐ Callback to check connection enabled status
        ConnectionEnabledCallback _connectionEnabledCallback;
    };

} // namespace ZenoPCB

#endif // ZENOPCB_MODBUS_DATA_BUFFER_H
