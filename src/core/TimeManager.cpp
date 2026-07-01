#include "TimeManager.h"
#include "ZenoPCBDebug.h"

namespace ZenoPCB
{

    // Static member initialization
    bool TimeManager::_debugEnabled = true;
    bool TimeManager::_ntpInitialized = false;

    void TimeManager::syncNTP(const char *ntpServer,
                              const char *ntpServer2,
                              const char *ntpServer3,
                              long gmtOffset_sec,
                              int daylightOffset_sec)
    {

        if (_ntpInitialized)
        {
            ZENO_LOG_CORE("NTP already initialized, skipping");
            return;
        }

        ZENO_LOG_CORE("Initializing NTP time sync...");
        ZENO_LOG_CORE("Primary: %s", ntpServer);
        ZENO_LOG_CORE("Secondary: %s", ntpServer2);
        ZENO_LOG_CORE("Tertiary: %s", ntpServer3);
        ZENO_LOG_CORE("GMT Offset: %ld seconds", gmtOffset_sec);

        // configTime is an
        // Espressif-core SNTP extension that is NOT declared on
        // ArduinoCore-renesas (UNO R4) or STM32duino. NTP on those
        // platforms is delegated to their HAL Time sub-impl
        // (UnoR4Time -> WiFiS3 WiFi.getTime(); Stm32Time placeholder
        // pending hardware UAT in). TU guard
        // around the configTime + sntp_set_sync_mode call so the
        // body compiles on UNO R4 / STM32; ESP32 + ESP8266 paths
        // byte-identical.
#if defined(ESP32) || defined(ESP8266)
        // Initialize SNTP
        configTime(gmtOffset_sec, daylightOffset_sec, ntpServer, ntpServer2, ntpServer3);

        // Set sync mode (immediate) ESP32-only
        // ESP8266 lwIP SNTP does not expose sntp_set_sync_mode; configTime()
        // above is sufficient on ESP8266.
#if defined(ESP32)
        sntp_set_sync_mode(SNTP_SYNC_MODE_IMMED);
#endif
#else
        // UNO R4 / STM32: NTP sync owned by the HAL Time sub-impl.
        // Suppress unused-parameter warnings; the entry-shim is not
        // expected to call syncNTP() yet.
        (void)gmtOffset_sec;
        (void)daylightOffset_sec;
        (void)ntpServer;
        (void)ntpServer2;
        (void)ntpServer3;
        ZENO_LOG_CORE("TimeManager::syncNTP no-op on this platform (route NTP through HAL Time sub-impl)");
#endif

        _ntpInitialized = true;

        ZENO_LOG_CORE("NTP sync initiated, waiting for response...");
    }

    bool TimeManager::isSynced()
    {
        time_t now = time(nullptr);
        // Time is synced if it's greater than 100000 (some date in 1970)
        // Unsynced time returns seconds since boot (usually < 100000)
        return (now > 100000);
    }

    bool TimeManager::waitForSync(uint8_t maxWaitSeconds)
    {
        ZENO_LOG_CORE("Waiting for NTP sync (max %d seconds)...", maxWaitSeconds);

        uint32_t startTime = millis();
        uint32_t timeout = maxWaitSeconds * 1000;

        while (!isSynced())
        {
            if (millis() - startTime > timeout)
            {
                ZENO_LOG_CORE("NTP sync timeout after %d seconds", maxWaitSeconds);
                return false;
            }

            delay(100); // Check every 100ms

            // Print progress every second
            if ((millis() - startTime) % 1000 == 0)
            {
                ZENO_LOG_RAW(".");
            }
        }

        time_t now = time(nullptr);
        struct tm timeinfo;
        gmtime_r(&now, &timeinfo);

        ZENO_LOG_RAW("\n");
        ZENO_LOG_CORE("NTP synced! UTC time: %04d-%02d-%02d %02d:%02d:%02d",
                      timeinfo.tm_year + 1900,
                      timeinfo.tm_mon + 1,
                      timeinfo.tm_mday,
                      timeinfo.tm_hour,
                      timeinfo.tm_min,
                      timeinfo.tm_sec);

        return true;
    }

    time_t TimeManager::getUTC()
    {
        return time(nullptr);
    }

    time_t TimeManager::getLocalTime(long timezoneOffsetSeconds)
    {
        return time(nullptr) + timezoneOffsetSeconds;
    }

    uint32_t TimeManager::getUptime()
    {
        return millis() / 1000;
    }

    bool TimeManager::parseTime(const String &timeStr, int &hour, int &minute, int &second)
    {
        // Expected format: "HH:mm:ss" (e.g., "08:30:00")
        if (timeStr.length() != 8)
        {
            return false;
        }

        if (timeStr.charAt(2) != ':' || timeStr.charAt(5) != ':')
        {
            return false;
        }

        // Parse components
        String hourStr = timeStr.substring(0, 2);
        String minStr = timeStr.substring(3, 5);
        String secStr = timeStr.substring(6, 8);

        hour = hourStr.toInt();
        minute = minStr.toInt();
        second = secStr.toInt();

        // Validate ranges
        if (hour < 0 || hour > 23)
            return false;
        if (minute < 0 || minute > 59)
            return false;
        if (second < 0 || second > 59)
            return false;

        return true;
    }

    String TimeManager::formatTime(time_t timestamp, const char *format)
    {
        struct tm timeinfo;
        gmtime_r(&timestamp, &timeinfo);

        char buffer[64];
        strftime(buffer, sizeof(buffer), format, &timeinfo);

        return String(buffer);
    }

    bool TimeManager::getCurrentTimeInfo(struct tm &timeinfo)
    {
        if (!isSynced())
        {
            return false;
        }

        time_t now = time(nullptr);
        gmtime_r(&now, &timeinfo);

        return true;
    }

    void TimeManager::setDebugEnabled(bool enable)
    {
        _debugEnabled = enable;
    }

} // namespace ZenoPCB
