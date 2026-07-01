/**
 * @file Zeno4GProvider.h
 * @brief Network provider for 4G/LTE cellular modules (SIM7600, A7670, SIM800, etc.)
 *
 * Implements ZenoNetworkProvider interface for cellular connectivity
 * via TinyGSM library. Supports auto-reconnect and APN configuration.
 *
 * @note Requires build flags:
 *   -DZENOPCB_ENABLE_CELLULAR
 *   -DTINY_GSM_MODEM_SIM7600  (or SIM800, A7670, EC200, etc.)
 * @note Requires lib_deps:
 *   vshymanskyy/TinyGSM @ ^0.11.5
 *
 * @author ZenoPCB Team
 */

#pragma once

#ifdef ZENOPCB_ENABLE_CELLULAR

// ============================================
// AT Command Dialog Logging
// Enable with: -DZENOPCB_DEBUG_4G=1
// Prints every AT command exchange to Serial
// ============================================
#ifndef ZENOPCB_DEBUG_4G
#define ZENOPCB_DEBUG_4G 0
#endif

#if ZENOPCB_DEBUG_4G
#define TINY_GSM_DEBUG Serial
#include "../vendor/StreamDebugger/StreamDebugger.h"
#endif

// TinyGSM vendored no external lib_deps required
#include "../vendor/TinyGSM/TinyGsmClient.h"
#include "../core/ZenoNetworkProvider.h"
#include "../core/ZenoPCBTypes.h"
#include "../core/ZenoPCBDebug.h"
#include <sys/time.h> // settimeofday() for modem NTP ESP32 RTC

namespace ZenoPCB
{

    /**
     * @brief 4G/LTE cellular network provider
     *
     * Usage:
     * @code
     * Zeno4GProvider cellProvider(17, 16, 4);      // TX, RX, PWR (no reset line)
     * Zeno4GProvider cellProvider(17, 16, 4, 5);   // TX, RX, PWR, RST
     * zeno.setNetworkProvider(&cellProvider).begin();
     * @endcode
     */
    class Zeno4GProvider : public ZenoNetworkProvider
    {
    public:
        /**
         * @brief Constructor
         * @param txPin  ESP32 TX  Modem RX (default: GPIO 17)
         * @param rxPin  ESP32 RX  Modem TX (default: GPIO 16)
         * @param pwrPin Modem power key pin (-1 = not used)
         * @param rstPin Modem reset pin (-1 = not used)
         * @param baudRate Serial baud rate (default: 115200)
         */
        Zeno4GProvider(uint8_t txPin = 17, uint8_t rxPin = 16,
                       int8_t pwrPin = -1, int8_t rstPin = -1,
                       uint32_t baudRate = 115200)
            : _txPin(txPin), _rxPin(rxPin), _pwrPin(pwrPin), _rstPin(rstPin),
#if ZENOPCB_DEBUG_4G
              _baudRate(baudRate), _debugger(Serial2, Serial), _modem(_debugger), _gsmClient(_modem), _gsmOTAClient(_modem, 1), // _debugger is ZenoStreamDebugger
#else
              _baudRate(baudRate), _modem(Serial2), _gsmClient(_modem), _gsmOTAClient(_modem, 1),
#endif
              _initialized(false), _connected(false), _initFailed(false),
              _initRetryCount(0), _lastInitRetry(0),
              _lastReconnectAttempt(0),
              _lastConnectCheck(0), _consecutiveFails(0),
              _reconnectIntervalMs(30000)
        {
        }

