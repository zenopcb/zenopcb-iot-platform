#ifndef ZENOPCB_ZKEY_TYPES_H
#define ZENOPCB_ZKEY_TYPES_H

/**
 * @file ZKeyTypes.h
 * @brief Z Key system types (Z0-Z254) for user-defined telemetry & control
 *
 * Hệ thống Z Key cho phép user tự code gửi/nhận dữ liệu qua 255 key cố định.
 * Dùng song song với hệ thống Modbus gateway (mqttKey 6-digit).
 *
 * @example
 * zeno.set(ZKey::Z0, 25.5);        // Gửi float
 * zeno.set(ZKey::Z1, "RUNNING");   // Gửi string
 * zeno.set(ZKey::Z2, true);        // Gửi bool
 * float temp = zeno.getFloat(ZKey::Z0);  // Đọc giá trị
 *
 * @author ZenoPCB Development Team
 * @version 1.0.0
 */

#include <Arduino.h>
#include <functional>

namespace ZenoPCB
{
    // ============================================
    // Z Key Constants
    // ============================================

    constexpr uint8_t Z_KEY_MIN = 0;
    constexpr uint8_t Z_KEY_MAX = 254;
    constexpr uint16_t Z_KEY_COUNT = 255;
    constexpr uint32_t Z_KEY_DEFAULT_PUBLISH_INTERVAL = 5000; // 5 seconds
    constexpr uint32_t Z_KEY_MIN_PUBLISH_INTERVAL = 1000;     // 1 second minimum

    // ============================================
    // Z Key Enum (Type-safe, IDE autocomplete)
    // ============================================

