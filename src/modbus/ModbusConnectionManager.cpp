// Modbus subsystem is ESP32-only.
#if defined(ESP32)

#include "ModbusConnectionManager.h"
#include "../core/ZenoPCBDebug.h"
#include <HardwareSerial.h>
#include <esp_netif.h>

// Check if lwIP TCP stack is available (any network interface active)
// 4G uses PPP over UART lwIP mailbox not initialized for socket API
// unless WiFi or Ethernet has started a netif
static bool isTCPStackReady()
{
    // esp_netif_get_nr_of_ifs() returns 0 when no network interface is registered
    // WiFi.begin() / ETH.begin() register netifs count > 0
    // Pure 4G (TinyGSM AT mode) does NOT register a netif count = 0
    return esp_netif_get_nr_of_ifs() > 0;
}

namespace ZenoPCB
{

    // Helper: Build serial config from dataBits, parity, stopBits
    static uint32_t buildSerialConfig(uint8_t dataBits, Parity parity, uint8_t stopBits)
    {
        // ESP32 Serial config format: SERIAL_xPy
        // x = data bits (5,6,7,8)
        // P = parity (N=None, E=Even, O=Odd)
        // y = stop bits (1,2)

        // Build config from combination
        if (dataBits == 8 && stopBits == 1)
        {
            switch (parity)
            {
            case Parity::NONE:
                ZENO_LOG("ModbusRTU", "Serial config: 8N1 (0x%08X)", SERIAL_8N1);
                return SERIAL_8N1;
            case Parity::EVEN:
                ZENO_LOG("ModbusRTU", "Serial config: 8E1 (0x%08X)", SERIAL_8E1);
                return SERIAL_8E1;
            case Parity::ODD:
                ZENO_LOG("ModbusRTU", "Serial config: 8O1 (0x%08X)", SERIAL_8O1);
                return SERIAL_8O1;
            }
        }
        else if (dataBits == 8 && stopBits == 2)
        {
            switch (parity)
            {
            case Parity::NONE:
                ZENO_LOG("ModbusRTU", "Serial config: 8N2 (0x%08X)", SERIAL_8N2);
                return SERIAL_8N2;
            case Parity::EVEN:
                ZENO_LOG("ModbusRTU", "Serial config: 8E2 (0x%08X)", SERIAL_8E2);
                return SERIAL_8E2;
            case Parity::ODD:
                ZENO_LOG("ModbusRTU", "Serial config: 8O2 (0x%08X)", SERIAL_8O2);
                return SERIAL_8O2;
            }
        }
        else if (dataBits == 7 && stopBits == 1)
        {
            switch (parity)
            {
            case Parity::NONE:
                ZENO_LOG("ModbusRTU", "Serial config: 7N1 (0x%08X)", SERIAL_7N1);
                return SERIAL_7N1;
            case Parity::EVEN:
                ZENO_LOG("ModbusRTU", "Serial config: 7E1 (0x%08X)", SERIAL_7E1);
                return SERIAL_7E1;
            case Parity::ODD:
                ZENO_LOG("ModbusRTU", "Serial config: 7O1 (0x%08X)", SERIAL_7O1);
                return SERIAL_7O1;
            }
        }
        else if (dataBits == 7 && stopBits == 2)
        {
            switch (parity)
            {
            case Parity::NONE:
                ZENO_LOG("ModbusRTU", "Serial config: 7N2 (0x%08X)", SERIAL_7N2);
                return SERIAL_7N2;
            case Parity::EVEN:
                ZENO_LOG("ModbusRTU", "Serial config: 7E2 (0x%08X)", SERIAL_7E2);
                return SERIAL_7E2;
            case Parity::ODD:
                ZENO_LOG("ModbusRTU", "Serial config: 7O2 (0x%08X)", SERIAL_7O2);
                return SERIAL_7O2;
            }
        }

        // Default fallback
        ZENO_LOG("ModbusRTU", "Unsupported serial config: %dbit, parity=%d, stop=%d - using 8N1",
                 dataBits, (int)parity, stopBits);
        return SERIAL_8N1;
    }

    static const char *TAG = "ModbusConn";

    // Static default pins (initialized to -1 = not set)
    int8_t ModbusConnectionManager::s_defaultRxPin = -1;
    int8_t ModbusConnectionManager::s_defaultTxPin = -1;
    int8_t ModbusConnectionManager::s_defaultDEPin = -1; // DE pin for RS485 direction control

    ModbusConnectionInstance::ModbusConnectionInstance(const ConnectionConfig &config)
        : _config(config), _status(IDLE), _modbus(nullptr),
          _lastConnectAttempt(0), _nextRetryTime(0), _errorCount(0), _lastErrorTime(0), _lastErrorCode(0),
          _consecutiveFailures(0), _backoffUntil(0),
          _asyncSuccess(false), _asyncBoolResult(false), _asyncTransId(0), _lastTransactionEnd(0)
    {
        memcpy(_shortId, config.shortId, SHORTID_LENGTH);
        _shortId[SHORTID_LENGTH] = '\0';
        _protocol = config.protocol;
    }