        bool begin(const DeviceConfig &config) override
        {
            // Store config for retry
            _savedConfig = config;

            ZENO_LOG("4G", "Initializing modem TX=%d, RX=%d, PWR=%d",
                     _txPin, _rxPin, _pwrPin);

            // Store APN config
            _apn = config.cellularAPN;
            _gprsUser = config.cellularUser;
            _gprsPass = config.cellularPass;

            if (_apn.length() == 0)
            {
                _apn = "internet"; // Default APN
                ZENO_LOG("4G", "No APN configured, using default: %s", _apn.c_str());
            }

            _pulseResetPin();

            // Hardware POWERKEY cycle (same as working reference project)
            // This is the ONLY reliable way to init A7680C / SIM7600-family modems.
            // DO NOT call modem.restart() / modem.init() before this sequence.
            if (_pwrPin >= 0)
            {
                pinMode(_pwrPin, OUTPUT);
                digitalWrite(_pwrPin, LOW); // Start LOW

                // Step 1: Power OFF (hold HIGH 2500 ms)
                ZENO_LOG("4G", "Powering OFF modem (PWRKEY pin %d)...", _pwrPin);
                digitalWrite(_pwrPin, HIGH);
                delay(2500);
                digitalWrite(_pwrPin, LOW);

                // Step 2: Wait for shutdown
                ZENO_LOG("4G", "Waiting for shutdown...");
                delay(5000);
            }

            // Step 3: (Re)start Serial2
            ZENO_LOG("4G", "Starting Serial2 RX=%d TX=%d baud=%d", _rxPin, _txPin, _baudRate);
            Serial2.end();
            delay(500);
            Serial2.begin(_baudRate, SERIAL_8N1, _rxPin, _txPin);
            delay(1000);

            if (_pwrPin >= 0)
            {
                // Step 4: Power ON (hold HIGH 700 ms)
                ZENO_LOG("4G", "Powering ON modem...");
                digitalWrite(_pwrPin, HIGH);
                delay(700);
                digitalWrite(_pwrPin, LOW);

                // Step 5: Wait for boot (12 s)
                ZENO_LOG("4G", "Waiting for modem boot (12 s)...");
                delay(12000);

                // Step 6: Probe with bare AT
                Serial2.println("AT");
                delay(1000);
                if (Serial2.available())
                {
                    ZENO_LOG("4G", "Modem responding to AT commands");
                    while (Serial2.available())
                        Serial2.read(); // flush
                }
                else
                {
                    ZENO_LOG("4G", "WARNING: modem not responding yet continuing anyway");
                }
            }
            else
            {
                // No PWRKEY pin: just wait for modem to stabilise
                delay(3000);
            }

            // Log modem info
            String modemInfo = _modem.getModemInfo();
            ZENO_LOG("4G", "Modem: %s", modemInfo.c_str());

            // Bail out early if modem is not responding at all
            if (modemInfo.length() == 0)
            {
                ZENO_LOG("4G", "ERROR: Modem not responding check power / pins");
                _shutdownHardware();
                return false;
            }

            // Check SIM status before blocking on waitForNetwork
            // Without this, waitForNetwork(60s) blocks the entire boot when no SIM is inserted.
            SimStatus simStatus = _modem.getSimStatus();
            if (simStatus != SimStatus::SIM_READY)
            {
                ZENO_LOG("4G", "ERROR: SIM not ready (status=%d) no SIM card or card error?",
                         (int)simStatus);
                _shutdownHardware();
                return false;
            }
            ZENO_LOG("4G", "SIM OK");

            // Enable network registration notifications (like reference project)
            ZENO_LOG("4G", "Enabling network registration notifications...");
            Serial2.println("AT+CGREG=1");
            delay(500);
            while (Serial2.available())
                Serial2.read(); // flush buffer

            // Wait for network registration
            ZENO_LOG("4G", "Waiting for network...");
            if (!_modem.waitForNetwork(60000))
            {
                ZENO_LOG("4G", "Network registration failed (60s timeout)");
                return false;
            }
            ZENO_LOG("4G", "Network registered");

            // Check signal quality before GPRS (critical for 4G)
            int csq = _modem.getSignalQuality();
            ZENO_LOG("4G", "Signal quality: %d CSQ", csq);
            if (csq == 0)
            {
                ZENO_LOG("4G", "ERROR: No signal detected");
                return false;
            }
            if (csq < 10)
            {
                ZENO_LOG("4G", "WARNING: Poor signal (CSQ < 10), connection may be unstable");
            }

            // Connect GPRS
            if (_connectGPRS())
            {
                _initialized = true;
                _initFailed = false;
                _initRetryCount = 0;
                return true;
            }

            // GPRS failed but modem + SIM OK allow loop() to retry
            _initialized = true;
            _initFailed = false;
            _initRetryCount = 0;
            return false;
        }

