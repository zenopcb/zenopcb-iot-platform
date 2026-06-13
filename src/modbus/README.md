# Modbus Integration - Complete Implementation

## ✅ Files Created

### Core Components
1. **ModbusConnectionManager.h/.cpp** - Manages RTU/TCP connections
2. **ModbusDataBuffer.h/.cpp** - Stores register values with scaling
3. **RegisterPollingEngine.h/.cpp** - Periodic register polling
4. **ModbusIntegration.h** - Main header with helper functions

## 📚 Usage Example

```cpp
#include "modbus/ModbusIntegration.h"

void setup() {
    Serial.begin(115200);
    
    // Initialize all Modbus components
    ZenoPCB::initializeModbusSystem();
    
    // Add Modbus RTU connection
    ZenoPCB::ConnectionConfig rtuConn;
    strncpy(rtuConn.shortId, "RTU1", 4);
    rtuConn.protocol = ZenoPCB::ConnectionProtocol::MODBUS_RTU;
    rtuConn.baudRate = 9600;
    rtuConn.dataBits = 8;
    rtuConn.stopBits = 1;
    rtuConn.parity = ZenoPCB::Parity::NONE;
    rtuConn.interFrameDelay = 50;
    
    ZenoPCB::ModbusConnectionManager::getInstance().addConnection(rtuConn);
    
    // Add Modbus TCP connection
    ZenoPCB::ConnectionConfig tcpConn;
    strncpy(tcpConn.shortId, "TCP1", 4);
    tcpConn.protocol = ZenoPCB::ConnectionProtocol::MODBUS_TCP;
    strncpy(tcpConn.ipAddress, "192.168.1.100", 15);
    tcpConn.port = 502;
    
    ZenoPCB::ModbusConnectionManager::getInstance().addConnection(tcpConn);
    
    // Add register monitoring
    ZenoPCB::DataMonitorConfig regConfig;
    strncpy(regConfig.mqttKey, "000001", 6);
    strncpy(regConfig.name, "Temperature", 20);
    regConfig.registerType = ZenoPCB::RegisterType::REG_HOLDING;
    regConfig.address = 0;
    regConfig.slaveId = 1;
    regConfig.dataType = ZenoPCB::DataType::SIGNED_INT16;
    regConfig.enabled = true;
    strncpy(regConfig.connectionId, "RTU1", 4);
    
    // Scaling: temperature in 0.1°C units
    regConfig.scaleEnabled = true;
    regConfig.scaleOperation = ZenoPCB::ScaleOperation::DIVIDE;
    regConfig.scaleValue = 10.0f;
    regConfig.offsetOperation = ZenoPCB::OffsetOperation::NONE;
    strncpy(regConfig.unit, "°C", 5);
    
    ZenoPCB::RegisterPollingEngine::getInstance().addRegister(regConfig);
}

void loop() {
    // Main loop for all Modbus operations
    ZenoPCB::loopModbusSystem();
    
    // Get telemetry JSON for MQTT publishing
    auto& buffer = ZenoPCB::ModbusDataBuffer::getInstance();
    String telemetryJson = buffer.buildTelemetryJson();
    
    if (telemetryJson.length() > 0) {
        // Publish to MQTT
        Serial.println("Telemetry: " + telemetryJson);
    }
    
    delay(100);
}
```

## 🔧 Configuration Mapping

### ConnectionConfig (from MQTT)
```json
{
  "id": "RTU1",
  "pr": "S",
  "en": 1,
  "br": 9600,
  "db": 8,
  "sb": 1,
  "pa": "N",
  "ifd": 50
}
```

Maps to:
```cpp
ConnectionConfig config;
strncpy(config.shortId, "RTU1", 4);
config.protocol = ConnectionProtocol::MODBUS_RTU;
config.enabled = true;
config.baudRate = 9600;
config.dataBits = 8;
config.stopBits = 1;
config.parity = Parity::NONE;
config.interFrameDelay = 50;
```

