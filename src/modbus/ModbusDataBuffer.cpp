// Plan 06-03 D-03 — Modbus subsystem is ESP32-only.
#if defined(ESP32)

#include "ModbusDataBuffer.h"
#include "../core/ZenoPCBDebug.h"
#include "../ZenoJson.h"  // ArduinoJson API from vendored copy (namespace ZenoJson; see vendor/ArduinoJson/LICENSE.md)

static const char *TAG = "ModbusDataBuffer";

namespace ZenoPCB
{
    // ⭐ Helper: Convert Modbus error code to human-readable text
    static const char *getModbusErrorText(uint8_t errorCode)
    {
        switch (errorCode)
        {
        case 0x00:
            return "SUCCESS";
        case 0x01:
            return "ILLEGAL_FUNCTION";
        case 0x02:
            return "ILLEGAL_ADDRESS";
        case 0x03:
            return "ILLEGAL_VALUE";
        case 0x04:
            return "SLAVE_FAILURE";
        case 0x05:
            return "ACKNOWLEDGE";
        case 0x06:
            return "SLAVE_BUSY";
        case 0x08:
            return "MEMORY_PARITY_ERROR";
        case 0x0A:
            return "PATH_UNAVAILABLE";
        case 0x0B:
            return "DEVICE_NO_RESPONSE";
        case 0xE1:
            return "GENERAL_FAILURE";
        case 0xE2:
            return "DATA_MISMATCH";
        case 0xE3:
            return "UNEXPECTED_RESPONSE";
        case 0xE4:
            return "TIMEOUT";
        case 0xE5:
            return "CONNECTION_LOST";
        case 0xE6:
            return "CANCELED";
        default:
            return "UNKNOWN_ERROR";
        }
    }

    ModbusDataBuffer &ModbusDataBuffer::getInstance()
    {
        static ModbusDataBuffer instance;
        return instance;
    }

    ModbusDataBuffer::~ModbusDataBuffer()
    {
        clear();
    }

    bool ModbusDataBuffer::begin()
    {
        ZENO_LOG_CORE("ModbusDataBuffer initialized");
        return true;
    }

    bool ModbusDataBuffer::addRegister(const DataMonitorConfig &config)
    {
        String key(config.mqttKey);

        if (_registerConfigs.find(key) != _registerConfigs.end())
        {
            ZENO_LOG_CORE("⚠️  Register already exists: %s", key.c_str());
            return false;
        }

        // Store config
        _registerConfigs[key] = config;

        // Initialize value
        RegisterValue value;
        value.status = RegisterValue::UNKNOWN;
        _registerValues[key] = value;

        ZENO_LOG_CORE("✅ Added register: %s (addr=%d, enabled=%d)", key.c_str(), config.address, config.enabled);
        return true;
    }

    // ⭐ Update existing register config (e.g., enabled flag changed)
    bool ModbusDataBuffer::updateRegisterConfig(const DataMonitorConfig &config)
    {
        String key(config.mqttKey);

        auto it = _registerConfigs.find(key);
        if (it == _registerConfigs.end())
        {
            // ⭐ Register not found - add it instead
            ZENO_LOG_CORE("⚠️  Register not found for update, adding: %s", key.c_str());
            return addRegister(config);
        }

        // Update config (keep existing value)
        _registerConfigs[key] = config;

        ZENO_LOG_CORE("✅ Updated register config: %s (enabled=%d)", key.c_str(), config.enabled);
        return true;
    }

    bool ModbusDataBuffer::removeRegister(const String &mqttKey)
    {
        auto it = _registerConfigs.find(mqttKey);
        if (it != _registerConfigs.end())
        {
            _registerConfigs.erase(it);
            _registerValues.erase(mqttKey);
            ZENO_LOG_CORE("Removed register: %s", mqttKey.c_str());
            return true;
        }
        return false;
    }

    bool ModbusDataBuffer::hasRegister(const String &mqttKey) const
    {
        return _registerConfigs.find(mqttKey) != _registerConfigs.end();
    }