        void loop() override
        {
            // Skip reconnection when paused (e.g. during AP provisioning mode)
            if (_paused)
                return;

            // ============================================
            // Init-retry: begin() failed (no SIM / no modem)
            // Periodically try to re-initialize
            // - Retries 1-5: every 60s
            // - Retries 6-10: every 120s
            // - Retries 11+: every 300s (5 min)
            // ============================================
            if (!_initialized && _initFailed)
            {
                unsigned long now = millis();
                uint32_t retryInterval;
                if (_initRetryCount < 5)
                    retryInterval = 60000; // 60s
                else if (_initRetryCount < 10)
                    retryInterval = 120000; // 2 min
                else
                    retryInterval = 300000; // 5 min

                if (now - _lastInitRetry >= retryInterval)
                {
                    _lastInitRetry = now;
                    _initRetryCount++;
                    ZENO_LOG("4G", "Init retry #%d (interval %ds) checking modem + SIM...",
                             _initRetryCount, retryInterval / 1000);
                    begin(_savedConfig); // Will set _initialized=true on success
                }
                return;
            }

            // Skip if begin() never called
            if (!_initialized)
                return;

            unsigned long now = millis();

            // isGprsConnected() is an AT command (~200-500ms) throttle to every 10s
            if (now - _lastConnectCheck < 10000)
                return;
            _lastConnectCheck = now;

            bool nowConnected = _modem.isGprsConnected();

            if (!nowConnected && _connected)
            {
                ZENO_LOG("4G", "GPRS connection lost");
                _connected = false;
            }

            if (!nowConnected)
            {
                if (now - _lastReconnectAttempt >= _reconnectIntervalMs)
                {
                    _lastReconnectAttempt = now;
                    ZENO_LOG("4G", "Attempting GPRS reconnect (fail streak: %d)...", _consecutiveFails);

                    // Restart modem after 5 consecutive failures
                    if (_consecutiveFails >= 5)
                    {
                        ZENO_LOG("4G", "5 consecutive failures restarting modem...");
                        _consecutiveFails = 0;
                        _restartModem();
                        return; // Wait for next interval after restart
                    }

                    _connectGPRS();
                }
            }
            else
            {
                _consecutiveFails = 0; // Reset on confirmed connection
            }
        }

        bool isConnected() const override
        {
            return _connected;
        }

        Client *getClient() override
        {
            return &_gsmClient;
        }

        Client *getOTAClient() override
        {
            // OTA dng mux 1 ring MQTT vn gi mux 0 khng b kill
            return &_gsmOTAClient;
        }

        String getLocalIP() const override
        {
            if (!_connected)
                return "0.0.0.0";
            String ip = _modem.getLocalIP();
            // TinyGSM on some firmware returns raw AT response e.g. "+IPADDR: 1.2.3.4"
            // Strip any prefix before the actual IP digits
            int idx = ip.lastIndexOf(' ');
            if (idx >= 0)
                ip = ip.substring(idx + 1);
            ip.trim();
            return ip;
        }

        const char *getName() const override
        {
            return "4G";
        }

        /**
         * @brief Get signal quality (0-31, 99=unknown)
         */
        int16_t getSignalQuality() const override
        {
            return _modem.getSignalQuality();
        }

        /**
         * @brief Get network operator name (e.g. "Viettel", "Mobifone")
         * @return Empty string if not registered
         */
        String getOperator() const override
        {
            return _modem.getOperator();
        }

        /**
         * @brief Get network type as string ("LTE", "3G", "2G", "Unknown")
         */
        String getNetworkType() const override
        {
            // A7672x/A7680C is LTE-only modem
            // SIM7600: check getNetworkMode() (only available on SIM7600 driver)
#ifdef TINY_GSM_MODEM_SIM7600
            uint8_t mode = _modem.getNetworkMode();
            if (mode == 13 || mode == 14 || mode == 38)
                return "LTE";
            if (mode == 2)
                return "AUTO";
            return "3G/2G";
#else
            return "LTE";
#endif
        }

        /**
         * @brief Get modem IMEI  unique hardware identifier for cellular
         */
        String getModemIMEI() const override
        {
            return _modem.getIMEI();
        }

        /**
         * @brief Get MAC address equivalent  IMEI for cellular modules
         * Cellular modems don't have MAC addresses, IMEI is the unique ID
         */
        String getMACAddress() const override
        {
            return _modem.getIMEI();
        }

        /**
         * @brief Set GPRS reconnect interval
         * @param ms Reconnect interval in milliseconds (default: 30000)
         */
        void setReconnectInterval(uint32_t ms)
        {
            _reconnectIntervalMs = ms;
        }