    ModbusConnectionInstance::~ModbusConnectionInstance() { stop(); }

    bool ModbusConnectionInstance::begin()
    {
        if (_status == CONNECTED)
            return true;
        _status = CONNECTING;
        _lastConnectAttempt = millis();

        bool success = (_protocol == ConnectionProtocol::MODBUS_RTU) ? _initRTU() : _initTCP();
        _status = success ? CONNECTED : ERROR;
        if (success)
            _clearError();
        return success;
    }

    void ModbusConnectionInstance::stop()
    {
        _modbusRTU.reset();
        _modbusTCP.reset();
        _modbus = nullptr;
        _status = DISCONNECTED;
    }

    void ModbusConnectionInstance::loop()
    {
        if (!_modbus)
            return;

        if (_modbusRTU)
        {
            _modbusRTU->task();
        }
        else if (_modbusTCP)
        {
            _modbusTCP->task();

            // FIX: Check TCP connection and reconnect if needed
            IPAddress serverIP;
            if (serverIP.fromString(_config.ipAddress))
            {
                if (!_modbusTCP->isConnected(serverIP))
                {
                    // Only try reconnect if enough time has passed
                    if (_status == CONNECTED)
                    {
                        ZENO_LOG("ModbusTCP", "Connection lost to %s", _config.ipAddress);
                        _status = ERROR;
                        _nextRetryTime = millis() + _config.reconnectInterval;
                    }
                }
                else if (_status != CONNECTED)
                {
                    ZENO_LOG("ModbusTCP", "Connection restored to %s", _config.ipAddress);
                    _status = CONNECTED;
                    _clearError();
                }
            }
        }

        if (_status == ERROR && millis() >= _nextRetryTime)
        {
            // Guard: Don't retry TCP when no network stack
            if (_protocol == ConnectionProtocol::MODBUS_TCP && !isTCPStackReady())
            {
                _nextRetryTime = millis() + _config.reconnectInterval;
                return; // Silently defer avoid log spam
            }
            begin();
        }
    }

    bool ModbusConnectionInstance::_initRTU()
    {
        _modbusRTU = std::make_unique<ModbusRTU>();
        _modbus = _modbusRTU.get();

        // Use static default pins set via setDefaultRTUPins(), or hardware defaults
        int8_t rxPin = ModbusConnectionManager::s_defaultRxPin;
        int8_t txPin = ModbusConnectionManager::s_defaultTxPin;
        int8_t dePin = ModbusConnectionManager::s_defaultDEPin;

        // DEBUG: Log config values BEFORE building serial config
        ZENO_LOG("ModbusRTU", "Config: dataBits=%d, parity=%d, stopBits=%d, baudRate=%lu",
                 _config.dataBits, (int)_config.parity, _config.stopBits, _config.baudRate);

        // Build serial config from dataBits, parity, stopBits
        uint32_t serialConfig = buildSerialConfig(_config.dataBits, _config.parity, _config.stopBits);

        // 1. Initialize Serial1 vi custom RX/TX pins v dynamic serial config
        if (rxPin != -1 && txPin != -1)
        {
            Serial1.begin(_config.baudRate, serialConfig, rxPin, txPin);
            ZENO_LOG("ModbusRTU", "Serial1.begin(%lu, 0x%02X, RX=%d, TX=%d) [%d%c%d]",
                     _config.baudRate, serialConfig, rxPin, txPin,
                     _config.dataBits,
                     _config.parity == Parity::NONE ? 'N' : (_config.parity == Parity::EVEN ? 'E' : 'O'),
                     _config.stopBits);
        }
        else
        {
            // Hardware default pins
            Serial1.begin(_config.baudRate, serialConfig);
            ZENO_LOG("ModbusRTU", "Serial1.begin(%lu, 0x%02X) - default pins [%d%c%d]",
                     _config.baudRate, serialConfig,
                     _config.dataBits,
                     _config.parity == Parity::NONE ? 'N' : (_config.parity == Parity::EVEN ? 'E' : 'O'),
                     _config.stopBits);
        }

        // 2. Initialize ModbusRTU with Serial1
        if (dePin >= 0)
        {
            _modbusRTU->begin(&Serial1, dePin);
            ZENO_LOG("ModbusRTU", "mb.begin(&Serial1, DE=%d)", dePin);
        }
        else
        {
            _modbusRTU->begin(&Serial1);
            ZENO_LOG("ModbusRTU", "mb.begin(&Serial1) - auto direction");
        }

        // 3. Configure as master/client
        _modbusRTU->master();

        // 4. Apply connection config: response timeout and inter-frame delay
        if (_config.responseTimeout > 0)
        {
            _modbusRTU->setTimeout(_config.responseTimeout);
            ZENO_LOG("ModbusRTU", "Response timeout: %dms", _config.responseTimeout);
        }

        ZENO_LOG("ModbusRTU", "RTU initialized: %lu baud, %d%c%d, timeout=%dms, ifd=%dms",
                 _config.baudRate, _config.dataBits,
                 _config.parity == Parity::NONE ? 'N' : (_config.parity == Parity::EVEN ? 'E' : 'O'),
                 _config.stopBits, _config.responseTimeout, _config.interFrameDelay);

        return true;
    }

