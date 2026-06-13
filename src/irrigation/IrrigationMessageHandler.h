#ifndef ZENOPCB_IRRIGATION_MESSAGE_HANDLER_H
#define ZENOPCB_IRRIGATION_MESSAGE_HANDLER_H

#include <Arduino.h>
#include "../ZenoJson.h"  // ArduinoJson API from vendored copy (namespace ZenoJson; see vendor/ArduinoJson/LICENSE.md)
#include <functional>
#include "IrrigationTypes.h"

namespace ZenoPCB
{

    /**
     * @brief Callbacks for irrigation message events
     */
    using IrrigationSyncedCallback = std::function<void(const char *scenarioId)>;
    using IrrigationDeletedCallback = std::function<void(const char *scenarioId)>;
    using IrrigationMessageErrorCallback = std::function<void(const String &error, const String &payload)>;

    /**
     * @brief MQTT Message Handler for Irrigation Module (V3)
     *
     * Handles incoming MQTT messages:
     * {
     *   "t": "irrigation",
     *   "a": "execute|ss|ds|sc|dc|fa",
     *   "d": { ... }
     * }
     *
     * Actions:
     *   execute — run scenario immediately (inline steps, NOT saved)
     *   ss      — sync scenario to flash (create/update)
     *   ds      — delete scenario from flash
     *   sc      — sync schedule to flash (create/update, separate entity)
     *   dc      — delete schedule from flash
     *   fa      — full sync (clear all + save all scenarios + schedules)
     */
    class IrrigationMessageHandler
    {
    public:
        static IrrigationMessageHandler &getInstance();

        IrrigationHandleResult handleMessage(const String &topic, const String &payload);

        // Callback Registration
        void onSynced(IrrigationSyncedCallback callback);
        void onDeleted(IrrigationDeletedCallback callback);
        void onError(IrrigationMessageErrorCallback callback);

    private:
        IrrigationMessageHandler() = default;
        IrrigationMessageHandler(const IrrigationMessageHandler &) = delete;
        IrrigationMessageHandler &operator=(const IrrigationMessageHandler &) = delete;

        // Parsing
        bool _parsePayload(const String &payload, JsonDocument &doc, String &error);
        bool _validateMessageStructure(const JsonDocument &doc, String &error);

        // V3 Action Handlers (6 actions)
        IrrigationHandleResult _handleExecute(const JsonObject &data);
        IrrigationHandleResult _handleSyncScenario(const JsonObject &data);
        IrrigationHandleResult _handleDeleteScenario(const JsonObject &data);
        IrrigationHandleResult _handleSyncSchedule(const JsonObject &data);
        IrrigationHandleResult _handleDeleteSchedule(const JsonObject &data);
        IrrigationHandleResult _handleFullSync(const JsonObject &data);

        // Step/Schedule Parsing
        bool _parseSteps(const JsonArray &stepsArr,
                         IrrigationStep *outSteps,
                         uint8_t &outCount,
                         String &error);

        // Callbacks
        IrrigationSyncedCallback _onSyncedCallback = nullptr;
        IrrigationDeletedCallback _onDeletedCallback = nullptr;
        IrrigationMessageErrorCallback _onErrorCallback = nullptr;
    };

} // namespace ZenoPCB

#endif // ZENOPCB_IRRIGATION_MESSAGE_HANDLER_H
