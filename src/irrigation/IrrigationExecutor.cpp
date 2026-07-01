// Irrigation subsystem is ESP32-only.
#if defined(ESP32)

#include "IrrigationExecutor.h"
#include "IrrigationStorage.h"
#include "../core/ZenoPCBDebug.h"

namespace ZenoPCB
{

    IrrigationExecutor &IrrigationExecutor::getInstance()
    {
        static IrrigationExecutor instance;
        return instance;
    }

    IrrigationExecutor::IrrigationExecutor()
        : _phase(StepPhase::IDLE),
          _phaseStartMs(0),
          _writeFn(nullptr),
          _onStepCb(nullptr),
          _onCompletedCb(nullptr),
          _onErrorCb(nullptr),
          _initialized(false)
    {
    }

    void IrrigationExecutor::begin()
    {
        _execution.reset();
        _phase = StepPhase::IDLE;
        _initialized = true;
        ZENO_LOG("IrrigationExecutor", "Initialized");
    }

    // ============================================
    // Start execution from inline steps (MQTT execute)
    // ============================================

    bool IrrigationExecutor::startExecution(const char *scenarioId,
                                            const char *executionId,
                                            uint32_t timestamp,
                                            const IrrigationStep *steps,
                                            uint8_t stepCount)
    {
        if (!_initialized)
            return false;

        if (isRunning())
        {
            ZENO_LOG("IrrigationExecutor", "Already running scenario %s", _execution.scenarioId);
            return false;
        }

        if (stepCount == 0 || stepCount > MAX_IRRIGATION_STEPS)
        {
            ZENO_LOG("IrrigationExecutor", "Invalid step count: %d", stepCount);
            return false;
        }

        if (!_writeFn)
        {
            ZENO_LOG("IrrigationExecutor", "No write function set");
            return false;
        }

        // Setup execution context
        _execution.reset();
        strlcpy(_execution.scenarioId, scenarioId, IRRIGATION_SID_LEN);
        strlcpy(_execution.executionId, executionId ? executionId : "",
                IRRIGATION_EID_LEN);
        _execution.timestamp = timestamp;
        _execution.stepCount = stepCount;
        _execution.currentStep = 0;
        _execution.status = IrrigationStatus::RUNNING;
        _execution.startTimeMs = millis();

        // Copy steps
        for (uint8_t i = 0; i < stepCount; i++)
        {
            _execution.steps[i] = steps[i];
        }

        _phase = StepPhase::EXECUTING;

        ZENO_LOG("IrrigationExecutor", "Starting scenario %s (%d steps)",
                 scenarioId, stepCount);

        // Execute first step immediately
        _executeCurrentStep();

        return true;
    }

    // ============================================
    // Start from storage (scheduler trigger)
    // ============================================

    bool IrrigationExecutor::startFromStorage(const char *scenarioId,
                                              const char *executionId)
    {
        IrrigationScenarioConfig config;
        if (!IrrigationStorage::loadScenario(scenarioId, config))
        {
            ZENO_LOG("IrrigationExecutor", "Failed to load scenario %s from storage",
                     scenarioId);
            return false;
        }

        return startExecution(config.scenarioId, executionId,
                              (uint32_t)time(nullptr),
                              config.steps, config.stepCount);
    }

    // ============================================
    // Stop execution safe state
    // ============================================

    void IrrigationExecutor::stopExecution()
    {
        if (!isRunning())
            return;

        ZENO_LOG("IrrigationExecutor", "Stopping scenario %s", _execution.scenarioId);

        _safeStopAllOutputs();

        _execution.status = IrrigationStatus::ERROR;
        _phase = StepPhase::IDLE;

        if (_onErrorCb)
        {
            _onErrorCb(_execution.scenarioId, _execution.executionId,
                       _execution.currentStep + 1, _execution.stepCount,
                       "Execution stopped");
        }
    }

    // ============================================
    // Main loop non-blocking state machine
    // ============================================

    void IrrigationExecutor::loop()
    {
        if (!_initialized || !isRunning())
            return;

        if (_phase == StepPhase::WAITING)
        {
            // Check if WAIT duration elapsed
            const IrrigationStep &step = _execution.steps[_execution.currentStep];
            uint32_t elapsed = millis() - _phaseStartMs;

            if (elapsed >= step.waitDuration * 1000UL)
            {
                ZENO_LOG("IrrigationExecutor", "WAIT step %d done (%ds)",
                         step.order, step.waitDuration);
                _advanceToNextStep();
            }
        }
        // EXECUTING phase steps are handled synchronously in _executeCurrentStep
    }