    bool ModbusConnectionInstance::_initTCP()
    {
        // Guard: lwIP TCP stack must be ready before creating sockets
        // 4G mode uses PPP (AT commands) lwip_socket() will crash (assert: Invalid mbox)
        if (!isTCPStackReady())
        {
            ZENO_LOG("ModbusTCP", "TCP stack not ready (no WiFi/Ethernet) deferring connection to %s", _config.ipAddress);
            _status = ERROR;
            _nextRetryTime = millis() + _config.reconnectInterval;
            return true; // Return true to keep config loaded, retry in loop()
        }

        _modbusTCP = std::make_unique<ModbusTCP>();
        _modbus = _modbusTCP.get();
        _modbusTCP->client();

        // FIX: Must establish TCP connection to the server
        IPAddress serverIP;
        if (!serverIP.fromString(_config.ipAddress))
        {
            ZENO_LOG("ModbusTCP", "Invalid IP address: %s", _config.ipAddress);
            return false;
        }

        uint16_t port = _config.port > 0 ? _config.port : 502; // Default Modbus TCP port

        ZENO_LOG("ModbusTCP", "Connecting to %s:%d...", _config.ipAddress, port);

        // Try to connect with timeout
        bool connected = _modbusTCP->connect(serverIP, port);

        if (connected)
        {
            ZENO_LOG("ModbusTCP", "TCP connected to %s:%d", _config.ipAddress, port);
            return true;
        }
        else
        {
            ZENO_LOG("ModbusTCP", "TCP connect initiated to %s:%d (async)", _config.ipAddress, port);
            // Connection might be async, return true and check in loop()
            return true;
        }
    }

    bool ModbusConnectionInstance::_reconnect() { return begin(); }

    // NEW: Ensure TCP connection is established before any read/write operation
    bool ModbusConnectionInstance::_ensureTCPConnection()
    {
        if (!_modbusTCP)
            return false;

        // Guard: TCP stack must be available
        if (!isTCPStackReady())
            return false;

        IPAddress serverIP;
        if (!serverIP.fromString(_config.ipAddress))
        {
            ZENO_LOG("ModbusTCP", "Invalid IP: %s", _config.ipAddress);
            return false;
        }

        // Check if already connected
        if (_modbusTCP->isConnected(serverIP))
        {
            return true;
        }

        // Try to connect
        uint16_t port = _config.port > 0 ? _config.port : 502;
        ZENO_LOG("ModbusTCP", "Reconnecting to %s:%d...", _config.ipAddress, port);

        _modbusTCP->connect(serverIP, port);

        // Give it a moment to establish connection
        uint32_t start = millis();
        while (millis() - start < 1000) // 1 second timeout
        {
            _modbusTCP->task();
            if (_modbusTCP->isConnected(serverIP))
            {
                ZENO_LOG("ModbusTCP", "Reconnected to %s:%d", _config.ipAddress, port);
                _status = CONNECTED;
                return true;
            }
            delay(10);
        }

        ZENO_LOG("ModbusTCP", "Failed to connect to %s:%d", _config.ipAddress, port);
        return false;
    }

    void ModbusConnectionInstance::_setError(const String &error)
    {
        _lastError = error;
        _lastErrorTime = millis();
        _errorCount++;
        _status = ERROR;
        _nextRetryTime = millis() + _config.reconnectInterval;
    }

    void ModbusConnectionInstance::_clearError()
    {
        _lastError = "";
        _lastErrorTime = 0;
        _errorCount = 0;
    }

    // ============================================
    // v2.4: Health tracking for connection-level backoff
    // ============================================

    bool ModbusConnectionInstance::isHealthy() const
    {
        // If in backoff period, check if it has expired
        if (_backoffUntil > 0)
        {
            return millis() >= _backoffUntil;
        }
        return true;
    }

    void ModbusConnectionInstance::markPollSuccess()
    {
        if (_consecutiveFailures > 0)
        {
            if (_consecutiveFailures >= HEALTH_FAILURE_THRESHOLD)
            {
                ZENO_LOG_RAW("[Modbus] Connection %s recovered after %d failures\n",
                              _shortId, _consecutiveFailures);
            }
            _consecutiveFailures = 0;
            _backoffUntil = 0;
        }
    }

