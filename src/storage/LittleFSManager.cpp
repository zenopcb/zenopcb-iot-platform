/**
 * @file LittleFSManager.cpp
 * @brief Connection + data-monitor persistence routed through IZenoStorage.
 *
 * . See ConnectionConfig.h / DataMonitorConfig.h for the path
 * constants `CONNECTIONS_DIR`, `META_FILE`, `DATA_MONITORS_DIR`,
 * `DM_META_FILE`  path strings unchanged (EDGE-02 preserved).
 *
 * TU guard for ZENOPCB_MICRO_BASIC profile (F103 64KB budget)
 * when `-DZENOPCB_DISABLE_STORAGE` is set, the entire storage layer compiles
 * out (LittleFSManager + ConnectionConfig + DataMonitorConfig persistence).
 */
#if !defined(ZENOPCB_DISABLE_STORAGE)

#include "LittleFSManager.h"
#include "../core/ZenoPCBDebug.h"
#include <string.h>

// Debug logging
#ifndef ZENOPCB_DEBUG_STORAGE
#define ZENOPCB_DEBUG_STORAGE 0
#endif

#if ZENOPCB_DEBUG_STORAGE
#define STORAGE_LOG(fmt, ...) ZENO_LOG("LittleFS", fmt, ##__VA_ARGS__)
#else
#define STORAGE_LOG(fmt, ...) ((void)0)
#endif

namespace ZenoPCB
{
    // Static member initialization
    bool LittleFSManager::_initialized = false;
    String LittleFSManager::_lastError = "";
    IZenoHal *LittleFSManager::_hal = nullptr;

    namespace
    {
        // Bounded buffers for JSON staging. Sized per 3.3.
        constexpr size_t CONFIG_FILE_BUF_SIZE = 2048;
        constexpr size_t CONFIG_META_BUF_SIZE = 4096;
        constexpr size_t DM_FILE_BUF_SIZE = 1024;
        constexpr size_t DM_META_BUF_SIZE = 4096;
    } // namespace

    // ============================================
    // HAL injection
    // ============================================

    void LittleFSManager::setHal(IZenoHal *hal)
    {
        _hal = hal;
    }

    // ============================================
    // Initialization
    // ============================================

    bool LittleFSManager::initialize()
    {
        if (_initialized)
        {
            return true;
        }

        if (!_hal || !(_hal->capabilities() & IZenoHal::CAP_FS_FILES))
        {
            logError("HAL not set or no filesystem capability");
            return false;
        }

        STORAGE_LOG("Initializing storage backend via HAL...");

        if (!_hal->storage().begin())
        {
            logError("Failed to mount HAL filesystem");
            return false;
        }

        if (!_ensureDirectories())
        {
            logError("Failed to create directories");
            return false;
        }

        _initialized = true;
        STORAGE_LOG("Storage initialized successfully");
        return true;
    }

    bool LittleFSManager::_ensureDirectories()
    {
        if (!_hal || !(_hal->capabilities() & IZenoHal::CAP_FS_FILES))
        {
            return false;
        }

        // Ensure connections directory
        if (!_hal->storage().exists(CONNECTIONS_DIR))
        {
            if (!_hal->storage().mkdir(CONNECTIONS_DIR))
            {
                return false;
            }
            STORAGE_LOG("Created directory: %s", CONNECTIONS_DIR);
        }

        // Ensure data-monitors directory
        if (!_hal->storage().exists(DATA_MONITORS_DIR))
        {
            if (!_hal->storage().mkdir(DATA_MONITORS_DIR))
            {
                return false;
            }
            STORAGE_LOG("Created directory: %s", DATA_MONITORS_DIR);
        }

        return true;
    }

    // ============================================
    // CRUD Operations
    // ============================================

