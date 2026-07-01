// TU guard for ZENOPCB_MICRO_BASIC profile.
#if !defined(ZENOPCB_DISABLE_OTA)

#include "ZenoPCBOTA.h"
#include "../core/ZenoPCBDebug.h"
// platform HAL bridge for static rollback helpers (Plan
// 04-05 removes once everything is instance-scoped).
#if defined(ESP32)
  #include "../hal/esp32/Esp32Hal.h"
#elif defined(ESP8266)
  #include "../hal/esp8266/Esp8266Hal.h"
#endif

namespace ZenoPCB
{

    ZenoPCBOTA::ZenoPCBOTA(IZenoHal &hal)
        : _hal(hal),
          _client(nullptr), _status(OTAStatus::IDLE), _progress(0),
          _stream(nullptr), _fileSize(0), _written(0),
          _port(80),
          _connectTimeoutMs(10000), _watchdogTimeoutMs(3600000), // 60 min enough for slow 4G connections
          _otaStartTime(0),
          _progressCallback(nullptr), _completeCallback(nullptr), _errorCallback(nullptr)
    {
    }

    ZenoPCBOTA::~ZenoPCBOTA()
    {
        _cleanup();
    }

    // ============================================
    // Configuration
    // ============================================

    void ZenoPCBOTA::setClient(Client *client)
    {
        _client = client;
    }

    void ZenoPCBOTA::setMD5(const String &md5)
    {
        _expectedMD5 = md5;
    }

    void ZenoPCBOTA::setNewVersion(const String &version)
    {
        _newVersion = version;
    }

    void ZenoPCBOTA::setConnectTimeout(uint32_t timeoutMs)
    {
        _connectTimeoutMs = timeoutMs;
    }

    void ZenoPCBOTA::setWatchdogTimeout(uint32_t timeoutMs)
    {
        _watchdogTimeoutMs = timeoutMs;
    }

    // ============================================
    // Blocking Mode
    // ============================================

    bool ZenoPCBOTA::startOTA(const char *url)
    {
        // Forward-compat capability gate (RESEARCH "Forward-Compat Considerations").
        // ESP32 has CAP_OTA so this branch is never taken; ports without
        // OTA partitions will return false here before any flash work.
        if (!(_hal.capabilities() & IZenoHal::CAP_OTA))
        {
            ZENO_LOG_OTA("OTA not supported on this platform (CAP_OTA missing)");
            return false;
        }

        if (_status == OTAStatus::DOWNLOADING || _status == OTAStatus::FLASHING)
        {
            ZENO_LOG_OTA("OTA already in progress!");
            return false;
        }

        if (!_beginHTTP(url))
            return false;

        _status = OTAStatus::FLASHING;
        ZENO_LOG_OTA("Downloading firmware: %d bytes", _fileSize);

        // MD5 (optional) is folded into begin() per IZenoOTA contract the
        // wrapper internally calls Update.setMD5 before Update.begin so the
        // order is identical to the pre-refactor pair of calls.
        const char *md5 = (_expectedMD5.length() == 32) ? _expectedMD5.c_str() : nullptr;
        if (md5)
        {
            ZENO_LOG_OTA("MD5 verification enabled: %s", md5);
        }
        if (!_hal.ota().begin(_fileSize, md5))
        {
            _setError(OTAError::NOT_ENOUGH_SPACE, "Not enough space for OTA");
            _cleanup();
            return false;
        }

        // Download and flash in chunks (blocking)
        while (_client->connected() && (_written < _fileSize))
        {
            if (_checkWatchdog())
            {
                _hal.ota().abort();
                _cleanup();
                return false;
            }

            int avail = _client->available();
            if (avail <= 0)
            {
                yield(); // Feed WDT without 1ms sleep penalty
                continue;
            }

            int toRead = (avail < (int)sizeof(_buff)) ? avail : (int)sizeof(_buff);
            int len = _client->read(_buff, toRead);
            if (len > 0)
            {
                _written += len;
                if ((size_t)_hal.ota().write(_buff, len) != (size_t)len)
                {
                    _setError(OTAError::WRITE_ERROR, "Error writing to flash");
                    _hal.ota().abort();
                    _cleanup();
                    return false;
                }
                _progress = (_written / (float)_fileSize) * 100.0f;

                // Report progress every ~5%
                static int lastReported = -1;
                int pct = (int)_progress;
                if (pct / 5 != lastReported)
                {
                    lastReported = pct / 5;
                    unsigned long elapsed = (millis() - _otaStartTime) / 1000;
                    ZENO_LOG_OTA("Progress: %.1f%% (%d/%d) %lus elapsed", _progress, _written, _fileSize, elapsed);
                    if (_progressCallback)
                        _progressCallback(_progress);
                }
            }
        }

        // end() commits the partition (true on success); on
        // success we MUST call restart(). Pair kept textually adjacent.
        if (_hal.ota().end()) {
            _status = OTAStatus::COMPLETED;
            ZENO_LOG_OTA("OTA complete! Version: %s Restarting...", _newVersion.c_str());
            if (_completeCallback) _completeCallback(_newVersion);
            _cleanup(); delay(1000); _hal.system().restart();  // [[noreturn]]
        }
        else
        {
            String err = String("OTA end() failed:") + _hal.ota().errorString();
            _setError(OTAError::UPDATE_END_FAILED, err);
            _cleanup();
            return false;
        }
        return true;  // unreachable (restart never returns)
    }

