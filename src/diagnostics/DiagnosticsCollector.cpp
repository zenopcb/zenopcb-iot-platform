// TU guard for ZENOPCB_MICRO_BASIC profile.
#if !defined(ZENOPCB_DISABLE_DIAGNOSTICS)

#include "DiagnosticsCollector.h"
#include "../core/ZenoPCBDebug.h"

#ifdef ZENOPCB_ENABLE_ETHERNET
#include <ETH.h>
#endif
#ifdef ZENOPCB_ENABLE_CELLULAR
#include "../vendor/TinyGSM/TinyGsmClient.h"
#endif

#if ZENOPCB_DEBUG_VERBOSE
#define ZENO_LOG_DIAG_V(fmt, ...) ZENO_LOG("ZenoPCB", fmt, ##__VA_ARGS__)
#else
#define ZENO_LOG_DIAG_V(fmt, ...)
#endif

namespace ZenoPCB
{

    DiagnosticsCollector::DiagnosticsCollector(IZenoHal &hal, const DeviceInfo *deviceInfo)
        : _hal(hal),
          _deviceInfo(deviceInfo),
          _connectionType(DiagnosticsConnectionType::UNKNOWN),
          _networkProvider(nullptr)
    {
    }

    void DiagnosticsCollector::setNetworkProvider(ZenoNetworkProvider *provider)
    {
        _networkProvider = provider;
    }

    void DiagnosticsCollector::setConnectionType(const String &type)
    {
        _connectionType = stringToConnectionType(type);
    }

    void DiagnosticsCollector::setConnectionType(DiagnosticsConnectionType type)
    {
        _connectionType = type;
    }

    DiagnosticsConnectionType DiagnosticsCollector::getConnectionType() const
    {
        return _connectionType;
    }

    DiagnosticsData DiagnosticsCollector::collect(const char *requestId)
    {
        DiagnosticsData data;

        ZENO_LOG_DIAG_V("[DIAG-COLLECT] Collecting diagnostics data...");

        // Set requestId if provided (on-demand response)
        if (requestId != nullptr)
        {
            data.requestId = String(requestId);
        }

        // Connection type (set by app, not auto-detected)
        data.connectionType = _connectionType;

        // Device IP address
        data.deviceIp = _collectDeviceIp();

        // Signal strength (RSSI)
        data.signalStrength = _collectSignalStrength();
        data.rssi = data.signalStrength; // Duplicate for backward compatibility

        // Ethernet link info
#ifdef ZENOPCB_ENABLE_ETHERNET
        if (_connectionType == DiagnosticsConnectionType::ETHERNET &&
            ETH.localIP() != IPAddress(0, 0, 0, 0))
        {
            data.linkSpeed = ETH.linkSpeed();
            data.fullDuplex = ETH.fullDuplex();
        }
#endif

        // Cellular-specific fields (4G/5G) read from provider
        if ((_connectionType == DiagnosticsConnectionType::CELLULAR_4G ||
             _connectionType == DiagnosticsConnectionType::CELLULAR_5G) &&
            _networkProvider != nullptr)
        {
            data.operatorName = _networkProvider->getOperator();
            data.networkType = _networkProvider->getNetworkType();
            data.imei = _networkProvider->getModemIMEI();
            data.csq = _networkProvider->getSignalQuality(); // Raw CSQ 0-31
        }

        // Firmware version from DeviceInfo
        if (_deviceInfo != nullptr)
        {
            data.firmwareVersion = _deviceInfo->version;
        }
        else
        {
            data.firmwareVersion = "unknown";
        }

        // System information
        data.uptime = _collectUptime();
        data.memoryUsage = _collectMemoryUsage();
        data.freeHeap = _collectFreeHeap();

        // MAC address
        data.macAddress = _collectMacAddress();

        // Log collected data summary
        ZENO_LOG_DIAG_V("[DIAG-COLLECT] Type: %s, IP: %s, Signal: %d, Uptime: %ds, Heap: %d/%d",
                        connectionTypeToString(data.connectionType).c_str(),
                        data.deviceIp.c_str(),
                        data.signalStrength,
                        data.uptime,
                        data.freeHeap,
                        data.memoryUsage);

        return data;
    }

    // ============================================
    // Private helper methods
    // ============================================

