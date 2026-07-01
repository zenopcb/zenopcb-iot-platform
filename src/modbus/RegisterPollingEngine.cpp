// Modbus subsystem is ESP32-only.
#if defined(ESP32)

#include "RegisterPollingEngine.h"
#include "../core/ZenoPCBDebug.h"
#include "../storage/LittleFSManager.h"

namespace ZenoPCB
{

    static const char *TAG = "RegisterPolling";

    // ============================================
    // Byte Order Conversion Helpers (v2.2)
    // ============================================

    /**
     * @brief Assemble 32-bit value from 2 Modbus registers with byte order
     *
     * Register layout in valueBuffer (as received from Modbus):
     * - regs[0] = first register (lower address)
     * - regs[1] = second register (higher address)
     *
     * Each register is 16-bit: [highByte][lowByte]
     *
     * For value 0x12345678:
     * - BIG_ENDIAN (ABCD): reg[0]=0x1234, reg[1]=0x5678
     * - BIG_ENDIAN_SWAP (BADC): reg[0]=0x3412, reg[1]=0x7856
     * - LITTLE_ENDIAN (DCBA): reg[0]=0x7856, reg[1]=0x3412
     * - LITTLE_ENDIAN_SWAP (CDAB): reg[0]=0x5678, reg[1]=0x1234
     */
    uint32_t RegisterPollingEngine::_assemble32Bit(const uint16_t *regs, ByteOrder order)
    {
        uint32_t result = 0;

        switch (order)
        {
        case ByteOrder::BYTE_ORDER_BE:
        default:
            // ABCD: Standard Modbus (high word first)
            result = ((uint32_t)regs[0] << 16) | regs[1];
            break;

        case ByteOrder::BYTE_ORDER_BE_SWAP:
            // BADC: Swap bytes within each word
            result = ((uint32_t)__builtin_bswap16(regs[0]) << 16) | __builtin_bswap16(regs[1]);
            break;

        case ByteOrder::BYTE_ORDER_LE:
            // DCBA: Low word first, bytes reversed
            result = ((uint32_t)__builtin_bswap16(regs[1]) << 16) | __builtin_bswap16(regs[0]);
            break;

        case ByteOrder::BYTE_ORDER_LE_SWAP:
            // CDAB: Low word first
            result = ((uint32_t)regs[1] << 16) | regs[0];
            break;
        }

        return result;
    }

    /**
     * @brief Assemble 64-bit value from 4 Modbus registers with byte order
     *
     * Similar logic to 32-bit but with 4 registers.
     */
    uint64_t RegisterPollingEngine::_assemble64Bit(const uint16_t *regs, ByteOrder order)
    {
        uint64_t result = 0;

        switch (order)
        {
        case ByteOrder::BYTE_ORDER_BE:
        default:
            // ABCDEFGH: Standard big-endian (highest word first)
            result = ((uint64_t)regs[0] << 48) |
                     ((uint64_t)regs[1] << 32) |
                     ((uint64_t)regs[2] << 16) |
                     ((uint64_t)regs[3]);
            break;

        case ByteOrder::BYTE_ORDER_BE_SWAP:
            // Swap bytes within each word
            result = ((uint64_t)__builtin_bswap16(regs[0]) << 48) |
                     ((uint64_t)__builtin_bswap16(regs[1]) << 32) |
                     ((uint64_t)__builtin_bswap16(regs[2]) << 16) |
                     ((uint64_t)__builtin_bswap16(regs[3]));
            break;

        case ByteOrder::BYTE_ORDER_LE:
            // HGFEDCBA: Lowest word first, bytes reversed
            result = ((uint64_t)__builtin_bswap16(regs[3]) << 48) |
                     ((uint64_t)__builtin_bswap16(regs[2]) << 32) |
                     ((uint64_t)__builtin_bswap16(regs[1]) << 16) |
                     ((uint64_t)__builtin_bswap16(regs[0]));
            break;

        case ByteOrder::BYTE_ORDER_LE_SWAP:
            // GHEFCDAB: Lowest word first
            result = ((uint64_t)regs[3] << 48) |
                     ((uint64_t)regs[2] << 32) |
                     ((uint64_t)regs[1] << 16) |
                     ((uint64_t)regs[0]);
            break;
        }

        return result;
    }

    /**
     * @brief Assemble IEEE 754 float from 2 Modbus registers with byte order
     */
    float RegisterPollingEngine::_assembleFloat32(const uint16_t *regs, ByteOrder order)
    {
        union
        {
            uint32_t u;
            float f;
        } conv;
        conv.u = _assemble32Bit(regs, order);
        return conv.f;
    }

    /**
     * @brief Assemble IEEE 754 double from 4 Modbus registers with byte order
     */
    double RegisterPollingEngine::_assembleFloat64(const uint16_t *regs, ByteOrder order)
    {
        union
        {
            uint64_t u;
            double d;
        } conv;
        conv.u = _assemble64Bit(regs, order);
        return conv.d;
    }

    RegisterPollingEngine &RegisterPollingEngine::getInstance()
    {
        static RegisterPollingEngine instance;
        return instance;
    }

    RegisterPollingEngine::~RegisterPollingEngine()
    {
        stop();
    }

    bool RegisterPollingEngine::begin()
    {
        ZENO_LOG_CORE("RegisterPollingEngine initializing...");
        _lastPollTime = 0;
        _pollsCompleted = 0;
        _pollsFailed = 0;
        _globalPollInterval = 5000;
        _writesCompleted = 0;
        _writesFailed = 0;
        _currentWrite = nullptr;
        _activePollKey = "";
        _activePollStartTime = 0;
        _lastPollKey = "";
        _pollGeneration = 0;
        return true;
    }

