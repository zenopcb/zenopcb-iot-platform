#ifndef ZENOPCB_H
#define ZENOPCB_H

/**
 * @file ZenoPCB.h
 * @brief Main entry point for ZenoPCB IoT Library
 *
 * ZenoPCB IoT Library - Fluent API for ESP32/ESP8266 IoT devices
 *
 * @example
 * Zeno zeno;
 * zeno.wifi("SSID", "password")
 *     .onConnected([]() { Serial.println("Connected!"); })
 *     .begin();
 *
 * void loop() {
 *     zeno.loop();
 * }
 */

// ============================================================================
// Auto MICRO_BASIC profile for tight-flash MCUs.
//
// STM32F103 Blue Pill (C8) has 64 KB Flash. The full library (provisioning,
// storage, schedule, alarm, OTA, diagnostics) overflows by ~70 KB. Auto-disable
// the optional modules so the library links by default on F103. Users with
// F103CB (128 KB) or who explicitly want a module can opt back in by defining
// `ZENOPCB_MICRO_BASIC_OFF` in their build flags.
// ============================================================================
#if defined(STM32F1xx) && !defined(ZENOPCB_MICRO_BASIC_OFF)
  #ifndef ZENOPCB_DISABLE_PROVISIONING
    #define ZENOPCB_DISABLE_PROVISIONING
  #endif
  #ifndef ZENOPCB_DISABLE_DIAGNOSTICS
    #define ZENOPCB_DISABLE_DIAGNOSTICS
  #endif
  #ifndef ZENOPCB_DISABLE_SCHEDULE
    #define ZENOPCB_DISABLE_SCHEDULE
  #endif
  #ifndef ZENOPCB_DISABLE_ALARM
    #define ZENOPCB_DISABLE_ALARM
  #endif
  #ifndef ZENOPCB_DISABLE_OTA
    #define ZENOPCB_DISABLE_OTA
  #endif
  #ifndef ZENOPCB_DISABLE_STORAGE
    #define ZENOPCB_DISABLE_STORAGE
  #endif
#endif

// ============================================================================
// Build hint — emitted at every ESP32 compile so users who hit the linker
// "text section exceeds available space in board" / "Sketch too big" error
// can scroll up in the IDE log and see what to do. Silence with
// `-DZENOPCB_SILENCE_HINTS` once the partition scheme is configured.
// ============================================================================
#if defined(ESP32) && !defined(ZENOPCB_SILENCE_HINTS)
#  pragma message("[ZenoPCB] ESP32 build: full library is ~1.3 MB. If linker reports 'text section exceeds available space' / 'Sketch too big', change Arduino IDE Tools > Partition Scheme to 'Minimal SPIFFS (1.9MB APP with OTA)' or larger ('Huge APP 3MB No OTA' for no-OTA builds). PlatformIO: board_build.partitions = min_spiffs.csv. To slim ~126 KB more, add -DZENOPCB_DISABLE_SCHEDULE -DZENOPCB_DISABLE_ALARM -DZENOPCB_DISABLE_DIAGNOSTICS -DZENOPCB_DISABLE_OTA -DZENOPCB_DISABLE_PROVISIONING. Silence this hint with -DZENOPCB_SILENCE_HINTS.")
#endif

#include <Arduino.h>
// Phase 7 Plan 07-06 — WiFi.h switch extended to 4 platforms. Phase 7 ports
// (UNO R4 WiFi, STM32) integrate alongside existing ESP32 + ESP8266 path. F4
// default Ethernet path has no WiFi.h equivalent (handled via STM32Ethernet
// inside the network provider layer).
#if defined(ESP32)
  #include <WiFi.h>
#elif defined(ESP8266)
  #include <ESP8266WiFi.h>
#elif defined(ARDUINO_UNOR4_WIFI)
  #include <WiFiS3.h>
#elif defined(STM32F4xx)
  // F4 may use WiFiEspAT (override -DZENOPCB_NET=esp-at) OR Ethernet (default).
  // No WiFi.h equivalent on default F4 Ethernet path.
#elif defined(STM32F1xx)
  // Default MICRO_BASIC profile auto-disables provisioning on F103, so the
  // WiFiEspAT external library only needs to be installed when the user
  // explicitly opts back in via -DZENOPCB_MICRO_BASIC_OFF.
  #if !defined(ZENOPCB_DISABLE_PROVISIONING)
    #include <WiFiEspAT.h>
  #endif
#endif

// Optional TLS/SSL support - Define ZENOPCB_ENABLE_TLS to enable (adds ~150KB Flash)
#ifdef ZENOPCB_ENABLE_TLS
#include <WiFiClientSecure.h>
#endif

#include <functional>
#include "hal/IZenoHal.h"
// Plan 06-03 Pattern 1 — platform-specific HAL singleton + ZENOPCB_DEFAULT_HAL()
// macro switch for Zeno ctor default-arg. Keeps T-4-03 backward-compat
// (`Zeno zeno;` still resolves to getEsp32Hal() on ESP32 builds).
#if defined(ESP32)
  #include "hal/esp32/Esp32Hal.h"
  #define ZENOPCB_DEFAULT_HAL() ZenoPCB::getEsp32Hal()
#elif defined(ESP8266)
  #include "hal/esp8266/Esp8266Hal.h"
  #define ZENOPCB_DEFAULT_HAL() ZenoPCB::getEsp8266Hal()
#elif defined(ARDUINO_UNOR4_WIFI)
  #include "hal/unor4/UnoR4Hal.h"
  #define ZENOPCB_DEFAULT_HAL() ZenoPCB::getUnoR4Hal()
#elif defined(STM32F1xx) || defined(STM32F4xx)
  #include "hal/stm32/Stm32Hal.h"
  #define ZENOPCB_DEFAULT_HAL() ZenoPCB::getStm32Hal()
#else
  #error "Unsupported platform — ZenoPCB v0.3.0 supports ESP32, ESP8266, UNO R4 WiFi, STM32 F1/F4"
#endif
#include "core/ZenoPCBTypes.h"
#include "core/ZKeyTypes.h"
#include "core/ZKeyBuffer.h"
#include "core/TimeManager.h"
#include "wifi/WiFiProvisioning.h"
#include "mqtt/ZenoPCBMQTT.h"

// Network Provider interface (abstract — no hardware dependencies)
#include "core/ZenoNetworkProvider.h"