    void ModbusConnectionInstance::markPollFailure()
    {
        _consecutiveFailures++;

        if (_consecutiveFailures >= HEALTH_FAILURE_THRESHOLD)
        {
            // Calculate exponential backoff: 10s, 20s, 40s, 60s (max)
            uint8_t backoffMultiplier = _consecutiveFailures - HEALTH_FAILURE_THRESHOLD + 1;
            uint32_t backoffMs = HEALTH_BACKOFF_BASE_MS * (1 << (backoffMultiplier - 1));
            if (backoffMs > HEALTH_BACKOFF_MAX_MS)
                backoffMs = HEALTH_BACKOFF_MAX_MS;

            _backoffUntil = millis() + backoffMs;
            ZENO_LOG_RAW("[Modbus] Connection %s backoff %dms (failures=%d)\n",
                          _shortId, backoffMs, _consecutiveFailures);
        }
    }

    uint32_t ModbusConnectionInstance::getBackoffRemaining() const
    {
        if (_backoffUntil == 0)
            return 0;
        uint32_t now = millis();
        if (now >= _backoffUntil)
            return 0;
        return _backoffUntil - now;
    }

    // ============================================
    // v2.5: Non-blocking async read support
    // ============================================

    void ModbusConnectionInstance::processTask()
    {
        if (_modbusRTU)
            _modbusRTU->task();
        else if (_modbusTCP)
            _modbusTCP->task();
    }

    // v2.7: Force-abort pending RTU transaction for write preemption
    void ModbusConnectionInstance::abortTransaction()
    {
        if (_modbusRTU)
        {
            _modbusRTU->abort(); // Fires callback with EX_TIMEOUT, flushes RX buffer
        }
    }

    bool ModbusConnectionInstance::isBusy() const
    {
        if (_modbusRTU)
        {
            if (_modbusRTU->slave() != 0)
                return true;
            // Enforce turnaround delay between RTU transactions (50ms fixed)
            if (_lastTransactionEnd > 0 && (millis() - _lastTransactionEnd) < 50)
                return true;
            return false;
        }
        if (_modbusTCP)
            return _asyncTransId != 0 && _modbusTCP->isTransaction(_asyncTransId);
        return false;
    }

    bool ModbusConnectionInstance::beginAsyncRead(RegisterType regType, uint16_t address,
                                                  uint16_t *valueBuffer, uint16_t count, uint8_t slaveId)
    {
        if (!isConnected())
            return false;
        if (isBusy())
            return false;

        _asyncSuccess = false;
        _asyncBoolResult = false;
        _asyncTransId = 0;
        _lastErrorCode = 0;

        auto cb = [this](Modbus::ResultCode event, uint16_t transactionId, void *data) -> bool
        {
            _lastErrorCode = (uint8_t)event;
            _asyncSuccess = (event == Modbus::EX_SUCCESS);
            _asyncTransId = 0;              // Mark TCP transaction complete
            _lastTransactionEnd = millis(); // RTU turnaround: mark end for bus silence
            return true;
        };

        if (_modbusRTU)
        {
            uint16_t transId = 0;
            switch (regType)
            {
            case RegisterType::REG_HOLDING:
                transId = _modbusRTU->readHreg(slaveId, address, valueBuffer, count, cb, 0);
                break;
            case RegisterType::REG_INPUT:
                transId = _modbusRTU->readIreg(slaveId, address, valueBuffer, count, cb, 0);
                break;
            case RegisterType::REG_COIL:
                transId = _modbusRTU->readCoil(slaveId, address, &_asyncBoolResult, 1, cb, 0);
                break;
            case RegisterType::REG_DISCRETE:
                transId = _modbusRTU->readIsts(slaveId, address, &_asyncBoolResult, 1, cb, 0);
                break;
            default:
                return false;
            }
            return transId != 0;
        }
        else if (_modbusTCP)
        {
            if (!_ensureTCPConnection())
                return false;
            IPAddress ip;
            ip.fromString(_config.ipAddress);

            uint16_t transId = 0;
            switch (regType)
            {
            case RegisterType::REG_HOLDING:
                transId = _modbusTCP->readHreg(ip, address, valueBuffer, count, cb, slaveId);
                break;
            case RegisterType::REG_INPUT:
                transId = _modbusTCP->readIreg(ip, address, valueBuffer, count, cb, slaveId);
                break;
            case RegisterType::REG_COIL:
                transId = _modbusTCP->readCoil(ip, address, &_asyncBoolResult, 1, cb, slaveId);
                break;
            case RegisterType::REG_DISCRETE:
                transId = _modbusTCP->readIsts(ip, address, &_asyncBoolResult, 1, cb, slaveId);
                break;
            default:
                return false;
            }
            _asyncTransId = transId;
            return transId != 0;
        }
        return false;
    }

    // Read operations
    bool ModbusConnectionInstance::readCoil(uint16_t address, uint8_t slaveId)
    {
        if (!isConnected())
            return false;
        if (_modbusTCP)
        {
            if (!_ensureTCPConnection())
                return false;
            IPAddress ip;
            ip.fromString(_config.ipAddress);
            return _modbusTCP->readCoil(ip, address, nullptr, 1, nullptr, slaveId) != 0;
        }
        else if (_modbusRTU)
        {
            return _modbusRTU->readCoil(slaveId, address, nullptr, 1, nullptr, slaveId) != 0;
        }
        return false;
    }

