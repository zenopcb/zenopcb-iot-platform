#ifndef ZENOPCB_MODBUS_CONNECTION_MANAGER_H
#define ZENOPCB_MODBUS_CONNECTION_MANAGER_H

#include "ModbusRTU.h"
#include "ModbusTCP.h"
#include "../storage/ConnectionConfig.h"
#include "../storage/DataMonitorConfig.h"
#include <map>
#include <memory>

namespace ZenoPCB
{

    /**
     * @brief Modbus Connection wrapper - supports both RTU and TCP
     */
    class ModbusConnectionInstance
    {
    public:
        enum ConnectionStatus
        {
            IDLE,
            CONNECTING,
            CONNECTED,
            ERROR,
            DISCONNECTED
        };

        ModbusConnectionInstance(const ConnectionConfig &config);
        ~ModbusConnectionInstance();

        // Lifecycle
        bool begin();
        void stop();
        void loop(); // Call from main loop for RTU/TCP processing

        // Status
        bool isConnected() const { return _status == CONNECTED; }
        ConnectionStatus getStatus() const { return _status; }
        const char *getShortId() const;
        ConnectionProtocol getProtocol() const { return _protocol; }

        // Modbus operations - returns transaction ID (0 = failed)
        bool readCoil(uint16_t address, uint8_t slaveId = 1);
        bool readDiscreteInput(uint16_t address, uint8_t slaveId = 1);
        bool readHoldingRegister(uint16_t address, uint8_t slaveId = 1);
        bool readInputRegister(uint16_t address, uint8_t slaveId = 1);
        bool writeCoil(uint16_t address, bool value, uint8_t slaveId = 1);
        bool writeRegister(uint16_t address, uint16_t value, uint8_t slaveId = 1);

        // Read multiple registers
        bool readCoils(uint16_t address, uint16_t count, uint8_t slaveId = 1);
        bool readDiscreteInputs(uint16_t address, uint16_t count, uint8_t slaveId = 1);
        bool readHoldingRegisters(uint16_t address, uint16_t count, uint8_t slaveId = 1);
        bool readInputRegisters(uint16_t address, uint16_t count, uint8_t slaveId = 1);

        // ⭐ NEW: Read with buffer - synchronous read that writes result to buffer
        uint16_t readHoldingRegistersWithBuffer(uint16_t address, uint16_t *buffer, uint16_t count, uint8_t slaveId = 1);
        uint16_t readInputRegistersWithBuffer(uint16_t address, uint16_t *buffer, uint16_t count, uint8_t slaveId = 1);
        bool readCoilWithBuffer(uint16_t address, bool *value, uint8_t slaveId = 1);
        bool readDiscreteWithBuffer(uint16_t address, bool *value, uint8_t slaveId = 1);

        // Get last value (for polling)
        uint16_t getLastValue(uint16_t address, bool *isValid = nullptr) const;
        bool getLastBitValue(uint16_t address, bool *isValid = nullptr) const;

        // Error handling
        uint32_t getErrorCount() const { return _errorCount; }
        uint32_t getLastErrorTime() const { return _lastErrorTime; }
        String getLastError() const { return _lastError; }
        uint8_t getLastErrorCode() const { return _lastErrorCode; } // Modbus error code (0x00-0xFF)

        // ⭐ v2.4: Connection health tracking for backoff
        // When consecutive failures exceed threshold, connection enters backoff mode
        // to prevent blocking main loop with repeated timeouts
        bool isHealthy() const; // Returns true if connection is healthy or backoff expired
        void markPollSuccess(); // Reset failure count on successful poll
        void markPollFailure(); // Increment failures and apply backoff if threshold exceeded
        uint8_t getConsecutiveFailures() const { return _consecutiveFailures; }
        uint32_t getBackoffRemaining() const; // ms until backoff expires (0 if healthy)

        // v2.5: Non-blocking async read support
        void processTask();  // Process serial/TCP data (call frequently in main loop)
        bool isBusy() const; // True if a transaction is pending
        bool beginAsyncRead(RegisterType regType, uint16_t address,
                            uint16_t *valueBuffer, uint16_t count, uint8_t slaveId);
        bool wasAsyncReadSuccessful() const { return _asyncSuccess; }
        bool getAsyncBoolResult() const { return _asyncBoolResult; }
        uint32_t getResponseTimeout() const { return _config.responseTimeout; }

        // v2.7: Force-abort pending transaction for write preemption
        void abortTransaction();