    // ============================================
    // Non-blocking Mode
    // ============================================

    bool ZenoPCBOTA::beginUpdate(const char *url)
    {
        // Forward-compat capability gate (RESEARCH "Forward-Compat Considerations").
        if (!(_hal.capabilities() & IZenoHal::CAP_OTA))
        {
            ZENO_LOG_OTA("OTA not supported on this platform (CAP_OTA missing)");
            return false;
        }

        if (_status == OTAStatus::DOWNLOADING || _status == OTAStatus::FLASHING)
        {
            ZENO_LOG_OTA("OTA already in progress!");
            return false;
        }

        if (!_beginHTTP(url))
            return false;

        _status = OTAStatus::DOWNLOADING;

        // MD5 (optional) folded into begin() same as startOTA path.
        const char *md5 = (_expectedMD5.length() == 32) ? _expectedMD5.c_str() : nullptr;
        if (md5)
        {
            ZENO_LOG_OTA("MD5 verification enabled: %s", md5);
        }
        if (!_hal.ota().begin(_fileSize, md5))
        {
            _setError(OTAError::NOT_ENOUGH_SPACE, "Not enough space for OTA");
            _cleanup();
            return false;
        }

        _stream = _client; // Client* implements Stream
        ZENO_LOG_OTA("Non-blocking OTA started: %d bytes", _fileSize);
        return true;
    }

    float ZenoPCBOTA::loop()
    {
        if (_status != OTAStatus::DOWNLOADING || !_stream)
            return -1;

        // Watchdog check
        if (_checkWatchdog())
        {
            _hal.ota().abort();
            _cleanup();
            return -1;
        }

        // Drain TCP buffer trong ti a 50ms mi ln gi loop()
        // 4G: AT command mt 20-50ms/ln cn t nht 50ms cho 1-2 reads
        // WiFi/ETH: c nhanh hn, 50ms vn OK yield cho MQTT keepalive 15-30s
        unsigned long loopStart = millis();
        const unsigned long MAX_READ_MS = 50; // yield cho MQTT sau 50ms

        while (millis() - loopStart < MAX_READ_MS)
        {
            int avail = _client->available();

            if (avail <= 0)
            {
                // Khng c data, kim tra connection
                if (!_client->connected() && _written >= _fileSize)
                    goto finalize;
                if (!_client->connected())
                {
                    _setError(OTAError::STREAM_ERROR, "Connection lost during OTA");
                    _hal.ota().abort();
                    _cleanup();
                    return -1;
                }
                // SAFE POINT: OTA idle (i data), AT command complete
                // An ton cho MQTT publish (khng interleave AT commands)
                if (_yieldCallback)
                    _yieldCallback();
                // Connected nhng cha c data yield, i ln sau
                return _progress;
            }

            size_t toRead = ((size_t)avail < sizeof(_buff)) ? (size_t)avail : sizeof(_buff);
            int len = _client->read(_buff, toRead);

            if (len < 0)
            {
                _setError(OTAError::STREAM_ERROR, "Stream read error");
                _hal.ota().abort();
                _cleanup();
                return -1;
            }

            if (len == 0)
                continue; // Cha c data, vn trong time budget

            _written += len;
            if ((size_t)_hal.ota().write(_buff, len) != (size_t)len)
            {
                _setError(OTAError::WRITE_ERROR, "Error writing to flash");
                _hal.ota().abort();
                _cleanup();
                return -1;
            }

            _progress = (_written / (float)_fileSize) * 100.0f;

            // Report progress every ~5%
            static int lastReported = -1;
            int pct = (int)_progress;
            if (pct / 5 != lastReported)
            {
                lastReported = pct / 5;
                unsigned long elapsed = (millis() - _otaStartTime) / 1000;
                ZENO_LOG_OTA("Progress: %.1f%% (%d/%d) %lus elapsed", _progress, _written, _fileSize, elapsed);
                if (_progressCallback)
                    _progressCallback(_progress);
            }

            // Download complete
            if (_written >= _fileSize)
                goto finalize;
        }

        // SAFE POINT: Time budget ht, AT command complete
        // An ton cho MQTT publish
        if (_yieldCallback)
            _yieldCallback();
        return _progress;

    finalize:
        // end()/restart() pair same shape as blocking path.
        if (_hal.ota().end())
        {
            _status = OTAStatus::COMPLETED;
            if (_completeCallback) _completeCallback(_newVersion);
            _cleanup(); delay(1000);
            _hal.system().restart();  // [[noreturn]] paired with end() above
        }
        else
        {
            String err = String("OTA end() failed:") + _hal.ota().errorString();
            _setError(OTAError::UPDATE_END_FAILED, err);
            _cleanup();
        }

        return -1;
    }

