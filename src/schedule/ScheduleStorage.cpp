// TU guard for ZENOPCB_MICRO_BASIC profile.
#if !defined(ZENOPCB_DISABLE_SCHEDULE)

#include "ScheduleStorage.h"
#include "../core/ZenoPCBDebug.h"
#include "../ZenoJson.h"  // ArduinoJson API from vendored copy (namespace ZenoJson; see vendor/ArduinoJson/LICENSE.md)
#include <string.h>

namespace ZenoPCB
{

    // Static constants
    const char *ScheduleStorage::SCHEDULE_DIR = "/schedules";
    const char *ScheduleStorage::METADATA_FILE = "/schedules/meta.json";

    // Static HAL pointer (injected by wiring).
    IZenoHal *ScheduleStorage::_hal = nullptr;

    // ============================================
    // HAL injection
    // ============================================

    void ScheduleStorage::setHal(IZenoHal *hal)
    {
        _hal = hal;
    }

    namespace
    {
        // Capacity for in-memory JSON staging when reading/writing schedule
        // files. /schedules/<id>.json is small (~512 B); 1024 leaves headroom.
        constexpr size_t SCHEDULE_FILE_BUF_SIZE = 1024;
        // /schedules/meta.json is tiny (~96 B).
        constexpr size_t METADATA_FILE_BUF_SIZE = 256;
    } // namespace

    // ============================================
    // Private Helpers
    // ============================================

    String ScheduleStorage::getScheduleFilePath(const String &scheduleId)
    {
        return String(SCHEDULE_DIR) + "/" + scheduleId + ".json";
    }

    String ScheduleStorage::getMetadataFilePath()
    {
        return String(METADATA_FILE);
    }

    bool ScheduleStorage::ensureScheduleDirectory()
    {
        if (!_hal || !(_hal->capabilities() & IZenoHal::CAP_FS_FILES))
        {
            ZENO_LOG("ScheduleStorage", "HAL not set or no filesystem capability");
            return false;
        }
        if (!_hal->storage().exists(SCHEDULE_DIR))
        {
            ZENO_LOG("ScheduleStorage", "Creating schedules directory: %s", SCHEDULE_DIR);
            return _hal->storage().mkdir(SCHEDULE_DIR);
        }
        return true;
    }

    // ============================================
    // Schedule CRUD Operations
    // ============================================

    bool ScheduleStorage::saveSchedule(const ScheduleConfig &config)
    {
        if (!_hal || !(_hal->capabilities() & IZenoHal::CAP_FS_FILES))
        {
            ZENO_LOG("ScheduleStorage", "HAL not set or no filesystem capability");
            return false;
        }

        if (!ensureScheduleDirectory())
        {
            ZENO_LOG("ScheduleStorage", "Failed to create schedules directory");
            return false;
        }

        // Check if adding new schedule would exceed limit
        if (!scheduleExists(config.id) && isMaxSchedulesReached())
        {
            ZENO_LOG("ScheduleStorage", "Max schedules limit reached (%d)", MAX_SCHEDULES);
            return false;
        }

        String filePath = getScheduleFilePath(config.id);

        // Create JSON document
        JsonDocument doc;

        doc["id"] = config.id;
        doc["rid"] = config.rid;
        doc["ra"] = config.address;
        doc["rt"] = (int)config.registerType; // Cast to int instead of char
        doc["st"] = (int)config.scheduleType; // Cast to int instead of char

        // Schedule type specific fields
        if (config.scheduleType == ScheduleType::RECURRING)
        {
            doc["et"] = config.executeTime;
            JsonArray rd = doc["rd"].to<JsonArray>();
            for (uint8_t i = 0; i < config.repeatDaysCount; i++)
            {
                rd.add(config.repeatDays[i]);
            }
        }
        else if (config.scheduleType == ScheduleType::ONCE)
        {
            doc["ea"] = config.executeAt;
        }
        else if (config.scheduleType == ScheduleType::INTERVAL)
        {
            doc["iv"] = config.intervalMs;
        }

        doc["at"] = (int)config.actionType; // Cast to int instead of char
        doc["sv"] = config.setValue;
        doc["en"] = config.enabled;
        doc["createdAt"] = config.createdAt;
        doc["updatedAt"] = config.updatedAt;

        // Serialise to a bounded String buffer then commit via HAL.
        String jsonStr;
        if (serializeJson(doc, jsonStr) == 0)
        {
            ZENO_LOG("ScheduleStorage", "Failed to serialize JSON");
            return false;
        }

        size_t written = _hal->storage().writeFile(filePath.c_str(),
                                                   jsonStr.c_str(),
                                                   jsonStr.length());
        if (written != jsonStr.length())
        {
            ZENO_LOG("ScheduleStorage", "Short write: %s (%u/%u)",
                     filePath.c_str(), (unsigned)written, (unsigned)jsonStr.length());
            return false;
        }

        ZENO_LOG("ScheduleStorage", "Schedule saved: %s", config.id);
        return true;
    }

