/**
 * @file ZenoEthernetProvider.h
 * @brief Network provider for W5500 Ethernet module using ESP32 built-in ETH driver
 *
 * Implements ZenoNetworkProvider interface for wired Ethernet
 * via the W5500 SPI module using ESP32's native ETH.h library.
 * No external Ethernet library required — uses ESP-IDF driver directly.
 *
 * Ưu điểm so với thư viện Ethernet cũ:
 * - Dùng driver native ESP-IDF, hiệu suất cao hơn
 * - DHCP tự động (không cần gọi maintain())
 * - Event-driven (link up/down, got IP)
 * - Chia sẻ lwIP stack với WiFi → WiFiClient dùng được cho MQTT
 * - Không cần thêm lib_deps
 *
 * @note Requires build flag: -DZENOPCB_ENABLE_ETHERNET
 * @note Requires Arduino ESP32 Core 3.0+ (pioarduino platform, ESP-IDF 5.x)
 *       ETH_PHY_W5500 và SPI Ethernet support chỉ có từ Core 3.x trở lên
 *
 * @author ZenoPCB Team
 */

#pragma once

#ifdef ZENOPCB_ENABLE_ETHERNET

#include <ETH.h>
#include <WiFi.h>
#include "../core/ZenoNetworkProvider.h"
#include "../core/ZenoPCBTypes.h"
#include "../core/ZenoPCBDebug.h"

namespace ZenoPCB
{

    /**
     * @brief Ethernet W5500 network provider (ESP32 native ETH driver)
     *
     * Sử dụng ETH.h built-in của ESP32 Arduino Core.
     * Kết nối W5500 qua SPI, hỗ trợ DHCP tự động và Static IP.
     *
     * Usage:
     * @code
     * // Default SPI pins (SCK=18, MISO=19, MOSI=23)
     * ZenoEthernetProvider ethProvider(5, 26);  // CS=GPIO5, RST=GPIO26
     * zeno.setNetworkProvider(&ethProvider).begin();
     *
     * // Custom SPI pins
     * ZenoEthernetProvider ethProvider(5, 26, -1, 14, 12, 13);
     * @endcode
     */
    class ZenoEthernetProvider : public ZenoNetworkProvider
    {
    public:
        /**
         * @brief Constructor
         * @param csPin   SPI Chip Select pin for W5500 (default: GPIO 5)
         * @param rstPin  Reset pin for W5500 (-1 = not used)
         * @param irqPin  Interrupt pin for W5500 (-1 = not used, polling mode)
         * @param sckPin  SPI SCK pin (default: 18)
         * @param misoPin SPI MISO pin (default: 19)
         * @param mosiPin SPI MOSI pin (default: 23)
         */
        ZenoEthernetProvider(uint8_t csPin = 5, int8_t rstPin = -1,
                             int8_t irqPin = -1,
                             uint8_t sckPin = 18, uint8_t misoPin = 19,
                             uint8_t mosiPin = 23)
            : _csPin(csPin), _rstPin(rstPin), _irqPin(irqPin),
              _sckPin(sckPin), _misoPin(misoPin), _mosiPin(mosiPin),
              _linkUp(false), _gotIP(false), _useDHCP(true)
        {
            _getInstanceRef() = this;
        }

        bool begin(const DeviceConfig &config) override
        {
            ZENO_LOG("ETH", "Initializing W5500 (native ETH driver)");
            ZENO_LOG("ETH", "  CS=%d, RST=%d, IRQ=%d", _csPin, _rstPin, _irqPin);
            ZENO_LOG("ETH", "  SCK=%d, MISO=%d, MOSI=%d", _sckPin, _misoPin, _mosiPin);

            // Store static IP config for event handler
            _useDHCP = config.ethernetDHCP;
            if (!_useDHCP)
            {
                _staticIP.fromString(config.ethernetIP);
                _staticGW.fromString(config.ethernetGateway);
                _staticSN.fromString(config.ethernetSubnet);
                _staticDNS.fromString(config.ethernetDNS);
                ZENO_LOG("ETH", "Static IP configured: %s", config.ethernetIP.c_str());
            }

            // Register ETH event handler
            WiFi.onEvent(_onEthEvent);

            // Start W5500 via native SPI Ethernet driver
            // ETH.begin() initializes SPI bus internally when pin numbers are provided
            bool result = ETH.begin(
                ETH_PHY_W5500, // PHY type
                1,             // PHY address (default for W5500)
                _csPin,        // CS pin
                _irqPin,       // IRQ pin (-1 = polling)
                _rstPin,       // RST pin (-1 = not used)
                SPI3_HOST,     // SPI host (VSPI = default Arduino SPI bus)
                _sckPin,       // SCK
                _misoPin,      // MISO
                _mosiPin       // MOSI
            );

            if (!result)
            {
                ZENO_LOG("ETH", "ETH.begin() failed! Check W5500 wiring.");
                return false;
            }

            ZENO_LOG("ETH", "ETH.begin() OK — waiting for link + IP...");
            return true;
        }

