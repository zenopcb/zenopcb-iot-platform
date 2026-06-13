#ifndef ZENOPCB_IRRIGATION_SCHEDULER_H
#define ZENOPCB_IRRIGATION_SCHEDULER_H

#include <Arduino.h>
#include <vector>
#include <map>
#include "IrrigationTypes.h"

namespace ZenoPCB
{

    class IrrigationScheduler
    {
    public:
        static IrrigationScheduler &getInstance();

        void begin();
        void loop();

        void reloadSchedules();
        void addOrUpdateSchedule(const IrrigationScheduleConfig &config);
        void removeSchedule(const String &id);
        void clearAll();

        uint8_t getScheduleCount() const { return _schedules.size(); }
        bool hasSchedule(const String &id) const;

        void setTriggerCallback(IrrigationScheduleTriggerCallback cb) { _triggerCb = cb; }

    private:
        IrrigationScheduler();
        IrrigationScheduler(const IrrigationScheduler &) = delete;
        IrrigationScheduler &operator=(const IrrigationScheduler &) = delete;

        void _checkRecurringSchedules();
        void _checkOnceSchedules();
        void _resetDailyFlagsIfNeeded();
        bool _shouldExecuteToday(const IrrigationScheduleConfig &config, uint8_t tmWday) const;
        bool _isTimeToExecute(const IrrigationScheduleConfig &config, const struct tm &timeinfo) const;

        IrrigationScheduleState &_getOrCreateState(const String &id);

        std::vector<IrrigationScheduleConfig> _schedules;
        std::map<String, IrrigationScheduleState> _states;

        IrrigationScheduleTriggerCallback _triggerCb;

        bool _initialized;
        uint8_t _lastDay;
        time_t _lastMidnightCheck;
    };

} // namespace ZenoPCB

#endif // ZENOPCB_IRRIGATION_SCHEDULER_H
