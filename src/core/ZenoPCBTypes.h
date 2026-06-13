#ifndef ZENOPCB_TYPES_H
#define ZENOPCB_TYPES_H

#include <Arduino.h>
#include <functional>

namespace ZenoPCB
{

    /**
     * @brief WiFi Provisioning configuration
     */
    struct ProvisioningConfig
    {
        uint8_t buttonPin;         // Button pin to trigger AP mode (default: GPIO 0)
        uint32_t buttonHoldTimeMs; // Hold time to trigger AP mode (default: 3000ms)
        uint32_t apTimeoutMs;      // AP mode timeout (default: 5 minutes)
        String apSSIDPrefix;       // AP SSID prefix (default: "ZENO-")
        String apPassword;         // AP password (empty = open network)
        uint16_t webServerPort;    // Web server port (default: 80)
        int8_t ledPin;             // LED pin for status indicator (-1 = disabled)
        uint32_t ledBlinkInterval; // LED blink interval in AP mode (ms, default: 200)
        bool ledActiveHigh;        // LED active high (true) or active low (false)

        ProvisioningConfig()
            : buttonPin(0), buttonHoldTimeMs(3000), apTimeoutMs(300000), apSSIDPrefix("ZENO-"), apPassword("zenopcb12345"), webServerPort(80), ledPin(-1), ledBlinkInterval(200), ledActiveHigh(true) {}
    };

    /**
     * @brief Device type enumeration for specialized libraries
     */
    enum class DeviceType
    {
        GENERIC,        // Generic ESP32/ESP8266 device
        SENSOR_HUB,     // Multi-sensor hub device
        RELAY_BOARD,    // Relay control board
        LED_CONTROLLER, // LED/RGB controller
        GATEWAY,        // IoT Gateway device
        DISPLAY_DEVICE, // Device with display
        CUSTOM          // Custom device type
    };

    /**
     * @brief Supported connection types (bitmask flags)
     * Use bitwise OR to combine: CONN_WIFI | CONN_ETHERNET | CONN_CELLULAR
     */
    enum ConnectionFlags : uint8_t
    {
        CONN_NONE = 0,
        CONN_WIFI = 1 << 0,     // 0b001
        CONN_ETHERNET = 1 << 1, // 0b010
        CONN_CELLULAR = 1 << 2  // 0b100
    };

    // Helper to combine flags: CONN_WIFI | CONN_ETHERNET
    inline ConnectionFlags operator|(ConnectionFlags a, ConnectionFlags b)
    {
        return static_cast<ConnectionFlags>(static_cast<uint8_t>(a) | static_cast<uint8_t>(b));
    }

    /**
     * @brief Device information set at compile time
     */
    struct DeviceInfo
    {
        DeviceType type;                      // Device type
        String typeName;                      // Human-readable device type name
        String name;                          // Device model/product name
        String version;                       // Firmware version
        String manufacturer;                  // Manufacturer name
        ConnectionFlags supportedConnections; // Supported connection types (bitmask)

        DeviceInfo()
            : type(DeviceType::GENERIC),
              typeName("Generic"),
              name("ZenoPCB Device"),
              version("1.0.0"),
              manufacturer("ZenoPCB"),
              supportedConnections(CONN_WIFI) {}

        DeviceInfo(DeviceType t, const char *tName, const char *n, const char *v,
                   const char *m = "ZenoPCB", ConnectionFlags conn = CONN_WIFI)
            : type(t), typeName(tName), name(n), version(v), manufacturer(m),
              supportedConnections(conn) {}

        // Helper methods
        bool supportsWiFi() const { return supportedConnections & CONN_WIFI; }
        bool supportsEthernet() const { return supportedConnections & CONN_ETHERNET; }
        bool supportsCellular() const { return supportedConnections & CONN_CELLULAR; }
    };

    /**
     * @brief Active connection type selected by user
     */
    enum class ConnectionType
    {
        NONE,     // No connection selected
        WIFI,     // WiFi connection
        ETHERNET, // Wired Ethernet
        CELLULAR  // 4G/LTE
    };

    /**
     * @brief Device configuration stored in NVS
     */
    struct DeviceConfig
    {
        // Connection type
        ConnectionType connectionType;

        // WiFi settings
        String wifiSSID;
        String wifiPassword;
        bool wifiDHCP;
        String wifiIP;
        String wifiGateway;
        String wifiSubnet;
        String wifiDNS;

        // Ethernet settings
        bool ethernetDHCP;
        String ethernetIP;
        String ethernetGateway;
        String ethernetSubnet;
        String ethernetDNS;

        // Cellular settings
        String cellularAPN;
        String cellularUser;
        String cellularPass;

        // Device info
        String userId;
        String deviceId;
        bool configured;
        bool isClaimed;

        DeviceConfig()
            : connectionType(ConnectionType::WIFI),
              wifiDHCP(true),
              ethernetDHCP(true),
              configured(false),
              isClaimed(false) {}
    };

    /**
     * @brief WiFi scan result
     */
    struct WiFiNetwork
    {
        String ssid;
        int32_t rssi;
        uint8_t encryptionType;

        WiFiNetwork() : rssi(0), encryptionType(0) {}
        WiFiNetwork(const String &s, int32_t r, uint8_t e)
            : ssid(s), rssi(r), encryptionType(e) {}
    };

    /**
     * @brief Provisioning state (internal use)
     */
    enum class ProvisioningState
    {
        IDLE,
        BUTTON_PRESSED,
        AP_MODE_STARTING,
        AP_MODE_ACTIVE,
        CONFIG_RECEIVED,
        CONNECTING_WIFI,
        CONNECTED,
        DISCONNECTED,
        FAILED,
        TIMEOUT
    };

    /**
     * @brief Main ZenoPCB state (simplified for user)
     */
    enum class ZenoState
    {
        IDLE,
        CONNECTING,
        CONNECTED,
        DISCONNECTED,
        AP_MODE,
        ERROR
    };

    /**
     * @brief Event callbacks
     */
    using ProvisioningStateCallback = std::function<void(ProvisioningState state)>;
    using ConfigReceivedCallback = std::function<void(const DeviceConfig &config)>;
    using WiFiConnectedCallback = std::function<void()>;
    using ErrorCallback = std::function<void(const String &error)>;

} // namespace ZenoPCB

#endif // ZENOPCB_TYPES_H