// Plan 06-03 Pattern 3 — guard ESP32-only subsystem includes (D-03 module
// strip). Network providers (W5500 native ETH.h + TinyGSM cellular) +
// irrigation stay ESP32-exclusive on the source-include surface so the
// ESP8266 link does not pull in unresolved Modbus/Ethernet/4G symbols.
#if defined(ESP32)
  // Optional: concrete providers (guarded by their own #ifdef)
  #include "network/ZenoEthernetProvider.h"
  #include "network/Zeno4GProvider.h"
  #include "network/ZenoMultiConnectProvider.h"
  #include "irrigation/IrrigationTypes.h"
  #include "irrigation/IrrigationStorage.h"
  #include "irrigation/IrrigationExecutor.h"
  #include "irrigation/IrrigationScheduler.h"
  #include "irrigation/IrrigationMessageHandler.h"
#endif

#include "diagnostics/ZenoPCBDiagnostics.h"   // platform-neutral (uses IZenoHal)
#include "storage/ConfigMessageHandler.h"     // platform-neutral (uses IZenoStorage)
#include "storage/DataMonitorMessageHandler.h"
#include "schedule/ScheduleMessageHandler.h"
#include "schedule/ScheduleExecutor.h"
#include "alarm/AlarmEngine.h"                // platform-neutral
#include "ota/ZenoPCBOTA.h"                   // platform-neutral (uses IZenoOTA)

namespace ZenoPCB
{

    /**
     * @brief Main ZenoPCB class - Single entry point for all IoT features
     *
     * Provides fluent API for:
     * - WiFi configuration with AP mode provisioning
     * - MQTT communication (future)
     * - OTA updates (future)
     * - Sensor management (future)
     */
    class Zeno
    {
    public:
        /**
         * @brief Construct a Zeno instance with a Hardware Abstraction Layer.
         *
         * Default argument uses the canonical ESP32 HAL singleton
         * (`getEsp32Hal()`), so existing user code `Zeno zeno;` continues
         * to compile and behave identically (T-4-03 backward-compat
         * invariant). Phase 6/7 ports can pass an `Esp8266Hal` / `UnoR4Hal`
         * / `Stm32Hal` instance instead.
         *
         * The HAL singleton is a Meyers function-local static; first call
         * constructs, subsequent calls return the same reference (zero
         * overhead beyond an indirect call).
         */
        explicit Zeno(IZenoHal &hal = ZENOPCB_DEFAULT_HAL());
        ~Zeno();

        // ============================================
        // Fluent Configuration - WiFi
        // ============================================

        /**
         * @brief Configure WiFi credentials
         * @param ssid WiFi SSID
         * @param password WiFi password
         * @return Reference to this for method chaining
         */
        Zeno &wifi(const char *ssid, const char *password);

        /**
         * @brief Configure WiFi with provisioning support
         * @param buttonPin Button pin to trigger AP mode (default: 0)
         * @param holdTimeMs Hold time in ms to trigger AP mode (default: 3000)
         * @return Reference to this for method chaining
         */
        Zeno &wifiProvisioning(uint8_t buttonPin = 0, uint32_t holdTimeMs = 3000);

        /**
         * @brief Pattern G (Phase 7 D-06) fallible captive-portal start.
         *
         * Distinct overload from the existing `Zeno& wifiProvisioning(uint8_t,
         * uint32_t)` builder (D-07 preserved): different parameter types =
         * unambiguous overload resolution, different return type = caller is
         * forced to check the outcome.
         *
         * Returns `ZenoCapability::Unavailable` (with a single `ZENO_LOG_CORE`
         * warning) if `hal.capabilities() & IZenoHal::CAP_CAPTIVE_PORTAL == 0`
         * — true on STM32 (no AP-mode hardware) and any Phase 7 port whose
         * platform lacks captive-portal support. Otherwise delegates to the
         * underlying `WiFiProvisioning::startAPMode()` (Plan 07-06 refines the
         * delegation surface for non-ESP32 platforms).
         *
         * @param apSsid     SSID broadcast in AP mode.
         * @param apPassword Optional WPA2 passphrase (empty = open AP).
         * @return ZenoCapability::OK on success, Unavailable if platform
         *         lacks CAP_CAPTIVE_PORTAL, Error on runtime failure.
         */
        ZenoCapability wifiProvisioning(const char *apSsid, const char *apPassword = "");

        /**
         * @brief Set AP SSID prefix for provisioning mode
         * @param prefix SSID prefix (default: "ZENO-")
         * @return Reference to this for method chaining
         */
        Zeno &apPrefix(const char *prefix);

        /**
         * @brief Set AP password for provisioning mode
         * @param password AP password (default: "zenopcb12345")
         * @return Reference to this for method chaining
         */
        Zeno &apPassword(const char *password);

        /**
         * @brief Set AP mode timeout
         * @param timeoutMs Timeout in ms (default: 300000 = 5 minutes)
         * @return Reference to this for method chaining
         */
        Zeno &apTimeout(uint32_t timeoutMs);

        // ============================================
        // Fluent Configuration - Network Provider
        // ============================================

        /**
         * @brief Set external network provider (Ethernet, 4G, MultiConnect...)
         *
         * Provider cung cấp kết nối mạng thay thế WiFi.
         * Object phải tồn tại suốt lifetime của ứng dụng (khai báo global).
         *
         * @param provider Pointer to network provider (caller owns lifetime)
         * @return Reference to this for method chaining
         *
         * @example
         * // Ethernet W5500 (requires -DZENOPCB_ENABLE_ETHERNET)
         * ZenoEthernetProvider ethProvider(5, 26);
         * zeno.setNetworkProvider(&ethProvider).begin();
         *
         * // 4G/LTE (requires -DZENOPCB_ENABLE_CELLULAR)
         * Zeno4GProvider cellProvider(17, 16, 4);
         * zeno.setNetworkProvider(&cellProvider).begin();
         *
         * // Multi-connect failover
         * ZenoMultiConnectProvider multiProvider;
         * multiProvider.addProvider(&ethProvider);
         * multiProvider.addProvider(&cellProvider);
         * zeno.setNetworkProvider(&multiProvider).begin();
         */
        Zeno &setNetworkProvider(ZenoNetworkProvider *provider);

        /**
         * @brief Set LED pin for status indicator (blinks in AP mode)
         * @param pin LED GPIO pin (-1 to disable, default: -1)
         * @param activeHigh true if LED is active HIGH, false if active LOW (default: true)
         * @param blinkInterval Blink interval in ms (default: 200)
         * @return Reference to this for method chaining
         */
        Zeno &statusLED(int8_t pin);

        // ============================================
        // Fluent Configuration - Device
        // ============================================

        /**
         * @brief Set device ID
         * @param id Device unique identifier
         * @return Reference to this for method chaining
         */
        Zeno &device(const char *id);