    // ============================================
    // Cancel
    // ============================================

    void ZenoPCBOTA::cancelUpdate(const String &reason)
    {
        if (!isInProgress())
        {
            ZENO_LOG_OTA("Cancel requested but no OTA in progress");
            return;
        }

        ZENO_LOG_OTA("OTA cancelled: %s (progress: %.1f%%)", reason.c_str(), _progress);
        _hal.ota().abort();
        _cleanup();
        _setError(OTAError::STREAM_ERROR, reason);
    }

    // ============================================
    // Rollback (static uses getXxxHal bridge until)
    // ============================================

    bool ZenoPCBOTA::canRollBack()
    {
#if defined(ESP32)
        return getEsp32Hal().ota().canRollBack();
#elif defined(ESP8266)
        return getEsp8266Hal().ota().canRollBack();
#else
        return false;
#endif
    }

    bool ZenoPCBOTA::rollBack()
    {
#if defined(ESP32)
        IZenoHal &hal = getEsp32Hal();
#elif defined(ESP8266)
        IZenoHal &hal = getEsp8266Hal();
#else
        // TU gate: UNO R4 / STM32 have no rollback infrastructure
        // (single-slot OTA on Renesas, no OTA on STM32 in this scope).
        // Earlier the body referenced 'hal' without a fallback declaration,
        // which slipped past ESP32 -fpermissive but trips strict toolchains.
        ZENO_LOG_OTA("Rollback not supported on this platform");
        return false;
#endif
#if defined(ESP32) || defined(ESP8266)
        if (!hal.ota().canRollBack())
        {
            ZENO_LOG_OTA("Rollback not available (no previous firmware)");
            return false;
        }

        ZENO_LOG_OTA("Rolling back to previous firmware...");
        if (hal.ota().rollBack())
        {
            ZENO_LOG_OTA("Rollback successful Restarting...");
            delay(1000);
            hal.system().restart();
            return true; // unreachable (restart is [[noreturn]])
        }
        else
        {
            ZENO_LOG_OTA("Rollback failed!");
            return false;
        }
#endif
    }

    // ============================================
    // Internal Methods
    // ============================================

    bool ZenoPCBOTA::_parseUrl(const char *url)
    {
        String urlStr(url);

        // Remove http:// prefix
        if (urlStr.startsWith("http://"))
        {
            urlStr = urlStr.substring(7);
        }
        else if (urlStr.startsWith("https://"))
        {
            // HTTPS not supported with raw Client*
            _setError(OTAError::HTTP_CONNECT_FAILED, "HTTPS not supported use HTTP");
            return false;
        }

        // Split host:port and path
        int pathIdx = urlStr.indexOf('/');
        String hostPort;
        if (pathIdx > 0)
        {
            hostPort = urlStr.substring(0, pathIdx);
            _path = urlStr.substring(pathIdx);
        }
        else
        {
            hostPort = urlStr;
            _path = "/";
        }

        // Parse port
        int colonIdx = hostPort.indexOf(':');
        if (colonIdx > 0)
        {
            _host = hostPort.substring(0, colonIdx);
            _port = hostPort.substring(colonIdx + 1).toInt();
        }
        else
        {
            _host = hostPort;
            _port = 80;
        }

        if (_host.length() == 0)
        {
            _setError(OTAError::HTTP_CONNECT_FAILED, "Invalid URL: empty host");
            return false;
        }

        return true;
    }

