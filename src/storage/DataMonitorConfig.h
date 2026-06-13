/**
 * @file DataMonitorConfig.h
 * @brief Data structures for PLC register (data-monitor) configuration management
 *
 * Manages PLC register configs received from MQTT and stored in LittleFS.
 * Supports Modbus register types: HOLDING, INPUT, COIL, DISCRETE
 *
 * MQTT Topic: v1/devices/{TOKEN}/data-monitors
 * Message Type: "dm" (data-monitor)
 * Actions: c (create), u (update), d (delete), e (enable/toggle)
 *
 * @author ZenoPCB Development Team
 * @version 1.0.0
 */

#ifndef DATA_MONITOR_CONFIG_H
#define DATA_MONITOR_CONFIG_H

#include <Arduino.h>
#include "../vendor/ArduinoJson/ArduinoJson.h"  // ArduinoJson API from vendored copy (namespace ZenoJson; see vendor/ArduinoJson/LICENSE.md)
#include <vector>

namespace ZenoPCB
{
    // ============================================
    // Constants
    // ============================================
    constexpr size_t MQTT_KEY_LENGTH = 6; // 6-digit numeric (000000-999999)
    constexpr size_t DM_MAX_NAME_LENGTH = 64;
    constexpr size_t DM_MAX_UNIT_LENGTH = 16;

    // Override via build flag: -DMAX_DATA_MONITORS=100
#ifndef MAX_DATA_MONITORS
#define MAX_DATA_MONITORS 200
#endif

    constexpr size_t DM_CONFIG_JSON_SIZE = 512;
    constexpr size_t DM_META_JSON_SIZE = 4096;

    // File paths
    constexpr const char *DATA_MONITORS_DIR = "/data-monitors";
    constexpr const char *DM_META_FILE = "/data-monitors/meta.json";

    // ============================================
    // Enums
    // ============================================

    /**
     * @brief MQTT message action types for data-monitor
     * Note: Includes TOGGLE action for enable/disable
     */
    enum class DataMonitorAction : char
    {
        CREATE = 'c',
        UPDATE = 'u',
        DELETE = 'd',
        TOGGLE = 'e', // Enable/disable toggle
        UNKNOWN = '?'
    };

    /**
     * @brief Modbus register types
     * Note: Using REG_ prefix to avoid conflict with Arduino INPUT macro
     */
    enum class RegisterType : uint8_t
    {
        REG_HOLDING = 0,  // Read/Write registers (FC 03, 06, 16)
        REG_INPUT = 1,    // Read-only registers (FC 04)
        REG_COIL = 2,     // Read/Write bits (FC 01, 05, 15)
        REG_DISCRETE = 3, // Read-only bits (FC 02)
        REG_UNKNOWN = 255
    };

    /**
     * @brief Data types for register values
     *
     * Compact aliases from MQTT:
     * - 16-bit: i16, u16 (1 register, no byte order needed)
     * - 32-bit: i32, u32, f32 (2 registers, byte order applies)
     * - 64-bit: i64, u64, f64 (4 registers, byte order applies)
     * - Boolean: bool
     */
    enum class DataType : uint8_t
    {
        // 16-bit (1 register, no byte order needed)
        SIGNED_INT16 = 0,   // i16 - int16_t
        UNSIGNED_INT16 = 1, // u16 - uint16_t

        // 32-bit (2 registers, byte order applies)
        SIGNED_INT32 = 2,   // i32 - int32_t
        UNSIGNED_INT32 = 3, // u32 - uint32_t
        FLOAT32 = 4,        // f32 - float IEEE754

        // 64-bit (4 registers, byte order applies) - NEW v2.2
        SIGNED_INT64 = 5,   // i64 - int64_t
        UNSIGNED_INT64 = 6, // u64 - uint64_t
        FLOAT64 = 7,        // f64 - double IEEE754

        // Boolean (1 bit for coils/discrete)
        BOOLEAN = 8, // bool

        UNKNOWN = 255
    };