    bool ModbusConnectionInstance::readDiscreteInput(uint16_t address, uint8_t slaveId)
    {
        if (!isConnected())
            return false;
        if (_modbusTCP)
        {
            if (!_ensureTCPConnection())
                return false;
            IPAddress ip;
            ip.fromString(_config.ipAddress);
            return _modbusTCP->readIsts(ip, address, nullptr, 1, nullptr, slaveId) != 0;
        }
        else if (_modbusRTU)
        {
            return _modbusRTU->readIsts(slaveId, address, nullptr, 1, nullptr, slaveId) != 0;
        }
        return false;
    }

    bool ModbusConnectionInstance::readHoldingRegister(uint16_t address, uint8_t slaveId)
    {
        if (!isConnected())
            return false;
        if (_modbusTCP)
        {
            if (!_ensureTCPConnection())
                return false;
            IPAddress ip;
            ip.fromString(_config.ipAddress);
            return _modbusTCP->readHreg(ip, address, nullptr, 1, nullptr, slaveId) != 0;
        }
        else if (_modbusRTU)
        {
            return _modbusRTU->readHreg(slaveId, address, nullptr, 1, nullptr, slaveId) != 0;
        }
        return false;
    }

    bool ModbusConnectionInstance::readInputRegister(uint16_t address, uint8_t slaveId)
    {
        if (!isConnected())
            return false;
        if (_modbusTCP)
        {
            if (!_ensureTCPConnection())
                return false;
            IPAddress ip;
            ip.fromString(_config.ipAddress);
            return _modbusTCP->readIreg(ip, address, nullptr, 1, nullptr, slaveId) != 0;
        }
        else if (_modbusRTU)
        {
            return _modbusRTU->readIreg(slaveId, address, nullptr, 1, nullptr, slaveId) != 0;
        }
        return false;
    }

    bool ModbusConnectionInstance::writeCoil(uint16_t address, bool value, uint8_t slaveId)
    {
        if (!isConnected())
            return false;
        if (_modbusTCP)
        {
            // FIX: Ensure TCP connection before write
            if (!_ensureTCPConnection())
            {
                ZENO_LOG("ModbusTCP", "Cannot writeCoil: TCP not connected");
                return false;
            }

            IPAddress ip;
            ip.fromString(_config.ipAddress);
            return _modbusTCP->writeCoil(ip, address, value, nullptr, slaveId) != 0;
        }
        else if (_modbusRTU)
        {
            return _modbusRTU->writeCoil(slaveId, address, value, nullptr, slaveId) != 0;
        }
        return false;
    }

    bool ModbusConnectionInstance::writeRegister(uint16_t address, uint16_t value, uint8_t slaveId)
    {
        if (!isConnected())
            return false;
        if (_modbusTCP)
        {
            // FIX: Ensure TCP connection before write
            if (!_ensureTCPConnection())
            {
                ZENO_LOG("ModbusTCP", "Cannot writeHreg: TCP not connected");
                return false;
            }

            IPAddress ip;
            ip.fromString(_config.ipAddress);
            return _modbusTCP->writeHreg(ip, address, value, nullptr, slaveId) != 0;
        }
        else if (_modbusRTU)
        {
            return _modbusRTU->writeHreg(slaveId, address, value, nullptr, slaveId) != 0;
        }
        return false;
    }

    bool ModbusConnectionInstance::readCoils(uint16_t address, uint16_t count, uint8_t slaveId)
    {
        if (!isConnected())
            return false;
        if (_modbusTCP)
        {
            if (!_ensureTCPConnection())
                return false;
            IPAddress ip;
            ip.fromString(_config.ipAddress);
            return _modbusTCP->readCoil(ip, address, nullptr, count, nullptr, slaveId) != 0;
        }
        else if (_modbusRTU)
        {
            return _modbusRTU->readCoil(slaveId, address, nullptr, count, nullptr, slaveId) != 0;
        }
        return false;
    }

    bool ModbusConnectionInstance::readDiscreteInputs(uint16_t address, uint16_t count, uint8_t slaveId)
    {
        if (!isConnected())
            return false;
        if (_modbusTCP)
        {
            if (!_ensureTCPConnection())
                return false;
            IPAddress ip;
            ip.fromString(_config.ipAddress);
            return _modbusTCP->readIsts(ip, address, nullptr, count, nullptr, slaveId) != 0;
        }
        else if (_modbusRTU)
        {
            return _modbusRTU->readIsts(slaveId, address, nullptr, count, nullptr, slaveId) != 0;
        }
        return false;
    }