        /**
         * @brief Set device ID + access token in one call (fluent overload)
         * @param id    Device unique identifier
         * @param token Provisioned device access token (used as MQTT username)
         * @return Reference to this for method chaining
         *
         * Convenience overload that mirrors `setDeviceCredentials(id, token)` for
         * use inside the fluent builder chain — the canonical 6 OSS example
         * sketches (`lib/ZenoPCB/examples/`) document this two-argument form
         * (`.device(DEVICE_ID, DEVICE_TOKEN)`) as the primary onboarding API.
         */
        Zeno &device(const char *id, const char *token);

        /**
         * @brief Set device name
         * @param name Human-readable device name
         * @return Reference to this for method chaining
         */
        Zeno &deviceName(const char *name);

        /**
         * @brief Set device information (type, model name, version, supported connections)
         * @param type Device type enum
         * @param typeName Human-readable type name (e.g., "Sensor Hub")
         * @param modelName Device model/product name
         * @param version Firmware version string
         * @param manufacturer Manufacturer name (default: "ZenoPCB")
         * @param supportedConn Supported connection types using bitmask flags
         *                      (default: CONN_WIFI, use CONN_WIFI | CONN_ETHERNET | CONN_CELLULAR for multiple)
         * @return Reference to this for method chaining
         *
         * @example
         * // Single connection
         * .deviceInfo(DeviceType::SENSOR_HUB, "Sensor Hub", "SH-01", "1.0.0", "ZenoPCB", CONN_WIFI)
         *
         * // Multiple connections
         * .deviceInfo(DeviceType::GATEWAY, "Gateway", "GW-01", "1.0.0", "ZenoPCB",
         *             CONN_WIFI | CONN_ETHERNET | CONN_CELLULAR)
         */
        Zeno &deviceInfo(DeviceType type, const char *typeName, const char *modelName,
                         const char *version, const char *manufacturer = "ZenoPCB",
                         ConnectionFlags supportedConn = CONN_WIFI);

        // ============================================
        // Fluent Configuration - MQTT
        // ============================================

        /**
         * @brief Configure MQTT broker
         * @param broker MQTT broker address
         * @param port MQTT port (default: 1883)
         * @return Reference to this for method chaining
         */
        Zeno &mqtt(const char *broker, uint16_t port = 1883);

        /**
         * @brief Configure MQTT broker with TLS
         * @param broker MQTT broker address
         * @param port MQTT port (default: 8883)
         * @return Reference to this for method chaining
         * @note TLS support is optional. Enable with: zeno.enableTLS() before calling this
         */
        Zeno &mqttTLS(const char *broker, uint16_t port = 8883);

        /**
         * @brief Enable TLS/SSL support (adds ~150KB Flash when used)
         * @return Reference to this for method chaining
         * @note Call this BEFORE .mqttTLS() to enable TLS support
         *
         * @example
         * zeno.enableTLS()
         *     .mqttTLS("<broker>", 8883);
         */
        Zeno &enableTLS();

        /**
         * @brief Pin a PEM-encoded root CA for MQTT TLS connections (Phase 7 D-27).
         *
         * Optional builder-style configuration: when `-DZENOPCB_ENABLE_TLS` is
         * enabled AND the user calls `setRootCA(pem)` before `begin()`, the
         * underlying `WiFiClientSecure` is initialised with the supplied PEM
         * buffer via `setCACert()` (ESP32) — replacing the default
         * `setInsecure()` dev-mode fallback. When TLS is NOT opt-in via
         * build-flag, this method is a silent no-op (Pattern F).
         *
         * @note ESP8266 BearSSL `setTrustAnchors()` requires a parsed
         *       `BearSSL::X509List` object, not a raw PEM `const char*`. For
         *       v0.3.0 the ESP8266 path logs a `[WARN]` and falls back to
         *       insecure mode; a future plan will author the BearSSL X509List
         *       allocation path.
         *
         * @param pemBuffer  Pointer to a null-terminated PEM-encoded root CA
         *                   chain. Caller owns the buffer for the Zeno
         *                   instance lifetime. NO root CA is shipped with the
         *                   library — user provides at deploy time.
         * @return Reference to this for method chaining.
         *
         * @example
         *   static const char ROOT_CA[] PROGMEM = R"(-----BEGIN CERTIFICATE-----
         *   ...
         *   -----END CERTIFICATE-----)";
         *   zeno.enableTLS()
         *       .setRootCA(ROOT_CA)
         *       .mqttTLS("<broker>", 8883);
         */
        Zeno &setRootCA(const char *pemBuffer);

        /**
         * @brief Enable storage for connection configurations
         * @return Reference to this for method chaining
         *
         * This enables the LittleFS-based storage system for managing
         * connection configurations received via MQTT.
         *
         * @example
         * zeno.enableStorage()
         *     .onConfigCreated([](const ConnectionConfig& cfg) { ... })
         *     .onConfigUpdated([](const ConnectionConfig& cfg) { ... })
         *     .onConfigDeleted([](const String& shortId) { ... });
         */
        Zeno &enableStorage();

        /**
         * @brief Set callback when a connection config is created
         * @param callback Callback with ConnectionConfig
         * @return Reference to this for method chaining
         */
        Zeno &onConfigCreated(std::function<void(const ConnectionConfig &)> callback);

        /**
         * @brief Set callback when a connection config is updated
         * @param callback Callback with ConnectionConfig
         * @return Reference to this for method chaining
         */
        Zeno &onConfigUpdated(std::function<void(const ConnectionConfig &)> callback);

        /**
         * @brief Set callback when a connection config is deleted
         * @param callback Callback with shortId
         * @return Reference to this for method chaining
         */
        Zeno &onConfigDeleted(std::function<void(const String &)> callback);

        /**
         * @brief Get connection config by shortId
         * @param shortId Config identifier
         * @param outConfig Output ConnectionConfig
         * @return true if found
         */
        bool getConnectionConfig(const String &shortId, ConnectionConfig &outConfig);

        /**
         * @brief Get all connection config IDs
         * @return Vector of shortIds
         */
        std::vector<String> getAllConnectionConfigIds();

        // ============================================
        // Fluent Configuration - Data Monitor Storage
        // ============================================

        /**
         * @brief Enable storage for data monitor (PLC register) configurations
         * @return Reference to this for method chaining
         *
         * This enables the LittleFS-based storage system for managing
         * data monitor configurations received via MQTT.
         *
         * @example
         * zeno.enableDataMonitorStorage()
         *     .onDataMonitorCreated([](const DataMonitorConfig& cfg) { ... })
         *     .onDataMonitorUpdated([](const DataMonitorConfig& cfg) { ... })
         *     .onDataMonitorDeleted([](const String& mqttKey) { ... })
         *     .onDataMonitorToggled([](const String& mqttKey, bool enabled) { ... });
         */
        Zeno &enableDataMonitorStorage();

