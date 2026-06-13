#ifndef ZENOPCB_REGISTER_POLLING_ENGINE_H
#define ZENOPCB_REGISTER_POLLING_ENGINE_H

#include "ModbusConnectionManager.h"
#include "ModbusDataBuffer.h"
#include "../storage/DataMonitorConfig.h"
#include <vector>
#include <memory>
#include <map>
#include <functional>

namespace ZenoPCB
{
    // ============================================
    // Write Request State Machine (v2.3)
    // ============================================

    /**
     * @brief State of a write request
     */
    enum class WriteState
    {
        PENDING,          // Waiting in queue
        EXECUTING,        // Currently being written
        WAITING_RESPONSE, // Waiting for Modbus response
        COMPLETED,        // Successfully completed
        FAILED            // Failed after retries
    };

    /**
     * @brief Write request for queued Modbus write operations
     *
     * Non-blocking state machine with timeout and retry support.
     * Supports deduplication - newer requests for same mqttKey replace older ones.
     */
    struct WriteRequest
    {
        String mqttKey;                                     // Register mqttKey for lookup
        String connectionId;                                // Connection shortId
        uint16_t address;                                   // Modbus address
        uint8_t slaveId;                                    // Slave ID
        DataType dataType;                                  // Data type for conversion
        ByteOrder byteOrder;                                // Byte order for multi-register
        RegisterType registerType;                          // HOLDING, COIL, etc.
        double value;                                       // Value to write
        uint32_t timestamp;                                 // Request creation time
        std::function<void(bool, const String &)> callback; // Completion callback

        // State machine fields
        WriteState state;        // Current state
        uint32_t stateStartTime; // When current state started
        uint8_t retryCount;      // Current retry count
        uint8_t currentRegIndex; // For multi-register writes (0-3)
        uint8_t totalRegs;       // Total registers to write (1, 2, or 4)
        uint16_t regsToWrite[4]; // Pre-computed register values

        // Configuration
        static constexpr uint8_t MAX_RETRIES = 3;
        static constexpr uint32_t WRITE_TIMEOUT_MS = 500;   // Timeout per write operation
        static constexpr uint32_t INTER_REG_DELAY_MS = 100; // Delay between multi-reg writes

        WriteRequest()
            : address(0), slaveId(1), dataType(DataType::UNSIGNED_INT16),
              byteOrder(ByteOrder::BYTE_ORDER_BE), registerType(RegisterType::REG_HOLDING),
              value(0), timestamp(0), state(WriteState::PENDING), stateStartTime(0),
              retryCount(0), currentRegIndex(0), totalRegs(1)
        {
            regsToWrite[0] = 0;
            regsToWrite[1] = 0;
            regsToWrite[2] = 0;
            regsToWrite[3] = 0;
        }
    };

    /**
     * @brief Polling task for a single register
     *
     * v2.2: Expanded valueBuffer to 4 registers for 64-bit types
     */
    struct RegisterPollingTask
    {
        DataMonitorConfig config;
        uint32_t lastPollTime;
        uint32_t pollInterval; // ms, from connection config
        uint8_t retryCount;
        uint8_t maxRetries;
        bool enabled;
        bool pendingRead;       // True if waiting for response
        uint16_t transactionId; // Current transaction ID
        char connectionId[5];   // shortId reference

        // Buffer for async read (v2.2: expanded to 4 registers for 64-bit types)
        // - 16-bit: uses valueBuffer[0] only
        // - 32-bit: uses valueBuffer[0-1]
        // - 64-bit: uses valueBuffer[0-3]
        uint16_t valueBuffer[4];
        bool valueReady;
        uint8_t lastErrorCode; // Last Modbus error code (0=success, 0xE4=timeout, etc.)

        RegisterPollingTask()
            : lastPollTime(0), pollInterval(5000), retryCount(0),
              maxRetries(3), enabled(true), pendingRead(false),
              transactionId(0), valueReady(false), lastErrorCode(0)
        {
            connectionId[0] = '\0';
            valueBuffer[0] = 0;
            valueBuffer[1] = 0;
            valueBuffer[2] = 0;
            valueBuffer[3] = 0;
        }

        bool isTimeToPoll(uint32_t now) const
        {
            if (!enabled || pendingRead)
                return false;
            return (now - lastPollTime) >= pollInterval;
        }
    };

    /**
     * @brief Polling engine to read registers periodically
     */
    class RegisterPollingEngine
    {
    public:
        static RegisterPollingEngine &getInstance();

        // Lifecycle
        bool begin();
        void loop();
        void stop();

        // Register polling management
        bool addRegister(const DataMonitorConfig &config);
        bool updateRegister(const DataMonitorConfig &config);
        bool removeRegister(const String &mqttKey);
        bool enableRegister(const String &mqttKey, bool enable = true);
        bool disableRegister(const String &mqttKey);

        // ⭐ Enable/disable all registers of a specific connection
        size_t enableRegistersByConnection(const String &connectionId, bool enable = true);
        size_t disableRegistersByConnection(const String &connectionId);

        // ⭐ Force immediate read of all registers
        void forceReadAll();

