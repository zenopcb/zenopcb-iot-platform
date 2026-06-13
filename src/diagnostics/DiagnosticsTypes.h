#ifndef ZENOPCB_DIAGNOSTICS_TYPES_H
#define ZENOPCB_DIAGNOSTICS_TYPES_H

#include <Arduino.h>
#include <functional>

namespace ZenoPCB
{

    /**
     * @brief Connection type for diagnostics reporting
     *
     * Valid values: WIFI, ETHERNET, 4G, 5G
     * Set by mobile app configuration, not auto-detected
     */
    enum class DiagnosticsConnectionType
    {
        UNKNOWN,
        WIFI,
        ETHERNET,
        CELLULAR_4G,
        CELLULAR_5G
    };

    /**
     * @brief Convert string to DiagnosticsConnectionType enum
     *
     * @param typeStr Connection type string (case-insensitive)
     * @return DiagnosticsConnectionType enum value
     *
     * Accepts: "WIFI", "ETHERNET", "4G", "5G" (case-insensitive, trimmed)
     * Returns UNKNOWN for invalid values
     */
    inline DiagnosticsConnectionType stringToConnectionType(const String &typeStr)
    {
        String normalized = typeStr;
        normalized.trim();
        normalized.toUpperCase();

        if (normalized == "WIFI")
            return DiagnosticsConnectionType::WIFI;
        if (normalized == "ETHERNET")
            return DiagnosticsConnectionType::ETHERNET;
        if (normalized == "4G")
            return DiagnosticsConnectionType::CELLULAR_4G;
        if (normalized == "5G")
            return DiagnosticsConnectionType::CELLULAR_5G;

        return DiagnosticsConnectionType::UNKNOWN;
    }

    /**
     * @brief Convert DiagnosticsConnectionType enum to string
     *
     * @param type Connection type enum
     * @return String representation (uppercase)
     */
    inline String connectionTypeToString(DiagnosticsConnectionType type)
    {
        switch (type)
        {
        case DiagnosticsConnectionType::WIFI:
            return "WIFI";
        case DiagnosticsConnectionType::ETHERNET:
            return "ETHERNET";
        case DiagnosticsConnectionType::CELLULAR_4G:
            return "4G";
        case DiagnosticsConnectionType::CELLULAR_5G:
            return "5G";
        default:
            return "UNKNOWN";
        }
    }

    /**
     * @brief Diagnostics data structure
     *
     * Contains all system information for diagnostics reporting
     */
    struct DiagnosticsData
    {
        // Required fields
        DiagnosticsConnectionType connectionType;
        String deviceIp;        // Local IP address
        int32_t signalStrength; // RSSI (WiFi) or link speed proxy (Ethernet)
        String firmwareVersion;
        uint32_t uptime;      // Device uptime in seconds
        uint32_t memoryUsage; // Heap memory used in bytes

        // Optional fields (recommended)
        uint32_t freeHeap; // Free heap memory in bytes
        int32_t rssi;      // Duplicate of signalStrength (backward compatibility)
        String macAddress; // Device MAC address

        // Ethernet-specific fields
        uint8_t linkSpeed; // 10 or 100 Mbps (0 = not applicable)
        bool fullDuplex;   // true = full duplex, false = half duplex

        // Cellular-specific fields (4G/5G)
        String operatorName; // Network operator (e.g. "Viettel", "Vietnamobile")
        String networkType;  // "LTE", "3G", "2G"
        String imei;         // Modem IMEI (unique hardware ID)
        int16_t csq;         // Raw CSQ value (0-31, 99=unknown) for cellular

        // For on-demand response
        String requestId; // Empty for passive updates, set for on-demand response

        DiagnosticsData()
            : connectionType(DiagnosticsConnectionType::UNKNOWN),
              signalStrength(0),
              uptime(0),
              memoryUsage(0),
              freeHeap(0),
              rssi(0),
              linkSpeed(0),
              fullDuplex(false),
              csq(0)
        {
        }
    };

    /**
     * @brief Diagnostics configuration
     */
    struct DiagnosticsConfig
    {
        uint32_t interval;                        // Send interval in milliseconds (default: 600000 = 10 minutes)
        DiagnosticsConnectionType connectionType; // Connection type (set by app)
        bool passiveEnabled;                      // Enable passive periodic updates
        uint8_t maxRetries;                       // Max retry attempts for on-demand responses (default: 2)

        DiagnosticsConfig()
            : interval(600000), // 10 minutes default
              connectionType(DiagnosticsConnectionType::UNKNOWN),
              passiveEnabled(true),
              maxRetries(2)
        {
        }
    };

    /**
     * @brief Callback for on-demand diagnostics request
     *
     * Called when backend sends diagnostics request
     * @param requestId UUID from backend request
     */
    using DiagnosticsRequestCallback = std::function<void(const String &requestId)>;

    /**
     * @brief Callback for diagnostics send success
     *
     * @param isPassive true if passive update, false if on-demand response
     */
    using DiagnosticsSentCallback = std::function<void(bool isPassive)>;

    /**
     * @brief Callback for diagnostics send failure
     *
     * @param error Error message
     * @param isPassive true if passive update, false if on-demand response
     */
    using DiagnosticsErrorCallback = std::function<void(const String &error, bool isPassive)>;

} // namespace ZenoPCB

#endif // ZENOPCB_DIAGNOSTICS_TYPES_H