    std::vector<String> ModbusDataBuffer::listRegisters() const
    {
        std::vector<String> list;
        for (const auto &[key, config] : _registerConfigs)
        {
            list.push_back(key);
        }
        return list;
    }

    void ModbusDataBuffer::updateRegisterValue(const String &mqttKey, uint16_t u16Value, RegisterValue::ValueStatus status)
    {
        auto it = _registerValues.find(mqttKey);
        if (it != _registerValues.end())
        {
            if (status == RegisterValue::VALID && isWriteHeld(mqttKey))
                return; // Write-hold active — preserve injected write value
            it->second.u16Value = u16Value;
            it->second.status = status;
            it->second.lastUpdateTime = millis();
            if (status != RegisterValue::VALID)
            {
                it->second.lastErrorTime = millis();
            }
            else
            {
                it->second.retryCount = 0;
                it->second.connectionErrorCode = 0;
                _nullSentCount[mqttKey] = 0; // Reset null counter on recovery
            }
        }
    }

    void ModbusDataBuffer::updateRegisterValue(const String &mqttKey, int16_t i16Value, RegisterValue::ValueStatus status)
    {
        auto it = _registerValues.find(mqttKey);
        if (it != _registerValues.end())
        {
            if (status == RegisterValue::VALID && isWriteHeld(mqttKey))
                return;
            it->second.i16Value = i16Value;
            it->second.status = status;
            it->second.lastUpdateTime = millis();
            if (status != RegisterValue::VALID)
            {
                it->second.lastErrorTime = millis();
            }
            else
            {
                it->second.retryCount = 0;
                it->second.connectionErrorCode = 0;
                _nullSentCount[mqttKey] = 0; // Reset null counter on recovery
            }
        }
    }

    void ModbusDataBuffer::updateRegisterValue(const String &mqttKey, uint32_t u32Value, RegisterValue::ValueStatus status)
    {
        auto it = _registerValues.find(mqttKey);
        if (it != _registerValues.end())
        {
            if (status == RegisterValue::VALID && isWriteHeld(mqttKey))
                return;
            it->second.u32Value = u32Value;
            it->second.status = status;
            it->second.lastUpdateTime = millis();
            if (status != RegisterValue::VALID)
            {
                it->second.lastErrorTime = millis();
            }
            else
            {
                it->second.retryCount = 0;
                it->second.connectionErrorCode = 0;
                _nullSentCount[mqttKey] = 0; // Reset null counter on recovery
            }
        }
    }

    void ModbusDataBuffer::updateRegisterValue(const String &mqttKey, int32_t i32Value, RegisterValue::ValueStatus status)
    {
        auto it = _registerValues.find(mqttKey);
        if (it != _registerValues.end())
        {
            if (status == RegisterValue::VALID && isWriteHeld(mqttKey))
                return;
            it->second.i32Value = i32Value;
            it->second.status = status;
            it->second.lastUpdateTime = millis();
            if (status != RegisterValue::VALID)
            {
                it->second.lastErrorTime = millis();
            }
            else
            {
                it->second.retryCount = 0;
                it->second.connectionErrorCode = 0;
                _nullSentCount[mqttKey] = 0; // Reset null counter on recovery
            }
        }
    }

    void ModbusDataBuffer::updateRegisterValue(const String &mqttKey, float floatValue, RegisterValue::ValueStatus status)
    {
        auto it = _registerValues.find(mqttKey);
        if (it != _registerValues.end())
        {
            if (status == RegisterValue::VALID && isWriteHeld(mqttKey))
                return;
            it->second.floatValue = floatValue;
            it->second.status = status;
            it->second.lastUpdateTime = millis();
            if (status != RegisterValue::VALID)
            {
                it->second.lastErrorTime = millis();
            }
            else
            {
                it->second.retryCount = 0;
                it->second.connectionErrorCode = 0;
                _nullSentCount[mqttKey] = 0; // Reset null counter on recovery
            }
        }
    }