    bool LittleFSManager::createConfig(const ConnectionConfig &config)
    {
        if (!_initialized)
        {
            logError("LittleFS not initialized");
            return false;
        }

        if (!config.isValid())
        {
            logError("Invalid config data");
            return false;
        }

        String path = getConfigFilePath(config.shortId);

        if (_hal->storage().exists(path.c_str()))
        {
            logError("Config already exists:" + String(config.shortId));
            return false;
        }

        // Serialize to JSON
        JsonDocument doc;
        JsonObject obj = doc.to<JsonObject>();
        config.toJson(obj);

        String jsonStr;
        serializeJson(doc, jsonStr);

        // Write via HAL (see 5 Deviation A
        // the prior temp+rename atomic semantic is replaced with a direct
        // writeFile() because IZenoStorage exposes no rename(). Short-write
        // detection retained via return-value compare.)
        if (!_atomicWrite(path, jsonStr))
        {
            logError("Failed to write config file");
            return false;
        }

        // Update metadata
        updateMetadataAfterChange(ConfigAction::CREATE, config.shortId);

        STORAGE_LOG("Created config: %s", config.shortId);
        return true;
    }

    bool LittleFSManager::readConfig(const String &shortId, ConnectionConfig &outConfig)
    {
        if (!_initialized)
        {
            logError("LittleFS not initialized");
            return false;
        }

        String path = getConfigFilePath(shortId.c_str());

        if (!_hal->storage().exists(path.c_str()))
        {
            logError("Config not found:" + shortId);
            return false;
        }

        String content;
        if (!_readFile(path, content))
        {
            logError("Failed to read config file");
            return false;
        }

        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, content);
        if (error)
        {
            logError("JSON parse error:" + String(error.c_str()));
            return false;
        }

        if (!outConfig.fromJson(doc.as<JsonObject>()))
        {
            logError("Failed to parse config data");
            return false;
        }