    void RegisterPollingEngine::loop()
    {
        uint32_t now = millis();

        // 1. If async read active -> process bus data and check completion
        if (_activePollKey.length() > 0)
        {
            _checkActiveRead(now);
        }

        // 1.5: Write preemption - abort slow reads when writes are pending
        if (_activePollKey.length() > 0 && !_writeQueue.empty())
        {
            _preemptReadForWrite(now);
        }

        // 2. Process any completed async responses
        _processResponses();

        // 3. Process write queue (non-blocking state machine)
        _processWriteQueue();

        // 4. Start next async read if idle and no active write
        if (!hasActiveWrite() && _activePollKey.length() == 0)
        {
            _startNextPoll(now);
        }
    }

    // ============================================
    // Write Queue Implementation (Non-blocking v2.3)
    // ============================================

    bool RegisterPollingEngine::hasActivePendingRead() const
    {
        for (const auto &pair : _pollingTasks)
        {
            if (pair.second.pendingRead)
            {
                return true;
            }
        }
        return false;
    }

    bool RegisterPollingEngine::enqueueWrite(const WriteRequest &request)
    {
        // Deduplication: Check if same mqttKey exists in queue
        for (auto &existing : _writeQueue)
        {
            if (existing.mqttKey == request.mqttKey)
            {
                // Replace with newer value (debounce rapid on/off)
                ZENO_LOG("WriteQueue", "Dedupe: %s = %.2f -> %.2f",
                              request.mqttKey.c_str(), existing.value, request.value);
                existing.value = request.value;
                existing.timestamp = request.timestamp;
                existing.callback = request.callback;
                return true;
            }
        }

        // Check queue size
        if (_writeQueue.size() >= MAX_WRITE_QUEUE_SIZE)
        {
            ZENO_LOG("WriteQueue", "Queue full (max=%d)", MAX_WRITE_QUEUE_SIZE);
            return false;
        }

        // Add new request
        _writeQueue.push_back(request);
        ZENO_LOG("WriteQueue", "Enqueued: %s = %.2f (queue size=%d)",
                      request.mqttKey.c_str(), request.value, _writeQueue.size());
        return true;
    }

    bool RegisterPollingEngine::enqueueWriteByKey(const String &mqttKey, double value,
                                                  std::function<void(bool, const String &)> callback)
    {
        auto &buffer = ModbusDataBuffer::getInstance();

        // Lookup register config
        const DataMonitorConfig *config = buffer.getRegisterConfig(mqttKey);
        if (!config)
        {
            ZENO_LOG("WriteQueue", "Register not found: %s", mqttKey.c_str());
            if (callback)
                callback(false, "Register not found");
            return false;
        }

        // Check if writable
        if (config->registerType != RegisterType::REG_HOLDING &&
            config->registerType != RegisterType::REG_COIL)
        {
            ZENO_LOG("WriteQueue", "Not writable: %s", mqttKey.c_str());
            if (callback)
                callback(false, "Register not writable");
            return false;
        }

        // Build write request
        WriteRequest request;
        request.mqttKey = mqttKey;
        request.connectionId = String(config->connectionId);
        request.address = config->address;
        request.slaveId = config->slaveId;
        request.dataType = config->dataType;
        request.byteOrder = config->byteOrder;
        request.registerType = config->registerType;
        request.value = value;
        request.timestamp = millis();
        request.callback = callback;
        request.state = WriteState::PENDING;
        request.retryCount = 0;
        request.currentRegIndex = 0;

        // Inject target value into buffer immediately on enqueue.
        // This prevents telemetry published while the write is pending
        // (waiting for sensor read to finish) from reporting the stale value
        // and causing the mobile UI to bounce back before the write executes.
        buffer.updateFromWriteValue(mqttKey, value);
        buffer.setWriteHold(mqttKey, ModbusDataBuffer::WRITE_HOLD_MS);

        return enqueueWrite(request);
    }

    void RegisterPollingEngine::_processWriteQueue()
    {
        uint32_t now = millis();

        // If we have an active write, process its state machine
        if (_currentWrite != nullptr)
        {
            _processCurrentWrite();
            return;
        }

        // No active write - check if we can start a new one
        // Only start write if no pending reads (avoid Modbus conflict)
        if (!_writeQueue.empty() && !hasActivePendingRead())
        {
            _startNextWrite();
        }
    }

    void RegisterPollingEngine::_startNextWrite()
    {
        if (_writeQueue.empty())
            return;

        // Get next request from front of queue
        _currentWrite = new WriteRequest(_writeQueue.front());
        _writeQueue.erase(_writeQueue.begin());

        // Prepare register values based on data type
        _prepareRegisterValues(*_currentWrite);

        // Start executing
        _currentWrite->state = WriteState::EXECUTING;
        _currentWrite->stateStartTime = millis();
        _currentWrite->currentRegIndex = 0;

        ZENO_LOG("WriteQueue", "Starting write: %s = %.2f (regs=%d)",
                      _currentWrite->mqttKey.c_str(), _currentWrite->value,
                      _currentWrite->totalRegs);

        // Send first register immediately
        if (!_sendSingleRegisterWrite())
        {
            // Immediate failure - will retry in _processCurrentWrite
            _currentWrite->state = WriteState::WAITING_RESPONSE;
        }
    }

