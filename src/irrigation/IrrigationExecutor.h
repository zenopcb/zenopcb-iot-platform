#ifndef ZENOPCB_IRRIGATION_EXECUTOR_H
#define ZENOPCB_IRRIGATION_EXECUTOR_H

#include <Arduino.h>
#include "IrrigationTypes.h"

namespace ZenoPCB
{

    class IrrigationExecutor
    {
    public:
        static IrrigationExecutor &getInstance();

        void begin();
        void loop();

        bool startExecution(const char *scenarioId, const char *executionId,
                            uint32_t timestamp,
                            const IrrigationStep *steps, uint8_t stepCount);

        bool startFromStorage(const char *scenarioId, const char *executionId);

        void stopExecution();

        bool isRunning() const { return _execution.status == IrrigationStatus::RUNNING; }
        IrrigationStatus getStatus() const { return _execution.status; }
        uint8_t getCurrentStep() const { return _execution.currentStep; }
        const IrrigationExecution &getExecution() const { return _execution; }

        // Callbacks
        void setWriteFunction(IrrigationWriteCallback cb) { _writeFn = cb; }
        void onStepProgress(IrrigationStepCallback cb) { _onStepCb = cb; }
        void onCompleted(IrrigationCompletedCallback cb) { _onCompletedCb = cb; }
        void onError(IrrigationErrorCallback cb) { _onErrorCb = cb; }

    private:
        IrrigationExecutor();
        IrrigationExecutor(const IrrigationExecutor &) = delete;
        IrrigationExecutor &operator=(const IrrigationExecutor &) = delete;

        void _executeCurrentStep();
        void _advanceToNextStep();
        void _completeExecution();
        void _failExecution(const char *error);
        void _safeStopAllOutputs();

        IrrigationExecution _execution;
        StepPhase _phase;
        uint32_t _phaseStartMs;

        IrrigationWriteCallback _writeFn;
        IrrigationStepCallback _onStepCb;
        IrrigationCompletedCallback _onCompletedCb;
        IrrigationErrorCallback _onErrorCb;

        bool _initialized;
    };

} // namespace ZenoPCB

#endif // ZENOPCB_IRRIGATION_EXECUTOR_H