        STORAGE_LOG("Read config: %s", shortId.c_str());
        return true;
    }

    bool LittleFSManager::updateConfig(const ConnectionConfig &config)
    {
        if (!_initialized)
        {
            logError("LittleFS not initialized");
            return false;
        }

        if (!config.isValid())
        {
            logError("Invalid config data");
            return false;
        }

        String path = getConfigFilePath(config.shortId);

        if (!_hal->storage().exists(path.c_str()))
        {
            logError("Config not found:" + String(config.shortId));
            return false;
        }

        // Read existing to preserve createdAt
        ConnectionConfig existing;
        if (readConfig(config.shortId, existing))
        {
            // Preserve createdAt, update updatedAt
            ConnectionConfig updated = config;
            updated.createdAt = existing.createdAt;
            updated.updatedAt = millis() / 1000;

            JsonDocument doc;
            JsonObject obj = doc.to<JsonObject>();
            updated.toJson(obj);

            String jsonStr;
            serializeJson(doc, jsonStr);

            if (!_atomicWrite(path, jsonStr))
            {
                logError("Failed to update config file");
                return false;
            }
        }
        else
        {
            return false;
        }

        updateMetadataAfterChange(ConfigAction::UPDATE, config.shortId);

        STORAGE_LOG("Updated config: %s", config.shortId);
        return true;
    }

    bool LittleFSManager::deleteConfig(const String &shortId)
    {
        if (!_initialized)
        {
            logError("LittleFS not initialized");
            return false;
        }

        String path = getConfigFilePath(shortId.c_str());

        if (!_hal->storage().exists(path.c_str()))
        {
            logError("Config not found:" + shortId);
            return false;
        }

        if (!_hal->storage().deleteFile(path.c_str()))
        {
            logError("Failed to delete config file");
            return false;
        }

        updateMetadataAfterChange(ConfigAction::DELETE, shortId);

        STORAGE_LOG("Deleted config: %s", shortId.c_str());
        return true;
    }

    bool LittleFSManager::configExists(const String &shortId)
    {
        if (!_initialized)
        {
            return false;
        }

        String path = getConfigFilePath(shortId.c_str());
        return _hal->storage().exists(path.c_str());
    }

    // ============================================
    // Metadata Operations
    // ============================================

    bool LittleFSManager::readMetadata(ConnectionMetadata &outMeta)
    {
        if (!_initialized)
        {
            return false;
        }

        if (!_hal->storage().exists(META_FILE))
        {
            // Return empty metadata if file doesn't exist
            outMeta = ConnectionMetadata();
            return true;
        }

        String content;
        if (!_readFile(String(META_FILE), content))
        {
            return false;
        }

        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, content);
        if (error)
        {
            return false;
        }

        return outMeta.fromJson(doc.as<JsonObject>());
    }

    bool LittleFSManager::writeMetadata(const ConnectionMetadata &meta)
    {
        if (!_initialized)
        {
            return false;
        }

        JsonDocument doc;
        JsonObject obj = doc.to<JsonObject>();
        meta.toJson(obj);

        String jsonStr;
        serializeJson(doc, jsonStr);

        return _atomicWrite(String(META_FILE), jsonStr);
    }

    bool LittleFSManager::updateMetadataAfterChange(ConfigAction action, const String &shortId)
    {
        ConnectionMetadata meta;
        readMetadata(meta);

        switch (action)
        {
        case ConfigAction::CREATE:
            meta.shortIds.push_back(shortId);
            meta.totalConnections++;
            break;

        case ConfigAction::DELETE:
            for (auto it = meta.shortIds.begin(); it != meta.shortIds.end(); ++it)
            {
                if (*it == shortId)
                {
                    meta.shortIds.erase(it);
                    break;
                }
            }
            if (meta.totalConnections > 0)
            {
                meta.totalConnections--;
            }
            break;

        case ConfigAction::UPDATE:
            // No count change for updates
            break;

        default:
            break;
        }

        meta.lastUpdateTime = millis() / 1000;
        return writeMetadata(meta);
    }

    // ============================================
    // Listing & Info
    // ============================================

    std::vector<String> LittleFSManager::listAllConfigs()
    {
        std::vector<String> result;

        if (!_initialized)
        {
            return result;
        }

        const char *prefix = "/connections/";
        _hal->storage().listFiles(prefix, [&result](const char *path) {
            if (!path) return;
            const char *base = strrchr(path, '/');
            if (!base) return;
            ++base;

            // Skip hidden + meta + non-.json
            if (base[0] == '.') return;
            if (strcmp(base, "meta.json") == 0) return;
            size_t baseLen = strlen(base);
            if (baseLen <= 5) return;
            if (strcmp(base + baseLen - 5, ".json") != 0) return;

            // shortId = base without ".json"
            String shortId;
            shortId.reserve(baseLen - 5);
            for (size_t i = 0; i + 5 < baseLen; ++i) shortId += base[i];
            result.push_back(shortId);
        });

        return result;
    }

    size_t LittleFSManager::getStorageUsed()
    {
        // Deviation B per 5 IZenoStorage does not expose
        // usedBytes(); return 0 until a future plan extends the HAL surface.
        return 0;
    }

    size_t LittleFSManager::getStorageAvailable()
    {
        // Deviation B per 5 IZenoStorage does not expose
        // totalBytes(); return 0 until a future plan extends the HAL surface.
        return 0;
    }

    // ============================================
    // Data Monitor CRUD Operations
    // ============================================

    bool LittleFSManager::createDataMonitor(const DataMonitorConfig &config)
    {
        if (!_initialized)
        {
            logError("LittleFS not initialized");
            return false;
        }

        if (!config.isValid())
        {
            logError("Invalid data monitor config");
            return false;
        }

        String path = getDataMonitorFilePath(config.mqttKey);

        if (_hal->storage().exists(path.c_str()))
        {
            logError("Data monitor already exists:" + String(config.mqttKey));
            return false;
        }

        // Serialize to JSON
        JsonDocument doc;
        JsonObject obj = doc.to<JsonObject>();
        config.toJson(obj);

        String jsonStr;
        serializeJson(doc, jsonStr);

        STORAGE_LOG("Creating data monitor: %s (%u bytes)", config.mqttKey, (unsigned)jsonStr.length());

        if (!_atomicWrite(path, jsonStr))
        {
            logError("Failed to write data monitor file");
            return false;
        }

        // Update metadata
        if (!updateDataMonitorMetadataAfterChange(DataMonitorAction::CREATE, config.mqttKey))
        {
            STORAGE_LOG("Warning: Failed to update metadata after create");
        }

        STORAGE_LOG("Data monitor created successfully: %s", config.mqttKey);
        return true;
    }

    bool LittleFSManager::readDataMonitor(const String &mqttKey, DataMonitorConfig &outConfig)
    {
        if (!_initialized)
        {
            logError("LittleFS not initialized");
            return false;
        }

        String path = getDataMonitorFilePath(mqttKey.c_str());

        String content;
        if (!_readFile(path, content))
        {
            logError("Data monitor not found:" + mqttKey);
            return false;
        }

        // Parse JSON
        JsonDocument doc;
        DeserializationError err = deserializeJson(doc, content);

        if (err)
        {
            logError("JSON parse error:" + String(err.c_str()));
            return false;
        }

        JsonObject obj = doc.as<JsonObject>();
        if (!outConfig.fromJson(obj))
        {
            logError("Invalid data monitor JSON structure");
            return false;
        }

        STORAGE_LOG("Data monitor read successfully: %s", mqttKey.c_str());
        return true;
    }

    bool LittleFSManager::updateDataMonitor(const DataMonitorConfig &config)
    {
        if (!_initialized)
        {
            logError("LittleFS not initialized");
            return false;
        }

        if (!config.isValid())
        {
            logError("Invalid data monitor config");
            return false;
        }

        String path = getDataMonitorFilePath(config.mqttKey);

        if (!_hal->storage().exists(path.c_str()))
        {
            logError("Data monitor not found for update:" + String(config.mqttKey));
            return false;
        }

        // Serialize to JSON
        JsonDocument doc;
        JsonObject obj = doc.to<JsonObject>();
        config.toJson(obj);

        String jsonStr;
        serializeJson(doc, jsonStr);

        STORAGE_LOG("Updating data monitor: %s", config.mqttKey);

        if (!_atomicWrite(path, jsonStr))
        {
            logError("Failed to write data monitor file");
            return false;
        }

        // Update metadata (for timestamp update)
        if (!updateDataMonitorMetadataAfterChange(DataMonitorAction::UPDATE, config.mqttKey))
        {
            STORAGE_LOG("Warning: Failed to update metadata after update");
        }

        STORAGE_LOG("Data monitor updated successfully: %s", config.mqttKey);
        return true;
    }

    bool LittleFSManager::deleteDataMonitor(const String &mqttKey)
    {
        if (!_initialized)
        {
            logError("LittleFS not initialized");
            return false;
        }

        String path = getDataMonitorFilePath(mqttKey.c_str());

        if (!_hal->storage().exists(path.c_str()))
        {
            logError("Data monitor not found for delete:" + mqttKey);
            return false;
        }

        if (!_hal->storage().deleteFile(path.c_str()))
        {
            logError("Failed to delete data monitor file");
            return false;
        }

        // Update metadata
        if (!updateDataMonitorMetadataAfterChange(DataMonitorAction::DELETE, mqttKey))
        {
            STORAGE_LOG("Warning: Failed to update metadata after delete");
        }

        STORAGE_LOG("Data monitor deleted successfully: %s", mqttKey.c_str());
        return true;
    }

    bool LittleFSManager::dataMonitorExists(const String &mqttKey)
    {
        if (!_initialized)
        {
            return false;
        }

        String path = getDataMonitorFilePath(mqttKey.c_str());
        return _hal->storage().exists(path.c_str());
    }

    bool LittleFSManager::toggleDataMonitor(const String &mqttKey, bool enabled)
    {
        if (!_initialized)
        {
            logError("LittleFS not initialized");
            return false;
        }

        // Read existing config
        DataMonitorConfig config;
        if (!readDataMonitor(mqttKey, config))
        {
            return false;
        }

        // Update enabled status
        config.enabled = enabled;
        config.updatedAt = time(nullptr);

        // Write back
        String path = getDataMonitorFilePath(mqttKey.c_str());

        JsonDocument doc;
        JsonObject obj = doc.to<JsonObject>();
        config.toJson(obj);

        String jsonStr;
        serializeJson(doc, jsonStr);

        if (!_atomicWrite(path, jsonStr))
        {
            logError("Failed to write toggled data monitor");
            return false;
        }

        // Update metadata for enabled count
        if (!updateDataMonitorMetadataAfterChange(DataMonitorAction::TOGGLE, mqttKey))
        {
            STORAGE_LOG("Warning: Failed to update metadata after toggle");
        }

        STORAGE_LOG("Data monitor toggled: %s -> %s", mqttKey.c_str(), enabled ? "enabled" : "disabled");
        return true;
    }

    // ============================================
    // Data Monitor Metadata Operations
    // ============================================

    bool LittleFSManager::readDataMonitorMetadata(DataMonitorMetadata &outMeta)
    {
        if (!_initialized)
        {
            logError("LittleFS not initialized");
            return false;
        }

        // Check if file exists first to avoid error log
        if (!_hal->storage().exists(DM_META_FILE))
        {
            STORAGE_LOG("No metadata file, returning defaults");
            outMeta = DataMonitorMetadata();
            return true;
        }

        String content;
        if (!_readFile(String(DM_META_FILE), content))
        {
            // File exists but failed to read
            logError("Failed to read metadata file");
            outMeta = DataMonitorMetadata();
            return true;
        }

        // Parse JSON
        JsonDocument doc;
        DeserializationError err = deserializeJson(doc, content);

        if (err)
        {
            logError("Metadata JSON parse error:" + String(err.c_str()));
            return false;
        }

        JsonObject obj = doc.as<JsonObject>();

        // Read metadata fields
        outMeta.totalMonitors = obj["total"] | 0;
        outMeta.enabledMonitors = obj["enabled"] | 0;
        outMeta.lastUpdateTime = obj["lastUpdated"] | (uint32_t)0;
        outMeta.storageUsedBytes = obj["storageUsed"] | 0;
        outMeta.storageAvailableBytes = obj["storageAvail"] | 0;

        // Read mqttKeys array
        outMeta.mqttKeys.clear();
        if (obj["keys"].is<JsonArray>())
        {
            JsonArray keysArr = obj["keys"].as<JsonArray>();
            for (JsonVariant v : keysArr)
            {
                if (v.is<const char *>())
                {
                    outMeta.mqttKeys.push_back(v.as<String>());
                }
            }
        }

        STORAGE_LOG("Data monitor metadata read: total=%d, enabled=%d",
                    outMeta.totalMonitors, outMeta.enabledMonitors);
        return true;
    }

    bool LittleFSManager::writeDataMonitorMetadata(const DataMonitorMetadata &meta)
    {
        if (!_initialized)
        {
            logError("LittleFS not initialized");
            return false;
        }

        JsonDocument doc;
        JsonObject obj = doc.to<JsonObject>();

        obj["total"] = meta.totalMonitors;
        obj["enabled"] = meta.enabledMonitors;
        obj["lastUpdated"] = meta.lastUpdateTime;
        obj["storageUsed"] = meta.storageUsedBytes;
        obj["storageAvail"] = meta.storageAvailableBytes;

        // Write mqttKeys array
        JsonArray keysArr = obj["keys"].to<JsonArray>();
        for (const String &key : meta.mqttKeys)
        {
            keysArr.add(key);
        }

        String jsonStr;
        serializeJson(doc, jsonStr);

        if (!_atomicWrite(String(DM_META_FILE), jsonStr))
        {
            logError("Failed to write data monitor metadata");
            return false;
        }

        STORAGE_LOG("Data monitor metadata written: total=%d, enabled=%d",
                    meta.totalMonitors, meta.enabledMonitors);
        return true;
    }

    bool LittleFSManager::updateDataMonitorMetadataAfterChange(DataMonitorAction action, const String &mqttKey)
    {
        DataMonitorMetadata meta;

        // Read current metadata (or get defaults)
        if (!readDataMonitorMetadata(meta))
        {
            meta = DataMonitorMetadata();
        }

        switch (action)
        {
        case DataMonitorAction::CREATE:
        {
            // Add to list if not exists
            bool found = false;
            for (const String &key : meta.mqttKeys)
            {
                if (key == mqttKey)
                {
                    found = true;
                    break;
                }
            }
            if (!found)
            {
                meta.mqttKeys.push_back(mqttKey);
                meta.totalMonitors = meta.mqttKeys.size();
            }

            // Check if enabled to update count
            DataMonitorConfig config;
            if (readDataMonitor(mqttKey, config) && config.enabled)
            {
                // Count all enabled monitors
                int enabledCount = 0;
                for (const String &key : meta.mqttKeys)
                {
                    DataMonitorConfig dm;
                    if (readDataMonitor(key, dm) && dm.enabled)
                    {
                        enabledCount++;
                    }
                }
                meta.enabledMonitors = enabledCount;
            }
            break;
        }

        case DataMonitorAction::DELETE:
        {
            // Remove from list
            std::vector<String> newKeys;
            for (const String &key : meta.mqttKeys)
            {
                if (key != mqttKey)
                {
                    newKeys.push_back(key);
                }
            }
            meta.mqttKeys = newKeys;
            meta.totalMonitors = meta.mqttKeys.size();

            // Recount enabled
            int enabledCount = 0;
            for (const String &key : meta.mqttKeys)
            {
                DataMonitorConfig dm;
                if (readDataMonitor(key, dm) && dm.enabled)
                {
                    enabledCount++;
                }
            }
            meta.enabledMonitors = enabledCount;
            break;
        }

        case DataMonitorAction::UPDATE:
        case DataMonitorAction::TOGGLE:
        {
            // Recount enabled monitors
            int enabledCount = 0;
            for (const String &key : meta.mqttKeys)
            {
                DataMonitorConfig dm;
                if (readDataMonitor(key, dm) && dm.enabled)
                {
                    enabledCount++;
                }
            }
            meta.enabledMonitors = enabledCount;
            break;
        }

        default:
            break;
        }

        // Update timestamps and storage info
        meta.lastUpdateTime = time(nullptr);
        meta.storageUsedBytes = getStorageUsed();
        meta.storageAvailableBytes = getStorageAvailable();

        return writeDataMonitorMetadata(meta);
    }

    // ============================================
    // Data Monitor Listing
    // ============================================

    std::vector<String> LittleFSManager::listAllDataMonitors()
    {
        std::vector<String> result;

        if (!_initialized)
        {
            return result;
        }

        const char *prefix = "/data-monitors/";
        _hal->storage().listFiles(prefix, [&result](const char *path) {
            if (!path) return;
            const char *base = strrchr(path, '/');
            if (!base) return;
            ++base;

            // Skip hidden + meta + non-.json
            if (base[0] == '.') return;
            if (strcmp(base, "meta.json") == 0) return;
            size_t baseLen = strlen(base);
            if (baseLen <= 5) return;
            if (strcmp(base + baseLen - 5, ".json") != 0) return;

            String mqttKey;
            mqttKey.reserve(baseLen - 5);
            for (size_t i = 0; i + 5 < baseLen; ++i) mqttKey += base[i];
            result.push_back(mqttKey);
        });

        STORAGE_LOG("Listed %d data monitors", result.size());
        return result;
    }

    // ============================================
    // Error Handling
    // ============================================

    void LittleFSManager::logError(const String &error)
    {
        _lastError = error;
        STORAGE_LOG("Error: %s", error.c_str());
    }

    // ============================================
    // Internal Helpers
    // ============================================

    bool LittleFSManager::_atomicWrite(const String &path, const String &content)
    {
        // 5 Deviation A: IZenoStorage does not expose rename
        // so the pre-refactor temp+rename atomic-write sequence is replaced
        // with a direct writeFile() and short-write detection. Power-loss
        // crash-resilience is reduced; functional callers already check the
        // bool return value and propagate failure (see e.g. createConfig).
        if (!_hal || !(_hal->capabilities() & IZenoHal::CAP_FS_FILES))
        {
            return false;
        }

        size_t written = _hal->storage().writeFile(path.c_str(),
                                                   content.c_str(),
                                                   content.length());
        return written == content.length();
    }

    bool LittleFSManager::_readFile(const String &path, String &outContent)
    {
        if (!_hal || !(_hal->capabilities() & IZenoHal::CAP_FS_FILES))
        {
            return false;
        }

        // We choose a bounded staging buffer per AUDIT 3.3. Connection /
        // data-monitor files top out around 2 KB; meta files top out around
        // 4 KB at MAX_CONNECTIONS/MAX_DATA_MONITORS=200. CONFIG_META_BUF_SIZE
        // = 4096 covers both.
        char buf[CONFIG_META_BUF_SIZE];
        size_t bytesRead = _hal->storage().readFile(path.c_str(), buf, sizeof(buf));
        if (bytesRead == 0)
        {
            return false;
        }
        outContent = String(buf);
        return true;
    }

} // namespace ZenoPCB

#endif // !ZENOPCB_DISABLE_STORAGE