    void RegisterPollingEngine::_processCurrentWrite()
    {
        if (_currentWrite == nullptr)
            return;

        uint32_t now = millis();
        uint32_t elapsed = now - _currentWrite->stateStartTime;

        switch (_currentWrite->state)
        {
        case WriteState::EXECUTING:
            // Should not stay here - immediately transitions to WAITING_RESPONSE
            break;

        case WriteState::WAITING_RESPONSE:
            // Check timeout
            if (elapsed >= WriteRequest::WRITE_TIMEOUT_MS)
            {
                // Timeout - check retry
                if (_currentWrite->retryCount < WriteRequest::MAX_RETRIES)
                {
                    _currentWrite->retryCount++;
                    ZENO_LOG("WriteQueue", "Timeout, retry %d/%d: %s reg[%d]",
                                  _currentWrite->retryCount, WriteRequest::MAX_RETRIES,
                                  _currentWrite->mqttKey.c_str(), _currentWrite->currentRegIndex);

                    // Retry same register
                    _currentWrite->stateStartTime = now;
                    _sendSingleRegisterWrite();
                }
                else
                {
                    // Max retries exceeded - fail
                    _completeCurrentWrite(false, "Timeout after retries");
                }
            }
            else if (elapsed >= WriteRequest::INTER_REG_DELAY_MS)
            {
                // Enough time passed, assume success and move to next register
                _currentWrite->currentRegIndex++;

                if (_currentWrite->currentRegIndex >= _currentWrite->totalRegs)
                {
                    // All registers written - success!
                    _completeCurrentWrite(true);
                }
                else
                {
                    // More registers to write
                    _currentWrite->stateStartTime = now;
                    _currentWrite->retryCount = 0; // Reset retry for new register
                    if (!_sendSingleRegisterWrite())
                    {
                        // Failed to send - will retry on next loop
                    }
                }
            }
            break;

        case WriteState::COMPLETED:
        case WriteState::FAILED:
            // Should have been cleaned up
            _completeCurrentWrite(_currentWrite->state == WriteState::COMPLETED);
            break;

        default:
            break;
        }
    }

    bool RegisterPollingEngine::_sendSingleRegisterWrite()
    {
        if (_currentWrite == nullptr)
            return false;

        auto &connMgr = ModbusConnectionManager::getInstance();
        ModbusConnectionInstance *conn = connMgr.getConnection(_currentWrite->connectionId);

        if (!conn || !conn->isConnected())
        {
            ZENO_LOG("WriteQueue", "Connection not available: %s",
                          _currentWrite->connectionId.c_str());
            return false;
        }

        uint16_t addr = _currentWrite->address + _currentWrite->currentRegIndex;
        uint16_t value = _currentWrite->regsToWrite[_currentWrite->currentRegIndex];
        bool success = false;

        // Handle COIL vs HOLDING differently
        if (_currentWrite->registerType == RegisterType::REG_COIL)
        {
            success = conn->writeCoil(addr, value != 0, _currentWrite->slaveId);
            ZENO_LOG("WriteQueue", "writeCoil addr=%d val=%d -> %s",
                          addr, value, success ? "OK" : "FAIL");
        }
        else
        {
            success = conn->writeRegister(addr, value, _currentWrite->slaveId);
            ZENO_LOG("WriteQueue", "writeReg addr=%d val=0x%04X -> %s",
                          addr, value, success ? "OK" : "FAIL");
        }

        if (success)
        {
            _currentWrite->state = WriteState::WAITING_RESPONSE;
            _currentWrite->stateStartTime = millis();
        }

        return success;
    }

    void RegisterPollingEngine::_completeCurrentWrite(bool success, const String &error)
    {
        if (_currentWrite == nullptr)
            return;

        // Call callback
        if (_currentWrite->callback)
        {
            _currentWrite->callback(success, error);
        }

        // Update statistics
        if (success)
        {
            // Inject written value into buffer and hold for WRITE_HOLD_MS
            // This prevents poll read-back from overwriting UI value ("bounce-back")
            auto &buffer = ModbusDataBuffer::getInstance();
            buffer.updateFromWriteValue(_currentWrite->mqttKey, _currentWrite->value);
            buffer.setWriteHold(_currentWrite->mqttKey, ModbusDataBuffer::WRITE_HOLD_MS);

            _writesCompleted++;
            ZENO_LOG("WriteQueue", "Completed: %s = %.2f (hold %dms)",
                          _currentWrite->mqttKey.c_str(), _currentWrite->value,
                          (int)ModbusDataBuffer::WRITE_HOLD_MS);
        }
        else
        {
            _writesFailed++;
            ZENO_LOG("WriteQueue", "Failed: %s - %s",
                          _currentWrite->mqttKey.c_str(), error.c_str());
        }

        // Cleanup
        delete _currentWrite;
        _currentWrite = nullptr;
    }