        /**
         * @brief Prioritize immediate read of a single register (for write verification)
         * Resets lastPollTime to 0 so register is read on next poll cycle
         */
        bool enqueueVerifyRead(const String &mqttKey);

        // Statistics
        size_t getPollingCount() const { return _pollingTasks.size(); }
        size_t getActiveCount() const;
        uint32_t getLastPollTime() const { return _lastPollTime; }
        uint32_t getPollsCompleted() const { return _pollsCompleted; }
        uint32_t getPollsFailed() const { return _pollsFailed; }

        // Configuration
        static constexpr uint32_t MIN_POLL_INTERVAL_MS = 100; // Minimum 100ms per register (bus protected by RTU turnaround delay)
        void setGlobalPollInterval(uint32_t intervalMs) { _globalPollInterval = max(intervalMs, MIN_POLL_INTERVAL_MS); }
        uint32_t getGlobalPollInterval() const { return _globalPollInterval; }

        // ============================================
        // ⭐ Write Queue API (non-blocking, v2.3)
        // ============================================

        /**
         * @brief Enqueue a write request with deduplication
         * If same mqttKey exists in queue, newer request replaces older one.
         * @param request Write request with all parameters
         * @return true if enqueued/updated successfully
         */
        bool enqueueWrite(const WriteRequest &request);

        /**
         * @brief Enqueue write by mqttKey (auto-lookup config)
         * @param mqttKey Register mqttKey
         * @param value Value to write
         * @param callback Optional completion callback
         * @return true if enqueued
         */
        bool enqueueWriteByKey(const String &mqttKey, double value,
                               std::function<void(bool, const String &)> callback = nullptr);

        /**
         * @brief Get pending write count
         */
        size_t getWriteQueueSize() const { return _writeQueue.size(); }

        /**
         * @brief Check if any read is pending
         */
        bool hasActivePendingRead() const;

        /**
         * @brief Check if currently processing a write
         */
        bool hasActiveWrite() const { return _currentWrite != nullptr; }

    private:
        RegisterPollingEngine() = default;
        ~RegisterPollingEngine();

        RegisterPollingEngine(const RegisterPollingEngine &) = delete;
        RegisterPollingEngine &operator=(const RegisterPollingEngine &) = delete;

        // v2.5: Non-blocking async poll state machine
        void _checkActiveRead(uint32_t now);
        void _startNextPoll(uint32_t now);
        bool _startAsyncRead(const String &mqttKey, RegisterPollingTask &task);
        void _handlePollFailure(const String &mqttKey, RegisterPollingTask &task);

        // Legacy blocking poll (kept for write-verify)
        void _pollRegister(RegisterPollingTask &task);
        bool _readRegisterValue(RegisterPollingTask &task);
        void _processResponses(); // Check for completed transactions
        void _updateBufferValue(const String &mqttKey, RegisterPollingTask &task);

        // ⭐ Write queue processing (non-blocking state machine)
        void _processWriteQueue();
        void _startNextWrite();
        void _processCurrentWrite();
        bool _sendSingleRegisterWrite();
        void _completeCurrentWrite(bool success, const String &error = "");
        void _prepareRegisterValues(WriteRequest &request);

        // Byte order conversion helpers (v2.2)
        uint32_t _assemble32Bit(const uint16_t *regs, ByteOrder order);
        uint64_t _assemble64Bit(const uint16_t *regs, ByteOrder order);
        float _assembleFloat32(const uint16_t *regs, ByteOrder order);
        double _assembleFloat64(const uint16_t *regs, ByteOrder order);

        // ⭐ Byte order disassembly for write (v2.2)
        void _disassemble32Bit(uint32_t value, uint16_t *regs, ByteOrder order);
        void _disassemble64Bit(uint64_t value, uint16_t *regs, ByteOrder order);

        // Storage
        std::map<String, RegisterPollingTask> _pollingTasks;

        // ⭐ Write queue (using vector for deduplication support)
        std::vector<WriteRequest> _writeQueue;
        WriteRequest *_currentWrite; // Currently processing write (nullptr if idle)
        static constexpr size_t MAX_WRITE_QUEUE_SIZE = 20;

        // Statistics
        uint32_t _lastPollTime;
        uint32_t _pollsCompleted;
        uint32_t _pollsFailed;
        uint32_t _globalPollInterval;
        uint32_t _writesCompleted;
        uint32_t _writesFailed;

        // v2.5: Async read state machine
        String _activePollKey;                             // Currently reading register (empty = idle)
        uint32_t _activePollStartTime;                     // When async read was sent
        String _lastPollKey;                               // Round-robin tracking
        uint8_t _pollGeneration;                           // Bumped on watchdog reset to invalidate stale callbacks
        static constexpr uint32_t POLL_WATCHDOG_MS = 3000; // Safety watchdog per request

        // v2.7: Write preemption — abort slow reads when writes are pending
        void _preemptReadForWrite(uint32_t now);
        static constexpr uint32_t WRITE_PREEMPT_MS = 100; // Abort read after 100ms if write pending
    };

} // namespace ZenoPCB

#endif // ZENOPCB_REGISTER_POLLING_ENGINE_H
