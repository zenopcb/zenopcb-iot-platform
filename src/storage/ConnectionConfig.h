/**
 * @file ConnectionConfig.h
 * @brief Data structures for connection configuration management
 *
 * Manages device connection configs received from MQTT and stored in LittleFS.
 * Supports Modbus RTU (Serial) and Modbus TCP connections.
 */

#ifndef CONNECTION_CONFIG_H
#define CONNECTION_CONFIG_H

#include <Arduino.h>
#include "../ZenoJson.h"  // ArduinoJson API from vendored copy (namespace ZenoJson; see vendor/ArduinoJson/LICENSE.md)
#include <vector>
#include "../core/ZenoPCBDebug.h"

namespace ZenoPCB
{
    // ============================================
    // Constants
    // ============================================
    constexpr size_t SHORTID_LENGTH = 4;
    constexpr size_t MAX_NAME_LENGTH = 64;
    constexpr size_t MAX_IP_LENGTH = 16;
    constexpr size_t MAX_COMPORT_LENGTH = 16;

    // Override via build flag: -DMAX_CONNECTIONS=100
#ifndef MAX_CONNECTIONS
#define MAX_CONNECTIONS 200
#endif

    constexpr size_t CONFIG_JSON_SIZE = 768; // Increased for new fields
    constexpr size_t META_JSON_SIZE = 2048;

    // File paths
    constexpr const char *CONNECTIONS_DIR = "/connections";
    constexpr const char *META_FILE = "/connections/meta.json";

    // ============================================
    // Enums
    // ============================================

    /**
     * @brief MQTT message action types
     */
    enum class ConfigAction : char
    {
        CREATE = 'c',
        UPDATE = 'u',
        DELETE = 'd',
        UNKNOWN = '?'
    };

    /**
     * @brief Connection protocol types
     * NOTE: Using MODBUS_RTU instead of SERIAL to avoid conflict with Arduino macro
     */
    enum class ConnectionProtocol : uint8_t
    {
        MODBUS_RTU = 0, // Modbus RTU over serial
        MODBUS_TCP = 1, // Modbus TCP over network
        UNKNOWN = 255
    };

    /**
     * @brief Serial parity types
     */
    enum class Parity : uint8_t
    {
        NONE = 0, // "N"
        EVEN = 1, // "E"
        ODD = 2   // "O"
    };

    /**
     * @brief Configuration operation state
     */
    enum class ConfigState : uint8_t
    {
        IDLE,
        PARSING,
        VALIDATING,
        WRITING,
        DELETING,
        UPDATING_META,
        ERROR,
        RECOVERING
    };

    // ============================================
    // Data Structures
    // ============================================

    /**
     * @brief Single connection configuration
     *
     * Full spec fields from CONNECTION_CONFIG_SHORTID_IMPLEMENTATION.md:
     * - Basic: id, pr, en
     * - Serial: br, db, sb, pa, pt, cp, ifd
     * - TCP: ip, p, uid, ct, ri, ka, kai
     * - Common: rt, dbp, mr
     */
    struct ConnectionConfig
    {
        // === Basic Fields ===
        char shortId[SHORTID_LENGTH + 1]; // id - 4 char base62
        char name[MAX_NAME_LENGTH + 1];   // (not sent via MQTT anymore)
        ConnectionProtocol protocol;      // pr - "T"=TCP, "S"=Serial
        bool enabled;                     // en - 1=Enabled, 0=Disabled

        // === Serial/RTU Parameters ===
        uint32_t baudRate;                    // br - 9600, 19200, 38400, 57600, 115200
        uint8_t dataBits;                     // db - Data bits (7 or 8)
        uint8_t stopBits;                     // sb - Stop bits (1 or 2)
        Parity parity;                        // pa - "N"=None, "E"=Even, "O"=Odd
        uint8_t portIndex;                    // pt - Port index (1, 2, 3...)
        char comPort[MAX_COMPORT_LENGTH + 1]; // cp - COM port name
        uint16_t interFrameDelay;             // ifd - Inter frame delay (ms)

        // === TCP Parameters ===
        char ipAddress[MAX_IP_LENGTH + 1]; // ip - IP address
        uint16_t port;                     // p - TCP port (default 502)
        uint8_t unitId;                    // uid - Modbus Unit ID (1-247)
        uint16_t connectTimeout;           // ct - Connect timeout (ms)
        uint16_t reconnectInterval;        // ri - Reconnect interval (ms)
        bool keepAlive;                    // ka - Keep alive (0/1)
        uint32_t keepAliveInterval;        // kai - Keep alive interval (ms)