### DataMonitorConfig (from MQTT)
```json
{
  "id": "000001",
  "nm": "Temperature",
  "rt": "H",
  "ad": 0,
  "sid": 1,
  "dt": "s16",
  "en": 1,
  "sc_en": 1,
  "sc_op": "divide",
  "sc_val": 10.0,
  "u": "°C",
  "cn": "RTU1"
}
```

Maps to:
```cpp
DataMonitorConfig config;
strncpy(config.mqttKey, "000001", 6);
strncpy(config.name, "Temperature", 20);
config.registerType = RegisterType::REG_HOLDING;
config.address = 0;
config.slaveId = 1;
config.dataType = DataType::SIGNED_INT16;
config.enabled = true;
config.scaleEnabled = true;
config.scaleOperation = ScaleOperation::DIVIDE;
config.scaleValue = 10.0f;
strncpy(config.unit, "°C", 5);
strncpy(config.connectionId, "RTU1", 4);
```

## 📊 Enum Mappings

### RegisterType
| MQTT Value | Enum | Description |
|------------|------|-------------|
| `"H"` | `REG_HOLDING` | Holding registers (R/W) |
| `"I"` | `REG_INPUT` | Input registers (R) |
| `"C"` | `REG_COIL` | Coils (R/W bits) |
| `"D"` | `REG_DISCRETE` | Discrete inputs (R bits) |

### DataType
| MQTT Value | Enum | Size |
|------------|------|------|
| `"s16"` | `SIGNED_INT16` | 1 register |
| `"u16"` | `UNSIGNED_INT16` | 1 register |
| `"s32"` | `SIGNED_INT32` | 2 registers |
| `"u32"` | `UNSIGNED_INT32` | 2 registers |
| `"f32"` | `FLOAT32` | 2 registers |
| `"bool"` | `BOOLEAN` | 1 bit |

### ScaleOperation
| MQTT Value | Enum |
|------------|------|
| `"multiply"` | `MULTIPLY` |
| `"divide"` | `DIVIDE` |

### OffsetOperation
| MQTT Value | Enum |
|------------|------|
| `"add"` | `ADD` |
| `"subtract"` | `SUBTRACT` |

## 🔌 Integration with ZenoPCB Main

Add to `ZenoPCB.h`:
```cpp
#include "modbus/ModbusIntegration.h"

class ZenoPCB {
    // ... existing code ...
    
    bool enableModbus() {
        return ZenoPCB::initializeModbusSystem();
    }
    
    void loopModbus() {
        ZenoPCB::loopModbusSystem();
    }
};
```

Add to `ZenoPCB::loop()`:
```cpp
void ZenoPCB::loop() {
    // ... existing loop code ...
    loopModbus();
}
```

## 📝 TODO for Complete Integration

- [ ] Add callback handlers for ConnectionConfig MQTT messages
- [ ] Add callback handlers for DataMonitorConfig MQTT messages
- [ ] Integrate telemetry publishing with existing MQTT client
- [ ] Add error callbacks for connection failures
- [ ] Add statistics/monitoring dashboard
- [ ] Add unit tests
- [ ] Memory profiling (5000 registers scenario)

## ⚠️ Important Notes

1. **Serial Port**: Currently uses `Serial2` for Modbus RTU. Configure based on ESP32 board.
2. **Async Operations**: modbus-esp8266 is async. Read operations return transaction ID, actual values come in callbacks.
3. **Memory**: Each register ~50 bytes. 5000 registers ≈ 250KB (close to ESP32 limit).
4. **Polling Interval**: Default 5 seconds. Adjust via `RegisterPollingEngine::setGlobalPollInterval()`.

## 🎯 Next Steps

1. Test with real Modbus devices
2. Add ConfigMessageHandler integration
3. Add DataMonitorMessageHandler integration
4. Performance testing
5. Production deployment
