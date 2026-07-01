// Irrigation subsystem is ESP32-only.
#if defined(ESP32)

#include "IrrigationStorage.h"
#include "../core/ZenoPCBDebug.h"
#include "../ZenoJson.h"  // ArduinoJson API from vendored copy (namespace ZenoJson; see vendor/ArduinoJson/LICENSE.md)
#include <string.h>

namespace ZenoPCB
{

    const char *IrrigationStorage::DIR_PATH = "/irrigation";
    const char *IrrigationStorage::META_FILE = "/irrigation/meta.json";

    IZenoHal *IrrigationStorage::_hal = nullptr;

    namespace
    {
        // Per-scenario file may grow to ~1.2 KB worst case (MAX_IRRIGATION_STEPS
        // keyCount IRRIGATION_KEY_LEN). 2 KB leaves headroom.
        constexpr size_t SCENARIO_FILE_BUF_SIZE = 2048;
        // Per-schedule file is small (id + sid + flags + executeTime + 7 days).
        constexpr size_t SCHEDULE_FILE_BUF_SIZE = 1024;
        // meta.json is tiny.
        constexpr size_t META_FILE_BUF_SIZE = 256;
    } // namespace

    void IrrigationStorage::setHal(IZenoHal *hal)
    {
        _hal = hal;
    }

    String IrrigationStorage::getScenarioFilePath(const String &sid)
    {
        return String(DIR_PATH) + "/" + sid + ".json";
    }

    String IrrigationStorage::getScheduleFilePath(const String &id)
    {
        return String(DIR_PATH) + "/sch_" + id + ".json";
    }

    String IrrigationStorage::getMetadataFilePath()
    {
        return String(META_FILE);
    }

    bool IrrigationStorage::ensureDirectory()
    {
        if (!_hal || !(_hal->capabilities() & IZenoHal::CAP_FS_FILES))
        {
            ZENO_LOG("IrrigationStorage", "HAL not set or no filesystem capability");
            return false;
        }
        if (!_hal->storage().exists(DIR_PATH))
        {
            ZENO_LOG("IrrigationStorage", "Creating directory: %s", DIR_PATH);
            return _hal->storage().mkdir(DIR_PATH);
        }
        return true;
    }

    // ============================================
    // Save scenario via HAL
    // ============================================

    bool IrrigationStorage::saveScenario(const IrrigationScenarioConfig &config)
    {
        if (!_hal || !(_hal->capabilities() & IZenoHal::CAP_FS_FILES))
        {
            ZENO_LOG("IrrigationStorage", "HAL not set or no filesystem capability");
            return false;
        }

        if (!ensureDirectory())
        {
            ZENO_LOG("IrrigationStorage", "Failed to create directory");
            return false;
        }

        if (!scenarioExists(config.scenarioId) && isMaxScenariosReached())
        {
            ZENO_LOG("IrrigationStorage", "Max scenarios reached (%d)", MAX_IRRIGATION_SCENARIOS);
            return false;
        }

        String filePath = getScenarioFilePath(config.scenarioId);

        JsonDocument doc;

        doc["sid"] = config.scenarioId;

        // Steps array
        JsonArray stepsArr = doc["steps"].to<JsonArray>();
        for (uint8_t i = 0; i < config.stepCount && i < MAX_IRRIGATION_STEPS; i++)
        {
            const IrrigationStep &step = config.steps[i];
            JsonObject stepObj = stepsArr.add<JsonObject>();
            stepObj["o"] = step.order;
            stepObj["a"] = irrigationActionToCode(step.action);

            if (isWaitAction(step.action))
            {
                stepObj["dur"] = step.waitDuration;
            }
            else
            {
                JsonArray keys = stepObj["k"].to<JsonArray>();
                for (uint8_t j = 0; j < step.keyCount && j < MAX_IRRIGATION_TARGETS; j++)
                {
                    keys.add(step.mqttKeys[j]);
                }
            }
        }

        doc["en"] = config.enabled;

        String jsonStr;
        if (serializeJson(doc, jsonStr) == 0)
        {
            ZENO_LOG("IrrigationStorage", "Failed to serialize");
            return false;
        }

        size_t written = _hal->storage().writeFile(filePath.c_str(),
                                                   jsonStr.c_str(),
                                                   jsonStr.length());
        if (written != jsonStr.length())
        {
            ZENO_LOG("IrrigationStorage", "Short write: %s (%u/%u)",
                     filePath.c_str(), (unsigned)written, (unsigned)jsonStr.length());
            return false;
        }

        // Update metadata
        IrrigationMetadata meta;
        readMetadata(meta);
        meta.count = getScenarioCount();
        meta.lastUpdated = (uint32_t)time(nullptr);
        writeMetadata(meta);

        ZENO_LOG("IrrigationStorage", "Saved: %s (%d steps)", config.scenarioId, config.stepCount);
        return true;
    }