    String DiagnosticsCollector::_collectDeviceIp()
    {
        // Cellular: get IP from network provider (modem AT command)
        if ((_connectionType == DiagnosticsConnectionType::CELLULAR_4G ||
             _connectionType == DiagnosticsConnectionType::CELLULAR_5G) &&
            _networkProvider != nullptr)
        {
            String ip = _networkProvider->getLocalIP();
            if (ip.length() > 0 && ip != "0.0.0.0")
                return ip;
        }

#ifdef ZENOPCB_ENABLE_ETHERNET
        // Ethernet takes priority if it has a valid IP
        if (_connectionType == DiagnosticsConnectionType::ETHERNET)
        {
            IPAddress ip = ETH.localIP();
            if (ip != IPAddress(0, 0, 0, 0))
                return ip.toString();
        }
#endif

        // Check WiFi connection.
        // gate: STM32F4 default-Ethernet has no WiFi.h; the
        // diagnostics IP collection falls through to the not-connected
        // sentinel in that branch.
#if defined(ESP32) || defined(ESP8266) || defined(ARDUINO_UNOR4_WIFI) || defined(STM32F1xx)
        if (WiFi.status() == WL_CONNECTED)
        {
            return WiFi.localIP().toString();
        }
#endif

        return "0.0.0.0"; // Not connected
    }

    int32_t DiagnosticsCollector::_collectSignalStrength()
    {
        // Cellular: convert CSQ (0-31) to approximate dBm
        // Formula: dBm = CSQ * 2 - 113 (standard 3GPP conversion)
        if ((_connectionType == DiagnosticsConnectionType::CELLULAR_4G ||
             _connectionType == DiagnosticsConnectionType::CELLULAR_5G) &&
            _networkProvider != nullptr)
        {
            int16_t csq = _networkProvider->getSignalQuality();
            if (csq == 99 || csq == 0)
                return 0;                    // Unknown or no signal
            return (int32_t)(csq * 2 - 113); // Convert to dBm
        }

#ifdef ZENOPCB_ENABLE_ETHERNET
        if (_connectionType == DiagnosticsConnectionType::ETHERNET)
        {
            // Ethernet khng c RSSI dng link speed lm signal quality
            // 100 Mbps full duplex = -50 (excellent), 10 Mbps = -80 (poor)
            if (ETH.localIP() != IPAddress(0, 0, 0, 0))
            {
                uint8_t speed = ETH.linkSpeed();
                return (speed >= 100) ? -50 : -80;
            }
            return 0;
        }
#endif

        // WiFi RSSI gate as above.
#if defined(ESP32) || defined(ESP8266) || defined(ARDUINO_UNOR4_WIFI) || defined(STM32F1xx)
        if (WiFi.status() == WL_CONNECTED)
        {
            return WiFi.RSSI();
        }
#endif

        return 0; // No connection
    }

    String DiagnosticsCollector::_collectMacAddress()
    {
        // Cellular: use IMEI as unique identifier (modems don't have MAC)
        if ((_connectionType == DiagnosticsConnectionType::CELLULAR_4G ||
             _connectionType == DiagnosticsConnectionType::CELLULAR_5G) &&
            _networkProvider != nullptr)
        {
            String imei = _networkProvider->getMACAddress(); // Returns IMEI
            if (imei.length() > 0)
                return imei;
        }

#ifdef ZENOPCB_ENABLE_ETHERNET
        if (_connectionType == DiagnosticsConnectionType::ETHERNET)
        {
            // ETH.macAddress() tr v String dng "AA:BB:CC:DD:EE:FF"
            return ETH.macAddress();
        }
#endif

        // TU gate: WiFi.macAddress zero-arg overload exists on
        // ESP32 / ESP8266 WiFi.h but not on UNO R4 (Renesas CWifi) or STM32
        // (WiFiEspAT) those expose only the buffer-out variant. Library
        // stays portable; the platform-specific MAC lookup is the HAL's job.
#if defined(ESP32) || defined(ESP8266)
        if (WiFi.status() == WL_CONNECTED)
        {
            return WiFi.macAddress();
        }

        // Fallback: WiFi MAC lun c c trn ESP32 d cha connect
        return WiFi.macAddress();
#else
        // UNO R4 / STM32: HAL system().getMacAddress() should be used.
        return String("00:00:00:00:00:00");
#endif
    }

    uint32_t DiagnosticsCollector::_collectUptime()
    {
        return TimeManager::getUptime();
    }

    uint32_t DiagnosticsCollector::_collectMemoryUsage()
    {
        // Routed through IZenoSystem per HAL-01. getTotalHeap() matches the
        // semantics of ESP.getHeapSize() byte-for-byte (Esp32System impl); the
        // formula is preserved so EDGE-04 freeHeap remains non-zero.
        return _hal.system().getTotalHeap() - _hal.system().getFreeHeap();
    }

    uint32_t DiagnosticsCollector::_collectFreeHeap()
    {
        return _hal.system().getFreeHeap();
    }

} // namespace ZenoPCB

#endif // !ZENOPCB_DISABLE_DIAGNOSTICS