    /**
     * @brief Byte order for multi-register values (32-bit and 64-bit)
     *
     * Modbus uses 16-bit registers. For 32/64-bit values, multiple registers
     * are combined. Different PLCs use different byte/word ordering.
     *
     * For a 32-bit value 0x12345678 stored in 2 registers [regA][regB]:
     * - BYTE_ORDER_BE (ABCD):      regA=0x1234, regB=0x5678 (Modbus standard)
     * - BYTE_ORDER_BE_SWAP (BADC): regA=0x5678, regB=0x1234 (word-swapped)
     * - BYTE_ORDER_LE (DCBA):      regA=0x3412, regB=0x7856 (byte-reversed)
     * - BYTE_ORDER_LE_SWAP (CDAB): regA=0x7856, regB=0x3412 (both swapped)
     *
     * MQTT field: "order" with values "be", "be_swap", "le", "le_swap"
     */
    enum class ByteOrder : uint8_t
    {
        BYTE_ORDER_BE = 0,      // be - ABCD (Modbus standard, default)
        BYTE_ORDER_BE_SWAP = 1, // be_swap - BADC (word-swapped)
        BYTE_ORDER_LE = 2,      // le - DCBA (byte-reversed in each word)
        BYTE_ORDER_LE_SWAP = 3, // le_swap - CDAB (byte + word swap)
        BYTE_ORDER_AUTO = 255   // Auto-detect (defaults to BYTE_ORDER_BE)
    };

    /**
     * @brief Scale operation types (v2.1)
     */
    enum class ScaleOperation : uint8_t
    {
        MULTIPLY = 0, // raw * scaleValue
        DIVIDE = 1,   // raw / scaleValue
        NONE = 255
    };

    /**
     * @brief Offset operation types (v2.1)
     */
    enum class OffsetOperation : uint8_t
    {
        ADD = 0,      // value + offsetValue
        SUBTRACT = 1, // value - offsetValue
        NONE = 255
    };

    /**
     * @brief Data-monitor operation state
     */
    enum class DataMonitorState : uint8_t
    {
        IDLE,
        PARSING,
        VALIDATING,
        WRITING,
        DELETING,
        TOGGLING,
        UPDATING_META,
        ERROR,
        RECOVERING
    };

    // ============================================
    // Data Structures
    // ============================================

    /**
     * @brief Single PLC register (data-monitor) configuration
     *
     * Field mapping (compact JSON keys) - v2.2:
     * - id  = mqttKey (6-digit numeric)
     * - nm  = name
     * - rt  = registerType (H/I/C/D)
     * - ad  = address (0-65535)
     * - sid = slaveId (1-247)
     * - dt  = dataType (i16/u16/i32/u32/f32/i64/u64/f64/bool)
     * - order = byteOrder (be/be_swap/le/le_swap) - NEW v2.2
     * - en  = enabled (0/1)
     * - dp  = decimalPoint (optional, int)
     * - sc_en = scaleEnabled (0/1)
     * - sc_op = scaleOperation (multiply|divide)
     * - sc_val = scaleValue (float)
     * - off_op = offsetOperation (add|subtract)
     * - off_val = offsetValue (float)
     * - u   = unit (optional, string)
     * - cn  = connectionId (optional, reference to connection config)
     */
    struct DataMonitorConfig
    {
        char mqttKey[MQTT_KEY_LENGTH + 1]; // 6-digit numeric ID
        char name[DM_MAX_NAME_LENGTH + 1];
        RegisterType registerType;
        uint16_t address;
        uint8_t slaveId;
        DataType dataType;
        ByteOrder byteOrder; // NEW v2.2 - Byte order for 32/64-bit values
        bool enabled;

        // === Scaling system (v2.1) ===
        uint8_t decimalPoint;            // dp - Decimal point for display (0-6)
        bool scaleEnabled;               // sc_en - Enable scale transformation
        ScaleOperation scaleOperation;   // sc_op - multiply or divide
        float scaleValue;                // sc_val - Scale factor
        OffsetOperation offsetOperation; // off_op - add or subtract
        float offsetValue;               // off_val - Offset value

        // Display unit (optional)
        char unit[DM_MAX_UNIT_LENGTH + 1];

        // Reference to connection config (optional)
        char connectionId[5]; // shortId of ConnectionConfig (4 chars + null)

        // Timestamps
        uint32_t createdAt;
        uint32_t updatedAt;