        // === Common Parameters ===
        uint16_t responseTimeout;   // rt - Response timeout (ms)
        uint16_t delayBetweenPolls; // dbp - Delay between polls (ms)
        uint8_t maxRetries;         // mr - Max retries

        // === Timestamps ===
        uint32_t createdAt; // ca - Created timestamp
        uint32_t updatedAt; // ua - Updated timestamp

        ConnectionConfig()
            : protocol(ConnectionProtocol::UNKNOWN),
              enabled(true),
              // Serial defaults
              baudRate(9600),
              dataBits(8),
              stopBits(1),
              parity(Parity::NONE),
              portIndex(1),
              interFrameDelay(50),
              // TCP defaults
              port(502),
              unitId(1),
              connectTimeout(5000),
              reconnectInterval(10000),
              keepAlive(true),
              keepAliveInterval(30000),
              // Common defaults
              responseTimeout(1000),
              delayBetweenPolls(100),
              maxRetries(3),
              // Timestamps
              createdAt(0),
              updatedAt(0)
        {
            shortId[0] = '\0';
            name[0] = '\0';
            comPort[0] = '\0';
            ipAddress[0] = '\0';
        }

        bool isSerialConfig() const { return protocol == ConnectionProtocol::MODBUS_RTU; }
        bool isTcpConfig() const { return protocol == ConnectionProtocol::MODBUS_TCP; }
        bool isValid() const { return shortId[0] != '\0' && strlen(shortId) == SHORTID_LENGTH; }

        void toJson(JsonObject &obj) const
        {
            // Basic fields
            obj["id"] = shortId;
            obj["pr"] = (protocol == ConnectionProtocol::MODBUS_TCP) ? "T" : "S";
            obj["en"] = enabled ? 1 : 0;

            // Serial parameters (always include for compatibility)
            obj["br"] = baudRate;
            obj["db"] = dataBits;
            obj["sb"] = stopBits;
            obj["pa"] = parityToString(parity); // Use string instead of char for ArduinoJson
            obj["pt"] = portIndex;
            if (comPort[0] != '\0')
            {
                obj["cp"] = comPort;
            }
            obj["ifd"] = interFrameDelay;

            // TCP parameters
            if (protocol == ConnectionProtocol::MODBUS_TCP)
            {
                obj["ip"] = ipAddress;
                obj["p"] = port;
            }

            // Common parameters
            obj["uid"] = unitId;
            obj["ct"] = connectTimeout;
            obj["ri"] = reconnectInterval;
            obj["ka"] = keepAlive ? 1 : 0;
            obj["kai"] = keepAliveInterval;
            obj["rt"] = responseTimeout;
            obj["dbp"] = delayBetweenPolls;
            obj["mr"] = maxRetries;

            // Timestamps
            obj["ca"] = createdAt;
            obj["ua"] = updatedAt;
        }

        bool fromJson(const JsonObject &obj)
        {
            // === Basic Fields ===
            const char *id = obj["id"] | obj["shortId"] | "";
            if (strlen(id) != SHORTID_LENGTH)
            {
                return false;
            }
            strncpy(shortId, id, SHORTID_LENGTH);
            shortId[SHORTID_LENGTH] = '\0';

            // Note: "nm" (name) field removed - MQTT khng gi na
            name[0] = '\0';

            const char *pr = obj["pr"] | obj["protocol"] | "S";
            if (pr[0] == 'T' || strcmp(pr, "TCP") == 0)
            {
                protocol = ConnectionProtocol::MODBUS_TCP;
            }
            else
            {
                protocol = ConnectionProtocol::MODBUS_RTU;
            }

            // Fix: ArduinoJson operator | treats 0 as falsy, must check explicitly
            if (obj["en"].is<int>() || obj["enabled"].is<int>())
            {
                int en = obj["en"] | obj["enabled"];
                enabled = (en != 0);
            }
            else
            {
                enabled = true; // Default if field not present
            }

            // === Serial Parameters ===
            baudRate = obj["br"] | obj["baudRate"] | 9600;
            dataBits = obj["db"] | obj["dataBits"] | 8;
            stopBits = obj["sb"] | obj["stopBits"] | 1;

            const char *pa = obj["pa"] | "N";
            parity = charToParity(pa[0]);

            portIndex = obj["pt"] | obj["portIndex"] | 1;

            const char *cp = obj["cp"] | obj["comPort"] | "";
            strncpy(comPort, cp, MAX_COMPORT_LENGTH);
            comPort[MAX_COMPORT_LENGTH] = '\0';

            interFrameDelay = obj["ifd"] | obj["interFrameDelay"] | 50;

            // === TCP Parameters ===
            const char *ip = obj["ip"] | obj["ipAddress"] | "";
            strncpy(ipAddress, ip, MAX_IP_LENGTH);
            ipAddress[MAX_IP_LENGTH] = '\0';

            port = obj["p"] | obj["port"] | 502;
            unitId = obj["uid"] | obj["unitId"] | obj["si"] | obj["slaveId"] | 1; // Support old si field
            connectTimeout = obj["ct"] | obj["connectTimeout"] | 5000;
            reconnectInterval = obj["ri"] | obj["reconnectInterval"] | 10000;

            int ka = obj["ka"] | obj["keepAlive"] | 1;
            keepAlive = (ka != 0);

            keepAliveInterval = obj["kai"] | obj["keepAliveInterval"] | 30000;

            // === Common Parameters ===
            responseTimeout = obj["rt"] | obj["responseTimeout"] | 1000;
            delayBetweenPolls = obj["dbp"] | obj["delayBetweenPolls"] | 100;
            maxRetries = obj["mr"] | obj["maxRetries"] | 3;

            // === Timestamps ===
            createdAt = obj["ca"] | obj["createdAt"] | 0;
            updatedAt = obj["ua"] | obj["updatedAt"] | 0;

            return true;
        }

