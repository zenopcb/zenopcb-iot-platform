#ifndef ZENOPCB_OTA_H
#define ZENOPCB_OTA_H

#include <Arduino.h>
#include <Client.h>
#include <functional>

#include "../hal/IZenoHal.h"

namespace ZenoPCB
{

    /**
     * @brief OTA update status codes
     */
    enum class OTAStatus : uint8_t
    {
        IDLE,        // No OTA in progress
        DOWNLOADING, // Downloading firmware
        FLASHING,    // Writing to flash
        VERIFYING,   // Verifying firmware
        COMPLETED,   // OTA completed, pending restart
        ERROR        // OTA failed
    };

    /**
     * @brief OTA error codes
     */
    enum class OTAError : uint8_t
    {
        NONE,
        ALREADY_IN_PROGRESS,
        HTTP_CONNECT_FAILED,
        HTTP_ERROR,
        NOT_ENOUGH_SPACE,
        WRITE_ERROR,
        STREAM_ERROR,
        MD5_MISMATCH,
        UPDATE_END_FAILED,
        TIMEOUT,
        NO_CLIENT
    };

    /**
     * @brief Callback types for OTA events
     */
    using OTAProgressCallback = std::function<void(float progress)>;
    using OTACompleteCallback = std::function<void(const String &version)>;
    using OTAErrorCallback = std::function<void(OTAError error, const String &message)>;
    using OTAYieldCallback = std::function<void()>; // Called at safe points (OTA idle, safe for MQTT)

    /**
     * @brief ZenoPCBOTA — OTA firmware update for all network types
     *
     * Supports WiFi, Ethernet (W5500), 4G (A7680C/SIM7600) via
     * generic Client* from ZenoNetworkProvider::getClient().
     *
     * Two modes:
     * - Blocking: startOTA(url) — downloads + flashes in one call
     * - Non-blocking: beginUpdate(url) + loop() — 2 bytes/tick
     *
     * Features:
     * - MD5 hash verification
     * - HTTP timeout (30s connect)
     * - Auto rollback on boot failure
     * - ZENO_LOG_OTA debug macros
     */
    class ZenoPCBOTA
    {
    public:
        /**
         * @brief Construct ZenoPCBOTA with an injected HAL.
         * @param hal Hardware abstraction layer (provides ota() + system()).
         *            Defaults to the canonical ESP32 HAL singleton via the
         *            inline default in ZenoPCBOTA.cpp (bridge — Plan 04-05
         *            will swap callers to pass `_hal` explicitly).
         */
        ZenoPCBOTA(IZenoHal &hal);
        ~ZenoPCBOTA();

        // ============================================
        // Configuration
        // ============================================

        /**
         * @brief Set network client for HTTP download
         * @param client Client* from network provider (WiFi/ETH/4G)
         */
        void setClient(Client *client);

        /**
         * @brief Set expected MD5 hash for firmware verification
         * @param md5 MD5 hex string (32 chars)
         */
        void setMD5(const String &md5);

        /**
         * @brief Set new firmware version string (for reporting)
         * @param version Version string
         */
        void setNewVersion(const String &version);

        /**
         * @brief Set HTTP connect timeout
         * @param timeoutMs Timeout in milliseconds (default: 10000)
         */
        void setConnectTimeout(uint32_t timeoutMs);

        /**
         * @brief Set overall OTA watchdog timeout
         * @param timeoutMs Timeout in milliseconds (default: 300000 = 5min)
         */
        void setWatchdogTimeout(uint32_t timeoutMs);

        // ============================================
        // Blocking Mode (download + flash in one call)
        // ============================================

        /**
         * @brief Start blocking OTA update (blocks until complete or error)
         * @param url Firmware download URL (HTTP only)
         * @return true if update completed successfully (will restart)
         */
        bool startOTA(const char *url);

        // ============================================
        // Non-blocking Mode (call loop() each tick)
        // ============================================

        /**
         * @brief Begin non-blocking OTA update
         * @param url Firmware download URL (HTTP only)
         * @return true if download started successfully
         */
        bool beginUpdate(const char *url);

        /**
         * @brief Process non-blocking OTA (call from main loop)
         * @return Current progress (0-100), or -1 if not active
         */
        float loop();

        // ============================================
        // Cancel
        // ============================================

        /**
         * @brief Cancel OTA in progress (abort download + flash)
         * @param reason Reason string for error callback
         */
        void cancelUpdate(const String &reason = "Cancelled by user");

        // ============================================
        // Status
        // ============================================

        bool isInProgress() const { return _status == OTAStatus::DOWNLOADING || _status == OTAStatus::FLASHING; }
        OTAStatus getStatus() const { return _status; }
        float getProgress() const { return _progress; }
        String getErrorMessage() const { return _lastError; }
        const String &getNewVersion() const { return _newVersion; }

        // ============================================
        // Rollback
        // ============================================

        /**
         * @brief Check if rollback to previous firmware is possible
         * @return true if previous firmware partition is valid
         */
        static bool canRollBack();

        /**
         * @brief Roll back to previous firmware
         * @return true if rollback initiated (will restart)
         */
        static bool rollBack();

        // ============================================
        // Callbacks
        // ============================================

        void onProgress(OTAProgressCallback callback) { _progressCallback = callback; }
        void onComplete(OTACompleteCallback callback) { _completeCallback = callback; }
        void onError(OTAErrorCallback callback) { _errorCallback = callback; }
        void onYield(OTAYieldCallback callback) { _yieldCallback = callback; }

    private:
        IZenoHal &_hal;
        Client *_client;

        // State
        OTAStatus _status;
        float _progress;
        String _lastError;

        // Non-blocking state
        Stream *_stream; // points to _client during download
        int _fileSize;
        int _written;
        uint8_t _buff[4096]; // read buffer — 128 bytes, tối ưu cho 4G AT command (ít roundtrip)

        // HTTP parsing
        String _host;
        uint16_t _port;
        String _path;

        // Configuration
        String _expectedMD5;
        String _newVersion;
        uint32_t _connectTimeoutMs;
        uint32_t _watchdogTimeoutMs;
        unsigned long _otaStartTime;

        // Callbacks
        OTAProgressCallback _progressCallback;
        OTACompleteCallback _completeCallback;
        OTAErrorCallback _errorCallback;
        OTAYieldCallback _yieldCallback;

        // Internal
        bool _beginHTTP(const char *url);
        bool _parseUrl(const char *url);
        void _cleanup();
        void _setError(OTAError error, const String &message);
        bool _checkWatchdog();

        static const char *_errorToString(OTAError error);
    };

} // namespace ZenoPCB

#endif // ZENOPCB_OTA_H