        // Health thresholds (configurable via build_flags)
        static constexpr uint8_t HEALTH_FAILURE_THRESHOLD = 3;    // Failures before backoff
        static constexpr uint32_t HEALTH_BACKOFF_BASE_MS = 10000; // 10s initial backoff
        static constexpr uint32_t HEALTH_BACKOFF_MAX_MS = 30000;  // 30s max backoff

        // RTU bus turnaround: minimum silence between consecutive transactions
        // Uses interFrameDelay from ConnectionConfig; fallback to 5ms minimum
        static constexpr uint32_t RTU_TURNAROUND_MIN_MS = 5; // 5ms absolute minimum

    private:
        // Connection config
        char _shortId[SHORTID_LENGTH + 1];
        ConnectionProtocol _protocol;
        ConnectionConfig _config;
        ConnectionStatus _status;

        // Modbus instances (RTU or TCP, but not both)
        std::unique_ptr<ModbusRTU> _modbusRTU;
        std::unique_ptr<ModbusTCP> _modbusTCP;

        // Modbus base pointer
        Modbus *_modbus;

        // State tracking
        uint32_t _lastConnectAttempt;
        uint32_t _nextRetryTime;
        uint32_t _errorCount;
        uint32_t _lastErrorTime;
        String _lastError;
        uint8_t _lastErrorCode; // Last Modbus error code (0=success, 0xE4=timeout, etc.)

        // ⭐ v2.4: Health tracking for backoff
        uint8_t _consecutiveFailures; // Count of consecutive poll failures
        uint32_t _backoffUntil;       // Don't poll until this time (millis)

        // ⭐ v2.5: Async read state
        bool _asyncSuccess;           // Result of last async read
        bool _asyncBoolResult;        // For coil/discrete async reads
        uint16_t _asyncTransId;       // TCP transaction tracking
        uint32_t _lastTransactionEnd; // millis() when last RTU transaction completed

        // Data cache
        std::map<uint16_t, uint16_t> _registerCache; // address -> value
        std::map<uint16_t, bool> _bitCache;          // address -> bit value

        // Helper methods
        bool _initRTU();
        bool _initTCP();
        bool _ensureTCPConnection(); // ⭐ NEW: Ensure TCP is connected before operations
        bool _reconnect();
        void _setError(const String &error);
        void _clearError();

        // Callback for Modbus events (static wrapper needed)
        static void _onModbusResponse(uint16_t *frame, uint16_t *regs, uint8_t len);
    };

    /**
     * @brief Manager for multiple Modbus connections
     * Maps shortId -> ModbusConnectionInstance
     */
    class ModbusConnectionManager
    {
    public:
        static ModbusConnectionManager &getInstance();

        // ⭐ Set default RX/TX/DE pins for ALL RTU connections (call before addConnection)
        // rxPin, txPin: Serial1 pins for ESP32
        // dePin: Direction Enable pin for RS485 transceiver (MAX485 DE/RE)
        static void setDefaultRTUPins(int8_t rxPin, int8_t txPin, int8_t dePin = -1);

        // ⭐ Default RTU pins (static, shared by all connections)
        static int8_t s_defaultRxPin; // RX pin for Serial1
        static int8_t s_defaultTxPin; // TX pin for Serial1
        static int8_t s_defaultDEPin; // DE pin for RS485 direction control (-1 = auto TX)

        // Lifecycle
        bool begin();
        void loop();
        void stop();

        // Connection management
        bool addConnection(const ConnectionConfig &config);
        bool updateConnection(const ConnectionConfig &config);
        bool removeConnection(const String &shortId);
        bool hasConnection(const String &shortId) const;
        ModbusConnectionInstance *getConnection(const String &shortId);

        // List operations
        std::vector<String> listConnections() const;
        size_t getConnectionCount() const { return _connections.size(); }

        // Global status
        uint32_t getActiveConnectionCount() const;
        uint32_t getTotalErrors() const;

        // Read operations (find connection by ID)
        uint16_t readRegister(const String &shortId, uint16_t address, uint8_t slaveId = 1);
        bool readBit(const String &shortId, uint16_t address, uint8_t slaveId = 1);

    private:
        ModbusConnectionManager() = default;
        ~ModbusConnectionManager();

        // Prevent copy/move
        ModbusConnectionManager(const ModbusConnectionManager &) = delete;
        ModbusConnectionManager &operator=(const ModbusConnectionManager &) = delete;

        // Connection storage
        std::map<String, std::unique_ptr<ModbusConnectionInstance>> _connections;

        // Timing
        uint32_t _lastLoopTime;
    };

} // namespace ZenoPCB

#endif // ZENOPCB_MODBUS_CONNECTION_MANAGER_H