    bool ZenoPCBOTA::_beginHTTP(const char *url)
    {
        if (!_client)
        {
            _setError(OTAError::NO_CLIENT, "No network client set (call setClient first)");
            return false;
        }

        if (!_parseUrl(url))
            return false;

        _progress = 0;
        _written = 0;
        _fileSize = 0;
        _lastError = "";
        _otaStartTime = millis();

        // === HTTP request with redirect following (max 5 redirects) ===
        const int MAX_REDIRECTS = 5;
        for (int redirectCount = 0; redirectCount <= MAX_REDIRECTS; redirectCount++)
        {
            ZENO_LOG_OTA("Connecting to %s:%d%s%s", _host.c_str(), _port, maskUrl(String(_path)).c_str(),
                         redirectCount > 0 ? "(redirect)" : "");

            // Set timeout
            _client->setTimeout(_connectTimeoutMs / 1000);

            if (!_client->connect(_host.c_str(), _port))
            {
                _setError(OTAError::HTTP_CONNECT_FAILED, String("Connect failed:") + _host + ":" + String(_port));
                return false;
            }

            // Send HTTP GET request
            // HTTP/1.0 QUAN TRNG cho 4G OTA:
            // - HTTP/1.1 cho php Transfer-Encoding: chunked chunk framing ln vo firmware data
            // - Cellular proxy thng thm chunked/gzip vo HTTP/1.1 response
            // - HTTP/1.0 buc server tr raw data + Connection: close an ton cho binary download
            _client->print(String("GET") + _path + "HTTP/1.0\r\n" +
                           "Host:" + _host + "\r\n" +
                           "Accept-Encoding: identity\r\n" +
                           "Cache-Control: no-transform\r\n" +
                           "Connection: close\r\n\r\n");

            // Wait for response headers
            unsigned long headerStart = millis();
            while (_client->connected() && !_client->available())
            {
                if (millis() - headerStart > _connectTimeoutMs)
                {
                    _setError(OTAError::TIMEOUT, "Timeout waiting for HTTP response");
                    _client->stop();
                    return false;
                }
                yield();
            }

            // Read HTTP status line
            String statusLine = _client->readStringUntil('\n');
            statusLine.trim();

            // Parse HTTP status code (e.g. "HTTP/1.1 200 OK")
            int httpCode = 0;
            int spaceIdx = statusLine.indexOf(' ');
            if (spaceIdx > 0)
            {
                httpCode = statusLine.substring(spaceIdx + 1).toInt();
            }

            // === Handle redirects (301, 302, 307, 308) ===
            if (httpCode == 301 || httpCode == 302 || httpCode == 307 || httpCode == 308)
            {
                String location = "";
                // Read headers to find Location
                while (_client->connected())
                {
                    String line = _client->readStringUntil('\n');
                    line.trim();
                    if (line.length() == 0)
                        break;
                    // Case-insensitive Location header
                    if (line.startsWith("Location:") || line.startsWith("location:"))
                    {
                        location = line.substring(line.indexOf(':') + 1);
                        location.trim();
                    }
                }
                _client->stop();

                if (location.length() == 0)
                {
                    _setError(OTAError::HTTP_ERROR, "Redirect" + String(httpCode) + "but no Location header");
                    return false;
                }

                ZENO_LOG_OTA("HTTP %d redirect %s", httpCode, location.c_str());

                // Check if redirect is to HTTPS (not supported)
                if (location.startsWith("https://"))
                {
                    _setError(OTAError::HTTP_ERROR,
                              "Server redirected to HTTPS which is not supported for OTA."
                              "Use direct HTTP URL or configure server to serve without redirect.");
                    return false;
                }

                // Parse new URL from Location (could be absolute or relative)
                if (location.startsWith("http://"))
                {
                    // Absolute URL re-parse completely
                    if (!_parseUrl(location.c_str()))
                        return false;
                }
                else if (location.startsWith("/"))
                {
                    // Relative path same host
                    _path = location;
                }
                else
                {
                    _setError(OTAError::HTTP_ERROR, "Unsupported redirect Location:" + location);
                    return false;
                }

                continue; // Retry with new URL
            }

            // === Non-redirect: must be 200 OK ===
            if (httpCode != 200)
            {
                String err = "HTTP error:" + String(httpCode) + "(" + statusLine + ")";
                _setError(OTAError::HTTP_ERROR, err);
                _client->stop();
                return false;
            }

            // Read headers to get Content-Length
            _fileSize = -1;
            bool isChunked = false;
            while (_client->connected())
            {
                String line = _client->readStringUntil('\n');
                line.trim();

                if (line.length() == 0)
                    break; // End of headers

                if (line.startsWith("Content-Length:") || line.startsWith("content-length:"))
                {
                    _fileSize = line.substring(line.indexOf(':') + 1).toInt();
                }

                // Detect chunked encoding firmware binary s b corrupt nu chunked
                String lineLower = line;
                lineLower.toLowerCase();
                if (lineLower.indexOf("transfer-encoding") >= 0 && lineLower.indexOf("chunked") >= 0)
                {
                    isChunked = true;
                }
            }

            if (isChunked)
            {
                _setError(OTAError::HTTP_ERROR,
                          "Server sent Transfer-Encoding: chunked not supported for OTA binary download."
                          "Use HTTP/1.0 or configure server to send Content-Length only.");
                _client->stop();
                return false;
            }

            if (_fileSize <= 0)
            {
                _setError(OTAError::HTTP_ERROR, "Invalid Content-Length:" + String(_fileSize));
                _client->stop();
                return false;
            }

            ZENO_LOG_OTA("HTTP OK File size: %d bytes", _fileSize);
            return true;
        }

        // Exhausted redirect limit
        _setError(OTAError::HTTP_ERROR, "Too many redirects (max" + String(MAX_REDIRECTS) + ")");
        return false;
    }