        /**
         * @brief Sync time via modem NTP (AT+CNTP) and set ESP32 RTC
         *
         * ESP32's configTime()/SNTP uses lwIP stack which only works over
         * WiFi/Ethernet. Over 4G, internet goes through TinyGSM modem so
         * SNTP packets never reach NTP servers. This method uses the modem's
         * built-in NTP command (AT+CNTP) to sync time, then sets ESP32 RTC
         * via settimeofday() so time(nullptr) returns correct UTC time.
         *
         * @return true if NTP sync + RTC set succeeded
         */
        bool syncTime() override
        {
            ZENO_LOG("4G", "Reading time from SIM module (AT+CCLK?)...");

            // Read modem clock directly carrier pushes time via NITZ
            // when modem registers on network. No NTP needed!
            // Format: "YY/MM/DD,HH:MM:SSTZ" (TZ in quarter-hours)
            String dateTime = _modem.getGSMDateTime(DATE_FULL);
            ZENO_LOG("4G", "Modem clock: %s", dateTime.c_str());

            if (dateTime.length() < 17)
            {
                ZENO_LOG("4G", "Modem clock not set (NITZ not received yet)");
                return false;
            }

            // Parse "YY/MM/DD,HH:MM:SSTZ"
            int year = 0, month = 0, day = 0, hour = 0, minute = 0, second = 0;
            char tzSign = '+';
            int tzQuarters = 0;
            // Try full parse with timezone
            int parsed = sscanf(dateTime.c_str(), "%d/%d/%d,%d:%d:%d%c%d",
                                &year, &month, &day, &hour, &minute, &second,
                                &tzSign, &tzQuarters);
            if (parsed < 6)
            {
                ZENO_LOG("4G", "Failed to parse modem time: %s", dateTime.c_str());
                return false;
            }

            // Sanity check modem returns 80/01/06 before NITZ sync
            if (year < 24)
            {
                ZENO_LOG("4G", "Modem time not valid yet (year=%d)", year);
                return false;
            }

            // Build struct tm in LOCAL time as reported by modem
            struct tm tm = {};
            tm.tm_year = (year < 100) ? (year + 100) : (year - 1900);
            tm.tm_mon = month - 1;
            tm.tm_mday = day;
            tm.tm_hour = hour;
            tm.tm_min = minute;
            tm.tm_sec = second;
            tm.tm_isdst = 0;

            // Convert to UTC by subtracting timezone offset
            // TZ is in quarter-hours (e.g. +28 = GMT+7)
            time_t localTime = mktime(&tm);
            int tzOffsetSec = tzQuarters * 15 * 60;
            if (tzSign == '-')
                tzOffsetSec = -tzOffsetSec;
            time_t utcTime = localTime - tzOffsetSec;

            // Set ESP32 RTC
            struct timeval tv = {.tv_sec = utcTime, .tv_usec = 0};
            settimeofday(&tv, nullptr);

            // Verify
            time_t now = time(nullptr);
            struct tm verify;
            gmtime_r(&now, &verify);
            ZENO_LOG("4G", "ESP32 RTC set UTC: %04d-%02d-%02d %02d:%02d:%02d (TZ:%c%d)",
                     verify.tm_year + 1900, verify.tm_mon + 1, verify.tm_mday,
                     verify.tm_hour, verify.tm_min, verify.tm_sec,
                     tzSign, tzQuarters);
            return true;
        }

    private:
        bool _connectGPRS()
        {
            // Check SIM first if no SIM, bail immediately (no blocking AT calls)
            SimStatus simStatus = _modem.getSimStatus();
            if (simStatus != SimStatus::SIM_READY)
            {
                ZENO_LOG("4G", "No SIM / SIM error (status=%d) skipping GPRS", (int)simStatus);
                _consecutiveFails++;
                return false;
            }

            // Check network registration before attempting GPRS
            if (!_modem.isNetworkConnected())
            {
                ZENO_LOG("4G", "Not on network waiting for registration (15s)...");
                if (!_modem.waitForNetwork(15000))
                {
                    ZENO_LOG("4G", "Network registration failed");
                    _consecutiveFails++;
                    return false;
                }
            }

            ZENO_LOG("4G", "Connecting GPRS APN: %s", _apn.c_str());
            if (!_modem.gprsConnect(_apn.c_str(), _gprsUser.c_str(), _gprsPass.c_str()))
            {
                ZENO_LOG("4G", "GPRS connect failed!");
                _connected = false;
                _consecutiveFails++;
                return false;
            }

            // Wait for network stability before MQTT (critical!)
            ZENO_LOG("4G", "GPRS connected, waiting for stability (3s)...");
            delay(3000); // Let carrier routing tables update

            _connected = true;
            _consecutiveFails = 0;
            ZENO_LOG("4G", "GPRS ready for MQTT!");
            ZENO_LOG("4G", "IP : %s", getLocalIP().c_str()); // uses parsed IP
            ZENO_LOG("4G", "Signal : %d CSQ", _modem.getSignalQuality());
            ZENO_LOG("4G", "Operator: %s", _modem.getOperator().c_str());
            return true;
        }