    bool ModbusConnectionInstance::readHoldingRegisters(uint16_t address, uint16_t count, uint8_t slaveId)
    {
        if (!isConnected())
            return false;
        if (_modbusTCP)
        {
            if (!_ensureTCPConnection())
                return false;
            IPAddress ip;
            ip.fromString(_config.ipAddress);
            return _modbusTCP->readHreg(ip, address, nullptr, count, nullptr, slaveId) != 0;
        }
        else if (_modbusRTU)
        {
            return _modbusRTU->readHreg(slaveId, address, nullptr, count, nullptr, slaveId) != 0;
        }
        return false;
    }

    bool ModbusConnectionInstance::readInputRegisters(uint16_t address, uint16_t count, uint8_t slaveId)
    {
        if (!isConnected())
            return false;
        if (_modbusTCP)
        {
            if (!_ensureTCPConnection())
                return false;
            IPAddress ip;
            ip.fromString(_config.ipAddress);
            return _modbusTCP->readIreg(ip, address, nullptr, count, nullptr, slaveId) != 0;
        }
        else if (_modbusRTU)
        {
            return _modbusRTU->readIreg(slaveId, address, nullptr, count, nullptr, slaveId) != 0;
        }
        return false;
    }

    // NEW: Synchronous read with buffer - waits for response
    uint16_t ModbusConnectionInstance::readHoldingRegistersWithBuffer(uint16_t address, uint16_t *buffer, uint16_t count, uint8_t slaveId)
    {
        if (!isConnected() || !buffer)
        {
            ZENO_LOG("Modbus", "readHreg failed: not connected or null buffer");
            return 0;
        }

        ZENO_LOG_MODBUS("Reading Hreg: slave=%d addr=%d count=%d", slaveId, address, count);

        if (_modbusTCP)
        {
            // FIX: Ensure TCP connection before read
            if (!_ensureTCPConnection())
            {
                ZENO_LOG("ModbusTCP", "Cannot read: TCP not connected");
                return 0;
            }

            IPAddress ip;
            ip.fromString(_config.ipAddress);
            uint16_t transId = _modbusTCP->readHreg(ip, address, buffer, count, nullptr, slaveId);

            if (transId == 0)
            {
                ZENO_LOG("ModbusTCP", "readHreg failed to start transaction");
                return 0;
            }

            // Wait for response (blocking with timeout)
            uint32_t start = millis();
            while (_modbusTCP->isTransaction(transId) && (millis() - start) < _config.responseTimeout)
            {
                _modbusTCP->task();
                delay(1);
            }
            ZENO_LOG_VERBOSE("ModbusTCP: TCP Response buffer[0]=%d (0x%04X)", buffer[0], buffer[0]);
            return transId;
        }
        else if (_modbusRTU)
        {
            // ModbusRTU: Send request and wait for response synchronously
            // Based on modbus-esp8266 example: use slave() to check transaction status
            // slave() returns 0 when no transaction is in progress

            // Don't send if transaction already in progress
            if (_modbusRTU->slave())
            {
                ZENO_LOG("ModbusRTU", "Transaction already in progress");
                return 0;
            }

            // Static variable to store error result
            static Modbus::ResultCode lastError = Modbus::EX_SUCCESS;

            // Capture 'this' pointer to store error code in instance variable
            auto cb = [this](Modbus::ResultCode event, uint16_t transactionId, void *data) -> bool
            {
                lastError = event;
                this->_lastErrorCode = (uint8_t)event; // Store in instance variable

                if (event != Modbus::EX_SUCCESS)
                {
                    ZENO_LOG("ModbusRTU", "Error: 0x%02X (transId=%d)", event, transactionId);
                }
                else
                {
                    ZENO_LOG_MODBUS("Success callback (transId=%d)", transactionId);
                }
                return true;
            };

            lastError = Modbus::EX_SUCCESS; // Reset before request
            this->_lastErrorCode = 0;       // Reset instance error code
            uint16_t transId = _modbusRTU->readHreg(slaveId, address, buffer, count, cb, 0);
            ZENO_LOG_MODBUS("Sent RTU request: slave=%d addr=%d count=%d transId=%d",
                            slaveId, address, count, transId);

            // Process the transaction - wait until slave() returns 0 (transaction complete)
            uint32_t start = millis();
            uint32_t timeout = _config.responseTimeout > 0 ? _config.responseTimeout : 5000;

            while (_modbusRTU->slave())
            {
                _modbusRTU->task();
                delay(10);

                if (millis() - start >= timeout)
                {
                    ZENO_LOG("ModbusRTU", "Timeout after %lums", millis() - start);
                    return transId;
                }
            }

            // Transaction completed
            if (lastError == Modbus::EX_SUCCESS)
            {
                this->_lastErrorCode = 0; // Clear error on success
                ZENO_LOG_MODBUS("RTU Response in %lums, buffer[0]=%d (0x%04X)",
                                millis() - start, buffer[0], buffer[0]);
                return transId; // Success: return transaction ID
            }
            else
            {
                this->_lastErrorCode = (uint8_t)lastError; // Ensure error code is stored
                ZENO_LOG("ModbusRTU", "RTU Error 0x%02X in %lums (timeout/no response)",
                         lastError, millis() - start);
                return 0; // Error: return 0 bo li
            }
        }
        return 0;
    }