        /**
         * @brief Set callback when a data monitor config is created
         * @param callback Callback with DataMonitorConfig
         * @return Reference to this for method chaining
         */
        Zeno &onDataMonitorCreated(std::function<void(const DataMonitorConfig &)> callback);

        /**
         * @brief Set callback when a data monitor config is updated
         * @param callback Callback with DataMonitorConfig
         * @return Reference to this for method chaining
         */
        Zeno &onDataMonitorUpdated(std::function<void(const DataMonitorConfig &)> callback);

        /**
         * @brief Set callback when a data monitor config is deleted
         * @param callback Callback with mqttKey
         * @return Reference to this for method chaining
         */
        Zeno &onDataMonitorDeleted(std::function<void(const String &)> callback);

        /**
         * @brief Set callback when a data monitor is toggled enabled/disabled
         * @param callback Callback with mqttKey and enabled state
         * @return Reference to this for method chaining
         */
        Zeno &onDataMonitorToggled(std::function<void(const String &, bool)> callback);

        /**
         * @brief Get data monitor config by mqttKey
         * @param mqttKey Monitor identifier (6-digit)
         * @param outConfig Output DataMonitorConfig
         * @return true if found
         */
        bool getDataMonitorConfig(const String &mqttKey, DataMonitorConfig &outConfig);

        /**
         * @brief Get all data monitor IDs
         * @return Vector of mqttKeys
         */
        std::vector<String> getAllDataMonitorIds();

        /**
         * @brief Get data monitors linked to a connection
         * @param connectionId Connection shortId
         * @return Vector of DataMonitorConfig
         */
        std::vector<DataMonitorConfig> getDataMonitorsByConnection(const String &connectionId);

        // ============================================
        // Fluent Configuration - Schedule Management
        // ============================================

        /**
         * @brief Enable schedule management system
         * @return Reference to this for method chaining
         *
         * Enables the schedule system which allows:
         * - Receiving schedule commands via MQTT
         * - Storing schedules in LittleFS
         * - Executing schedules based on time/interval
         * - Publishing execution reports to MQTT
         *
         * @note Requires NTP time sync. Call after WiFi connected.
         *
         * @example
         * zeno.enableSchedule()
         *     .onScheduleExecuted([](const String& id, ExecutionStatus status,
         *                            int64_t value, const String& error) {
         *         Serial.printf("Schedule %s: %s\n", id.c_str(),
         *                       executionStatusToString(status));
         *     });
         */
        Zeno &enableSchedule();

        /**
         * @brief Enable irrigation module (scenario execution + scheduling)
         *
         * Enables the irrigation system which allows:
         * - Receiving irrigation execute/sync/delete commands via MQTT
         * - Storing scenarios + schedules in LittleFS
         * - Executing multi-step irrigation scenarios (valves, pumps, WAIT)
         * - Auto-scheduling recurring/once scenarios
         * - Publishing execution status to MQTT
         *
         * @note Requires NTP time sync for scheduling.
         */
        // Plan 06-03 Pattern F (OQ-1 RESOLVED): enableIrrigation() stays
        // declared on every platform so a publish-all sketch
        // (`zeno.enableIrrigation();`) compiles on ESP8266 and logs a
        // runtime warning. The typed callback setters reference
        // IrrigationWriteCallback / IrrigationStepCallback / ... which live
        // in irrigation/IrrigationTypes.h — that header is ESP32-only per
        // D-03, so the callback setters are guarded. Users wiring
        // irrigation callbacks must #ifdef ESP32 around those calls.
        Zeno &enableIrrigation();

#if defined(ESP32)
        /**
         * @brief Set write function for irrigation executor
         * Maps MQTT key + value to actual hardware write (GPIO/Modbus)
         */
        Zeno &setIrrigationWriteFunction(IrrigationWriteCallback callback);

        /**
         * @brief Set callback when irrigation step completes
         */
        Zeno &onIrrigationStepProgress(IrrigationStepCallback callback);

        /**
         * @brief Set callback when irrigation scenario completes
         */
        Zeno &onIrrigationCompleted(IrrigationCompletedCallback callback);

        /**
         * @brief Set callback when irrigation error occurs
         */
        Zeno &onIrrigationError(IrrigationErrorCallback callback);
#endif  // ESP32

        /**
         * @brief Set callback when schedule is executed
         * @param callback Callback with schedule ID, status, value written, error message
         * @return Reference to this for method chaining
         */
        Zeno &onScheduleExecuted(ScheduleExecutedCallback callback);

        /**
         * @brief Set callback when schedule execution fails
         * @param callback Callback with schedule ID and error message
         * @return Reference to this for method chaining
         */
        Zeno &onScheduleError(ScheduleErrorCallback callback);

        /**
         * @brief Set callback when schedule is created via MQTT
         * @param callback Callback with ScheduleConfig
         * @return Reference to this for method chaining
         */
        Zeno &onScheduleCreated(ScheduleCreatedCallback callback);

        /**
         * @brief Set callback when schedule is updated via MQTT
         * @param callback Callback with ScheduleConfig
         * @return Reference to this for method chaining
         */
        Zeno &onScheduleUpdated(ScheduleUpdatedCallback callback);

        /**
         * @brief Set callback when schedule is deleted via MQTT
         * @param callback Callback with schedule ID
         * @return Reference to this for method chaining
         */
        Zeno &onScheduleDeleted(ScheduleDeletedCallback callback);

        /**
         * @brief Set callback when schedules are synced via MQTT
         * @param callback Callback with count of schedules synced
         * @return Reference to this for method chaining
         */
        Zeno &onScheduleSynced(ScheduleSyncedCallback callback);

        /**
         * @brief Get schedule count
         * @return Number of schedules loaded in memory
         */
        uint8_t getScheduleCount() const;

        /**
         * @brief Get active Modbus connection count
         * @return Number of connections currently registered
         */
        int getConnectionCount() const;

        /**
         * @brief Get active data monitor count (registers in polling engine)
         * @return Number of registers being polled
         */
        int getDataMonitorCount() const;

        /**
         * @brief Check if max schedule limit reached
         * @return true if 20 schedules reached
         */
        bool isMaxSchedulesReached() const;

        // ============================================
        // Fluent Configuration - Edge Alarm
        // ============================================