        DataMonitorConfig()
            : registerType(RegisterType::REG_HOLDING),
              address(0),
              slaveId(1),
              dataType(DataType::SIGNED_INT16),
              byteOrder(ByteOrder::BYTE_ORDER_BE), // v2.2 default
              enabled(true),
              // v2.1 defaults
              decimalPoint(2),
              scaleEnabled(false),
              scaleOperation(ScaleOperation::MULTIPLY),
              scaleValue(1.0f),
              offsetOperation(OffsetOperation::ADD),
              offsetValue(0.0f),
              // Timestamps
              createdAt(0),
              updatedAt(0)
        {
            mqttKey[0] = '\0';
            name[0] = '\0';
            unit[0] = '\0';
            connectionId[0] = '\0';
        }

        bool isValid() const
        {
            return mqttKey[0] != '\0' && strlen(mqttKey) == MQTT_KEY_LENGTH;
        }

        bool hasUnit() const { return unit[0] != '\0'; }
        bool hasConnectionId() const { return connectionId[0] != '\0'; }

        /**
         * @brief Check if scale transformation is enabled (v2.1)
         */
        bool hasScaling() const
        {
            return scaleEnabled && scaleValue != 1.0f;
        }

        /**
         * @brief Check if offset transformation is configured (v2.1)
         */
        bool hasOffset() const
        {
            return scaleEnabled && offsetValue != 0.0f;
        }

        /**
         * @brief Check if byte order matters for this data type (v2.2)
         *
         * Only 32-bit and 64-bit types require byte order configuration.
         * 16-bit types don't need byte order (only 1 register).
         */
        bool requiresByteOrder() const
        {
            switch (dataType)
            {
            case DataType::SIGNED_INT32:
            case DataType::UNSIGNED_INT32:
            case DataType::FLOAT32:
            case DataType::SIGNED_INT64:
            case DataType::UNSIGNED_INT64:
            case DataType::FLOAT64:
                return true;
            default:
                return false;
            }
        }

        /**
         * @brief Get number of Modbus registers needed for this data type (v2.2)
         *
         * - 16-bit types: 1 register
         * - 32-bit types: 2 registers
         * - 64-bit types: 4 registers
         * - Boolean: 1 register (or 1 bit for coils)
         */
        uint8_t getRegisterCount() const
        {
            switch (dataType)
            {
            case DataType::SIGNED_INT16:
            case DataType::UNSIGNED_INT16:
            case DataType::BOOLEAN:
                return 1;
            case DataType::SIGNED_INT32:
            case DataType::UNSIGNED_INT32:
            case DataType::FLOAT32:
                return 2;
            case DataType::SIGNED_INT64:
            case DataType::UNSIGNED_INT64:
            case DataType::FLOAT64:
                return 4;
            default:
                return 1;
            }
        }