    void ModbusDataBuffer::updateRegisterValue(const String &mqttKey, bool boolValue, RegisterValue::ValueStatus status)
    {
        auto it = _registerValues.find(mqttKey);
        if (it != _registerValues.end())
        {
            if (status == RegisterValue::VALID && isWriteHeld(mqttKey))
                return;
            it->second.boolValue = boolValue;
            it->second.status = status;
            it->second.lastUpdateTime = millis();
            if (status != RegisterValue::VALID)
            {
                it->second.lastErrorTime = millis();
            }
            else
            {
                it->second.retryCount = 0;
                it->second.connectionErrorCode = 0;
                _nullSentCount[mqttKey] = 0; // Reset null counter on recovery
            }
        }
    }

    // ============================================
    // 64-bit value update overloads (v2.2)
    // ============================================

    void ModbusDataBuffer::updateRegisterValue(const String &mqttKey, uint64_t u64Value, RegisterValue::ValueStatus status)
    {
        auto it = _registerValues.find(mqttKey);
        if (it != _registerValues.end())
        {
            if (status == RegisterValue::VALID && isWriteHeld(mqttKey))
                return;
            it->second.u64Value = u64Value;
            it->second.status = status;
            it->second.lastUpdateTime = millis();
            if (status != RegisterValue::VALID)
            {
                it->second.lastErrorTime = millis();
            }
            else
            {
                it->second.retryCount = 0;
                it->second.connectionErrorCode = 0;
                _nullSentCount[mqttKey] = 0; // Reset null counter on recovery
            }
        }
    }

    void ModbusDataBuffer::updateRegisterValue(const String &mqttKey, int64_t i64Value, RegisterValue::ValueStatus status)
    {
        auto it = _registerValues.find(mqttKey);
        if (it != _registerValues.end())
        {
            if (status == RegisterValue::VALID && isWriteHeld(mqttKey))
                return;
            it->second.i64Value = i64Value;
            it->second.status = status;
            it->second.lastUpdateTime = millis();
            if (status != RegisterValue::VALID)
            {
                it->second.lastErrorTime = millis();
            }
            else
            {
                it->second.retryCount = 0;
                it->second.connectionErrorCode = 0;
                _nullSentCount[mqttKey] = 0; // Reset null counter on recovery
            }
        }
    }

    void ModbusDataBuffer::updateRegisterValue(const String &mqttKey, double doubleValue, RegisterValue::ValueStatus status)
    {
        auto it = _registerValues.find(mqttKey);
        if (it != _registerValues.end())
        {
            if (status == RegisterValue::VALID && isWriteHeld(mqttKey))
                return;
            it->second.doubleValue = doubleValue;
            it->second.status = status;
            it->second.lastUpdateTime = millis();
            if (status != RegisterValue::VALID)
            {
                it->second.lastErrorTime = millis();
            }
            else
            {
                it->second.retryCount = 0;
                it->second.connectionErrorCode = 0;
                _nullSentCount[mqttKey] = 0; // Reset null counter on recovery
            }
        }
    }

    void ModbusDataBuffer::setError(const String &mqttKey, const String &error, uint8_t connectionErrorCode)
    {
        auto it = _registerValues.find(mqttKey);
        if (it != _registerValues.end())
        {
            it->second.status = RegisterValue::ERROR;
            it->second.lastError = error;
            it->second.connectionErrorCode = connectionErrorCode; // Store Modbus error code (0xE4, etc.)
            it->second.lastErrorTime = millis();
            it->second.retryCount++;
        }
    }

    RegisterValue ModbusDataBuffer::getValue(const String &mqttKey) const
    {
        auto it = _registerValues.find(mqttKey);
        if (it != _registerValues.end())
        {
            return it->second;
        }
        return RegisterValue();
    }

    // ============================================
    // Write-hold: prevent poll read-back from overwriting after a write
    // ============================================