    uint16_t ModbusConnectionInstance::readInputRegistersWithBuffer(uint16_t address, uint16_t *buffer, uint16_t count, uint8_t slaveId)
    {
        if (!isConnected() || !buffer)
            return 0;

        if (_modbusTCP)
        {
            if (!_ensureTCPConnection())
                return 0;
            IPAddress ip;
            ip.fromString(_config.ipAddress);
            uint16_t transId = _modbusTCP->readIreg(ip, address, buffer, count, nullptr, slaveId);

            uint32_t start = millis();
            while (_modbusTCP->isTransaction(transId) && (millis() - start) < _config.responseTimeout)
            {
                _modbusTCP->task();
                delay(1);
            }
            return transId;
        }
        else if (_modbusRTU)
        {
            if (_modbusRTU->slave())
                return 0;

            static Modbus::ResultCode lastError = Modbus::EX_SUCCESS;
            auto cb = [this](Modbus::ResultCode event, uint16_t transactionId, void *data) -> bool
            {
                lastError = event;
                this->_lastErrorCode = (uint8_t)event;
                return true;
            };

            lastError = Modbus::EX_SUCCESS;
            this->_lastErrorCode = 0;
            uint16_t transId = _modbusRTU->readIreg(slaveId, address, buffer, count, cb, 0);

            uint32_t start = millis();
            uint32_t timeout = _config.responseTimeout > 0 ? _config.responseTimeout : 5000;
            while (_modbusRTU->slave())
            {
                _modbusRTU->task();
                delay(10);
                if (millis() - start >= timeout)
                    return transId;
            }

            if (lastError == Modbus::EX_SUCCESS)
            {
                this->_lastErrorCode = 0;
                return transId;
            }
            else
            {
                this->_lastErrorCode = (uint8_t)lastError;
                return 0;
            }
        }
        return 0;
    }

    bool ModbusConnectionInstance::readCoilWithBuffer(uint16_t address, bool *value, uint8_t slaveId)
    {
        if (!isConnected() || !value)
            return false;

        if (_modbusTCP)
        {
            if (!_ensureTCPConnection())
                return false;
            IPAddress ip;
            ip.fromString(_config.ipAddress);
            uint16_t transId = _modbusTCP->readCoil(ip, address, value, 1, nullptr, slaveId);

            uint32_t start = millis();
            while (_modbusTCP->isTransaction(transId) && (millis() - start) < _config.responseTimeout)
            {
                _modbusTCP->task();
                delay(1);
            }
            return transId != 0;
        }
        else if (_modbusRTU)
        {
            if (_modbusRTU->slave())
                return false;

            static Modbus::ResultCode lastError = Modbus::EX_SUCCESS;
            auto cb = [this](Modbus::ResultCode event, uint16_t transactionId, void *data) -> bool
            {
                lastError = event;
                this->_lastErrorCode = (uint8_t)event;
                return true;
            };

            lastError = Modbus::EX_SUCCESS;
            this->_lastErrorCode = 0;
            uint16_t transId = _modbusRTU->readCoil(slaveId, address, value, 1, cb, 0);

            uint32_t start = millis();
            uint32_t timeout = _config.responseTimeout > 0 ? _config.responseTimeout : 5000;
            while (_modbusRTU->slave())
            {
                _modbusRTU->task();
                delay(10);
                if (millis() - start >= timeout)
                    return transId != 0;
            }

            if (lastError == Modbus::EX_SUCCESS)
            {
                this->_lastErrorCode = 0;
                return true;
            }
            else
            {
                this->_lastErrorCode = (uint8_t)lastError;
                return false;
            }
        }
        return false;
    }

    bool ModbusConnectionInstance::readDiscreteWithBuffer(uint16_t address, bool *value, uint8_t slaveId)
    {
        if (!isConnected() || !value)
            return false;

        if (_modbusTCP)
        {
            if (!_ensureTCPConnection())
                return false;
            IPAddress ip;
            ip.fromString(_config.ipAddress);
            uint16_t transId = _modbusTCP->readIsts(ip, address, value, 1, nullptr, slaveId);

            uint32_t start = millis();
            while (_modbusTCP->isTransaction(transId) && (millis() - start) < _config.responseTimeout)
            {
                _modbusTCP->task();
                delay(1);
            }
            return transId != 0;
        }
        else if (_modbusRTU)
        {
            if (_modbusRTU->slave())
                return false;

            static Modbus::ResultCode lastError = Modbus::EX_SUCCESS;
            auto cb = [this](Modbus::ResultCode event, uint16_t transactionId, void *data) -> bool
            {
                lastError = event;
                this->_lastErrorCode = (uint8_t)event;
                return true;
            };

            lastError = Modbus::EX_SUCCESS;
            this->_lastErrorCode = 0;
            uint16_t transId = _modbusRTU->readIsts(slaveId, address, value, 1, cb, 0);

            uint32_t start = millis();
            uint32_t timeout = _config.responseTimeout > 0 ? _config.responseTimeout : 5000;
            while (_modbusRTU->slave())
            {
                _modbusRTU->task();
                delay(10);
                if (millis() - start >= timeout)
                    return transId != 0;
            }

            if (lastError == Modbus::EX_SUCCESS)
            {
                this->_lastErrorCode = 0;
                return true;
            }
            else
            {
                this->_lastErrorCode = (uint8_t)lastError;
                return false;
            }
        }
        return false;
    }