        /**
         * @brief Serialize config to JSON object (compact format)
         * Supports both legacy (sc, off) and v2.1 (sc_en, sc_op, sc_val, off_op, off_val) formats
         */
        void toJson(JsonObject &obj) const
        {
            obj["id"] = mqttKey;
            obj["nm"] = name;

            // Register type: H/I/C/D
            switch (registerType)
            {
            case RegisterType::REG_HOLDING:
                obj["rt"] = "H";
                break;
            case RegisterType::REG_INPUT:
                obj["rt"] = "I";
                break;
            case RegisterType::REG_COIL:
                obj["rt"] = "C";
                break;
            case RegisterType::REG_DISCRETE:
                obj["rt"] = "D";
                break;
            default:
                obj["rt"] = "H";
                break;
            }

            obj["ad"] = address;
            obj["sid"] = slaveId;

            // Data type: i16/u16/i32/u32/f32/i64/u64/f64/bool (v2.2 format)
            switch (dataType)
            {
            case DataType::SIGNED_INT16:
                obj["dt"] = "i16";
                break;
            case DataType::UNSIGNED_INT16:
                obj["dt"] = "u16";
                break;
            case DataType::SIGNED_INT32:
                obj["dt"] = "i32";
                break;
            case DataType::UNSIGNED_INT32:
                obj["dt"] = "u32";
                break;
            case DataType::FLOAT32:
                obj["dt"] = "f32";
                break;
            case DataType::SIGNED_INT64:
                obj["dt"] = "i64";
                break;
            case DataType::UNSIGNED_INT64:
                obj["dt"] = "u64";
                break;
            case DataType::FLOAT64:
                obj["dt"] = "f64";
                break;
            case DataType::BOOLEAN:
                obj["dt"] = "bool";
                break;
            default:
                obj["dt"] = "i16";
                break;
            }

            // Byte order for 32/64-bit types (v2.2)
            if (requiresByteOrder())
            {
                switch (byteOrder)
                {
                case ByteOrder::BYTE_ORDER_BE:
                    obj["order"] = "be";
                    break;
                case ByteOrder::BYTE_ORDER_BE_SWAP:
                    obj["order"] = "be_swap";
                    break;
                case ByteOrder::BYTE_ORDER_LE:
                    obj["order"] = "le";
                    break;
                case ByteOrder::BYTE_ORDER_LE_SWAP:
                    obj["order"] = "le_swap";
                    break;
                default:
                    obj["order"] = "be";
                    break;
                }
            }

            obj["en"] = enabled ? 1 : 0;

            // === Decimal point (v2.1) ===
            if (decimalPoint > 0)
            {
                obj["dp"] = decimalPoint;
            }

            // === Scale/Offset system (v2.1) ===
            if (scaleEnabled)
            {
                obj["sc_en"] = 1;
                obj["sc_op"] = (scaleOperation == ScaleOperation::MULTIPLY) ? "multiply" : "divide";
                obj["sc_val"] = scaleValue;

                // Offset operation (always include if offsetValue != 0)
                if (offsetValue != 0.0f)
                {
                    obj["off_op"] = (offsetOperation == OffsetOperation::ADD) ? "add" : "subtract";
                    obj["off_val"] = offsetValue;
                }
            }

            if (hasUnit())
            {
                obj["u"] = unit;
            }
            if (hasConnectionId())
            {
                obj["cn"] = connectionId;
            }

            obj["ca"] = createdAt;
            obj["ua"] = updatedAt;
        }