    void RegisterPollingEngine::_prepareRegisterValues(WriteRequest &request)
    {
        // Determine number of registers and prepare values based on data type
        switch (request.dataType)
        {
        case DataType::BOOLEAN:
            request.totalRegs = 1;
            request.regsToWrite[0] = (request.value != 0) ? 1 : 0;
            break;

        case DataType::SIGNED_INT16:
        case DataType::UNSIGNED_INT16:
            request.totalRegs = 1;
            request.regsToWrite[0] = static_cast<uint16_t>(request.value);
            break;

        case DataType::SIGNED_INT32:
        case DataType::UNSIGNED_INT32:
            request.totalRegs = 2;
            _disassemble32Bit(static_cast<uint32_t>(request.value),
                              request.regsToWrite, request.byteOrder);
            break;

        case DataType::FLOAT32:
        {
            request.totalRegs = 2;
            union
            {
                float f;
                uint32_t u;
            } conv;
            conv.f = static_cast<float>(request.value);
            _disassemble32Bit(conv.u, request.regsToWrite, request.byteOrder);
            break;
        }

        case DataType::SIGNED_INT64:
        case DataType::UNSIGNED_INT64:
            request.totalRegs = 4;
            _disassemble64Bit(static_cast<uint64_t>(request.value),
                              request.regsToWrite, request.byteOrder);
            break;

        case DataType::FLOAT64:
        {
            request.totalRegs = 4;
            union
            {
                double d;
                uint64_t u;
            } conv;
            conv.d = request.value;
            _disassemble64Bit(conv.u, request.regsToWrite, request.byteOrder);
            break;
        }

        default:
            request.totalRegs = 1;
            request.regsToWrite[0] = static_cast<uint16_t>(request.value);
            break;
        }
    }

    // ============================================
    // Byte Order Disassembly for Write
    // ============================================

    void RegisterPollingEngine::_disassemble32Bit(uint32_t value, uint16_t *regs, ByteOrder order)
    {
        switch (order)
        {
        case ByteOrder::BYTE_ORDER_BE: // ABCD - Big Endian (MSB first)
            regs[0] = (value >> 16) & 0xFFFF;
            regs[1] = value & 0xFFFF;
            break;

        case ByteOrder::BYTE_ORDER_LE: // DCBA - Little Endian (LSB first)
            regs[0] = value & 0xFFFF;
            regs[1] = (value >> 16) & 0xFFFF;
            break;

        case ByteOrder::BYTE_ORDER_BE_SWAP: // BADC - Byte swap within words
        {
            uint16_t high = (value >> 16) & 0xFFFF;
            uint16_t low = value & 0xFFFF;
            regs[0] = ((high << 8) & 0xFF00) | ((high >> 8) & 0x00FF);
            regs[1] = ((low << 8) & 0xFF00) | ((low >> 8) & 0x00FF);
            break;
        }

        case ByteOrder::BYTE_ORDER_LE_SWAP: // CDAB - Word swap
            regs[0] = value & 0xFFFF;
            regs[1] = (value >> 16) & 0xFFFF;
            regs[0] = ((regs[0] << 8) & 0xFF00) | ((regs[0] >> 8) & 0x00FF);
            regs[1] = ((regs[1] << 8) & 0xFF00) | ((regs[1] >> 8) & 0x00FF);
            break;

        default:
            regs[0] = (value >> 16) & 0xFFFF;
            regs[1] = value & 0xFFFF;
            break;
        }
    }

    void RegisterPollingEngine::_disassemble64Bit(uint64_t value, uint16_t *regs, ByteOrder order)
    {
        switch (order)
        {
        case ByteOrder::BYTE_ORDER_BE: // Big Endian (MSB first)
            regs[0] = (value >> 48) & 0xFFFF;
            regs[1] = (value >> 32) & 0xFFFF;
            regs[2] = (value >> 16) & 0xFFFF;
            regs[3] = value & 0xFFFF;
            break;

        case ByteOrder::BYTE_ORDER_LE: // Little Endian (LSB first)
            regs[0] = value & 0xFFFF;
            regs[1] = (value >> 16) & 0xFFFF;
            regs[2] = (value >> 32) & 0xFFFF;
            regs[3] = (value >> 48) & 0xFFFF;
            break;

        case ByteOrder::BYTE_ORDER_BE_SWAP: // Byte swap within words
            regs[0] = (value >> 48) & 0xFFFF;
            regs[1] = (value >> 32) & 0xFFFF;
            regs[2] = (value >> 16) & 0xFFFF;
            regs[3] = value & 0xFFFF;
            for (int i = 0; i < 4; i++)
            {
                regs[i] = ((regs[i] << 8) & 0xFF00) | ((regs[i] >> 8) & 0x00FF);
            }
            break;

        case ByteOrder::BYTE_ORDER_LE_SWAP: // Word swap
            regs[0] = value & 0xFFFF;
            regs[1] = (value >> 16) & 0xFFFF;
            regs[2] = (value >> 32) & 0xFFFF;
            regs[3] = (value >> 48) & 0xFFFF;
            for (int i = 0; i < 4; i++)
            {
                regs[i] = ((regs[i] << 8) & 0xFF00) | ((regs[i] >> 8) & 0x00FF);
            }
            break;

        default:
            regs[0] = (value >> 48) & 0xFFFF;
            regs[1] = (value >> 32) & 0xFFFF;
            regs[2] = (value >> 16) & 0xFFFF;
            regs[3] = value & 0xFFFF;
            break;
        }
    }

    void RegisterPollingEngine::_processResponses()
    {
        auto &connMgr = ModbusConnectionManager::getInstance();

        for (auto &pair : _pollingTasks)
        {
            RegisterPollingTask &task = pair.second;

            if (task.pendingRead && task.valueReady)
            {
                // Value has been filled by the callback
                _updateBufferValue(pair.first, task);
                task.pendingRead = false;
                task.valueReady = false;
                task.retryCount = 0;
                _pollsCompleted++;

                // Mark connection healthy
                auto conn = connMgr.getConnection(String(task.connectionId));
                if (conn)
                {
                    conn->markPollSuccess();
                }
            }
        }
    }

