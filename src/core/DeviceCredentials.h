#ifndef DEVICE_CREDENTIALS_H
#define DEVICE_CREDENTIALS_H

#include <Arduino.h>
#include "../hal/IZenoHal.h"

namespace ZenoPCB
{
    /**
     * @brief Device Credentials Manager
     *
     * Quản lý DEVICE_ID và TOKEN (32 ký tự):
     * - Khai báo 1 lần trong main
     * - Tự động lưu vào NVS
     * - Đọc từ NVS khi khởi động
     *
     * Plan 04-03 — NVS access routes through `IZenoNVS` via constructor
     * injection. The legacy default-constructor is retained as a thin
     * wrapper that uses the canonical ESP32 HAL singleton — preserves
     * source compatibility for existing main.cpp / zf01_main.cpp callers
     * (`DeviceCredentials credentials;`).
     *
     * Usage:
     * ```cpp
     * DeviceCredentials creds;        // uses getEsp32Hal()
     * creds.begin();
     *
     * if (!creds.isProvisioned()) {
     *     creds.provision("ABCD1234...", "TOKEN1234...");
     * }
     *
     * // Don't println(getToken()) — it identifies the device on the
     * // cloud and lets anyone reading the serial log impersonate it.
     * ```
     *
     * NVS namespace + keys preserved byte-for-byte from pre-refactor
     * code (T-4-02 — see 04-03-AUDIT.md §1.2).
     */
    class DeviceCredentials
    {
    public:
        static constexpr size_t CREDENTIAL_LENGTH_MIN = 32;
        static constexpr size_t CREDENTIAL_LENGTH_MAX = 36; // UUID format with dashes
        static constexpr const char *NVS_NAMESPACE = "zeno_creds";

        /**
         * @brief Construct with explicit HAL reference.
         */
        explicit DeviceCredentials(IZenoHal &hal);

        /**
         * @brief Default constructor — uses the ESP32 HAL singleton.
         *
         * Retained for backward compatibility (existing `DeviceCredentials
         * credentials;` declarations in main.cpp / zf01_main.cpp continue
         * to work without code change).
         */
        DeviceCredentials();

        ~DeviceCredentials();

        /**
         * @brief Initialize credentials manager
         * Đọc credentials từ NVS nếu đã có
         * @return true if credentials exist in NVS
         */
        bool begin();

        /**
         * @brief Check if device has been provisioned
         * @return true if both deviceId and token are set
         */
        bool isProvisioned() const;

        /**
         * @brief Provision device with new credentials
         * Lưu vào NVS và memory
         * @param deviceId 32-character device ID
         * @param token 32-character token
         * @return true if saved successfully
         */
        bool provision(const char *deviceId, const char *token);

        /**
         * @brief Get device ID
         * @return 32-character device ID or empty string
         */
        String getDeviceId() const { return _deviceId; }

        /**
         * @brief Get token
         * @return 32-character token or empty string
         */
        String getToken() const { return _token; }

        /**
         * @brief Clear all credentials from NVS
         */
        void clear();

        /**
         * @brief Validate credential format
         * @param credential String to validate
         * @return true if exactly 32 characters
         */
        static bool isValidCredential(const String &credential);

    private:
        IZenoHal &_hal;
        String _deviceId;
        String _token;
        bool _provisioned;

        bool _loadFromNVS();
        bool _saveToNVS();
    };

} // namespace ZenoPCB

#endif // DEVICE_CREDENTIALS_H
