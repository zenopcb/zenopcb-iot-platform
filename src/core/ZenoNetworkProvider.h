/**
 * @file ZenoNetworkProvider.h
 * @brief Abstract interface for pluggable network providers
 *
 * Cho phép thư viện bên ngoài (Zeno4G, ZenoEthernetW5500, ZenoMultiConnect)
 * cung cấp kết nối mạng cho ZenoPCB mà core library không phụ thuộc
 * vào bất kỳ hardware driver nào.
 *
 * User tạo provider, rồi truyền vào Zeno class:
 * @code
 * #include <ZenoEthernetW5500.h>
 * ZenoEthernetProvider ethProvider(5, 26);
 * zeno.setNetworkProvider(&ethProvider).begin();
 * @endcode
 *
 * @author ZenoPCB Team
 */

#pragma once

#include <Arduino.h>
#include <Client.h>

namespace ZenoPCB
{

    // Forward declaration — full definition in ZenoPCBTypes.h
    struct DeviceConfig;

    /**
     * @brief Abstract base class for all network providers
     *
     * Implement this interface khi tạo network provider mới.
     * Mỗi provider cung cấp:
     * - begin()       → khởi tạo hardware + kết nối mạng
     * - loop()        → bảo trì kết nối (DHCP, GPRS reconnect, ...)
     * - isConnected() → kiểm tra trạng thái
     * - getClient()   → trả về Client* cho MQTT sử dụng
     */
    class ZenoNetworkProvider
    {
    public:
        virtual ~ZenoNetworkProvider() = default;

        /**
         * @brief Khởi tạo network hardware và kết nối
         *
         * Được gọi từ Zeno::begin() sau khi DeviceConfig đã load từ NVS.
         * Provider đọc config liên quan (APN, IP tĩnh, DHCP, ...) từ đây.
         *
         * @param config DeviceConfig chứa thông tin từ NVS provisioning
         * @return true nếu hardware OK (không nhất thiết phải connected ngay)
         */
        virtual bool begin(const DeviceConfig &config) = 0;

        /**
         * @brief Bảo trì kết nối — gọi từ Zeno::loop() mỗi vòng lặp
         *
         * Thực hiện: DHCP renewal, GPRS reconnect, link check, v.v.
         */
        virtual void loop() = 0;

        /**
         * @brief Kiểm tra kết nối mạng có sẵn sàng không
         * @return true nếu có link + IP hợp lệ
         */
        virtual bool isConnected() const = 0;

        /**
         * @brief Lấy Client* cho MQTT sử dụng
         *
         * Client* phải tồn tại suốt lifetime của provider.
         * MQTT sẽ dùng pointer này trực tiếp.
         *
         * @return Pointer đến Arduino Client (EthernetClient, TinyGsmClient, ...)
         */
        virtual Client *getClient() = 0;

        /**
         * @brief Lấy Client* riêng cho OTA (không ảnh hưởng MQTT)
         *
         * OTA và MQTT phải dùng TCP connection ĐỘC LẬP.
         * Nếu chia sẻ cùng 1 Client*: OTA connect() sẽ kill TCP của MQTT.
         *
         * Default: return getClient() (fallback cho provider chưa override).
         * Các provider có dedicated OTA client (Ethernet, 4G) nên override.
         *
         * @return Pointer đến Arduino Client riêng cho OTA
         */
        virtual Client *getOTAClient() { return getClient(); }

        /**
         * @brief Lấy địa chỉ IP hiện tại
         * @return IP dưới dạng String, "0.0.0.0" nếu chưa kết nối
         */
        virtual String getLocalIP() const = 0;

        /**
         * @brief Tên provider cho logging
         * @return Chuỗi tĩnh, ví dụ: "Ethernet", "4G", "MultiConnect"
         */
        virtual const char *getName() const = 0;

        /**
         * @brief Đồng bộ thời gian qua network provider
         *
         * Cho phép provider tự sync time bằng cách riêng (ví dụ: modem NTP
         * qua AT+CNTP cho 4G). Nếu thành công, provider set ESP32 RTC
         * bằng settimeofday() và return true.
         *
         * Default: return false (provider không hỗ trợ, dùng configTime fallback).
         *
         * @return true nếu sync thành công và ESP32 RTC đã được set
         * @return false nếu provider không hỗ trợ hoặc sync thất bại
         */
        virtual bool syncTime() { return false; }

        // ============================================
        // Diagnostics support — override in providers
        // ============================================

        /** @brief Signal quality (WiFi: RSSI dBm, 4G: CSQ 0-31, Ethernet: link speed proxy) */
        virtual int16_t getSignalQuality() const { return 0; }

        /** @brief Network operator name (4G only, e.g. "Viettel") */
        virtual String getOperator() const { return ""; }

        /** @brief Network type string ("LTE", "3G", "WiFi", "Ethernet") */
        virtual String getNetworkType() const { return ""; }

        /** @brief Modem IMEI — unique hardware ID for cellular modules */
        virtual String getModemIMEI() const { return ""; }

        /** @brief MAC address or equivalent unique ID (IMEI for 4G) */
        virtual String getMACAddress() const { return ""; }

        // ============================================
        // Pause support — pause reconnection during AP mode
        // ============================================

        /** @brief Pause network maintenance (reconnect, etc.) */
        void setPaused(bool paused) { _paused = paused; }

        /** @brief Check if provider is paused */
        bool isPaused() const { return _paused; }

    protected:
        bool _paused = false;
    };

} // namespace ZenoPCB