    void RegisterPollingEngine::_updateBufferValue(const String &mqttKey, RegisterPollingTask &task)
    {
        auto &buffer = ModbusDataBuffer::getInstance();
        const DataMonitorConfig &config = task.config;

        // v2.2: Apply byte order and save to correct field by dataType
        // getScaledValue() in RegisterValue will apply scale/offset
        switch (config.dataType)
        {
        case DataType::SIGNED_INT16:
            buffer.updateRegisterValue(mqttKey, (int16_t)task.valueBuffer[0]);
            break;

        case DataType::UNSIGNED_INT16:
            buffer.updateRegisterValue(mqttKey, (uint16_t)task.valueBuffer[0]);
            break;

        case DataType::SIGNED_INT32:
        {
            int32_t val32 = (int32_t)_assemble32Bit(task.valueBuffer, config.byteOrder);
            buffer.updateRegisterValue(mqttKey, val32);
            break;
        }

        case DataType::UNSIGNED_INT32:
        {
            uint32_t val32u = _assemble32Bit(task.valueBuffer, config.byteOrder);
            buffer.updateRegisterValue(mqttKey, val32u);
            break;
        }

        case DataType::FLOAT32:
        {
            float f = _assembleFloat32(task.valueBuffer, config.byteOrder);
            buffer.updateRegisterValue(mqttKey, f);
            break;
        }

        case DataType::SIGNED_INT64:
        {
            int64_t val64 = (int64_t)_assemble64Bit(task.valueBuffer, config.byteOrder);
            buffer.updateRegisterValue(mqttKey, val64);
            break;
        }

        case DataType::UNSIGNED_INT64:
        {
            uint64_t val64u = _assemble64Bit(task.valueBuffer, config.byteOrder);
            buffer.updateRegisterValue(mqttKey, val64u);
            break;
        }

        case DataType::FLOAT64:
        {
            double d = _assembleFloat64(task.valueBuffer, config.byteOrder);
            buffer.updateRegisterValue(mqttKey, d);
            break;
        }

        case DataType::BOOLEAN:
            buffer.updateRegisterValue(mqttKey, (bool)(task.valueBuffer[0] != 0));
            break;

        default:
            buffer.updateRegisterValue(mqttKey, (uint16_t)task.valueBuffer[0]);
            break;
        }

        // Debug: Log scaled value
        double scaledValue = buffer.getScaledValue(mqttKey);
        ZENO_LOG_MODBUS(" [%s] addr=%d scaled=%.4f (raw=%d)",
                        mqttKey.c_str(), config.address, scaledValue, task.valueBuffer[0]);
    }

    void RegisterPollingEngine::stop()
    {
        ZENO_LOG_CORE("RegisterPollingEngine stopping...");
        _pollingTasks.clear();
    }

    bool RegisterPollingEngine::addRegister(const DataMonitorConfig &config)
    {
        // Validate mqttKey
        if (config.mqttKey[0] == '\0' || strlen(config.mqttKey) != MQTT_KEY_LENGTH)
        {
            ZENO_LOG_CORE("Invalid mqttKey");
            return false;
        }

        String mqttKey(config.mqttKey);

        // Check if connection exists
        String connId(config.connectionId);
        auto &connMgr = ModbusConnectionManager::getInstance();
        if (!connMgr.hasConnection(connId))
        {
            ZENO_LOG_CORE("Connection not found: %s", connId.c_str());
            return false;
        }

        // Get connection config to retrieve delayBetweenPolls
        ConnectionConfig connConfig;
        bool configLoaded = LittleFSManager::readConfig(connId, connConfig);
        if (!configLoaded)
        {
            ZENO_LOG_CORE(" Failed to read connection config: %s", connId.c_str());
            return false;
        }
        ZENO_LOG_CORE(" Loaded config for %s: dbp=%dms, maxRetries=%d",
                      connId.c_str(), connConfig.delayBetweenPolls, connConfig.maxRetries);

        // Create polling task
        RegisterPollingTask task;
        task.config = config;
        memcpy(task.connectionId, config.connectionId, 4);
        task.connectionId[4] = '\0';
        task.pollInterval = max((uint32_t)connConfig.delayBetweenPolls, MIN_POLL_INTERVAL_MS); // Use dbp, min 1000ms
        task.maxRetries = connConfig.maxRetries; // Also use maxRetries from connection
        task.enabled = config.enabled;
        task.lastPollTime = millis();
        task.pendingRead = false;
        task.valueReady = false;

        ZENO_LOG_CORE(" Task created: pollInterval=%dms, lastPollTime=%d",
                      task.pollInterval, task.lastPollTime);

        _pollingTasks[mqttKey] = task;

        // Also add to data buffer
        auto &buffer = ModbusDataBuffer::getInstance();
        buffer.addRegister(config);

        ZENO_LOG_CORE("Register added: %s (conn=%s, addr=%d, pollInterval=%dms)",
                      mqttKey.c_str(), connId.c_str(), config.address, task.pollInterval);
        return true;
    }