        // === Debug Output ===
        // body guarded to ESP32/ESP8266 only because
        // `Serial.printf` is not portable: UNO R4 (Renesas `UART` class) and
        // STM32duino (`HardwareSerial`) do not expose it. The function stays
        // declared on all platforms (ABI/call-site preserved) but the body is
        // a no-op on UNO R4 / STM32. Consistent with /
        // whole-class TU gates: keep the
        // ESP32/ESP8266 surface byte-identical, stub on new ports.
        void printDebug() const
        {
#if defined(ESP32) || defined(ESP8266)
            ZENO_LOG_RAW("\n");
            ZENO_LOG_RAW("ConnectionConfig: [%s] \n", shortId);
            ZENO_LOG_RAW("\n");
            ZENO_LOG_RAW("Protocol: %-10s Enabled: %-3s \n",
                          protocolToString(protocol), enabled ? "Yes" : "No");
            ZENO_LOG_RAW("\n");

            if (protocol == ConnectionProtocol::MODBUS_RTU)
            {
                ZENO_LOG_RAW("[Serial/RTU Parameters]\n");
                ZENO_LOG_RAW("Baud Rate: %-8lu Data Bits: %d Stop Bits: %d \n",
                              baudRate, dataBits, stopBits);
                ZENO_LOG_RAW("Parity: %-4s Port Index: %d COM Port: %-10s \n",
                              parityToDisplayString(parity), portIndex, comPort[0] ? comPort : "(none)");
                ZENO_LOG_RAW("Inter Frame Delay: %d ms \n", interFrameDelay);
            }
            else if (protocol == ConnectionProtocol::MODBUS_TCP)
            {
                ZENO_LOG_RAW("[TCP Parameters]\n");
                ZENO_LOG_RAW("IP Address: %-15s Port: %-5d \n",
                              ipAddress[0] ? ipAddress : "(none)", port);
                ZENO_LOG_RAW("Connect Timeout: %d ms Reconnect Interval: %d ms \n",
                              connectTimeout, reconnectInterval);
                ZENO_LOG_RAW("Keep Alive: %-3s Keep Alive Interval: %lu ms \n",
                              keepAlive ? "Yes" : "No", keepAliveInterval);
            }

            ZENO_LOG_RAW("\n");
            ZENO_LOG_RAW("[Common Modbus Parameters]\n");
            ZENO_LOG_RAW("Unit ID: %-3d Response Timeout: %d ms \n",
                          unitId, responseTimeout);
            ZENO_LOG_RAW("Delay Between Polls: %d ms Max Retries: %d \n",
                          delayBetweenPolls, maxRetries);
            ZENO_LOG_RAW("\n");
#else
            // Body intentionally empty on UNO R4 / STM32 (no portable
            // Serial.printf). Debug output for these platforms lives in
            // the per-port HAL diagnostics surface (UAT).
            (void)0;
#endif
        }