        /**
         * @brief Deserialize config from JSON object
         * @param obj JSON object with config data
         * @return true if parsing successful
         */
        bool fromJson(const JsonObject &obj)
        {
            // Parse mqttKey (required)
            const char *id = obj["id"] | "";
            if (strlen(id) != MQTT_KEY_LENGTH)
            {
                return false;
            }
            strncpy(mqttKey, id, MQTT_KEY_LENGTH);
            mqttKey[MQTT_KEY_LENGTH] = '\0';

            // Parse name (required)
            const char *nm = obj["nm"] | "";
            strncpy(name, nm, DM_MAX_NAME_LENGTH);
            name[DM_MAX_NAME_LENGTH] = '\0';

            // Parse register type: Support both compact (H/I/C/D) and full format (HOLDING/INPUT/COIL/DISCRETE)
            const char *rt = obj["rt"] | "H";

            if (strcmp(rt, "H") == 0 || strcmp(rt, "h") == 0 ||
                strcmp(rt, "HOLDING") == 0 || strcmp(rt, "holding") == 0)
            {
                registerType = RegisterType::REG_HOLDING;
            }
            else if (strcmp(rt, "I") == 0 || strcmp(rt, "i") == 0 ||
                     strcmp(rt, "INPUT") == 0 || strcmp(rt, "input") == 0)
            {
                registerType = RegisterType::REG_INPUT;
            }
            else if (strcmp(rt, "C") == 0 || strcmp(rt, "c") == 0 ||
                     strcmp(rt, "COIL") == 0 || strcmp(rt, "coil") == 0)
            {
                registerType = RegisterType::REG_COIL;
            }
            else if (strcmp(rt, "D") == 0 || strcmp(rt, "d") == 0 ||
                     strcmp(rt, "DISCRETE") == 0 || strcmp(rt, "discrete") == 0)
            {
                registerType = RegisterType::REG_DISCRETE;
            }
            else
            {
                // Default to HOLDING if invalid
                registerType = RegisterType::REG_HOLDING;
            }

            // Parse address (required)
            address = obj["ad"] | 0;

            // Parse slaveId (default 1)
            slaveId = obj["sid"] | 1;

            // Parse data type: Support both compact (i16/u16/etc) and legacy (s16/signed_int16/etc) formats
            // v2.2: Added i64/u64/f64 for 64-bit types
            const char *dt = obj["dt"] | "i16";
            if (strcmp(dt, "i16") == 0 || strcmp(dt, "s16") == 0 || strcmp(dt, "signed_int16") == 0)
            {
                dataType = DataType::SIGNED_INT16;
            }
            else if (strcmp(dt, "u16") == 0 || strcmp(dt, "unsigned_int16") == 0)
            {
                dataType = DataType::UNSIGNED_INT16;
            }
            else if (strcmp(dt, "i32") == 0 || strcmp(dt, "s32") == 0 || strcmp(dt, "signed_int32") == 0)
            {
                dataType = DataType::SIGNED_INT32;
            }
            else if (strcmp(dt, "u32") == 0 || strcmp(dt, "unsigned_int32") == 0)
            {
                dataType = DataType::UNSIGNED_INT32;
            }
            else if (strcmp(dt, "f32") == 0 || strcmp(dt, "float32") == 0 || strcmp(dt, "float_ieee754") == 0)
            {
                dataType = DataType::FLOAT32;
            }
            else if (strcmp(dt, "i64") == 0 || strcmp(dt, "s64") == 0 || strcmp(dt, "signed_int64") == 0)
            {
                dataType = DataType::SIGNED_INT64;
            }
            else if (strcmp(dt, "u64") == 0 || strcmp(dt, "unsigned_int64") == 0)
            {
                dataType = DataType::UNSIGNED_INT64;
            }
            else if (strcmp(dt, "f64") == 0 || strcmp(dt, "float64") == 0 || strcmp(dt, "double") == 0)
            {
                dataType = DataType::FLOAT64;
            }
            else if (strcmp(dt, "bool") == 0 || strcmp(dt, "boolean") == 0 || strcmp(dt, "bool_coil") == 0)
            {
                dataType = DataType::BOOLEAN;
            }
            else
            {
                dataType = DataType::SIGNED_INT16;
            }

            // Parse byte order (v2.2) - only for 32/64-bit types
            // Values: be, be_swap, le, le_swap
            const char *order = obj["order"] | "be";
            if (strcmp(order, "be") == 0 || strcmp(order, "big_endian") == 0)
            {
                byteOrder = ByteOrder::BYTE_ORDER_BE;
            }
            else if (strcmp(order, "be_swap") == 0 || strcmp(order, "big_endian_swap") == 0)
            {
                byteOrder = ByteOrder::BYTE_ORDER_BE_SWAP;
            }
            else if (strcmp(order, "le") == 0 || strcmp(order, "little_endian") == 0)
            {
                byteOrder = ByteOrder::BYTE_ORDER_LE;
            }
            else if (strcmp(order, "le_swap") == 0 || strcmp(order, "little_endian_swap") == 0)
            {
                byteOrder = ByteOrder::BYTE_ORDER_LE_SWAP;
            }
            else
            {
                byteOrder = ByteOrder::BYTE_ORDER_BE; // Default
            }

            // Parse enabled flag
            int en = obj["en"] | 1;
            enabled = (en != 0);

            // === Parse v2.1 fields (scale/offset system) ===
            decimalPoint = obj["dp"] | 2;

            // Parse scale configuration
            scaleEnabled = (obj["sc_en"] | 0) != 0;

            const char *sc_op = obj["sc_op"] | "multiply";
            if (strcmp(sc_op, "divide") == 0)
            {
                scaleOperation = ScaleOperation::DIVIDE;
            }
            else
            {
                scaleOperation = ScaleOperation::MULTIPLY;
            }

            scaleValue = obj["sc_val"] | 1.0f;

            // Parse offset configuration
            const char *off_op = obj["off_op"] | "add";
            if (strcmp(off_op, "subtract") == 0)
            {
                offsetOperation = OffsetOperation::SUBTRACT;
            }
            else
            {
                offsetOperation = OffsetOperation::ADD;
            }

            offsetValue = obj["off_val"] | 0.0f;

            // Parse optional unit
            const char *u = obj["u"] | "";
            strncpy(unit, u, DM_MAX_UNIT_LENGTH);
            unit[DM_MAX_UNIT_LENGTH] = '\0';

            // Parse optional connectionId
            const char *cn = obj["cn"] | "";
            strncpy(connectionId, cn, 4);
            connectionId[4] = '\0';

            // Parse timestamps
            createdAt = obj["ca"] | 0;
            updatedAt = obj["ua"] | 0;

            return true;
        }
    };