        static constexpr uint32_t RESET_PULSE_MS = 200;
        static constexpr uint32_t POST_RESET_WAIT_MS = 5000;

        void _pulseResetPin()
        {
            if (_rstPin < 0)
                return;
            pinMode(_rstPin, OUTPUT);
            digitalWrite(_rstPin, LOW);
            delay(RESET_PULSE_MS);
            digitalWrite(_rstPin, HIGH);
            pinMode(_rstPin, INPUT);
        }

        void _restartModem()
        {
            ZENO_LOG("4G", "Hardware-resetting modem...");
            _connected = false;

            _pulseResetPin();

            if (_pwrPin >= 0)
            {
                // Power OFF
                digitalWrite(_pwrPin, HIGH);
                delay(2500);
                digitalWrite(_pwrPin, LOW);
                delay(5000);

                // Restart Serial2
                Serial2.end();
                delay(500);
                Serial2.begin(_baudRate, SERIAL_8N1, _rxPin, _txPin);
                delay(1000);

                // Power ON
                digitalWrite(_pwrPin, HIGH);
                delay(700);
                digitalWrite(_pwrPin, LOW);

                // Wait for boot
                ZENO_LOG("4G", "Waiting for modem boot (12 s)...");
                delay(12000);

                // Probe
                Serial2.println("AT");
                delay(1000);
                while (Serial2.available())
                    Serial2.read();
            }
            else if (_rstPin >= 0)
            {
                delay(POST_RESET_WAIT_MS);
                Serial2.end();
                delay(500);
                Serial2.begin(_baudRate, SERIAL_8N1, _rxPin, _txPin);
                delay(1000);
            }
            else
            {
                _modem.restart();
                delay(5000);
            }

            if (_modem.waitForNetwork(30000))
                _connectGPRS();
            else
                ZENO_LOG("4G", "Modem restart: network not found");
        }

        uint8_t _txPin;
        uint8_t _rxPin;
        int8_t _pwrPin;
        int8_t _rstPin;
        uint32_t _baudRate;

#if ZENOPCB_DEBUG_4G
        ZenoStreamDebugger _debugger;
#endif
        mutable TinyGsm _modem;
        TinyGsmClient _gsmClient;    // MQTT mux 0
        TinyGsmClient _gsmOTAClient; // OTA mux 1 ring, khng nh hng MQTT

        /**
         * @brief Shutdown modem hardware  release Serial2 + hold PWRKEY LOW
         * Called when begin() fails (no modem / no SIM) to free resources
         * and prevent loop() from sending AT commands into the void.
         */
        void _shutdownHardware()
        {
            ZENO_LOG("4G", "Shutting down modem hardware (cleanup after init failure)");

            // Power OFF modem if PWRKEY is available
            if (_pwrPin >= 0)
            {
                digitalWrite(_pwrPin, HIGH);
                delay(2500);
                digitalWrite(_pwrPin, LOW);
                ZENO_LOG("4G", "PWRKEY held LOW modem powered off");
            }

            // Release Serial2 so other peripherals can use it
            Serial2.end();
            ZENO_LOG("4G", "Serial2 released");

            _connected = false;
            _initFailed = true; // loop() will attempt re-init on timer
            // _initialized stays false
        }

        bool _initialized;
        bool _connected;
        bool _initFailed;        // begin() was called but failed retry in loop()
        uint8_t _initRetryCount; // Number of re-init attempts
        unsigned long _lastInitRetry;
        unsigned long _lastReconnectAttempt;
        unsigned long _lastConnectCheck;
        uint8_t _consecutiveFails;
        uint32_t _reconnectIntervalMs;

        String _apn;
        String _gprsUser;
        String _gprsPass;
        DeviceConfig _savedConfig; // Stored for retry
    };

} // namespace ZenoPCB

#endif // ZENOPCB_ENABLE_CELLULAR