        // Helper functions
        static const char *protocolToString(ConnectionProtocol p)
        {
            switch (p)
            {
            case ConnectionProtocol::MODBUS_RTU:
                return "Modbus RTU";
            case ConnectionProtocol::MODBUS_TCP:
                return "Modbus TCP";
            default:
                return "Unknown";
            }
        }

        static const char *parityToString(Parity p)
        {
            switch (p)
            {
            case Parity::NONE:
                return "N";
            case Parity::EVEN:
                return "E";
            case Parity::ODD:
                return "O";
            default:
                return "N";
            }
        }

        static const char *parityToDisplayString(Parity p)
        {
            switch (p)
            {
            case Parity::NONE:
                return "None";
            case Parity::EVEN:
                return "Even";
            case Parity::ODD:
                return "Odd";
            default:
                return "None";
            }
        }

        static Parity charToParity(char c)
        {
            switch (c)
            {
            case 'E':
            case 'e':
                return Parity::EVEN;
            case 'O':
            case 'o':
                return Parity::ODD;
            default:
                return Parity::NONE;
            }
        }
    };

    /**
     * @brief MQTT config message structure
     * NOTE: Lu dataJson string thay v JsonObject  trnh dangling reference
     * khi JsonDocument b destroy sau khi parse
     */
    struct ConfigMessage
    {
        String type; // "cc" = connection-config
        ConfigAction action;
        String dataJson; // Raw JSON string ca data object (thay v JsonObject)

        static ConfigAction actionFromChar(char c)
        {
            switch (c)
            {
            case 'c':
            case 'C':
                return ConfigAction::CREATE;
            case 'u':
            case 'U':
                return ConfigAction::UPDATE;
            case 'd':
            case 'D':
                return ConfigAction::DELETE;
            default:
                return ConfigAction::UNKNOWN;
            }
        }
    };

    /**
     * @brief Connection Metadata
     */
    struct ConnectionMetadata
    {
        uint16_t totalConnections;
        uint16_t enabledConnections;
        uint32_t storageUsedBytes;
        uint32_t storageAvailableBytes;
        uint32_t lastUpdateTime;
        uint32_t lastSyncTime;
        uint16_t totalErrors;
        char lastError[64];
        uint32_t lastErrorTime;
        std::vector<String> shortIds;

        ConnectionMetadata()
            : totalConnections(0),
              enabledConnections(0),
              storageUsedBytes(0),
              storageAvailableBytes(0),
              lastUpdateTime(0),
              lastSyncTime(0),
              totalErrors(0),
              lastErrorTime(0)
        {
            lastError[0] = '\0';
        }

        void toJson(JsonObject &obj) const
        {
            obj["total"] = totalConnections;
            obj["enabled"] = enabledConnections;
            obj["usedBytes"] = storageUsedBytes;
            obj["availBytes"] = storageAvailableBytes;
            obj["lastUpdate"] = lastUpdateTime;
            obj["errors"] = totalErrors;

            JsonArray ids = obj["ids"].to<JsonArray>();
            for (const auto &id : shortIds)
            {
                ids.add(id);
            }
        }

        bool fromJson(const JsonObject &obj)
        {
            totalConnections = obj["total"] | 0;
            enabledConnections = obj["enabled"] | 0;
            storageUsedBytes = obj["usedBytes"] | 0;
            storageAvailableBytes = obj["availBytes"] | 0;
            lastUpdateTime = obj["lastUpdate"] | 0;
            totalErrors = obj["errors"] | 0;

            const char *err = obj["lastErr"] | "";
            strncpy(lastError, err, 63);
            lastError[63] = '\0';

            shortIds.clear();
            JsonArray ids = obj["ids"];
            for (JsonVariant v : ids)
            {
                shortIds.push_back(v.as<String>());
            }
            return true;
        }

        void setError(const char *error)
        {
            strncpy(lastError, error, 63);
            lastError[63] = '\0';
            lastErrorTime = millis() / 1000;
            totalErrors++;
        }
    };

    // ============================================
    // Helper Functions
    // ============================================

    inline String getConfigFilePath(const char *shortId)
    {
        String path = CONNECTIONS_DIR;
        path += "/";
        path += shortId;
        path += ".json";
        return path;
    }

    inline const char *actionToString(ConfigAction action)
    {
        switch (action)
        {
        case ConfigAction::CREATE:
            return "CREATE";
        case ConfigAction::UPDATE:
            return "UPDATE";
        case ConfigAction::DELETE:
            return "DELETE";
        default:
            return "UNKNOWN";
        }
    }

} // namespace ZenoPCB

#endif // CONNECTION_CONFIG_H
