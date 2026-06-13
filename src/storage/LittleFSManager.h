/**
 * @file LittleFSManager.h
 * @brief Connection + DataMonitor config persistence (via HAL).
 *
 * Plan 04-03 — file I/O now routes through `IZenoStorage` injected via
 * `setHal(IZenoHal*)`. The class name is retained for caller compatibility
 * (`zeno.enableStorage()` and `LittleFSManager::*` calls in main.cpp /
 * zf01_main.cpp); only the implementation backend has changed.
 *
 * Plan 04-05 wires the canonical ESP32 HAL from `Zeno::begin()`. Until
 * then, methods early-return when no HAL is set.
 */

#ifndef LITTLEFS_MANAGER_H
#define LITTLEFS_MANAGER_H

#include <Arduino.h>
#include "ConnectionConfig.h"
#include "DataMonitorConfig.h"
#include "../hal/IZenoHal.h"

namespace ZenoPCB
{
    /**
     * @brief Static class for connection + data-monitor file operations.
     *
     * Uses static-pointer HAL injection (see RESEARCH "Dependency Injection
     * Pattern" — minimal blast radius for static classes).
     */
    class LittleFSManager
    {
    public:
        // ============================================
        // HAL injection (Plan 04-03)
        // ============================================
        static void setHal(IZenoHal *hal);

        // ============================================
        // Initialization
        // ============================================

        static bool initialize();
        static bool isInitialized() { return _initialized; }

        // ============================================
        // Connection Config CRUD Operations
        // ============================================

        static bool createConfig(const ConnectionConfig &config);
        static bool readConfig(const String &shortId, ConnectionConfig &outConfig);
        static bool updateConfig(const ConnectionConfig &config);
        static bool deleteConfig(const String &shortId);
        static bool configExists(const String &shortId);

        // ============================================
        // Connection Metadata Operations
        // ============================================

        static bool readMetadata(ConnectionMetadata &outMeta);
        static bool writeMetadata(const ConnectionMetadata &meta);
        static bool updateMetadataAfterChange(ConfigAction action, const String &shortId);

        // ============================================
        // Data Monitor CRUD Operations
        // ============================================

        static bool createDataMonitor(const DataMonitorConfig &config);
        static bool readDataMonitor(const String &mqttKey, DataMonitorConfig &outConfig);
        static bool updateDataMonitor(const DataMonitorConfig &config);
        static bool deleteDataMonitor(const String &mqttKey);
        static bool dataMonitorExists(const String &mqttKey);
        static bool toggleDataMonitor(const String &mqttKey, bool enabled);

        // ============================================
        // Data Monitor Metadata Operations
        // ============================================

        static bool readDataMonitorMetadata(DataMonitorMetadata &outMeta);
        static bool writeDataMonitorMetadata(const DataMonitorMetadata &meta);
        static bool updateDataMonitorMetadataAfterChange(DataMonitorAction action, const String &mqttKey);

        // ============================================
        // Data Monitor Listing
        // ============================================

        static std::vector<String> listAllDataMonitors();

        // ============================================
        // Connection Config Listing & Info
        // ============================================

        static std::vector<String> listAllConfigs();
        static size_t getStorageUsed();
        static size_t getStorageAvailable();

        // ============================================
        // Error Handling
        // ============================================

        static String getLastError() { return _lastError; }
        static void logError(const String &error);

    private:
        LittleFSManager() = delete; // Static class - no instances

        static bool _initialized;
        static String _lastError;
        static IZenoHal *_hal;

        // Internal helpers
        static bool _ensureDirectories();
        static bool _atomicWrite(const String &path, const String &content);
        static bool _readFile(const String &path, String &outContent);
    };

} // namespace ZenoPCB

#endif // LITTLEFS_MANAGER_H