    // ============================================
    // Load scenario via HAL
    // ============================================

    bool IrrigationStorage::loadScenario(const String &sid, IrrigationScenarioConfig &out)
    {
        if (!_hal || !(_hal->capabilities() & IZenoHal::CAP_FS_FILES))
        {
            ZENO_LOG("IrrigationStorage", "HAL not set or no filesystem capability");
            return false;
        }

        String filePath = getScenarioFilePath(sid);

        if (!_hal->storage().exists(filePath.c_str()))
        {
            ZENO_LOG("IrrigationStorage", "Not found: %s", sid.c_str());
            return false;
        }

        char buf[SCENARIO_FILE_BUF_SIZE];
        size_t bytesRead = _hal->storage().readFile(filePath.c_str(), buf, sizeof(buf));
        if (bytesRead == 0)
        {
            ZENO_LOG("IrrigationStorage", "Failed to read: %s", filePath.c_str());
            return false;
        }

        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, buf, bytesRead);

        if (error)
        {
            ZENO_LOG("IrrigationStorage", "JSON parse error: %s", error.c_str());
            return false;
        }

        // Parse identification
        strlcpy(out.scenarioId, doc["sid"] | "", sizeof(out.scenarioId));

        // Parse steps
        JsonArray stepsArr = doc["steps"];
        out.stepCount = 0;
        for (JsonObject stepObj : stepsArr)
        {
            if (out.stepCount >= MAX_IRRIGATION_STEPS)
                break;

            IrrigationStep &step = out.steps[out.stepCount];
            step.order = stepObj["o"] | 0;
            step.action = parseIrrigationAction(stepObj["a"] | "w");

            if (isWaitAction(step.action))
            {
                step.waitDuration = stepObj["dur"] | 0;
                step.keyCount = 0;
            }
            else
            {
                step.waitDuration = 0;
                JsonArray keys = stepObj["k"];
                step.keyCount = 0;
                for (const char *key : keys)
                {
                    if (step.keyCount >= MAX_IRRIGATION_TARGETS)
                        break;
                    if (key)
                    {
                        strlcpy(step.mqttKeys[step.keyCount], key, IRRIGATION_KEY_LEN);
                        step.keyCount++;
                    }
                }
            }

            out.stepCount++;
        }

        out.enabled = doc["en"] | true;