    bool RegisterPollingEngine::updateRegister(const DataMonitorConfig &config)
    {
        if (config.mqttKey[0] == '\0')
            return false;

        String mqttKey(config.mqttKey);
        auto it = _pollingTasks.find(mqttKey);
        if (it == _pollingTasks.end())
        {
            // Register not found - try to add it instead
            ZENO_LOG_CORE("Register not found for update, attempting to add: %s", mqttKey.c_str());
            return addRegister(config);
        }

        // Update existing task
        it->second.config = config;
        memcpy(it->second.connectionId, config.connectionId, 4);
        it->second.connectionId[4] = '\0';
        it->second.enabled = config.enabled;
        it->second.retryCount = 0;
        it->second.pendingRead = false;

        // Update pollInterval from connection config
        String connId(config.connectionId);
        ConnectionConfig connConfig;
        if (LittleFSManager::readConfig(connId, connConfig))
        {
            it->second.pollInterval = max((uint32_t)connConfig.delayBetweenPolls, MIN_POLL_INTERVAL_MS);
            it->second.maxRetries = connConfig.maxRetries;
        }

        // Update buffer config
        auto &buffer = ModbusDataBuffer::getInstance();
        buffer.updateRegisterConfig(config);

        ZENO_LOG_CORE("Register updated: %s (enabled=%d)", mqttKey.c_str(), config.enabled);
        return true;
    }

    bool RegisterPollingEngine::removeRegister(const String &mqttKey)
    {
        auto it = _pollingTasks.find(mqttKey);
        if (it == _pollingTasks.end())
            return false;

        _pollingTasks.erase(it);

        auto &buffer = ModbusDataBuffer::getInstance();
        buffer.removeRegister(mqttKey);

        ZENO_LOG_CORE("Register removed: %s", mqttKey.c_str());
        return true;
    }

    bool RegisterPollingEngine::enableRegister(const String &mqttKey, bool enable)
    {
        auto it = _pollingTasks.find(mqttKey);
        if (it == _pollingTasks.end())
            return false;

        it->second.enabled = enable;
        return true;
    }

    bool RegisterPollingEngine::disableRegister(const String &mqttKey)
    {
        return enableRegister(mqttKey, false);
    }

    // Enable/disable all registers of a specific connection
    size_t RegisterPollingEngine::enableRegistersByConnection(const String &connectionId, bool enable)
    {
        size_t count = 0;

        for (auto &pair : _pollingTasks)
        {
            String taskConnId = String(pair.second.connectionId);

            if (taskConnId == connectionId)
            {
                pair.second.enabled = enable;
                count++;
            }
        }

        if (count > 0)
        {
            ZENO_LOG("PollingEngine", "%s %d registers for connection '%s'",
                          enable ? " Enabled" : "Disabled", count, connectionId.c_str());
        }
        return count;
    }

    size_t RegisterPollingEngine::disableRegistersByConnection(const String &connectionId)
    {
        return enableRegistersByConnection(connectionId, false);
    }

    // v2.5: Force immediate read - mark all tasks as due, async loop picks them up
    void RegisterPollingEngine::forceReadAll()
    {
        ZENO_LOG("PollingEngine", "Force reading ALL enabled registers...");

        size_t totalRegisters = 0;

        for (auto &pair : _pollingTasks)
        {
            RegisterPollingTask &task = pair.second;

            if (!task.enabled)
                continue;

            totalRegisters++;
            task.lastPollTime = 0; // Mark as due for immediate polling
        }

        ZENO_LOG("PollingEngine", "Triggered force read: %d/%d registers",
                      totalRegisters, _pollingTasks.size());
    }

    bool RegisterPollingEngine::enqueueVerifyRead(const String &mqttKey)
    {
        auto it = _pollingTasks.find(mqttKey);
        if (it == _pollingTasks.end())
        {
            ZENO_LOG("PollingEngine", "Verify read failed: register not found: %s", mqttKey.c_str());
            return false;
        }

        RegisterPollingTask &task = it->second;

        if (!task.enabled)
        {
            ZENO_LOG("PollingEngine", "Verify read skipped (disabled): %s", mqttKey.c_str());
            return false;
        }

        // Reset poll timer - async loop will pick it up on next iteration
        task.lastPollTime = 0;
        ZENO_LOG("PollingEngine", "Verify read queued: %s", mqttKey.c_str());
        return true;
    }

    size_t RegisterPollingEngine::getActiveCount() const
    {
        size_t count = 0;
        for (const auto &pair : _pollingTasks)
        {
            if (pair.second.enabled)
                count++;
        }
        return count;
    }

    // ============================================
    // v2.5: Non-blocking async poll state machine
    // ============================================

    void RegisterPollingEngine::_startNextPoll(uint32_t now)
    {
        // Round-robin: start searching after the last polled register
        auto it = _pollingTasks.begin();
        if (_lastPollKey.length() > 0)
        {
            it = _pollingTasks.upper_bound(_lastPollKey);
            if (it == _pollingTasks.end())
                it = _pollingTasks.begin();
        }

        size_t checked = 0;
        size_t total = _pollingTasks.size();
        while (checked < total)
        {
            RegisterPollingTask &task = it->second;

            if (task.isTimeToPoll(now))
            {
                if (_startAsyncRead(it->first, task))
                {
                    _activePollKey = it->first;
                    _activePollStartTime = now;
                    _lastPollKey = it->first;
                    task.lastPollTime = now;
                    _lastPollTime = now;
                    return;
                }
            }

            ++it;
            if (it == _pollingTasks.end())
                it = _pollingTasks.begin();
            checked++;
        }
    }

