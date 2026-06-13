#ifndef ZENOPCB_SCHEDULE_STORAGE_H
#define ZENOPCB_SCHEDULE_STORAGE_H

#include <Arduino.h>
#include <vector>
#include "../schedule/ScheduleConfig.h"
#include "../hal/IZenoHal.h"

namespace ZenoPCB
{

    /**
     * @brief Schedule metadata structure
     *
     * Stored in /schedules/meta.json
     */
    struct ScheduleMetadata
    {
        uint8_t count;        // Number of schedules
        uint32_t lastUpdated; // Unix timestamp of last update
        uint32_t version;     // Metadata version (for future migration)

        ScheduleMetadata() : count(0), lastUpdated(0), version(1) {}
    };

    /**
     * @brief Schedule storage manager for filesystem operations (via HAL)
     *
     * Handles CRUD operations for schedule configurations stored in:
     * - /schedules/0001.json
     * - /schedules/0002.json
     * - ...
     * - /schedules/meta.json
     *
     * @note Uses singleton/static pattern. Plan 04-03 — file I/O routes
     *       through `IZenoStorage` injected via `setHal(IZenoHal*)`;
     *       Plan 04-05 wires the canonical ESP32 HAL from `Zeno::begin()`.
     */
    class ScheduleStorage
    {
    public:
        // ============================================
        // HAL injection (Plan 04-03)
        // ============================================

        /**
         * @brief Inject the HAL used for all filesystem ops.
         *
         * Static-pointer injection — chosen for ScheduleStorage's static
         * class design (per RESEARCH "Dependency Injection Pattern").
         * Called once at boot from Plan 04-05; until then methods early-
         * return false / empty when `_hal == nullptr`.
         */
        static void setHal(IZenoHal *hal);

        // ============================================
        // Schedule CRUD Operations
        // ============================================

        /**
         * @brief Create or update a schedule
         *
         * @param config Schedule configuration
         * @return true if saved successfully
         * @return false if failed (e.g., disk full, max schedules reached)
         */
        static bool saveSchedule(const ScheduleConfig &config);

        /**
         * @brief Read a schedule by ID
         *
         * @param scheduleId Schedule ID (e.g., "0001")
         * @param outConfig Output schedule configuration
         * @return true if found and loaded
         * @return false if not found or read error
         */
        static bool loadSchedule(const String &scheduleId, ScheduleConfig &outConfig);

        /**
         * @brief Delete a schedule by ID
         *
         * @param scheduleId Schedule ID (e.g., "0001")
         * @return true if deleted successfully
         * @return false if not found or delete error
         */
        static bool deleteSchedule(const String &scheduleId);

        /**
         * @brief Check if schedule exists
         *
         * @param scheduleId Schedule ID
         * @return true if schedule file exists
         */
        static bool scheduleExists(const String &scheduleId);

        /**
         * @brief Load all schedules from storage
         *
         * @param outSchedules Output vector of schedules
         * @return true if loaded successfully (even if empty)
         * @return false if critical error
         */
        static bool loadAllSchedules(std::vector<ScheduleConfig> &outSchedules);

        /**
         * @brief Delete all schedules (used for full sync with empty array)
         *
         * @return true if all schedules deleted
         * @return false if error
         */
        static bool clearAllSchedules();

        /**
         * @brief Get count of schedules
         *
         * @return uint8_t Number of schedules (0-20)
         */
        static uint8_t getScheduleCount();

        /**
         * @brief Check if max schedules limit reached
         *
         * @return true if count >= MAX_SCHEDULES (20)
         */
        static bool isMaxSchedulesReached();

        // ============================================
        // Metadata Operations
        // ============================================

        /**
         * @brief Read schedule metadata
         *
         * @param outMeta Output metadata structure
         * @return true if read successfully
         * @return false if not found or error (uses defaults)
         */
        static bool readMetadata(ScheduleMetadata &outMeta);

        /**
         * @brief Write schedule metadata
         *
         * @param meta Metadata to write
         * @return true if written successfully
         * @return false if error
         */
        static bool writeMetadata(const ScheduleMetadata &meta);

        /**
         * @brief Update metadata after schedule change
         *
         * Automatically updates count and lastUpdated timestamp
         *
         * @param action Action performed (create/update/delete/sync)
         * @return true if updated successfully
         */
        static bool updateMetadataAfterChange(ScheduleAction action);

        // ============================================
        // Schedule Listing
        // ============================================

        /**
         * @brief List all schedule IDs
         *
         * @return std::vector<String> Vector of schedule IDs (e.g., ["0001", "0002"])
         */
        static std::vector<String> listAllScheduleIds();

        // ============================================
        // Utilities
        // ============================================

        /**
         * @brief Get file path for schedule
         *
         * @param scheduleId Schedule ID (e.g., "0001")
         * @return String File path (e.g., "/schedules/0001.json")
         */
        static String getScheduleFilePath(const String &scheduleId);

        /**
         * @brief Get metadata file path
         *
         * @return String Metadata file path ("/schedules/meta.json")
         */
        static String getMetadataFilePath();

        /**
         * @brief Ensure schedules directory exists
         *
         * @return true if directory exists or created
         * @return false if error
         */
        static bool ensureScheduleDirectory();

    private:
        static const char *SCHEDULE_DIR;
        static const char *METADATA_FILE;
        static IZenoHal *_hal;
    };

} // namespace ZenoPCB

#endif // ZENOPCB_SCHEDULE_STORAGE_H