    /**
     * @brief MQTT data-monitor message structure
     * NOTE: Lưu dataJson string thay vì JsonObject để tránh dangling reference
     */
    struct DataMonitorMessage
    {
        String type; // "dm" = data-monitor
        DataMonitorAction action;
        String dataJson; // Raw JSON string của data object

        static DataMonitorAction actionFromChar(char c)
        {
            switch (c)
            {
            case 'c':
            case 'C':
                return DataMonitorAction::CREATE;
            case 'u':
            case 'U':
                return DataMonitorAction::UPDATE;
            case 'd':
            case 'D':
                return DataMonitorAction::DELETE;
            case 'e':
            case 'E':
                return DataMonitorAction::TOGGLE;
            default:
                return DataMonitorAction::UNKNOWN;
            }
        }
    };

    /**
     * @brief Data-monitor metadata
     */
    struct DataMonitorMetadata
    {
        uint16_t totalMonitors;
        uint16_t enabledMonitors;
        uint32_t storageUsedBytes;
        uint32_t storageAvailableBytes;
        uint32_t lastUpdateTime;
        uint32_t lastSyncTime;
        uint16_t totalErrors;
        char lastError[64];
        uint32_t lastErrorTime;
        std::vector<String> mqttKeys;

        DataMonitorMetadata()
            : totalMonitors(0),
              enabledMonitors(0),
              storageUsedBytes(0),
              storageAvailableBytes(0),
              lastUpdateTime(0),
              lastSyncTime(0),
              totalErrors(0),
              lastErrorTime(0)
        {
            lastError[0] = '\0';
        }
    };

    // ============================================
    // Helper Functions
    // ============================================

    /**
     * @brief Get file path for a data-monitor config
     * @param mqttKey 6-digit numeric key
     * @return File path string (e.g., "/data-monitors/123456.json")
     */
    inline String getDataMonitorFilePath(const char *mqttKey)
    {
        return String(DATA_MONITORS_DIR) + "/" + String(mqttKey) + ".json";
    }

    /**
     * @brief Convert action enum to string
     */
    inline const char *dataMonitorActionToString(DataMonitorAction action)
    {
        switch (action)
        {
        case DataMonitorAction::CREATE:
            return "CREATE";
        case DataMonitorAction::UPDATE:
            return "UPDATE";
        case DataMonitorAction::DELETE:
            return "DELETE";
        case DataMonitorAction::TOGGLE:
            return "TOGGLE";
        default:
            return "UNKNOWN";
        }
    }

    /**
     * @brief Convert ScaleOperation enum to string
     */
    inline const char *scaleOperationToString(ScaleOperation op)
    {
        switch (op)
        {
        case ScaleOperation::MULTIPLY:
            return "multiply";
        case ScaleOperation::DIVIDE:
            return "divide";
        default:
            return "multiply";
        }
    }

    /**
     * @brief Convert OffsetOperation enum to string
     */
    inline const char *offsetOperationToString(OffsetOperation op)
    {
        switch (op)
        {
        case OffsetOperation::ADD:
            return "add";
        case OffsetOperation::SUBTRACT:
            return "subtract";
        default:
            return "add";
        }
    }

    /**
     * @brief Convert register type enum to string
     */
    inline const char *registerTypeToString(RegisterType type)
    {
        switch (type)
        {
        case RegisterType::REG_HOLDING:
            return "HOLDING";
        case RegisterType::REG_INPUT:
            return "INPUT";
        case RegisterType::REG_COIL:
            return "COIL";
        case RegisterType::REG_DISCRETE:
            return "DISCRETE";
        default:
            return "UNKNOWN";
        }
    }

    /**
     * @brief Convert data type enum to string
     */
    inline const char *dataTypeToString(DataType type)
    {
        switch (type)
        {
        case DataType::SIGNED_INT16:
            return "int16";
        case DataType::UNSIGNED_INT16:
            return "uint16";
        case DataType::SIGNED_INT32:
            return "int32";
        case DataType::UNSIGNED_INT32:
            return "uint32";
        case DataType::FLOAT32:
            return "float32";
        case DataType::BOOLEAN:
            return "boolean";
        default:
            return "unknown";
        }
    }

} // namespace ZenoPCB

#endif // DATA_MONITOR_CONFIG_H