    bool RegisterPollingEngine::_startAsyncRead(const String &mqttKey, RegisterPollingTask &task)
    {
        auto &connMgr = ModbusConnectionManager::getInstance();
        auto conn = connMgr.getConnection(String(task.connectionId));

        if (!conn || !conn->isConnected())
            return false;
        if (!conn->isHealthy())
            return false;
        if (conn->isBusy())
            return false;

        const DataMonitorConfig &config = task.config;
        uint16_t count = config.getRegisterCount();

        // Reset buffer
        task.valueBuffer[0] = 0;
        task.valueBuffer[1] = 0;
        task.valueBuffer[2] = 0;
        task.valueBuffer[3] = 0;
        task.valueReady = false;

        bool sent = conn->beginAsyncRead(
            config.registerType, config.address,
            task.valueBuffer, count, config.slaveId);

        if (sent)
        {
            task.pendingRead = true;
            ZENO_LOG_MODBUS(" Async read: %s addr=%d slave=%d rt=%d cnt=%d",
                            mqttKey.c_str(), config.address, config.slaveId,
                            (int)config.registerType, count);
        }
        return sent;
    }

    void RegisterPollingEngine::_checkActiveRead(uint32_t now)
    {
        auto it = _pollingTasks.find(_activePollKey);
        if (it == _pollingTasks.end())
        {
            _activePollKey = "";
            return;
        }

        RegisterPollingTask &task = it->second;
        auto &connMgr = ModbusConnectionManager::getInstance();
        auto conn = connMgr.getConnection(String(task.connectionId));

        if (!conn)
        {
            task.pendingRead = false;
            _activePollKey = "";
            return;
        }

        // v2.6: Watchdog - if request hangs too long, reset and move on (ZF-01 pattern)
        if (now - _activePollStartTime > POLL_WATCHDOG_MS)
        {
            task.lastErrorCode = 0xE4;
            task.pendingRead = false;
            task.lastPollTime = now; // Spread retries - wait full interval before next attempt
            _pollGeneration++;
            ZENO_LOG_CORE("Watchdog [%s] addr=%d (gen=%d)",
                          _activePollKey.c_str(), task.config.address, _pollGeneration);
            _handlePollFailure(_activePollKey, task);
            _activePollKey = "";
            return;
        }

        if (conn->isBusy())
            return; // Still waiting - processTask() already called by ConnectionManager::loop()

        // Transaction completed
        if (conn->wasAsyncReadSuccessful())
        {
            // For coil/discrete, convert bool result to uint16
            if (task.config.registerType == RegisterType::REG_COIL ||
                task.config.registerType == RegisterType::REG_DISCRETE)
            {
                task.valueBuffer[0] = conn->getAsyncBoolResult() ? 1 : 0;
            }
            task.valueReady = true;
            task.lastErrorCode = 0;
            task.retryCount = 0;
            // pendingRead stays true - _processResponses() will handle it

            ZENO_LOG_MODBUS(" Async read OK: %s sid=%d addr=%d rt=%d buf[0]=%d",
                            _activePollKey.c_str(), task.config.slaveId,
                            task.config.address, (int)task.config.registerType,
                            task.valueBuffer[0]);
        }
        else
        {
            // Read failed - skip immediately, pollInterval will retry next cycle naturally
            task.lastErrorCode = conn->getLastErrorCode();
            task.pendingRead = false;
            task.lastPollTime = now; // Wait full interval before next attempt

            ZENO_LOG_CORE(" Read failed [%s] sid=%d addr=%d rt=%d error=0x%02X",
                          _activePollKey.c_str(), task.config.slaveId, task.config.address,
                          (int)task.config.registerType, task.lastErrorCode);

            _handlePollFailure(_activePollKey, task);
        }

        _activePollKey = "";
    }

    void RegisterPollingEngine::_handlePollFailure(const String &mqttKey, RegisterPollingTask &task)
    {
        auto &buffer = ModbusDataBuffer::getInstance();
        buffer.setError(mqttKey, "Poll failed", task.lastErrorCode);
        ZENO_LOG_CORE("Polling failed: %s sid=%d addr=%d rt=%d error=0x%02X",
                      mqttKey.c_str(), task.config.slaveId, task.config.address,
                      (int)task.config.registerType, task.lastErrorCode);
        _pollsFailed++;
        task.retryCount = 0;

        // Connection-level health tracking (no register-level backoff - ZF-01 pattern)
        auto &connMgr = ModbusConnectionManager::getInstance();
        auto conn = connMgr.getConnection(String(task.connectionId));
        if (conn)
            conn->markPollFailure();
    }

    // ============================================
    // v2.7: Write preemption - abort slow reads for pending writes
    // ============================================

    void RegisterPollingEngine::_preemptReadForWrite(uint32_t now)
    {
        if (now - _activePollStartTime < WRITE_PREEMPT_MS)
            return; // Give the read a chance to complete naturally

        auto it = _pollingTasks.find(_activePollKey);
        if (it == _pollingTasks.end())
        {
            _activePollKey = "";
            return;
        }

        // Force-abort the RTU transaction to free the bus for write
        auto &connMgr = ModbusConnectionManager::getInstance();
        auto conn = connMgr.getConnection(String(it->second.connectionId));
        if (conn)
        {
            conn->abortTransaction();
        }

        RegisterPollingTask &task = it->second;
        task.pendingRead = false;
        task.lastPollTime = now; // Retry next cycle
        _pollGeneration++;

        ZENO_LOG_CORE(" Write preempt: aborted read [%s] after %dms (writes=%d)",
                      _activePollKey.c_str(), (int)(now - _activePollStartTime), _writeQueue.size());

        _activePollKey = "";
    }