    bool ScheduleStorage::loadSchedule(const String &scheduleId, ScheduleConfig &outConfig)
    {
        if (!_hal || !(_hal->capabilities() & IZenoHal::CAP_FS_FILES))
        {
            ZENO_LOG("ScheduleStorage", "HAL not set or no filesystem capability");
            return false;
        }

        String filePath = getScheduleFilePath(scheduleId);

        if (!_hal->storage().exists(filePath.c_str()))
        {
            ZENO_LOG("ScheduleStorage", "Schedule not found: %s", scheduleId.c_str());
            return false;
        }

        // Read entire file into a bounded buffer (wrapper owns
        // file handle lifecycle).
        char buf[SCHEDULE_FILE_BUF_SIZE];
        size_t bytesRead = _hal->storage().readFile(filePath.c_str(), buf, sizeof(buf));
        if (bytesRead == 0)
        {
            ZENO_LOG("ScheduleStorage", "Failed to read file: %s", filePath.c_str());
            return false;
        }

        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, buf, bytesRead);

        if (error)
        {
            ZENO_LOG("ScheduleStorage", "Failed to parse JSON: %s", error.c_str());
            return false;
        }

        // Parse JSON to ScheduleConfig
        strlcpy(outConfig.id, doc["id"] | "", sizeof(outConfig.id));
        strlcpy(outConfig.rid, doc["rid"] | "", sizeof(outConfig.rid));
        outConfig.address = doc["ra"] | 0;
        outConfig.registerType = (RegisterType)(doc["rt"] | (int)RegisterType::REG_HOLDING);
        outConfig.scheduleType = (ScheduleType)(doc["st"] | (int)ScheduleType::RECURRING);

        // Schedule type specific fields
        if (outConfig.scheduleType == ScheduleType::RECURRING)
        {
            strlcpy(outConfig.executeTime, doc["et"] | "", sizeof(outConfig.executeTime));
            JsonArray rd = doc["rd"];
            outConfig.repeatDaysCount = rd.size();
            for (uint8_t i = 0; i < outConfig.repeatDaysCount && i < 7; i++)
            {
                outConfig.repeatDays[i] = rd[i];
            }
        }
        else if (outConfig.scheduleType == ScheduleType::ONCE)
        {
            outConfig.executeAt = doc["ea"] | 0;
        }
        else if (outConfig.scheduleType == ScheduleType::INTERVAL)
        {
            outConfig.intervalMs = doc["iv"] | 0;
        }

        outConfig.actionType = (ActionType)(doc["at"] | (int)ActionType::SET);
        outConfig.setValue = doc["sv"] | 0;
        outConfig.enabled = doc["en"] | true;
        outConfig.createdAt = doc["createdAt"] | 0;
        outConfig.updatedAt = doc["updatedAt"] | 0;