        /**
         * @brief Enable edge alarm system
         * @return Reference to this for method chaining
         *
         * Enables local alarm evaluation on the device:
         * - Receives alarm rules from backend via MQTT
         * - Evaluates conditions against Z Key values
         * - Publishes alarm events when triggered
         * - Per-rule cooldown management
         *
         * @note Requires MQTT and Z Keys enabled.
         *
         * @example
         * zeno.enableAlarm()
         *     .onAlarmTriggered([](const String& ruleId, const String& key,
         *                          double value, uint8_t severity) {
         *         Serial.printf("ALARM: %s=%0.2f (severity %d)\n",
         *                       key.c_str(), value, severity);
         *     });
         */
        Zeno &enableAlarm();

        /**
         * @brief Set callback when alarm is triggered
         * @param callback Callback with rule ID, key, value, severity
         * @return Reference to this for method chaining
         */
        Zeno &onAlarmTriggered(AlarmTriggeredCallback callback);

        /**
         * @brief Set callback when alarm config received from backend
         * @param callback Callback with rule count loaded
         * @return Reference to this for method chaining
         */
        Zeno &onAlarmConfigReceived(AlarmConfigCallback callback);

        /**
         * @brief Get current alarm rule count
         * @return Number of alarm rules active
         */
        int getAlarmRuleCount() const;

        // ============================================
        // Fluent Configuration - OTA (Over-The-Air Update)
        // ============================================

        /**
         * @brief Enable OTA firmware update system
         * @return Reference to this for method chaining
         *
         * Enables OTA firmware update via MQTT trigger:
         * - Receives OTA commands from cloud via MQTT topic /ota
         * - Downloads firmware over HTTP using current network (WiFi/ETH/4G)
         * - MD5 hash verification
         * - Auto rollback support
         * - Progress reporting via MQTT /ota/response
         *
         * @example
         * zeno.enableOTA()
         *     .onOTAProgress([](float pct) { Serial.printf("OTA: %.0f%%\n", pct); })
         *     .onOTAComplete([](const String& ver) { Serial.println("Updated to " + ver); })
         *     .onOTAError([](OTAError err, const String& msg) { Serial.println(msg); });
         */
        Zeno &enableOTA();

        /**
         * @brief Pattern G (Phase 7 D-06) fallible OTA trigger.
         *
         * Distinct overload from the existing `Zeno& enableOTA()` builder
         * (D-07 preserved): different signature + different return type =
         * additive, no existing caller breaks. ESP32 production firmware
         * (`src/main.cpp` + `src/zf01_main.cpp`) does NOT call this method
         * directly today (it uses the builder + the internal MQTT command
         * handler), so the T-4-03 invariant is preserved.
         *
         * Returns `ZenoCapability::Unavailable` (with a single `ZENO_LOG_CORE`
         * warning) if `hal.capabilities() & IZenoHal::CAP_OTA == 0` — true on
         * STM32 builds with the off-default OTA bootloader missing, and any
         * Phase 7 port whose platform lacks OTA support. Otherwise delegates
         * to the already-allocated `ZenoPCBOTA` instance via `beginUpdate(url)`
         * (the same code path the MQTT `/ota` topic handler uses internally),
         * mapping `true → OK`, `false → Error`. If the `ZenoPCBOTA` instance
         * has not been allocated yet (`enableOTA()` builder + `begin()` not
         * called), returns `ZenoCapability::Error` rather than crashing on a
         * null deref.
         *
         * @param url HTTP(S) URL of the firmware `.bin` to download.
         * @return ZenoCapability::OK on successful start, Unavailable if
         *         platform lacks CAP_OTA, Error on runtime failure.
         */
        ZenoCapability ota(const char *url);

        /**
         * @brief Set callback for OTA progress updates
         * @param callback Callback with progress percentage (0-100)
         * @return Reference to this for method chaining
         */
        Zeno &onOTAProgress(OTAProgressCallback callback);

        /**
         * @brief Set callback when OTA completes successfully
         * @param callback Callback with new version string
         * @return Reference to this for method chaining
         */
        Zeno &onOTAComplete(OTACompleteCallback callback);

        /**
         * @brief Set callback when OTA fails
         * @param callback Callback with error code and message
         * @return Reference to this for method chaining
         */
        Zeno &onOTAError(OTAErrorCallback callback);

        /**
         * @brief Check if OTA rollback is available
         * @return true if previous firmware partition is valid
         */
        bool canOTARollBack() const;

        /**
         * @brief Check if OTA is currently in progress
         * @return true if downloading/flashing
         */
        bool isOTAInProgress() const;

        /**
         * @brief Check if NTP time is synced
         * @return true if time is valid
         */
        bool isTimeSynced() const;

        /**
         * @brief Get current UTC time
         * @return UTC timestamp (0 if not synced)
         */
        time_t getUTC() const;

        // ============================================
        // Fluent Configuration - Diagnostics
        // ============================================

        /**
         * @brief Enable diagnostics reporting system
         * @param intervalMs Send interval in milliseconds (default: 600000 = 10 minutes)
         * @return Reference to this for method chaining
         *
         * Enables automatic diagnostics reporting including:
         * - Passive updates every 10 minutes (configurable)
         * - Auto-send on first MQTT connection
         * - On-demand request/response support
         * - Connection type management (set by mobile app)
         * - System information (IP, RSSI, memory, uptime)
         * - Retry logic for on-demand responses (1-2 retries)
         *
         * @note Requires MQTT connection. Call after mqtt() configuration.
         *
         * @example
         * zeno.enableDiagnostics(600000)  // 10 minutes
         *     .setConnectionType("WIFI")  // Set by mobile app
         *     .onDiagnosticsRequest([](const String& requestId) {
         *         Serial.println("On-demand request: " + requestId);
         *     });
         */
        Zeno &enableDiagnostics(uint32_t intervalMs = 600000);

        /**
         * @brief Set connection type for diagnostics
         * @param type Connection type ("WIFI", "ETHERNET", "4G", "5G")
         * @return Reference to this for method chaining
         *
         * @note This is set by mobile app configuration, not auto-detected
         */
        Zeno &setConnectionType(const char *type);

        /**
         * @brief Set diagnostics send interval
         * @param intervalMs Interval in milliseconds (default: 600000 = 10 minutes)
         * @return Reference to this for method chaining
         */
        Zeno &setDiagnosticsInterval(uint32_t intervalMs);

        /**
         * @brief Enable/disable passive diagnostics updates
         * @param enable true to enable, false to disable (default: true)
         * @return Reference to this for method chaining
         */
        Zeno &enablePassiveDiagnostics(bool enable = true);