    // ============================================
    // Legacy blocking poll (kept for compatibility)
    // ============================================

    void RegisterPollingEngine::_pollRegister(RegisterPollingTask &task)
    {
        // Try to read register value
        if (_readRegisterValue(task))
        {
            task.pendingRead = true;
            task.retryCount = 0;

            // Mark connection healthy on successful poll
            auto &connMgr = ModbusConnectionManager::getInstance();
            auto conn = connMgr.getConnection(String(task.connectionId));
            if (conn)
            {
                conn->markPollSuccess();
            }
        }
        else
        {
            task.retryCount++;
            if (task.retryCount >= task.maxRetries)
            {
                String mqttKey(task.config.mqttKey);
                _handlePollFailure(mqttKey, task);
            }
        }
    }

    bool RegisterPollingEngine::_readRegisterValue(RegisterPollingTask &task)
    {
        auto &connMgr = ModbusConnectionManager::getInstance();
        String connId(task.connectionId);

        auto conn = connMgr.getConnection(connId);
        if (!conn)
        {
            ZENO_LOG_CORE("Connection not found: %s", connId.c_str());
            return false;
        }
        if (!conn->isConnected())
        {
            ZENO_LOG_CORE("Connection not ready: %s", connId.c_str());
            return false;
        }

        // v2.4: Check connection health - skip if in backoff
        if (!conn->isHealthy())
        {
            // Connection in backoff mode - skip poll to avoid timeout blocking
            return false;
        }

        const DataMonitorConfig &config = task.config;
        uint8_t slaveId = config.slaveId;
        uint16_t address = config.address;

        // v2.2: Use getRegisterCount() to determine count automatically
        // - 16-bit: 1 register
        // - 32-bit: 2 registers
        // - 64-bit: 4 registers
        uint16_t count = config.getRegisterCount();

        // Reset buffer
        task.valueBuffer[0] = 0;
        task.valueBuffer[1] = 0;
        task.valueBuffer[2] = 0;
        task.valueBuffer[3] = 0;
        task.valueReady = false;

        // Read based on register type - pass buffer to receive data
        uint16_t transId = 0;

        if (config.registerType == RegisterType::REG_HOLDING)
        {
            transId = conn->readHoldingRegistersWithBuffer(address, task.valueBuffer, count, slaveId);
            if (transId == 0)
            {
                // Error occurred
                task.lastErrorCode = conn->getLastErrorCode();
                String mqttKey(task.config.mqttKey);
                ZENO_LOG_CORE(" Read failed [%s] addr=%d error=0x%02X",
                              mqttKey.c_str(), address, task.lastErrorCode);

                // Set error in buffer
                auto &buffer = ModbusDataBuffer::getInstance();
                buffer.setError(mqttKey, String("E") + String(task.lastErrorCode, HEX), task.lastErrorCode);
                return false;
            }
        }
        else if (config.registerType == RegisterType::REG_INPUT)
        {
            transId = conn->readInputRegistersWithBuffer(address, task.valueBuffer, count, slaveId);
            if (transId == 0)
            {
                task.lastErrorCode = conn->getLastErrorCode();
                String mqttKey(task.config.mqttKey);
                auto &buffer = ModbusDataBuffer::getInstance();
                buffer.setError(mqttKey, String("E") + String(task.lastErrorCode, HEX), task.lastErrorCode);
                return false;
            }
        }
        else if (config.registerType == RegisterType::REG_COIL)
        {
            // For coils, use single bit read
            bool bitValue = false;
            if (conn->readCoilWithBuffer(address, &bitValue, slaveId))
            {
                task.valueBuffer[0] = bitValue ? 1 : 0;
                task.valueReady = true;
                task.lastErrorCode = 0;
                return true;
            }
            task.lastErrorCode = conn->getLastErrorCode();
            String mqttKey(task.config.mqttKey);
            auto &buffer = ModbusDataBuffer::getInstance();
            buffer.setError(mqttKey, String("E") + String(task.lastErrorCode, HEX), task.lastErrorCode);
            return false;
        }
        else if (config.registerType == RegisterType::REG_DISCRETE)
        {
            bool bitValue = false;
            if (conn->readDiscreteWithBuffer(address, &bitValue, slaveId))
            {
                task.valueBuffer[0] = bitValue ? 1 : 0;
                task.valueReady = true;
                task.lastErrorCode = 0;
                return true;
            }
            task.lastErrorCode = conn->getLastErrorCode();
            String mqttKey(task.config.mqttKey);
            auto &buffer = ModbusDataBuffer::getInstance();
            buffer.setError(mqttKey, String("E") + String(task.lastErrorCode, HEX), task.lastErrorCode);
            return false;
        }

        if (transId != 0)
        {
            task.transactionId = transId;
            task.lastErrorCode = 0; // Clear error on success
            // For synchronous reads, mark as ready immediately
            task.valueReady = true;
            return true;
        }
        else
        {
            // transId = 0 means error occurred
            task.lastErrorCode = conn->getLastErrorCode();
            auto &buffer = ModbusDataBuffer::getInstance();
            String mqttKey(config.mqttKey);
            buffer.setError(mqttKey, String("E") + String(task.lastErrorCode, HEX), task.lastErrorCode);
            ZENO_LOG_CORE(" Modbus read failed: %s", mqttKey.c_str());
        }

        return false;
    }

} // namespace ZenoPCB

#endif // defined(ESP32)