    void ModbusDataBuffer::setWriteHold(const String &mqttKey, uint32_t durationMs)
    {
        _writeHoldUntil[mqttKey] = millis() + durationMs;
    }

    bool ModbusDataBuffer::isWriteHeld(const String &mqttKey) const
    {
        auto it = _writeHoldUntil.find(mqttKey);
        if (it == _writeHoldUntil.end())
            return false;
        return millis() < it->second;
    }

    // Update buffer with correct field based on config's dataType
    // rawValue = the value written to Modbus register (from cloud control command)
    void ModbusDataBuffer::updateFromWriteValue(const String &mqttKey, double rawValue)
    {
        auto itConfig = _registerConfigs.find(mqttKey);
        if (itConfig == _registerConfigs.end())
            return;

        switch (itConfig->second.dataType)
        {
        case DataType::UNSIGNED_INT16:
            updateRegisterValue(mqttKey, static_cast<uint16_t>(lround(rawValue)));
            break;
        case DataType::SIGNED_INT16:
            updateRegisterValue(mqttKey, static_cast<int16_t>(lround(rawValue)));
            break;
        case DataType::UNSIGNED_INT32:
            updateRegisterValue(mqttKey, static_cast<uint32_t>(lround(rawValue)));
            break;
        case DataType::SIGNED_INT32:
            updateRegisterValue(mqttKey, static_cast<int32_t>(lround(rawValue)));
            break;
        case DataType::FLOAT32:
            updateRegisterValue(mqttKey, static_cast<float>(rawValue));
            break;
        case DataType::FLOAT64:
            updateRegisterValue(mqttKey, rawValue);
            break;
        case DataType::BOOLEAN:
            updateRegisterValue(mqttKey, rawValue != 0.0);
            break;
        default:
            updateRegisterValue(mqttKey, static_cast<uint16_t>(lround(rawValue)));
            break;
        }
    }

    double ModbusDataBuffer::getScaledValue(const String &mqttKey) const
    {
        auto itConfig = _registerConfigs.find(mqttKey);
        auto itValue = _registerValues.find(mqttKey);

        if (itConfig != _registerConfigs.end() && itValue != _registerValues.end())
        {
            return itValue->second.getScaledValue(itConfig->second);
        }
        return 0.0;
    }

    bool ModbusDataBuffer::isValid(const String &mqttKey) const
    {
        auto it = _registerValues.find(mqttKey);
        if (it != _registerValues.end())
        {
            return it->second.status == RegisterValue::VALID;
        }
        return false;
    }

    size_t ModbusDataBuffer::getValidCount() const
    {
        size_t count = 0;
        for (const auto &[key, value] : _registerValues)
        {
            if (value.status == RegisterValue::VALID)
            {
                count++;
            }
        }
        return count;
    }

    size_t ModbusDataBuffer::getErrorCount() const
    {
        size_t count = 0;
        for (const auto &[key, value] : _registerValues)
        {
            if (value.status != RegisterValue::VALID)
            {
                count++;
            }
        }
        return count;
    }

    const DataMonitorConfig *ModbusDataBuffer::getRegisterConfig(const String &mqttKey) const
    {
        auto it = _registerConfigs.find(mqttKey);
        if (it != _registerConfigs.end())
        {
            return &it->second;
        }
        return nullptr;
    }

    String ModbusDataBuffer::buildRegisterJson(const String &mqttKey)
    {
        DynamicJsonDocument doc(256);

        auto itConfig = _registerConfigs.find(mqttKey);
        auto itValue = _registerValues.find(mqttKey);

        if (itConfig == _registerConfigs.end() || itValue == _registerValues.end())
        {
            return "";
        }

        const DataMonitorConfig &config = itConfig->second;
        const RegisterValue &value = itValue->second;

        // Build JSON for single register
        doc["id"] = mqttKey;
        doc["name"] = config.name;
        doc["addr"] = config.address;
        doc["v"] = value.getScaledValue(config); // Scaled value
        doc["u"] = config.unit;
        doc["s"] = (int)value.status;
        doc["t"] = value.lastUpdateTime;

        String result;
        serializeJson(doc, result);
        return result;
    }

