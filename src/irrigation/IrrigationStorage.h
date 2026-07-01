#ifndef ZENOPCB_IRRIGATION_STORAGE_H
#define ZENOPCB_IRRIGATION_STORAGE_H

#include <Arduino.h>
#include <vector>
#include "IrrigationTypes.h"
#include "../hal/IZenoHal.h"

namespace ZenoPCB
{

    struct IrrigationMetadata
    {
        uint8_t count;
        uint32_t lastUpdated;

        IrrigationMetadata() : count(0), lastUpdated(0) {}
    };

    /**
     * @brief Irrigation scenario + schedule persistence (via HAL).
     *
     * all file I/O routes through `IZenoStorage` injected via
     * `setHal(IZenoHal*)`. wires the canonical ESP32 HAL from
     * `Zeno::begin()`. Until then methods early-return when no HAL is set.
     */
    class IrrigationStorage
    {
    public:
        // ============================================
        // HAL injection
        // ============================================
        static void setHal(IZenoHal *hal);

        // Scenario CRUD
        static bool saveScenario(const IrrigationScenarioConfig &config);
        static bool loadScenario(const String &sid, IrrigationScenarioConfig &out);
        static bool deleteScenario(const String &sid);
        static bool scenarioExists(const String &sid);
        static bool loadAllScenarios(std::vector<IrrigationScenarioConfig> &out);
        static bool clearAllScenarios();
        static uint8_t getScenarioCount();
        static bool isMaxScenariosReached();

        // Schedule CRUD (V3 separate entity)
        static bool saveSchedule(const IrrigationScheduleConfig &config);
        static bool loadSchedule(const String &id, IrrigationScheduleConfig &out);
        static bool deleteSchedule(const String &id);
        static bool scheduleExists(const String &id);
        static bool loadAllSchedules(std::vector<IrrigationScheduleConfig> &out);
        static bool clearAllSchedules();
        static uint8_t getScheduleCount();
        static bool isMaxSchedulesReached();

        // Clear everything (for "fa" full sync)
        static bool clearAll();

        static bool readMetadata(IrrigationMetadata &outMeta);
        static bool writeMetadata(const IrrigationMetadata &meta);

        static String getScenarioFilePath(const String &sid);
        static String getScheduleFilePath(const String &id);
        static String getMetadataFilePath();
        static bool ensureDirectory();

    private:
        static const char *DIR_PATH;
        static const char *META_FILE;
        static IZenoHal *_hal;
    };

} // namespace ZenoPCB

#endif // ZENOPCB_IRRIGATION_STORAGE_H