        ZENO_LOG("ScheduleStorage", "Schedule loaded: %s", scheduleId.c_str());
        return true;
    }

    bool ScheduleStorage::deleteSchedule(const String &scheduleId)
    {
        if (!_hal || !(_hal->capabilities() & IZenoHal::CAP_FS_FILES))
        {
            ZENO_LOG("ScheduleStorage", "HAL not set or no filesystem capability");
            return false;
        }

        String filePath = getScheduleFilePath(scheduleId);

        if (!_hal->storage().exists(filePath.c_str()))
        {
            ZENO_LOG("ScheduleStorage", "Schedule not found for deletion: %s", scheduleId.c_str());
            return false;
        }

        bool result = _hal->storage().deleteFile(filePath.c_str());

        if (result)
        {
            ZENO_LOG("ScheduleStorage", "Schedule deleted: %s", scheduleId.c_str());
        }
        else
        {
            ZENO_LOG("ScheduleStorage", "Failed to delete schedule: %s", scheduleId.c_str());
        }

        return result;
    }

    bool ScheduleStorage::scheduleExists(const String &scheduleId)
    {
        if (!_hal || !(_hal->capabilities() & IZenoHal::CAP_FS_FILES))
        {
            return false;
        }
        return _hal->storage().exists(getScheduleFilePath(scheduleId).c_str());
    }

    bool ScheduleStorage::loadAllSchedules(std::vector<ScheduleConfig> &outSchedules)
    {
        outSchedules.clear();

        if (!_hal || !(_hal->capabilities() & IZenoHal::CAP_FS_FILES))
        {
            ZENO_LOG("ScheduleStorage", "HAL not set or no filesystem capability");
            return false;
        }

        if (!_hal->storage().exists(SCHEDULE_DIR))
        {
            ZENO_LOG("ScheduleStorage", "Schedules directory not found, returning empty list");
            return true; // Not an error, just empty
        }

        // Use HAL listFiles to enumerate the schedules directory. The
        // callback receives an absolute path (e.g. "/schedules/0001.json");
        // we extract the basename to recover the schedule ID, then call
        // loadSchedule(). Skip the meta.json sentinel.
        // Note: SCHEDULE_DIR is "/schedules" (no trailing slash). Esp32Storage
        // uses strncmp(entry, prefix, strlen(prefix)) to filter so we pass
        // "/schedules/" with the slash so that meta.json under /schedules is
        // matched but unrelated paths are excluded.
        const char *prefix = "/schedules/";

        std::vector<String> idsToLoad;
        _hal->storage().listFiles(prefix, [&idsToLoad](const char *path) {
            if (!path) return;
            // Skip metadata
            const char *base = strrchr(path, '/');
            if (!base) return;
            ++base; // skip the slash
            if (strcmp(base, "meta.json") == 0) return;

            // Must end with ".json"
            size_t baseLen = strlen(base);
            if (baseLen <= 5) return;
            if (strcmp(base + baseLen - 5, ".json") != 0) return;

            // Schedule ID = basename without ".json"
            String id;
            id.reserve(baseLen - 5);
            for (size_t i = 0; i + 5 < baseLen; ++i) id += base[i];
            idsToLoad.push_back(id);
        });

        for (const String &id : idsToLoad)
        {
            ScheduleConfig config;
            if (loadSchedule(id, config))
            {
                outSchedules.push_back(config);
            }
        }

        ZENO_LOG("ScheduleStorage", "Loaded %d schedules", outSchedules.size());
        return true;
    }

    bool ScheduleStorage::clearAllSchedules()
    {
        std::vector<String> scheduleIds = listAllScheduleIds();

        bool allDeleted = true;
        for (const String &id : scheduleIds)
        {
            if (!deleteSchedule(id))
            {
                allDeleted = false;
            }
        }

        // Update metadata
        ScheduleMetadata meta;
        meta.count = 0;
        meta.lastUpdated = time(nullptr);
        writeMetadata(meta);

        ZENO_LOG("ScheduleStorage", "Cleared all schedules");
        return allDeleted;
    }

    uint8_t ScheduleStorage::getScheduleCount()
    {
        ScheduleMetadata meta;
        if (readMetadata(meta))
        {
            return meta.count;
        }

        // Fallback: count files
        return listAllScheduleIds().size();
    }

    bool ScheduleStorage::isMaxSchedulesReached()
    {
        return getScheduleCount() >= MAX_SCHEDULES;
    }

    // ============================================
    // Metadata Operations
    // ============================================

    bool ScheduleStorage::readMetadata(ScheduleMetadata &outMeta)
    {
        if (!_hal || !(_hal->capabilities() & IZenoHal::CAP_FS_FILES))
        {
            outMeta = ScheduleMetadata();
            return false;
        }

        String filePath = getMetadataFilePath();

        if (!_hal->storage().exists(filePath.c_str()))
        {
            ZENO_LOG("ScheduleStorage", "Metadata not found, using defaults");
            outMeta = ScheduleMetadata();
            return false;
        }

        char buf[METADATA_FILE_BUF_SIZE];
        size_t bytesRead = _hal->storage().readFile(filePath.c_str(), buf, sizeof(buf));
        if (bytesRead == 0)
        {
            ZENO_LOG("ScheduleStorage", "Failed to read metadata file");
            return false;
        }

        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, buf, bytesRead);

        if (error)
        {
            ZENO_LOG("ScheduleStorage", "Failed to parse metadata JSON");
            return false;
        }

        outMeta.count = doc["count"] | 0;
        outMeta.lastUpdated = doc["lastUpdated"] | 0;
        outMeta.version = doc["version"] | 1;

        return true;
    }

    bool ScheduleStorage::writeMetadata(const ScheduleMetadata &meta)
    {
        if (!_hal || !(_hal->capabilities() & IZenoHal::CAP_FS_FILES))
        {
            return false;
        }

        if (!ensureScheduleDirectory())
        {
            return false;
        }

        String filePath = getMetadataFilePath();

        JsonDocument doc;
        doc["count"] = meta.count;
        doc["lastUpdated"] = meta.lastUpdated;
        doc["version"] = meta.version;

        String jsonStr;
        if (serializeJson(doc, jsonStr) == 0)
        {
            ZENO_LOG("ScheduleStorage", "Failed to serialize metadata");
            return false;
        }

        size_t written = _hal->storage().writeFile(filePath.c_str(),
                                                   jsonStr.c_str(),
                                                   jsonStr.length());
        if (written != jsonStr.length())
        {
            ZENO_LOG("ScheduleStorage", "Short write metadata (%u/%u)",
                     (unsigned)written, (unsigned)jsonStr.length());
            return false;
        }
        return true;
    }

    bool ScheduleStorage::updateMetadataAfterChange(ScheduleAction action)
    {
        ScheduleMetadata meta;
        readMetadata(meta); // Load existing or use defaults

        // Update count based on action
        if (action == ScheduleAction::CREATE)
        {
            meta.count++;
        }
        else if (action == ScheduleAction::DELETE)
        {
            if (meta.count > 0)
                meta.count--;
        }
        else if (action == ScheduleAction::SYNC)
        {
            // Sync replaces all, count from actual files
            meta.count = listAllScheduleIds().size();
        }
        // UPDATE doesn't change count

        meta.lastUpdated = time(nullptr);

        return writeMetadata(meta);
    }

    // ============================================
    // Schedule Listing
    // ============================================

    std::vector<String> ScheduleStorage::listAllScheduleIds()
    {
        std::vector<String> ids;

        if (!_hal || !(_hal->capabilities() & IZenoHal::CAP_FS_FILES))
        {
            return ids;
        }

        if (!_hal->storage().exists(SCHEDULE_DIR))
        {
            return ids;
        }

        const char *prefix = "/schedules/";
        _hal->storage().listFiles(prefix, [&ids](const char *path) {
            if (!path) return;
            const char *base = strrchr(path, '/');
            if (!base) return;
            ++base;
            if (strcmp(base, "meta.json") == 0) return;

            size_t baseLen = strlen(base);
            if (baseLen <= 5) return;
            if (strcmp(base + baseLen - 5, ".json") != 0) return;

            String id;
            id.reserve(baseLen - 5);
            for (size_t i = 0; i + 5 < baseLen; ++i) id += base[i];
            ids.push_back(id);
        });

        return ids;
    }

} // namespace ZenoPCB

#endif // !ZENOPCB_DISABLE_SCHEDULE