    bool ModbusDataBuffer::hasPublishableValues() const
    {
        uint32_t now = millis();
        for (const auto &[mqttKey, value] : _registerValues)
        {
            if (value.status == RegisterValue::VALID)
                return true;
            // last-known-good within stale window counts as publishable
            if (value.lastUpdateTime > 0 && (now - value.lastUpdateTime) < STALE_VALUE_MAX_AGE_MS)
                return true;
            // Still need to send null signals for this register
            auto it = _nullSentCount.find(mqttKey);
            if (it == _nullSentCount.end() || it->second < NULL_PERIODIC_SEND_COUNT)
                return true;
        }
        return false;
    }

    String ModbusDataBuffer::buildTelemetryJson(size_t maxSize, bool includeNulls)
    {
        DynamicJsonDocument doc(maxSize);
        uint8_t connectionError = 0;     // Connection-level error code (0xE4, etc.)
        bool hasConnectionError = false; // Track if we have a connection error
        int errorCount = 0;              // Count failed registers
        int skippedCount = 0;            // Count skipped (disabled) registers

        ZENO_LOG_MODBUS("📤 Building telemetry JSON (%d registers)", _registerConfigs.size());

        for (const auto &[mqttKey, config] : _registerConfigs)
        {
            // ⭐ Skip disabled registers (not include in JSON at all)
            if (!config.enabled)
            {
                ZENO_LOG_CORE("  ⏸️ [%s] Skipped - register disabled", mqttKey.c_str());
                skippedCount++;
                continue;
            }

            // ⭐ Skip if connection is disabled (check via callback)
            if (config.connectionId[0] != '\0' && _connectionEnabledCallback)
            {
                String connId(config.connectionId);
                if (!_connectionEnabledCallback(connId))
                {
                    ZENO_LOG_CORE("  ⏸️ [%s] Skipped - connection '%s' disabled", mqttKey.c_str(), connId.c_str());
                    skippedCount++;
                    continue;
                }
            }

            auto it = _registerValues.find(mqttKey);
            if (it == _registerValues.end())
            {
                ZENO_LOG_CORE("  ⚠️ [%s] No value in buffer", mqttKey.c_str());
                continue;
            }

            const RegisterValue &value = it->second;
            double scaledValue = value.getScaledValue(config);

            ZENO_LOG_CORE("  📊 [%s] status=%d scaled=%.4f errCode=0x%02X",
                          mqttKey.c_str(), (int)value.status, scaledValue, value.connectionErrorCode);

            // ⭐ Publish if VALID, or if ERROR/TIMEOUT with a fresh enough last-known-good value.
            // UNKNOWN (never polled) and values stale > STALE_VALUE_MAX_AGE_MS are skipped.
            bool hasLastKnown = (value.status != RegisterValue::UNKNOWN && value.lastUpdateTime > 0);
            bool isFreshEnough = hasLastKnown &&
                                 (millis() - value.lastUpdateTime < STALE_VALUE_MAX_AGE_MS);
            bool publishValue = (value.status == RegisterValue::VALID) || isFreshEnough;

            if (publishValue)
            {
                // v2.2: Handle 64-bit integer precision
                // JSON can only safely represent integers up to 2^53 (9,007,199,254,740,992)
                // For i64/u64 values beyond this range, serialize as string to preserve precision
                if (config.dataType == DataType::SIGNED_INT64)
                {
                    int64_t rawValue = value.i64Value;
                    // Apply scale/offset if needed (result is double, may lose precision for huge values)
                    if (config.scaleEnabled || config.offsetValue != 0.0f)
                    {
                        // Use scaled double value
                        doc[mqttKey] = scaledValue;
                    }
                    else if (rawValue > 9007199254740992LL || rawValue < -9007199254740992LL)
                    {
                        // Large integer: serialize as string to preserve precision
                        doc[mqttKey] = String(rawValue);
                    }
                    else
                    {
                        // Safe to use as number
                        doc[mqttKey] = rawValue;
                    }
                }
                else if (config.dataType == DataType::UNSIGNED_INT64)
                {
                    uint64_t rawValue = value.u64Value;
                    if (config.scaleEnabled || config.offsetValue != 0.0f)
                    {
                        doc[mqttKey] = scaledValue;
                    }
                    else if (rawValue > 9007199254740992ULL)
                    {
                        doc[mqttKey] = String(rawValue);
                    }
                    else
                    {
                        doc[mqttKey] = rawValue;
                    }
                }
                else if (config.dataType == DataType::FLOAT64)
                {
                    // Double - ArduinoJson handles this correctly
                    doc[mqttKey] = scaledValue;
                }
                else if (config.dataType == DataType::BOOLEAN)
                {
                    // Boolean: output as true/false
                    doc[mqttKey] = value.boolValue;
                }
                else
                {
                    // 16-bit and 32-bit: use double for scaled value (no precision issues)
                    doc[mqttKey] = scaledValue;
                }
            }
            else
            {
                errorCount++;
                if (includeNulls)
                {
                    // get_all: always include failed registers as null (whether never-polled or stale-expired)
                    doc[mqttKey] = nullptr;
                    ZENO_LOG_CORE("  ❌ [%s] Error (status=%d, lastOK=%lums ago) - published null",
                                  mqttKey.c_str(), (int)value.status,
                                  value.lastUpdateTime > 0 ? (unsigned long)(millis() - value.lastUpdateTime) : 0UL);
                }
                else if (_nullSentCount[mqttKey] < NULL_PERIODIC_SEND_COUNT)
                {
                    // Periodic: send null N times so app receives error signal
                    doc[mqttKey] = nullptr;
                    _nullSentCount[mqttKey]++;
                    ZENO_LOG_CORE("  ❌ [%s] Error (status=%d) - periodic null %d/%d",
                                  mqttKey.c_str(), (int)value.status,
                                  (int)_nullSentCount[mqttKey], (int)NULL_PERIODIC_SEND_COUNT);
                }
                else
                {
                    // Periodic: already sent N nulls — omit entirely
                    ZENO_LOG_CORE("  ⏭️ [%s] Error (status=%d) - omitted (null sent %d times)",
                                  mqttKey.c_str(), (int)value.status, (int)_nullSentCount[mqttKey]);
                }
            }

            // Log stale-value warning separately (for ERROR/TIMEOUT with last-known-good data)
            if (publishValue && value.status != RegisterValue::VALID)
            {
                // Track connection error (use first non-zero error code found)
                if (value.connectionErrorCode != 0 && !hasConnectionError)
                {
                    connectionError = value.connectionErrorCode;
                    hasConnectionError = true;
                }
                ZENO_LOG_CORE("  ⚠️ [%s] Stale value (status=%d errCode=0x%02X) — using last-known %.4f",
                              mqttKey.c_str(), (int)value.status, value.connectionErrorCode, scaledValue);
            }
        }

        // ⭐ DISABLED: Error field không cần thiết cho backend
        // Nếu cần bật lại, uncomment đoạn dưới:
        // if (hasConnectionError)
        // {
        //     // Format: "TIMEOUT", "CONNECTION_LOST", etc. instead of "0xE4"
        //     doc["error"] = getModbusErrorText(connectionError);
        // }
        // else
        // {
        //     doc["error"] = "";
        // }

        String result;
        serializeJson(doc, result);
        ZENO_LOG_MODBUS("📤 JSON: %s (errors=%d, skipped=%d)", result.c_str(), errorCount, skippedCount);
        return result;
    }

    void ModbusDataBuffer::clear()
    {
        _registerConfigs.clear();
        _registerValues.clear();
        ZENO_LOG_CORE("ModbusDataBuffer cleared");
    }

} // namespace ZenoPCB

#endif  // Plan 06-03 D-03 — defined(ESP32)