    void ZenoPCBOTA::_cleanup()
    {
        if (_client && _client->connected())
            _client->stop();
        _stream = nullptr;
        _fileSize = 0;
        _written = 0;
        if (_status != OTAStatus::COMPLETED && _status != OTAStatus::ERROR)
            _status = OTAStatus::IDLE;
    }

    void ZenoPCBOTA::_setError(OTAError error, const String &message)
    {
        _status = OTAStatus::ERROR;
        _lastError = message;
        ZENO_LOG_OTA("ERROR [%s]: %s", _errorToString(error), message.c_str());
        if (_errorCallback)
            _errorCallback(error, message);
    }

    bool ZenoPCBOTA::_checkWatchdog()
    {
        if (_watchdogTimeoutMs > 0 && (millis() - _otaStartTime) > _watchdogTimeoutMs)
        {
            _setError(OTAError::TIMEOUT, "OTA watchdog timeout (" + String(_watchdogTimeoutMs / 1000) + "s)");
            return true;
        }
        return false;
    }

    const char *ZenoPCBOTA::_errorToString(OTAError error)
    {
        switch (error)
        {
        case OTAError::NONE:
            return "NONE";
        case OTAError::ALREADY_IN_PROGRESS:
            return "ALREADY_IN_PROGRESS";
        case OTAError::HTTP_CONNECT_FAILED:
            return "HTTP_CONNECT_FAILED";
        case OTAError::HTTP_ERROR:
            return "HTTP_ERROR";
        case OTAError::NOT_ENOUGH_SPACE:
            return "NOT_ENOUGH_SPACE";
        case OTAError::WRITE_ERROR:
            return "WRITE_ERROR";
        case OTAError::STREAM_ERROR:
            return "STREAM_ERROR";
        case OTAError::MD5_MISMATCH:
            return "MD5_MISMATCH";
        case OTAError::UPDATE_END_FAILED:
            return "UPDATE_END_FAILED";
        case OTAError::TIMEOUT:
            return "TIMEOUT";
        case OTAError::NO_CLIENT:
            return "NO_CLIENT";
        default:
            return "UNKNOWN";
        }
    }

} // namespace ZenoPCB

#endif // !ZENOPCB_DISABLE_OTA