        ZENO_LOG("IrrigationStorage", "Loaded: %s (%d steps)", sid.c_str(), out.stepCount);
        return true;
    }

    // ============================================
    // Delete
    // ============================================

    bool IrrigationStorage::deleteScenario(const String &sid)
    {
        if (!_hal || !(_hal->capabilities() & IZenoHal::CAP_FS_FILES))
        {
            return false;
        }

        String filePath = getScenarioFilePath(sid);

        if (!_hal->storage().exists(filePath.c_str()))
        {
            ZENO_LOG("IrrigationStorage", "Not found for deletion: %s", sid.c_str());
            return false;
        }

        bool result = _hal->storage().deleteFile(filePath.c_str());
        if (result)
        {
            // Update metadata
            IrrigationMetadata meta;
            readMetadata(meta);
            meta.count = getScenarioCount();
            meta.lastUpdated = (uint32_t)time(nullptr);
            writeMetadata(meta);

            ZENO_LOG("IrrigationStorage", "Deleted: %s", sid.c_str());
        }
        else
        {
            ZENO_LOG("IrrigationStorage", "Failed to delete: %s", sid.c_str());
        }

        return result;
    }

    bool IrrigationStorage::scenarioExists(const String &sid)
    {
        if (!_hal || !(_hal->capabilities() & IZenoHal::CAP_FS_FILES))
        {
            return false;
        }
        return _hal->storage().exists(getScenarioFilePath(sid).c_str());
    }

    // ============================================
    // Load all
    // ============================================
    //
    // The pre-refactor implementation iterated `LittleFS.open(DIR_PATH, "r")`
    // with `dir.openNextFile()` and classified entries by filename. We use
    // `_hal->storage().listFiles("/irrigation/", cb)` and apply the same
    // classification rules: skip meta.json and any "sch_*" file when
    // collecting scenarios; pick only "sch_*" files when collecting
    // schedules. File-path strings unchanged (EDGE-02 preserved).

    bool IrrigationStorage::loadAllScenarios(std::vector<IrrigationScenarioConfig> &out)
    {
        out.clear();

        if (!_hal || !(_hal->capabilities() & IZenoHal::CAP_FS_FILES))
        {
            return false;
        }

        if (!_hal->storage().exists(DIR_PATH))
        {
            ZENO_LOG("IrrigationStorage", "Directory not found, returning empty");
            return true;
        }

        std::vector<String> sidsToLoad;
        const char *prefix = "/irrigation/";
        _hal->storage().listFiles(prefix, [&sidsToLoad](const char *path) {
            if (!path) return;
            const char *base = strrchr(path, '/');
            if (!base) return;
            ++base;

            // Skip meta and schedule files
            if (strcmp(base, "meta.json") == 0) return;
            if (strncmp(base, "sch_", 4) == 0) return;

            size_t baseLen = strlen(base);
            if (baseLen <= 5) return;
            if (strcmp(base + baseLen - 5, ".json") != 0) return;

            String sid;
            sid.reserve(baseLen - 5);
            for (size_t i = 0; i + 5 < baseLen; ++i) sid += base[i];
            sidsToLoad.push_back(sid);
        });

        for (const String &sid : sidsToLoad)
        {
            IrrigationScenarioConfig config;
            if (loadScenario(sid, config))
            {
                out.push_back(config);
            }
        }

        ZENO_LOG("IrrigationStorage", "Loaded %d scenarios", out.size());
        return true;
    }

    bool IrrigationStorage::clearAllScenarios()
    {
        if (!_hal || !(_hal->capabilities() & IZenoHal::CAP_FS_FILES))
        {
            return false;
        }

        if (!_hal->storage().exists(DIR_PATH))
        {
            return true;
        }

        std::vector<String> filesToDelete;
        const char *prefix = "/irrigation/";
        _hal->storage().listFiles(prefix, [&filesToDelete](const char *path) {
            if (!path) return;
            const char *base = strrchr(path, '/');
            if (!base) return;
            ++base;
            if (strcmp(base, "meta.json") == 0) return;
            if (strncmp(base, "sch_", 4) == 0) return;
            size_t baseLen = strlen(base);
            if (baseLen <= 5) return;
            if (strcmp(base + baseLen - 5, ".json") != 0) return;
            filesToDelete.push_back(String(path));
        });

        for (const auto &path : filesToDelete)
        {
            _hal->storage().deleteFile(path.c_str());
        }

        IrrigationMetadata meta;
        meta.count = 0;
        meta.lastUpdated = (uint32_t)time(nullptr);
        writeMetadata(meta);

        ZENO_LOG("IrrigationStorage", "Cleared all scenarios");
        return true;
    }

    uint8_t IrrigationStorage::getScenarioCount()
    {
        if (!_hal || !(_hal->capabilities() & IZenoHal::CAP_FS_FILES))
        {
            return 0;
        }
        if (!_hal->storage().exists(DIR_PATH))
        {
            return 0;
        }

        uint8_t count = 0;
        const char *prefix = "/irrigation/";
        _hal->storage().listFiles(prefix, [&count](const char *path) {
            if (!path) return;
            const char *base = strrchr(path, '/');
            if (!base) return;
            ++base;
            if (strcmp(base, "meta.json") == 0) return;
            if (strncmp(base, "sch_", 4) == 0) return;
            size_t baseLen = strlen(base);
            if (baseLen <= 5) return;
            if (strcmp(base + baseLen - 5, ".json") != 0) return;
            ++count;
        });

        return count;
    }

    bool IrrigationStorage::isMaxScenariosReached()
    {
        return getScenarioCount() >= MAX_IRRIGATION_SCENARIOS;
    }

    // ============================================
    // Schedule CRUD (V3 separate entity)
    // ============================================

    bool IrrigationStorage::saveSchedule(const IrrigationScheduleConfig &config)
    {
        if (!_hal || !(_hal->capabilities() & IZenoHal::CAP_FS_FILES))
        {
            return false;
        }

        if (!ensureDirectory())
            return false;

        if (!scheduleExists(config.scheduleId) && isMaxSchedulesReached())
        {
            ZENO_LOG("IrrigationStorage", "Max schedules reached (%d)", MAX_IRRIGATION_SCHEDULES);
            return false;
        }

        String filePath = getScheduleFilePath(config.scheduleId);
        JsonDocument doc;

        doc["id"] = config.scheduleId;
        doc["sid"] = config.scenarioId;
        doc["en"] = config.enabled;
        char typeStr[2] = {(char)config.scheduleType, '\0'};
        doc["st"] = typeStr;

        if (config.isRecurring())
        {
            doc["et"] = config.executeTime;
            JsonArray rd = doc["rd"].to<JsonArray>();
            for (uint8_t i = 0; i < config.repeatDaysCount && i < 7; i++)
            {
                rd.add(config.repeatDays[i]);
            }
        }
        if (config.isOnce())
        {
            doc["ea"] = config.executeAt;
        }

        String jsonStr;
        if (serializeJson(doc, jsonStr) == 0)
        {
            ZENO_LOG("IrrigationStorage", "Failed to serialize schedule");
            return false;
        }

        size_t written = _hal->storage().writeFile(filePath.c_str(),
                                                   jsonStr.c_str(),
                                                   jsonStr.length());
        if (written != jsonStr.length())
        {
            ZENO_LOG("IrrigationStorage", "Short write schedule: %s (%u/%u)",
                     filePath.c_str(), (unsigned)written, (unsigned)jsonStr.length());
            return false;
        }

        ZENO_LOG("IrrigationStorage", "Schedule saved: %s scenario %s",
                 config.scheduleId, config.scenarioId);
        return true;
    }

    bool IrrigationStorage::loadSchedule(const String &id, IrrigationScheduleConfig &out)
    {
        if (!_hal || !(_hal->capabilities() & IZenoHal::CAP_FS_FILES))
        {
            return false;
        }

        String filePath = getScheduleFilePath(id);

        if (!_hal->storage().exists(filePath.c_str()))
            return false;

        char buf[SCHEDULE_FILE_BUF_SIZE];
        size_t bytesRead = _hal->storage().readFile(filePath.c_str(), buf, sizeof(buf));
        if (bytesRead == 0)
            return false;

        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, buf, bytesRead);

        if (error)
        {
            ZENO_LOG("IrrigationStorage", "Schedule JSON error: %s", error.c_str());
            return false;
        }

        memset(&out, 0, sizeof(IrrigationScheduleConfig));
        strlcpy(out.scheduleId, doc["id"] | "", sizeof(out.scheduleId));
        strlcpy(out.scenarioId, doc["sid"] | "", sizeof(out.scenarioId));
        out.enabled = doc["en"] | true;

        const char *st = doc["st"] | "N";
        out.scheduleType = parseIrrigationScheduleType(st[0]);

        if (out.scheduleType == IrrigationScheduleType::RECURRING)
        {
            strlcpy(out.executeTime, doc["et"] | "", sizeof(out.executeTime));
            JsonArray rd = doc["rd"];
            out.repeatDaysCount = 0;
            for (uint8_t d : rd)
            {
                if (out.repeatDaysCount >= 7)
                    break;
                out.repeatDays[out.repeatDaysCount++] = d;
            }
        }
        if (out.scheduleType == IrrigationScheduleType::ONCE)
        {
            out.executeAt = doc["ea"] | (uint32_t)0;
        }

        ZENO_LOG("IrrigationStorage", "Schedule loaded: %s (type=%c)", id.c_str(), (char)out.scheduleType);
        return true;
    }

    bool IrrigationStorage::deleteSchedule(const String &id)
    {
        if (!_hal || !(_hal->capabilities() & IZenoHal::CAP_FS_FILES))
        {
            return false;
        }

        String filePath = getScheduleFilePath(id);
        if (!_hal->storage().exists(filePath.c_str()))
        {
            ZENO_LOG("IrrigationStorage", "Schedule not found: %s", id.c_str());
            return false;
        }
        bool result = _hal->storage().deleteFile(filePath.c_str());
        if (result)
            ZENO_LOG("IrrigationStorage", "Schedule deleted: %s", id.c_str());
        return result;
    }

    bool IrrigationStorage::scheduleExists(const String &id)
    {
        if (!_hal || !(_hal->capabilities() & IZenoHal::CAP_FS_FILES))
        {
            return false;
        }
        return _hal->storage().exists(getScheduleFilePath(id).c_str());
    }

    bool IrrigationStorage::loadAllSchedules(std::vector<IrrigationScheduleConfig> &out)
    {
        out.clear();

        if (!_hal || !(_hal->capabilities() & IZenoHal::CAP_FS_FILES))
        {
            return false;
        }

        if (!_hal->storage().exists(DIR_PATH))
            return true;

        std::vector<String> idsToLoad;
        const char *prefix = "/irrigation/";
        _hal->storage().listFiles(prefix, [&idsToLoad](const char *path) {
            if (!path) return;
            const char *base = strrchr(path, '/');
            if (!base) return;
            ++base;

            if (strncmp(base, "sch_", 4) != 0) return;
            size_t baseLen = strlen(base);
            if (baseLen <= 9) return; // 4 ("sch_") + ".json" = 9 min
            if (strcmp(base + baseLen - 5, ".json") != 0) return;

            // id = base[4 .. baseLen-5]
            String id;
            id.reserve(baseLen - 9);
            for (size_t i = 4; i + 5 < baseLen; ++i) id += base[i];
            idsToLoad.push_back(id);
        });

        for (const String &id : idsToLoad)
        {
            IrrigationScheduleConfig config;
            if (loadSchedule(id, config))
            {
                out.push_back(config);
            }
        }

        ZENO_LOG("IrrigationStorage", "Loaded %d schedules", out.size());
        return true;
    }

    bool IrrigationStorage::clearAllSchedules()
    {
        if (!_hal || !(_hal->capabilities() & IZenoHal::CAP_FS_FILES))
        {
            return false;
        }

        if (!_hal->storage().exists(DIR_PATH))
            return true;

        std::vector<String> filesToDelete;
        const char *prefix = "/irrigation/";
        _hal->storage().listFiles(prefix, [&filesToDelete](const char *path) {
            if (!path) return;
            const char *base = strrchr(path, '/');
            if (!base) return;
            ++base;
            if (strncmp(base, "sch_", 4) != 0) return;
            size_t baseLen = strlen(base);
            if (baseLen <= 5) return;
            if (strcmp(base + baseLen - 5, ".json") != 0) return;
            filesToDelete.push_back(String(path));
        });

        for (const auto &path : filesToDelete)
        {
            _hal->storage().deleteFile(path.c_str());
        }

        ZENO_LOG("IrrigationStorage", "Cleared all schedules (%d)", filesToDelete.size());
        return true;
    }

    uint8_t IrrigationStorage::getScheduleCount()
    {
        if (!_hal || !(_hal->capabilities() & IZenoHal::CAP_FS_FILES))
        {
            return 0;
        }
        if (!_hal->storage().exists(DIR_PATH))
            return 0;

        uint8_t count = 0;
        const char *prefix = "/irrigation/";
        _hal->storage().listFiles(prefix, [&count](const char *path) {
            if (!path) return;
            const char *base = strrchr(path, '/');
            if (!base) return;
            ++base;
            if (strncmp(base, "sch_", 4) != 0) return;
            size_t baseLen = strlen(base);
            if (baseLen <= 5) return;
            if (strcmp(base + baseLen - 5, ".json") != 0) return;
            ++count;
        });

        return count;
    }

    bool IrrigationStorage::isMaxSchedulesReached()
    {
        return getScheduleCount() >= MAX_IRRIGATION_SCHEDULES;
    }

    bool IrrigationStorage::clearAll()
    {
        bool ok = true;
        if (!clearAllScenarios())
            ok = false;
        if (!clearAllSchedules())
            ok = false;
        ZENO_LOG("IrrigationStorage", "Clear all: %s", ok ? "OK" : "PARTIAL");
        return ok;
    }

    // ============================================
    // Metadata
    // ============================================

    bool IrrigationStorage::readMetadata(IrrigationMetadata &outMeta)
    {
        if (!_hal || !(_hal->capabilities() & IZenoHal::CAP_FS_FILES))
        {
            outMeta = IrrigationMetadata();
            return false;
        }

        if (!_hal->storage().exists(META_FILE))
        {
            outMeta = IrrigationMetadata();
            return false;
        }

        char buf[META_FILE_BUF_SIZE];
        size_t bytesRead = _hal->storage().readFile(META_FILE, buf, sizeof(buf));
        if (bytesRead == 0)
        {
            return false;
        }

        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, buf, bytesRead);

        if (error)
        {
            return false;
        }

        outMeta.count = doc["count"] | (uint8_t)0;
        outMeta.lastUpdated = doc["lastUpdated"] | (uint32_t)0;
        return true;
    }

    bool IrrigationStorage::writeMetadata(const IrrigationMetadata &meta)
    {
        if (!_hal || !(_hal->capabilities() & IZenoHal::CAP_FS_FILES))
        {
            return false;
        }

        if (!ensureDirectory())
        {
            return false;
        }

        JsonDocument doc;
        doc["count"] = meta.count;
        doc["lastUpdated"] = meta.lastUpdated;

        String jsonStr;
        if (serializeJson(doc, jsonStr) == 0)
        {
            return false;
        }

        size_t written = _hal->storage().writeFile(META_FILE,
                                                   jsonStr.c_str(),
                                                   jsonStr.length());
        return written == jsonStr.length();
    }

} // namespace ZenoPCB

#endif  // defined(ESP32)