    enum class ZKey : uint8_t
    {
        Z0 = 0,
        Z1 = 1,
        Z2 = 2,
        Z3 = 3,
        Z4 = 4,
        Z5 = 5,
        Z6 = 6,
        Z7 = 7,
        Z8 = 8,
        Z9 = 9,
        Z10 = 10,
        Z11 = 11,
        Z12 = 12,
        Z13 = 13,
        Z14 = 14,
        Z15 = 15,
        Z16 = 16,
        Z17 = 17,
        Z18 = 18,
        Z19 = 19,
        Z20 = 20,
        Z21 = 21,
        Z22 = 22,
        Z23 = 23,
        Z24 = 24,
        Z25 = 25,
        Z26 = 26,
        Z27 = 27,
        Z28 = 28,
        Z29 = 29,
        Z30 = 30,
        Z31 = 31,
        Z32 = 32,
        Z33 = 33,
        Z34 = 34,
        Z35 = 35,
        Z36 = 36,
        Z37 = 37,
        Z38 = 38,
        Z39 = 39,
        Z40 = 40,
        Z41 = 41,
        Z42 = 42,
        Z43 = 43,
        Z44 = 44,
        Z45 = 45,
        Z46 = 46,
        Z47 = 47,
        Z48 = 48,
        Z49 = 49,
        Z50 = 50,
        Z51 = 51,
        Z52 = 52,
        Z53 = 53,
        Z54 = 54,
        Z55 = 55,
        Z56 = 56,
        Z57 = 57,
        Z58 = 58,
        Z59 = 59,
        Z60 = 60,
        Z61 = 61,
        Z62 = 62,
        Z63 = 63,
        Z64 = 64,
        Z65 = 65,
        Z66 = 66,
        Z67 = 67,
        Z68 = 68,
        Z69 = 69,
        Z70 = 70,
        Z71 = 71,
        Z72 = 72,
        Z73 = 73,
        Z74 = 74,
        Z75 = 75,
        Z76 = 76,
        Z77 = 77,
        Z78 = 78,
        Z79 = 79,
        Z80 = 80,
        Z81 = 81,
        Z82 = 82,
        Z83 = 83,
        Z84 = 84,
        Z85 = 85,
        Z86 = 86,
        Z87 = 87,
        Z88 = 88,
        Z89 = 89,
        Z90 = 90,
        Z91 = 91,
        Z92 = 92,
        Z93 = 93,
        Z94 = 94,
        Z95 = 95,
        Z96 = 96,
        Z97 = 97,
        Z98 = 98,
        Z99 = 99,
        Z100 = 100,
        Z101 = 101,
        Z102 = 102,
        Z103 = 103,
        Z104 = 104,
        Z105 = 105,
        Z106 = 106,
        Z107 = 107,
        Z108 = 108,
        Z109 = 109,
        Z110 = 110,
        Z111 = 111,
        Z112 = 112,
        Z113 = 113,
        Z114 = 114,
        Z115 = 115,
        Z116 = 116,
        Z117 = 117,
        Z118 = 118,
        Z119 = 119,
        Z120 = 120,
        Z121 = 121,
        Z122 = 122,
        Z123 = 123,
        Z124 = 124,
        Z125 = 125,
        Z126 = 126,
        Z127 = 127,
        Z128 = 128,
        Z129 = 129,
        Z130 = 130,
        Z131 = 131,
        Z132 = 132,
        Z133 = 133,
        Z134 = 134,
        Z135 = 135,
        Z136 = 136,
        Z137 = 137,
        Z138 = 138,
        Z139 = 139,
        Z140 = 140,
        Z141 = 141,
        Z142 = 142,
        Z143 = 143,
        Z144 = 144,
        Z145 = 145,
        Z146 = 146,
        Z147 = 147,
        Z148 = 148,
        Z149 = 149,
        Z150 = 150,
        Z151 = 151,
        Z152 = 152,
        Z153 = 153,
        Z154 = 154,
        Z155 = 155,
        Z156 = 156,
        Z157 = 157,
        Z158 = 158,
        Z159 = 159,
        Z160 = 160,
        Z161 = 161,
        Z162 = 162,
        Z163 = 163,
        Z164 = 164,
        Z165 = 165,
        Z166 = 166,
        Z167 = 167,
        Z168 = 168,
        Z169 = 169,
        Z170 = 170,
        Z171 = 171,
        Z172 = 172,
        Z173 = 173,
        Z174 = 174,
        Z175 = 175,
        Z176 = 176,
        Z177 = 177,
        Z178 = 178,
        Z179 = 179,
        Z180 = 180,
        Z181 = 181,
        Z182 = 182,
        Z183 = 183,
        Z184 = 184,
        Z185 = 185,
        Z186 = 186,
        Z187 = 187,
        Z188 = 188,
        Z189 = 189,
        Z190 = 190,
        Z191 = 191,
        Z192 = 192,
        Z193 = 193,
        Z194 = 194,
        Z195 = 195,
        Z196 = 196,
        Z197 = 197,
        Z198 = 198,
        Z199 = 199,
        Z200 = 200,
        Z201 = 201,
        Z202 = 202,
        Z203 = 203,
        Z204 = 204,
        Z205 = 205,
        Z206 = 206,
        Z207 = 207,
        Z208 = 208,
        Z209 = 209,
        Z210 = 210,
        Z211 = 211,
        Z212 = 212,
        Z213 = 213,
        Z214 = 214,
        Z215 = 215,
        Z216 = 216,
        Z217 = 217,
        Z218 = 218,
        Z219 = 219,
        Z220 = 220,
        Z221 = 221,
        Z222 = 222,
        Z223 = 223,
        Z224 = 224,
        Z225 = 225,
        Z226 = 226,
        Z227 = 227,
        Z228 = 228,
        Z229 = 229,
        Z230 = 230,
        Z231 = 231,
        Z232 = 232,
        Z233 = 233,
        Z234 = 234,
        Z235 = 235,
        Z236 = 236,
        Z237 = 237,
        Z238 = 238,
        Z239 = 239,
        Z240 = 240,
        Z241 = 241,
        Z242 = 242,
        Z243 = 243,
        Z244 = 244,
        Z245 = 245,
        Z246 = 246,
        Z247 = 247,
        Z248 = 248,
        Z249 = 249,
        Z250 = 250,
        Z251 = 251,
        Z252 = 252,
        Z253 = 253,
        Z254 = 254
    };

    // ============================================
    // Z Value Type Tags
    // ============================================