        /**
         * @brief Set maximum retry attempts for on-demand diagnostics responses
         * @param maxRetries Max retry count (default: 2)
         * @return Reference to this for method chaining
         */
        Zeno &setDiagnosticsMaxRetries(uint8_t maxRetries);

        /**
         * @brief Set callback when on-demand diagnostics request received
         * @param callback Callback with requestId
         * @return Reference to this for method chaining
         */
        Zeno &onDiagnosticsRequest(std::function<void(const String &requestId)> callback);

        /**
         * @brief Manually send diagnostics now (bypasses interval timer)
         * @return true if sent successfully
         */
        bool sendDiagnosticsNow();

        // ============================================
        // Fluent Configuration - Z Key System
        // ============================================

        /**
         * @brief Enable Z Key system for custom telemetry/control
         * @return Reference to this for method chaining
         *
         * Z Keys (Z0-Z254) provide a simple API for user-defined
         * telemetry and control values. Values are merged into
         * the telemetry JSON alongside Modbus data.
         *
         * @example
         * zeno.enableZKeys()
         *     .set(ZKey::Z0, 25.5f)         // Temperature
         *     .set(ZKey::Z1, "running")      // Status string
         *     .onZKeyChange(ZKey::Z0, [](ZKey key, const ZValue& val) {
         *         Serial.printf("Z0 changed to: %.1f\n", val.toFloat());
         *     });
         */
        Zeno &enableZKeys();

        /**
         * @brief Set Z key value (int)
         * @param key Z key enum (Z0-Z254)
         * @param value Integer value
         * @return Reference to this for method chaining
         */
        Zeno &set(ZKey key, int32_t value);

        /**
         * @brief Set Z key value (float)
         * @param key Z key enum (Z0-Z254)
         * @param value Float value
         * @return Reference to this for method chaining
         */
        Zeno &set(ZKey key, float value);

        /**
         * @brief Set Z key value (string)
         * @param key Z key enum (Z0-Z254)
         * @param value String value (max 64 chars)
         * @return Reference to this for method chaining
         */
        Zeno &set(ZKey key, const String &value);

        /**
         * @brief Set Z key value (C string)
         * @param key Z key enum (Z0-Z254)
         * @param value C string value
         * @return Reference to this for method chaining
         */
        Zeno &set(ZKey key, const char *value);

        /**
         * @brief Set Z key value (bool)
         * @param key Z key enum (Z0-Z254)
         * @param value Boolean value
         * @return Reference to this for method chaining
         */
        Zeno &set(ZKey key, bool value);

        /**
         * @brief Get Z key value
         * @param key Z key enum
         * @return ZValue container with type info
         */
        ZValue get(ZKey key) const;

        /**
         * @brief Get Z key as int
         * @param key Z key enum
         * @param defaultVal Default if not set (default: 0)
         * @return int value
         */
        int32_t getInt(ZKey key, int32_t defaultVal = 0) const;

        /**
         * @brief Get Z key as float
         * @param key Z key enum
         * @param defaultVal Default if not set (default: 0.0f)
         * @return float value
         */
        float getFloat(ZKey key, float defaultVal = 0.0f) const;

        /**
         * @brief Get Z key as string
         * @param key Z key enum
         * @param defaultVal Default if not set (default: "")
         * @return String value
         */
        String getString(ZKey key, const String &defaultVal = "") const;

        /**
         * @brief Get Z key as bool
         * @param key Z key enum
         * @param defaultVal Default if not set (default: false)
         * @return bool value
         */
        bool getBool(ZKey key, bool defaultVal = false) const;

        /**
         * @brief Set callback when specific Z key changes (from cloud control)
         * @param key Z key to watch
         * @param callback Callback with key and new value
         * @return Reference to this for method chaining
         */
        Zeno &onZKeyChange(ZKey key, ZKeyChangeCallback callback);

        // --- Typed convenience overloads (no manual conversion needed) ---
        inline Zeno &onZKeyChange(ZKey key, std::function<void(float)> cb)
        {
            return onZKeyChange(key, [cb](ZKey, const ZValue &v)
                                { cb(v.toFloat()); });
        }
        inline Zeno &onZKeyChange(ZKey key, std::function<void(int32_t)> cb)
        {
            return onZKeyChange(key, [cb](ZKey, const ZValue &v)
                                { cb(v.toInt()); });
        }
        inline Zeno &onZKeyChange(ZKey key, std::function<void(const String &)> cb)
        {
            return onZKeyChange(key, [cb](ZKey, const ZValue &v)
                                { cb(v.toString()); });
        }
        inline Zeno &onZKeyChange(ZKey key, std::function<void(bool)> cb)
        {
            return onZKeyChange(key, [cb](ZKey, const ZValue &v)
                                { cb(v.toBool()); });
        }

        /**
         * @brief Set callback when any Z key changes (from cloud control)
         * @param callback Callback with key and new value
         * @return Reference to this for method chaining
         */
        Zeno &onAnyZKeyChange(ZKeyChangeCallback callback);

        /**
         * @brief Set callback called right before each Z key publish cycle
         *
         * Use with ZENO_READ_ALL macro to collect sensor values just-in-time.
         * Called only when a publish is actually due (not every loop).
         *
         * @param callback void() function - call zeno.set() inside
         * @return Reference to this for method chaining
         */
        Zeno &onZKeyRead(std::function<void()> callback);

        /**
         * @brief Set Z key publish interval
         * @param intervalMs Publish interval in milliseconds (min 1000ms)
         * @return Reference to this for method chaining
         */
        Zeno &setZPublishInterval(uint32_t intervalMs);

        /**
         * @brief Set Modbus telemetry publish interval
         * @param intervalMs Publish interval in milliseconds (min 1000ms)
         * @return Reference to this for method chaining
         */
        Zeno &setModbusTelemetryInterval(uint32_t intervalMs);

        /**
         * @brief Enable instant publish when Z key value changes
         * @param enable true to enable (default: false)
         * @return Reference to this for method chaining
         */
        Zeno &setZInstantPublish(bool enable);

        // ============================================
        // Fluent Configuration - MQTT Credentials
        // ============================================

        /**
         * @brief Set MQTT credentials
         * @param username MQTT username
         * @param password MQTT password
         * @return Reference to this for method chaining
         */
        Zeno &mqttCredentials(const char *clientId, const char *username, const char *password);

        /**
         * @brief Set callback when MQTT connected
         * @param callback Callback function
         * @return Reference to this for method chaining
         */
        Zeno &onMqttConnected(std::function<void()> callback);