    // ============================================
    // Execute current step
    // ============================================

    void IrrigationExecutor::_executeCurrentStep()
    {
        if (_execution.currentStep >= _execution.stepCount)
        {
            _completeExecution();
            return;
        }

        const IrrigationStep &step = _execution.steps[_execution.currentStep];

        if (isWaitAction(step.action))
        {
            // WAIT step report then start timer
            ZENO_LOG("IrrigationExecutor", "Step %d: WAIT %ds",
                     step.order, step.waitDuration);

            if (_onStepCb)
            {
                _onStepCb(_execution.scenarioId, _execution.executionId,
                          step.order, _execution.stepCount,
                          step.action, step);
            }

            _phase = StepPhase::WAITING;
            _phaseStartMs = millis();
        }
        else
        {
            // Non-WAIT step write all targets immediately
            int value = irrigationActionValue(step.action);

            ZENO_LOG("IrrigationExecutor", "Step %d: %s %d (%d targets)",
                     step.order, irrigationActionToCode(step.action),
                     value, step.keyCount);

            for (uint8_t i = 0; i < step.keyCount; i++)
            {
                bool ok = _writeFn(step.mqttKeys[i], value);
                if (!ok)
                {
                    char err[64];
                    snprintf(err, sizeof(err), "Write failed for %s", step.mqttKeys[i]);
                    _failExecution(err);
                    return;
                }
            }

            // Report step progress
            if (_onStepCb)
            {
                _onStepCb(_execution.scenarioId, _execution.executionId,
                          step.order, _execution.stepCount,
                          step.action, step);
            }

            // Non-WAIT fires instantly advance immediately
            _advanceToNextStep();
        }
    }

    // ============================================
    // Advance to next step
    // ============================================

    void IrrigationExecutor::_advanceToNextStep()
    {
        _execution.currentStep++;

        if (_execution.currentStep >= _execution.stepCount)
        {
            _completeExecution();
        }
        else
        {
            _phase = StepPhase::EXECUTING;
            _executeCurrentStep();
        }
    }

    // ============================================
    // Complete execution
    // ============================================

    void IrrigationExecutor::_completeExecution()
    {
        _execution.status = IrrigationStatus::COMPLETED;
        _phase = StepPhase::IDLE;

        // Calculate total WAIT duration
        uint32_t totalWaitSec = 0;
        for (uint8_t i = 0; i < _execution.stepCount; i++)
        {
            if (isWaitAction(_execution.steps[i].action))
            {
                totalWaitSec += _execution.steps[i].waitDuration;
            }
        }

        ZENO_LOG("IrrigationExecutor", "Scenario %s completed (%d steps, %ds total)",
                 _execution.scenarioId, _execution.stepCount, totalWaitSec);

        if (_onCompletedCb)
        {
            _onCompletedCb(_execution.scenarioId, _execution.executionId,
                           totalWaitSec);
        }
    }

    // ============================================
    // Fail execution safe state
    // ============================================

    void IrrigationExecutor::_failExecution(const char *error)
    {
        ZENO_LOG("IrrigationExecutor", "Error at step %d: %s",
                 _execution.steps[_execution.currentStep].order, error);

        _safeStopAllOutputs();

        uint8_t failStep = _execution.currentStep < _execution.stepCount
                               ? _execution.steps[_execution.currentStep].order
                               : 0;

        _execution.status = IrrigationStatus::ERROR;
        _phase = StepPhase::IDLE;

        if (_onErrorCb)
        {
            _onErrorCb(_execution.scenarioId, _execution.executionId,
                       failStep, _execution.stepCount, error);
        }
    }

    // ============================================
    // Safe stop write 0 to all targets used in scenario
    // ============================================

    void IrrigationExecutor::_safeStopAllOutputs()
    {
        if (!_writeFn)
            return;

        ZENO_LOG("IrrigationExecutor", "Safe stop turning off all outputs");

        // Collect unique keys and write 0
        for (uint8_t i = 0; i < _execution.stepCount; i++)
        {
            const IrrigationStep &step = _execution.steps[i];
            if (isWaitAction(step.action))
                continue;

            for (uint8_t j = 0; j < step.keyCount; j++)
            {
                _writeFn(step.mqttKeys[j], 0);
            }
        }
    }

} // namespace ZenoPCB

#endif  // defined(ESP32)