    enum class ZValueType : uint8_t
    {
        NONE = 0,   // Unset / empty
        INT = 1,    // int32_t
        FLOAT = 2,  // float
        STRING = 3, // String (max 64 chars)
        BOOL = 4    // bool
    };

    // ============================================
    // Z Value Container
    // ============================================

    struct ZValue
    {
        ZValueType type;
        union
        {
            int32_t intVal;
            float floatVal;
            bool boolVal;
        };
        String strVal; // Stored separately (not in union due to non-trivial destructor)
        bool dirty;    // Changed since last publish

        ZValue() : type(ZValueType::NONE), intVal(0), strVal(""), dirty(false) {}

        // Type-safe getters with defaults
        int32_t toInt(int32_t defaultVal = 0) const
        {
            switch (type)
            {
            case ZValueType::INT:
                return intVal;
            case ZValueType::FLOAT:
                return static_cast<int32_t>(floatVal);
            case ZValueType::BOOL:
                return boolVal ? 1 : 0;
            case ZValueType::STRING:
                return strVal.toInt();
            default:
                return defaultVal;
            }
        }

        float toFloat(float defaultVal = 0.0f) const
        {
            switch (type)
            {
            case ZValueType::FLOAT:
                return floatVal;
            case ZValueType::INT:
                return static_cast<float>(intVal);
            case ZValueType::BOOL:
                return boolVal ? 1.0f : 0.0f;
            case ZValueType::STRING:
                return strVal.toFloat();
            default:
                return defaultVal;
            }
        }

        String toString(const String &defaultVal = "") const
        {
            switch (type)
            {
            case ZValueType::STRING:
                return strVal;
            case ZValueType::INT:
                return String(intVal);
            case ZValueType::FLOAT:
                return String(floatVal, 2);
            case ZValueType::BOOL:
                return boolVal ? "true" : "false";
            default:
                return defaultVal;
            }
        }

        bool toBool(bool defaultVal = false) const
        {
            switch (type)
            {
            case ZValueType::BOOL:
                return boolVal;
            case ZValueType::INT:
                return intVal != 0;
            case ZValueType::FLOAT:
                return floatVal != 0.0f;
            case ZValueType::STRING:
                return strVal == "true" || strVal == "1" || strVal == "ON";
            default:
                return defaultVal;
            }
        }

        bool isSet() const { return type != ZValueType::NONE; }
    };

    // ============================================
    // Z Key Helper Functions
    // ============================================

    /**
     * @brief Convert ZKey enum to JSON string key "Z0", "Z1", etc.
     * Generated on-the-fly to save RAM (no static storage for 255 strings)
     */
    inline String zKeyToString(ZKey key)
    {
        return String("Z") + String(static_cast<uint8_t>(key));
    }

    /**
     * @brief Check if a string is a valid Z key ("Z0" - "Z254")
     */
    inline bool isZKey(const char *str)
    {
        if (str == nullptr || str[0] != 'Z')
            return false;
        const char *numPart = str + 1;
        if (*numPart == '\0')
            return false;

        // Parse number
        int val = atoi(numPart);

        // Verify it's a clean number (no trailing chars)
        char check[8];
        snprintf(check, sizeof(check), "Z%d", val);
        if (strcmp(str, check) != 0)
            return false;

        return val >= Z_KEY_MIN && val <= Z_KEY_MAX;
    }

    /**
     * @brief Parse "Z0"-"Z254" string to ZKey enum
     * @return ZKey value, or ZKey::Z0 if invalid (check with isZKey first)
     */
    inline ZKey stringToZKey(const char *str)
    {
        if (!isZKey(str))
            return ZKey::Z0;
        int val = atoi(str + 1);
        return static_cast<ZKey>(val);
    }

    /**
     * @brief Get uint8_t index from ZKey
     */
    inline uint8_t zKeyIndex(ZKey key)
    {
        return static_cast<uint8_t>(key);
    }

    // ============================================
    // Z Key Callbacks
    // ============================================

    using ZKeyChangeCallback = std::function<void(ZKey key, const ZValue &value)>;

} // namespace ZenoPCB

#endif // ZENOPCB_ZKEY_TYPES_H