        void loop() override
        {
            // Nothing needed — ESP-IDF handles DHCP renewal and link monitoring
            // State changes come through WiFi.onEvent() callbacks
        }

        bool isConnected() const override
        {
            return _linkUp && _gotIP;
        }

        Client *getClient() override
        {
            // ETH shares lwIP stack with WiFi → WiFiClient routes through ETH
            // when Ethernet is the active interface
            return &_ethClient;
        }

        Client *getOTAClient() override
        {
            // OTA dùng client riêng — tránh kill TCP connection của MQTT
            return &_ethOTAClient;
        }

        String getLocalIP() const override
        {
            if (!_gotIP)
                return "0.0.0.0";
            return ETH.localIP().toString();
        }

        const char *getName() const override
        {
            return "Ethernet";
        }

        /**
         * @brief Check if Ethernet link is physically up (cable connected)
         */
        bool isLinkUp() const { return _linkUp; }

        /**
         * @brief Get link speed in Mbps (10 or 100)
         */
        uint8_t getLinkSpeed() const { return ETH.linkSpeed(); }

        /**
         * @brief Check if full duplex mode
         */
        bool isFullDuplex() const { return ETH.fullDuplex(); }

    private:
        // Singleton accessor (avoids ODR issue for static member in header-only)
        static ZenoEthernetProvider *&_getInstanceRef()
        {
            static ZenoEthernetProvider *inst = nullptr;
            return inst;
        }

        // Event handler for ETH events (registered via WiFi.onEvent)
        static void _onEthEvent(WiFiEvent_t event)
        {
            auto *inst = _getInstanceRef();
            if (!inst)
                return;

            switch (event)
            {
            case ARDUINO_EVENT_ETH_START:
                ZENO_LOG("ETH", "Driver started");
                ETH.setHostname("zenopcb-eth");
                // Apply static IP if configured
                if (!inst->_useDHCP)
                {
                    ETH.config(inst->_staticIP, inst->_staticGW,
                               inst->_staticSN, inst->_staticDNS);
                    ZENO_LOG("ETH", "Static IP applied: %s", inst->_staticIP.toString().c_str());
                }
                break;

            case ARDUINO_EVENT_ETH_CONNECTED:
                ZENO_LOG("ETH", "Link UP (%d Mbps, %s duplex)",
                         ETH.linkSpeed(), ETH.fullDuplex() ? "Full" : "Half");
                inst->_linkUp = true;
                break;

            case ARDUINO_EVENT_ETH_GOT_IP:
                ZENO_LOG("ETH", "Got IP: %s  GW: %s  Mask: %s",
                         ETH.localIP().toString().c_str(),
                         ETH.gatewayIP().toString().c_str(),
                         ETH.subnetMask().toString().c_str());
                inst->_gotIP = true;
                break;

            case ARDUINO_EVENT_ETH_DISCONNECTED:
                ZENO_LOG("ETH", "Link DOWN");
                inst->_linkUp = false;
                inst->_gotIP = false;
                break;

            case ARDUINO_EVENT_ETH_STOP:
                ZENO_LOG("ETH", "Driver stopped");
                inst->_linkUp = false;
                inst->_gotIP = false;
                break;

            default:
                break;
            }
        }

        uint8_t _csPin;
        int8_t _rstPin;
        int8_t _irqPin;
        uint8_t _sckPin;
        uint8_t _misoPin;
        uint8_t _mosiPin;

        bool _linkUp;
        bool _gotIP;
        bool _useDHCP;

        IPAddress _staticIP;
        IPAddress _staticGW;
        IPAddress _staticSN;
        IPAddress _staticDNS;

        WiFiClient _ethClient;    // MQTT client — shares lwIP stack, routes via ETH
        WiFiClient _ethOTAClient; // OTA client riêng — không ảnh hưởng MQTT
    };

} // namespace ZenoPCB

#endif // ZENOPCB_ENABLE_ETHERNET