        /**
         * @brief Set callback when MQTT message received
         * @param callback Callback function with topic and payload
         * @return Reference to this for method chaining
         */
        Zeno &onMqttMessage(std::function<void(const String &topic, const String &payload)> callback);

        /**
         * @brief Get MQTT instance for advanced operations
         * @return Reference to ZenoPCBMQTT instance
         */
        ZenoPCBMQTT &getMQTT();

        // ============================================
        // Fluent Configuration - Callbacks
        // ============================================

        /**
         * @brief Set callback when WiFi connected
         * @param callback Callback function
         * @return Reference to this for method chaining
         */
        Zeno &onConnected(std::function<void()> callback);

        /**
         * @brief Set callback when WiFi disconnected
         * @param callback Callback function
         * @return Reference to this for method chaining
         */
        Zeno &onDisconnected(std::function<void()> callback);

        /**
         * @brief Set callback when configuration received (via AP mode)
         * @param callback Callback function with DeviceConfig
         * @return Reference to this for method chaining
         */
        Zeno &onConfigured(std::function<void(const DeviceConfig &)> callback);

        /**
         * @brief Set callback for errors
         * @param callback Callback function with error message
         * @return Reference to this for method chaining
         */
        Zeno &onError(std::function<void(const String &)> callback);

        /**
         * @brief Set callback for state changes
         * @param callback Callback function with state
         * @return Reference to this for method chaining
         */
        Zeno &onStateChange(std::function<void(ZenoState)> callback);

        // ============================================
        // Lifecycle
        // ============================================

        /**
         * @brief Initialize and start ZenoPCB
         * @return true if successful
         */
        bool begin();

        /**
         * @brief Main loop - must call in loop()
         */
        void loop();

        // ============================================
        // Actions
        // ============================================

        /**
         * @brief Force enter AP provisioning mode
         */
        void startAPMode();

        /**
         * @brief Exit AP mode
         */
        void stopAPMode();

        /**
         * @brief Factory reset - clear all configuration
         */
        void factoryReset();

        /**
         * @brief Reconnect WiFi
         */
        void reconnect();

        // ============================================
        // Status
        // ============================================

        /**
         * @brief Check if WiFi is connected
         */
        bool isConnected() const;

        /**
         * @brief Check if device is configured
         */
        bool isConfigured() const;

        /**
         * @brief Check if in AP mode
         */
        bool isAPMode() const;

        /**
         * @brief Get current state
         */
        ZenoState getState() const;

        /**
         * @brief Get actual connection type being used (WIFI, ETHERNET, 4G)
         * This reflects the real network in use, accounting for provisioning switches.
         */
        String getActualConnectionType() const;

        /**
         * @brief Get AP SSID
         */
        String getAPSSID() const;

        /**
         * @brief Get WiFi IP address
         */
        String getIP() const;

        /**
         * @brief Get device configuration
         */
        DeviceConfig getConfig() const;

        /**
         * @brief Set device credentials for /api/info response
         * @param deviceId Provisioned device ID (32 chars)
         * @param token Provisioned token (32 chars)
         *
         * Call this after DeviceCredentials.begin() to make credentials
         * available in the /api/info API response.
         *
         * @example
         * DeviceCredentials credentials;
         * credentials.begin();
         * zeno.setDeviceCredentials(credentials.getDeviceId(), credentials.getToken());
         */
        void setDeviceCredentials(const String &deviceId, const String &token);

    private:
        // Hardware Abstraction Layer reference (Plan 04-05).
        // Injected via ctor; default = getEsp32Hal() singleton. Wired down
        // into every consumer module from Zeno::begin() — replaces the
        // temporary getEsp32Hal() bridges installed in plans 04-03/04-04.
        IZenoHal &_hal;

        // Internal modules
        WiFiProvisioning *_wifiProvisioning;
        ZenoPCBMQTT *_mqtt;
        ZenoPCBDiagnostics *_diagnostics;
        ScheduleExecutor *_scheduleExecutor;
        AlarmEngine *_alarmEngine;
        ZenoPCBOTA *_ota;

        // Network clients — MQTT và OTA dùng client RIÊNG BIỆT
        // Nếu chia sẻ cùng 1 WiFiClient: OTA connect() sẽ đóng TCP của MQTT → MQTT drop
        //
        // Phase 7 Plan 07-06.5 (Area B follow-up): Pattern H gate on member
        // declarations. STM32F4 default-Ethernet build doesn't include any
        // WiFi.h equivalent (the ZenoPCB.h switch chain above leaves the
        // F4-default arm empty), so `WiFiClient` is an unknown type there.
        // The owning code paths (direct WiFi.begin() in Zeno::begin()) are
        // already Pattern-H-guarded; the network provider layer supplies
        // the client via `_networkProvider->getClient()` for non-WiFi paths.
#if defined(ESP32) || defined(ESP8266) || defined(ARDUINO_UNOR4_WIFI) || defined(STM32F1xx)
        WiFiClient _wifiClient;    // MQTT client
        WiFiClient _otaWifiClient; // OTA client (riêng, không ảnh hưởng MQTT)
#endif
#ifdef ZENOPCB_ENABLE_TLS
        WiFiClientSecure *_wifiClientSecure; // Pointer - only allocated when enableTLS() called
#endif

        // External network provider (Ethernet / 4G / MultiConnect)
        ZenoNetworkProvider *_networkProvider;
        ZenoNetworkProvider *_registeredNetworkProvider; // Original provider set by user (preserved for mode switching)
        String _actualConnectionType;                    // Actual network type being used (WIFI, ETHERNET, 4G) - tracked by library

        // Configuration
        ProvisioningConfig _provConfig;
        DeviceInfo _deviceInfo;
        String _wifiSSID;
        String _wifiPassword;
        String _deviceId;
        String _deviceToken; // Device access token for MQTT topics
        String _deviceName;
        String _mqttBroker;
        uint16_t _mqttPort;
        String _mqttClientId;
        String _mqttUsername;
        String _mqttPassword;
        bool _mqttEnabled;
        bool _mqttTLS;
        bool _tlsEnabled;                  // Runtime flag to check if TLS was enabled
        const char *_rootCA = nullptr;     // Phase 7 D-27 — user-provided PEM root CA (caller owns lifetime; nullptr = setInsecure fallback)
        bool _storageEnabled;              // Flag to enable storage module
        bool _dataMonitorStorageEnabled;   // Flag to enable data monitor storage
        bool _scheduleEnabled;             // Flag to enable schedule module
        bool _irrigationEnabled;           // Flag to enable irrigation module
        bool _diagnosticsEnabled;          // Flag to enable diagnostics module
        uint32_t _diagnosticsIntervalMs;   // Diagnostics send interval (ms)
        bool _zKeysEnabled;                // Flag to enable Z Key system
        bool _alarmEnabled;                // Flag to enable edge alarm system
        bool _otaEnabled;                  // Flag to enable OTA update system
        uint32_t _modbusTelemetryInterval; // Modbus telemetry publish interval (ms), min 1000
        String _pendingOTAErrorPayload;    // Queued OTA error response — sent when MQTT reconnects

