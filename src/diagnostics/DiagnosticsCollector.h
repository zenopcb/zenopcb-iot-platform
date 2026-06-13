#ifndef ZENOPCB_DIAGNOSTICS_COLLECTOR_H
#define ZENOPCB_DIAGNOSTICS_COLLECTOR_H

#include "DiagnosticsTypes.h"
#include "../core/ZenoPCBTypes.h"
#include "../core/TimeManager.h"
#include "../core/ZenoNetworkProvider.h"
#include "../hal/IZenoHal.h"
// Plan 06-03 Pitfall 5 — platform-specific WiFi header.
// Phase 7 Plan 07-06 — switch extended with UNO R4 + STM32 arms.
// F1 MICRO profile drops Diagnostics entirely (D-12); the class still
// compiles on STM32F1 builds but Pattern F enableDiagnostics() short-circuit
// (deferred to Plan 07-07) prevents instantiation when CAP_DIAGNOSTICS=0.
#if defined(ESP32)
  #include <WiFi.h>
#elif defined(ESP8266)
  #include <ESP8266WiFi.h>
#elif defined(ARDUINO_UNOR4_WIFI)
  #include <WiFiS3.h>
#elif defined(STM32F4)
  // F4 default = Ethernet (no WiFi.h equivalent on STM32duino Ethernet path).
#elif defined(STM32F1)
  #include <WiFiEspAT.h>
#endif

namespace ZenoPCB
{

    /**
     * @brief Collects system diagnostics data from various sources
     *
     * Gathers data from:
     * - WiFi/Ethernet: IP, RSSI, MAC address
     * - ESP System: Heap memory, uptime
     * - Device Info: Firmware version
     * - Configuration: Connection type (set by app)
     */
    class DiagnosticsCollector
    {
    public:
        /**
         * @brief Construct a new DiagnosticsCollector
         *
         * @param hal Hardware abstraction layer (for IZenoSystem heap stats)
         * @param deviceInfo Pointer to device information
         */
        DiagnosticsCollector(IZenoHal &hal, const DeviceInfo *deviceInfo);

        /**
         * @brief Set network provider for 4G/Ethernet diagnostics data
         *
         * @param provider Pointer to network provider (Zeno4GProvider, etc.)
         */
        void setNetworkProvider(ZenoNetworkProvider *provider);

        /**
         * @brief Set connection type (configured by mobile app)
         *
         * @param type Connection type string ("WIFI", "ETHERNET", "4G", "5G")
         */
        void setConnectionType(const String &type);

        /**
         * @brief Set connection type enum
         *
         * @param type Connection type enum
         */
        void setConnectionType(DiagnosticsConnectionType type);

        /**
         * @brief Get current connection type
         *
         * @return DiagnosticsConnectionType Current connection type
         */
        DiagnosticsConnectionType getConnectionType() const;

        /**
         * @brief Collect all diagnostics data
         *
         * @param requestId Optional request ID for on-demand response (nullptr for passive)
         * @return DiagnosticsData Complete diagnostics data
         */
        DiagnosticsData collect(const char *requestId = nullptr);

    private:
        IZenoHal &_hal;
        const DeviceInfo *_deviceInfo;
        DiagnosticsConnectionType _connectionType;
        ZenoNetworkProvider *_networkProvider;

        // Collect specific data types
        String _collectDeviceIp();
        int32_t _collectSignalStrength();
        String _collectMacAddress();
        uint32_t _collectUptime();
        uint32_t _collectMemoryUsage();
        uint32_t _collectFreeHeap();
    };

} // namespace ZenoPCB

#endif // ZENOPCB_DIAGNOSTICS_COLLECTOR_H