    uint16_t ModbusConnectionInstance::getLastValue(uint16_t address, bool *isValid) const
    {
        auto it = _registerCache.find(address);
        if (it != _registerCache.end())
        {
            if (isValid)
                *isValid = true;
            return it->second;
        }
        if (isValid)
            *isValid = false;
        return 0;
    }

    bool ModbusConnectionInstance::getLastBitValue(uint16_t address, bool *isValid) const
    {
        auto it = _bitCache.find(address);
        if (it != _bitCache.end())
        {
            if (isValid)
                *isValid = true;
            return it->second;
        }
        if (isValid)
            *isValid = false;
        return false;
    }

    const char *ModbusConnectionInstance::getShortId() const { return _shortId; }

    // Manager
    ModbusConnectionManager &ModbusConnectionManager::getInstance()
    {
        static ModbusConnectionManager instance;
        return instance;
    }

    // Set default RX/TX/DE pins for all RTU connections
    void ModbusConnectionManager::setDefaultRTUPins(int8_t rxPin, int8_t txPin, int8_t dePin)
    {
        s_defaultRxPin = rxPin;
        s_defaultTxPin = txPin;
        s_defaultDEPin = dePin;
        ZENO_LOG_RAW("[ModbusManager] Default RTU pins set: RX=%d, TX=%d, DE=%d\n", rxPin, txPin, dePin);
    }

    ModbusConnectionManager::~ModbusConnectionManager() { stop(); }

    bool ModbusConnectionManager::begin()
    {
        _lastLoopTime = millis();
        return true;
    }

    void ModbusConnectionManager::loop()
    {
        for (auto &pair : _connections)
            pair.second->loop();
        _lastLoopTime = millis();
    }

    void ModbusConnectionManager::stop() { _connections.clear(); }

    bool ModbusConnectionManager::addConnection(const ConnectionConfig &config)
    {
        if (config.shortId[0] == '\0')
            return false;
        String shortId(config.shortId);
        if (hasConnection(shortId))
            return updateConnection(config);

        auto conn = std::make_unique<ModbusConnectionInstance>(config);
        if (!conn->begin())
            return false;

        _connections[shortId] = std::move(conn);
        return true;
    }

    bool ModbusConnectionManager::updateConnection(const ConnectionConfig &config)
    {
        if (config.shortId[0] == '\0')
            return false;
        String shortId(config.shortId);
        removeConnection(shortId);
        return addConnection(config);
    }

    bool ModbusConnectionManager::removeConnection(const String &shortId)
    {
        auto it = _connections.find(shortId);
        if (it == _connections.end())
            return false;
        _connections.erase(it);
        return true;
    }

    bool ModbusConnectionManager::hasConnection(const String &shortId) const
    {
        return _connections.find(shortId) != _connections.end();
    }

    ModbusConnectionInstance *ModbusConnectionManager::getConnection(const String &shortId)
    {
        auto it = _connections.find(shortId);
        return (it != _connections.end()) ? it->second.get() : nullptr;
    }

    std::vector<String> ModbusConnectionManager::listConnections() const
    {
        std::vector<String> result;
        for (const auto &pair : _connections)
            result.push_back(pair.first);
        return result;
    }

    uint32_t ModbusConnectionManager::getActiveConnectionCount() const
    {
        uint32_t count = 0;
        for (const auto &pair : _connections)
        {
            if (pair.second->isConnected())
                count++;
        }
        return count;
    }

    uint32_t ModbusConnectionManager::getTotalErrors() const
    {
        uint32_t total = 0;
        for (const auto &pair : _connections)
            total += pair.second->getErrorCount();
        return total;
    }

    uint16_t ModbusConnectionManager::readRegister(const String &shortId, uint16_t address, uint8_t slaveId)
    {
        auto conn = getConnection(shortId);
        if (!conn)
            return 0;
        if (conn->readHoldingRegister(address, slaveId))
        {
            return conn->getLastValue(address);
        }
        return 0;
    }

    bool ModbusConnectionManager::readBit(const String &shortId, uint16_t address, uint8_t slaveId)
    {
        auto conn = getConnection(shortId);
        if (!conn)
            return false;
        if (conn->readCoil(address, slaveId))
        {
            return conn->getLastBitValue(address);
        }
        return false;
    }

} // namespace ZenoPCB

#endif  // defined(ESP32)