        // ========================================
        // 4G OTA MQTT Queue — messages queued during OTA, flushed at yield points
        // ========================================
        struct QueuedMQTTMessage
        {
            String topic;
            String payload;
            MQTTQoS qos;
            bool retain;
        };
        static const int MAX_MQTT_QUEUE = 10;
        QueuedMQTTMessage _mqttQueue[MAX_MQTT_QUEUE];
        int _mqttQueueHead = 0;
        int _mqttQueueTail = 0;
        bool _mqttQueueEnabled = false; // True when OTA on 4G active

        void _enqueueMQTT(const String &topic, const String &payload, MQTTQoS qos = MQTTQoS::QOS_0, bool retain = false);
        void _flushMQTTQueue();
        int _mqttQueueCount() const;

        // Deferred OTA start — tránh block MQTT callback (beginUpdate blocks TCP connect)
        bool _pendingOTAStart;            // Flag: có OTA command chờ xử lý
        String _pendingOTAUrl;            // URL firmware cần download
        String _pendingOTAVersion;        // Version string (optional)
        String _pendingOTAPayload;        // Original payload (for dedup guard)
        String _lastFailedOTAPayload;     // Last failed OTA payload (dedup guard)
        unsigned long _lastFailedOTATime; // Timestamp of last failed OTA
        unsigned long _otaStartTimeMs;    // OTA start timestamp for elapsed time tracking

        // State
        ZenoState _state;
        bool _provisioningEnabled;
        bool _wifiConfigured;
        bool _ntpSyncPending;          // NTP sync started but not yet confirmed
        unsigned long _ntpSyncStartMs; // When NTP sync was initiated

        // Callbacks
        std::function<void()> _connectedCallback;
        std::function<void()> _disconnectedCallback;
        std::function<void(const DeviceConfig &)> _configuredCallback;
        std::function<void(const String &)> _errorCallback;
        std::function<void(ZenoState)> _stateCallback;
        std::function<void()> _mqttConnectedCallback;
        std::function<void(const String &, const String &)> _mqttMessageCallback;

        // Storage callbacks - Connection Config
        std::function<void(const ConnectionConfig &)> _configCreatedCallback;
        std::function<void(const ConnectionConfig &)> _configUpdatedCallback;
        std::function<void(const String &)> _configDeletedCallback;

        // Storage callbacks - Data Monitor
        std::function<void(const DataMonitorConfig &)> _dataMonitorCreatedCallback;
        std::function<void(const DataMonitorConfig &)> _dataMonitorUpdatedCallback;
        std::function<void(const String &)> _dataMonitorDeletedCallback;
        std::function<void(const String &, bool)> _dataMonitorToggledCallback;

        // Schedule callbacks
        ScheduleExecutedCallback _scheduleExecutedCallback;
        ScheduleErrorCallback _scheduleErrorCallback;
        ScheduleCreatedCallback _scheduleCreatedCallback;
        ScheduleUpdatedCallback _scheduleUpdatedCallback;
        ScheduleDeletedCallback _scheduleDeletedCallback;
        ScheduleSyncedCallback _scheduleSyncedCallback;

#if defined(ESP32)
        // Irrigation callbacks (ESP32-only — irrigation/IrrigationTypes.h
        // guarded by D-03 module strip)
        IrrigationWriteCallback _irrigationWriteCallback;
        IrrigationStepCallback _irrigationStepCallback;
        IrrigationCompletedCallback _irrigationCompletedCallback;
        IrrigationErrorCallback _irrigationErrorCallback;
#endif  // ESP32

        // Diagnostics callbacks
        std::function<void(const String &requestId)> _diagnosticsRequestCallback;

        // Alarm callbacks
        AlarmTriggeredCallback _alarmTriggeredCallback;
        AlarmConfigCallback _alarmConfigReceivedCallback;

        // OTA callbacks
        OTAProgressCallback _otaProgressCallback;
        OTACompleteCallback _otaCompleteCallback;
        OTAErrorCallback _otaErrorCallback;

        // Z Key read callback (called before each publish cycle)
        std::function<void()> _zKeyReadCallback;

        // Internal methods
        void _initProvisioning();
        void _initMQTT();
        void _initStorage();
        void _initDataMonitorStorage();
        void _initSchedule();
        void _initIrrigation();
        void _initDiagnostics();
        void _initAlarm();
        void _handleConnectionConfigMessage(const String &topic, const String &payload);
        void _handleDataMonitorMessage(const String &topic, const String &payload);
        void _handleScheduleMessage(const String &topic, const String &payload);
#if defined(ESP32)
        // Plan 06-03 D-03 — irrigation message helpers use
        // IRRIGATION_KEY_LEN from the guarded IrrigationTypes.h, so the
        // method declarations themselves must be ESP32-only. Caller sites
        // in Zeno::loop()/begin()/etc. are also wrapped.
        void _handleIrrigationMessage(const String &topic, const String &payload);
        void _publishIrrigationStatus(const char *status, uint8_t step, uint8_t total,
                                      const char *action = nullptr,
                                      const char (*keys)[IRRIGATION_KEY_LEN] = nullptr,
                                      uint8_t keyCount = 0,
                                      uint32_t dur = 0,
                                      const char *error = nullptr);
        void _publishIrrigationAck(const char *action, const char *id, const char *eid,
                                   bool success, uint32_t ms, uint32_t ts,
                                   const char *error = nullptr,
                                   uint8_t scenarioCount = 0, uint8_t scheduleCount = 0);
#endif  // ESP32
        void _handleDiagnosticsRequest(const String &topic, const String &payload);
        void _handleAlarmConfigMessage(const String &topic, const String &payload);
        void _handleOTAMessage(const String &topic, const String &payload);
        void _publishOTAResponse(const char *status, float progress = -1, const String &version = "", const String &error = "");
        void _initOTA();
        void _publishAck(const char *module, const char *action, const char *id, bool success, uint32_t ms, const String &error = "");
        void _checkAndPublishAlarms();
        void _setState(ZenoState newState);
        void _publishZKeyTelemetry();
    };

} // namespace ZenoPCB

#endif // ZENOPCB_H
