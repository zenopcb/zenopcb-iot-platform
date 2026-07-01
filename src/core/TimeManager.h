#ifndef ZENOPCB_TIME_MANAGER_H
#define ZENOPCB_TIME_MANAGER_H

#include <Arduino.h>
#include <time.h>
// (follow-up) `<sys/time.h>` is incomplete
// on Renesas (UNO R4) toolchain: `struct timeval` is forward-declared
// without a definition, so any TU that includes TimeManager.h fails to
// compile downstream. ESP32 / ESP8266 / STM32 newlib all provide a
// complete `<sys/time.h>` keep it included there to preserve the
// `configTime()` call path inside TimeManager.cpp.
#if defined(ESP32) || defined(ESP8266) || defined(STM32F1xx) || defined(STM32F4xx)
  #include <sys/time.h>
#endif
// esp_sntp.h is an ESP-IDF header that does
// not exist on ESP8266. ESP8266 lwIP SNTP works via configTime() alone;
// no equivalent to sntp_set_sync_mode() is needed because the ESP8266
// SNTP backend defaults to immediate sync semantics.
#if defined(ESP32)
  #include "esp_sntp.h"
#endif

namespace ZenoPCB
{

    /**
     * @brief Time Manager for NTP synchronization and UTC time management
     *
     * This class handles NTP synchronization on WiFi connection and maintains
     * UTC time for schedule execution. After initial sync, ESP32 RTC maintains
     * time automatically without re-querying NTP servers.
     *
     * @note All times are in UTC. Use gmtime() not localtime() for schedule comparison.
     */
    class TimeManager
    {
    public:
        /**
         * @brief Synchronize time with NTP server
         *
         * Initializes SNTP and syncs with NTP server. This should be called
         * once after WiFi connection. ESP32 RTC will maintain time afterwards.
         *
         * @param ntpServer Primary NTP server (default: "pool.ntp.org")
         * @param ntpServer2 Secondary NTP server (optional)
         * @param ntpServer3 Tertiary NTP server (optional)
         * @param gmtOffset_sec GMT offset in seconds (default: 0 for UTC)
         * @param daylightOffset_sec Daylight saving offset (default: 0)
         */
        static void syncNTP(const char *ntpServer = "pool.ntp.org",
                            const char *ntpServer2 = "time.google.com",
                            const char *ntpServer3 = "time.cloudflare.com",
                            long gmtOffset_sec = 0,
                            int daylightOffset_sec = 0);

        /**
         * @brief Check if NTP time has been synchronized
         *
         * @return true if time is synced (time > 100000 seconds since epoch)
         * @return false if time not synced yet
         */
        static bool isSynced();

        /**
         * @brief Wait for NTP synchronization (blocking)
         *
         * Blocks up to maxWaitSeconds waiting for NTP sync to complete.
         * Useful during startup to ensure schedules have valid time.
         *
         * @param maxWaitSeconds Maximum seconds to wait (default: 10)
         * @return true if synced within timeout
         * @return false if timeout without sync
         */
        static bool waitForSync(uint8_t maxWaitSeconds = 10);

        /**
         * @brief Get current UTC time
         *
         * @return time_t UTC timestamp (seconds since Unix epoch)
         * @note Returns 0 if not synced yet. Always check isSynced() first!
         */
        static time_t getUTC();

        /**
         * @brief Get current time with timezone offset
         *
         * @param timezoneOffsetSeconds Offset from UTC in seconds (e.g., 25200 for GMT+7)
         * @return time_t Local timestamp
         */
        static time_t getLocalTime(long timezoneOffsetSeconds);

        /**
         * @brief Get system uptime in seconds
         *
         * @return uint32_t Uptime since boot (millis() / 1000)
         */
        static uint32_t getUptime();

        /**
         * @brief Parse time string "HH:mm:ss" to hour, minute, second
         *
         * @param timeStr Time string in format "HH:mm:ss" (e.g., "08:30:00")
         * @param hour Output hour (0-23)
         * @param minute Output minute (0-59)
         * @param second Output second (0-59)
         * @return true if parsed successfully
         * @return false if invalid format
         */
        static bool parseTime(const String &timeStr, int &hour, int &minute, int &second);

        /**
         * @brief Get formatted time string
         *
         * @param timestamp Unix timestamp
         * @param format Format string (strftime format)
         * @return String Formatted time string
         */
        static String formatTime(time_t timestamp, const char *format = "%Y-%m-%d %H:%M:%S");

        /**
         * @brief Get current time info (UTC)
         *
         * @param timeinfo Output tm structure
         * @return true if time is synced
         * @return false if not synced
         */
        static bool getCurrentTimeInfo(struct tm &timeinfo);

        /**
         * @brief Enable debug logging for time sync
         *
         * @param enable true to enable debug logs
         */
        static void setDebugEnabled(bool enable);

    private:
        static bool _debugEnabled;
        static bool _ntpInitialized;
    };

} // namespace ZenoPCB

#endif // ZENOPCB_TIME_MANAGER_H
