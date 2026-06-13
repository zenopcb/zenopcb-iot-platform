# ESP32 Schedule System - Implementation Guide

## Overview

ZenoPCB ESP32 Schedule System là hệ thống hẹn giờ tự động cho phép điều khiển các thiết bị Modbus dựa trên thời gian hoặc khoảng thời gian. Hệ thống hỗ trợ 3 loại lịch:

- **Recurring (R)**: Lặp lại theo ngày và giờ (VD: Mỗi ngày lúc 8:00:00)
- **Once (O)**: Thực thi một lần tại timestamp cụ thể (VD: 2025-01-15 10:30:00 UTC)
- **Interval (I)**: Thực thi theo khoảng thời gian (VD: Mỗi 30 giây)

## Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                      ZenoPCB Main Class                     │
│  ┌───────────────────────────────────────────────────────┐  │
│  │  enableSchedule() → Callback Registration             │  │
│  │  loop() → Check every 1 second                        │  │
│  └─────────────────────┬─────────────────────────────────┘  │
└────────────────────────┼────────────────────────────────────┘
                         │
         ┌───────────────┴───────────────┐
         │                               │
         ▼                               ▼
┌────────────────────┐        ┌─────────────────────┐
│  TimeManager       │        │ ScheduleExecutor    │
│  (NTP Sync)        │        │ (Execution Engine)  │
├────────────────────┤        ├─────────────────────┤
│ • syncNTP()        │────────│ • loop()            │
│ • getUTC()         │        │ • checkRecurring()  │
│ • isSynced()       │        │ • checkOnce()       │
│ • parseTime()      │        │ • checkInterval()   │
└────────────────────┘        │ • executeAction()   │
                              └──────────┬──────────┘
                                         │
                         ┌───────────────┴───────────────┐
                         │                               │
                         ▼                               ▼
              ┌──────────────────┐          ┌─────────────────────┐
              │ ScheduleStorage  │          │ ModbusWriteQueue    │
              │ (LittleFS)       │          │ (Non-blocking I/O)  │
              ├──────────────────┤          ├─────────────────────┤
              │ • save()         │          │ • enqueueWrite()    │
              │ • load()         │          │ • getRegisterValue()│
              │ • delete()       │          └─────────────────────┘
              │ • loadAll()      │
              └──────────────────┘
                         ▲
                         │
              ┌──────────────────────┐
              │ ScheduleMessageHandler│
              │ (MQTT Parser)         │
              ├──────────────────────┤
              │ • handleMessage()    │
              │ • validateConfig()   │
              │ • publishACK()       │
              └──────────────────────┘
                         ▲
                         │
                    MQTT Topic:
              v1/devices/{token}/schedules
```

## Module Components

### 1. TimeManager (core/TimeManager.h)

**Purpose**: NTP time synchronization và UTC time utilities

**Key Functions**:
```cpp
void syncNTP();              // Sync with NTP servers (once at boot)
bool isSynced();             // Check if time synced (> 100000 seconds)
void waitForSync(uint8_t timeout); // Block wait for sync
time_t getUTC();             // Get current UTC time
bool parseTime(const char* timeStr, uint8_t& h, uint8_t& m, uint8_t& s);
```

**NTP Servers**:
- `pool.ntp.org` (primary)
- `time.google.com` (secondary)
- `time.cloudflare.com` (tertiary)

**Usage**:
```cpp
// Called automatically in WiFi connected callback
TimeManager::syncNTP();
TimeManager::waitForSync(10);  // Max 10 seconds

// Check sync status
if (TimeManager::isSynced()) {
    time_t now = TimeManager::getUTC();
    // Use gmtime_r() NOT localtime_r()
    struct tm timeinfo;
    gmtime_r(&now, &timeinfo);
}
```

### 2. ScheduleTypes.h

**Purpose**: Constants, enums, callbacks

**Key Definitions**:
```cpp
#define MAX_SCHEDULES 20  // Max number of schedules

enum class ScheduleType {
    RECURRING = 'R',  // Day+Time based
    ONCE = 'O',       // Timestamp based
    INTERVAL = 'I'    // Countdown based
};

enum class ActionType {
    SET = 'S',        // Write value
    TOGGLE = 'T'      // Flip current value
};

enum class RegisterType {
    HOLDING = 'H',    // Holding Register (0x03/0x06)
    COIL = 'C',       // Coil (0x01/0x05)
    INPUT = 'I',      // Input Register (0x04)
    DISCRETE = 'D'    // Discrete Input (0x02)
};

enum class ExecutionStatus {
    SUCCESS,
    FAILED,
    SKIPPED
};

// Callbacks
typedef std::function<void(const String&, ExecutionStatus, int64_t, const String&)> ScheduleExecutedCallback;
typedef std::function<void(const String&, const String&)> ScheduleErrorCallback;
```

### 3. ScheduleConfig.h

**Purpose**: Schedule configuration structure

**Key Structures**:
```cpp
struct ScheduleConfig {
    char id[5];            // Schedule ID "0001" (4 digits + null)
    char registerKey[7];   // Register key "000001" (6 digits + null)
    uint16_t address;      // Modbus address (0-65535)
    RegisterType registerType;  // H/C/I/D
    ScheduleType scheduleType;  // R/O/I
    
    // Recurring fields
    char executeTime[9];   // "HH:mm:ss" format (8 chars + null)
    char repeatDays[8];    // "1111111" = Mon-Sun (7 chars + null)
    
    // Once field
    time_t executeAt;      // Unix timestamp (UTC)
    
    // Interval field
    uint32_t intervalMs;   // 1000-3600000ms (1s to 1h)
    
    // Action fields
    ActionType actionType; // S/T
    int64_t setValue;      // Value for SET action
    
    bool enabled;          // Enable/disable schedule
};

struct ScheduleState {
    unsigned long lastExecutionTime;  // millis()
    bool executedToday;               // For recurring schedules
    
    // Overflow-safe interval check
    bool intervalElapsed(uint32_t interval) const {
        return (millis() - lastExecutionTime) >= interval;
    }
};
```

### 4. ScheduleStorage (schedule/ScheduleStorage.h)

**Purpose**: LittleFS persistence

**File Structure**:
```
/schedules/
├── 0001.json  (individual schedule)
├── 0002.json
├── ...
└── meta.json  (metadata: count)
```

**Key Functions**:
```cpp
bool saveSchedule(const ScheduleConfig& config);
bool loadSchedule(const char* scheduleId, ScheduleConfig& out);
bool deleteSchedule(const char* scheduleId);
std::vector<ScheduleConfig> loadAllSchedules();
void clearAllSchedules();
bool isMaxSchedulesReached();  // Check if >= 20 schedules
```

**JSON Format** (Example):
```json
{
  "id": "0001",
  "rid": "000001",
  "ad": 100,
  "rt": "H",
  "st": "R",
  "et": "08:00:00",
  "rp": "1111111",
  "at": "S",
  "sv": 1,
  "en": true
}
```

### 5. ScheduleMessageHandler (schedule/ScheduleMessageHandler.h)

**Purpose**: Parse MQTT messages và validate

**Singleton Pattern**:
```cpp
ScheduleMessageHandler& handler = ScheduleMessageHandler::getInstance();
```

**Key Functions**:
```cpp
ScheduleHandleResult handleMessage(const String& topic, const String& payload);
void onScheduleCreated(ScheduleCreatedCallback callback);
void onScheduleUpdated(ScheduleUpdatedCallback callback);
void onScheduleDeleted(ScheduleDeletedCallback callback);
void onScheduleSynced(ScheduleSyncedCallback callback);
```

**MQTT Message Format**:
```json
{
  "t": "s",     // Type: schedule
  "a": "c",     // Action: c/u/d/s
  "d": {        // Data
    "id": "0001",
    "rid": "000001",
    ...
  }
}
```

**Actions**:
- `c` (create): Tạo schedule mới (check MAX_SCHEDULES limit)
- `u` (update): Cập nhật schedule existing
- `d` (delete): Xóa schedule by ID
- `s` (sync): Replace all schedules (hỗ trợ empty array)

**Validation Rules**:
- Schedule ID: 4 digits (0001-9999)
- Register Key: 6 digits (000001-999999)
- Time: "HH:mm:ss" format (00:00:00 - 23:59:59)
- Interval: 1000-3600000ms (1s - 1h)
- Repeat Days: 7 chars "0110100" (0=skip, 1=execute)

### 6. ScheduleExecutor (schedule/ScheduleExecutor.h)

**Purpose**: Execute schedules based on time/interval

**Key Functions**:
```cpp
bool begin();  // Load schedules from LittleFS
void loop();   // Check schedules (called every 1 second)
void addOrUpdateSchedule(const ScheduleConfig& config);
void removeSchedule(const String& id);
void reloadSchedules();  // After sync
uint8_t getScheduleCount() const;

void onScheduleExecuted(ScheduleExecutedCallback callback);
void onScheduleError(ScheduleErrorCallback callback);
```

**Execution Logic**:

1. **Recurring Schedules**:
   - Parse `executeTime` to hours, minutes, seconds
   - Check if current day matches `repeatDays` (tm_wday: 0=Sun, 1=Mon, ...)
   - Compare current UTC time (HH:mm:ss) with executeTime
   - If match && !executedToday → execute
   - Reset executedToday flag at midnight

2. **Once Schedules**:
   - Compare `time(nullptr) >= executeAt`
   - Execute once, then mark as completed (not deleted)

3. **Interval Schedules**:
   - Use `millis()` for countdown
   - Check `(millis() - lastExecutionTime) >= intervalMs`
   - Overflow-safe comparison
   - Execute and update lastExecutionTime

**Action Execution**:
- **SET**: Write `setValue` to register via `enqueueWriteByKey()`
- **TOGGLE**: Read current value, flip it (0↔1, value↔0), write back

**Modbus Integration**:
```cpp
// Non-blocking write
RegisterPollingEngine::enqueueWriteByKey(
    registerKey,
    setValue,
    [](bool success) {
        // Callback after write completes
    }
);

// Read current value for TOGGLE
int64_t currentValue = ModbusDataBuffer::getRegisterValue(registerKey);
```

## Integration with ZenoPCB Main Class

### 1. Enable Schedule Module

```cpp
Zeno zeno;

zeno.enableSchedule()
    .onScheduleExecuted([](const String& id, ExecutionStatus status, 
                           int64_t value, const String& error) {
        Serial.printf("Schedule %s: %s\n", id.c_str(), 
                     executionStatusToString(status));
    })
    .onScheduleError([](const String& id, const String& error) {
        Serial.printf("Error %s: %s\n", id.c_str(), error.c_str());
    })
    .onScheduleCreated([](const ScheduleConfig& config) {
        Serial.printf("Created: %s\n", config.id);
    })
    .onScheduleUpdated([](const ScheduleConfig& config) {
        Serial.printf("Updated: %s\n", config.id);
    })
    .onScheduleDeleted([](const String& id) {
        Serial.printf("Deleted: %s\n", id.c_str());
    })
    .onScheduleSynced([](uint8_t count) {
        Serial.printf("Synced: %d schedules\n", count);
    });

zeno.begin();
```

### 2. Initialization Flow

**In `Zeno::begin()`**:
1. `_initProvisioning()` - WiFi setup
2. `_initMQTT()` - MQTT client
3. `_initStorage()` - LittleFS mount
4. `_initDataMonitorStorage()` - Data monitor
5. **`_initSchedule()`** - Schedule module ← NEW
6. Modbus system init
7. WiFi connect

**In `_initSchedule()`**:
1. Create `ScheduleExecutor` instance
2. Load schedules from LittleFS
3. Register callbacks from `ScheduleMessageHandler` to `ScheduleExecutor`
4. Register executor callbacks to user callbacks
5. Setup MQTT subscription to `v1/devices/{token}/schedules` (QoS 1)
6. **Sync NTP** when MQTT connected

### 3. Main Loop

```cpp
void Zeno::loop() {
    // ... existing modules ...
    
    // Schedule executor (every 1 second)
    if (_scheduleExecutor && _scheduleEnabled) {
        static unsigned long lastScheduleCheck = 0;
        if (millis() - lastScheduleCheck >= 1000) {
            lastScheduleCheck = millis();
            _scheduleExecutor->loop();
        }
    }
}
```

### 4. MQTT Message Routing

```cpp
_mqtt->onMessage([this](const String& topic, const String& payload) {
    if (topic.endsWith("/schedules")) {
        _handleScheduleMessage(topic, payload);
    }
    // ... other topics ...
});
```

### 5. Message Handler

```cpp
void Zeno::_handleScheduleMessage(const String& topic, const String& payload) {
    ScheduleMessageHandler& handler = ScheduleMessageHandler::getInstance();
    ScheduleHandleResult result = handler.handleMessage(topic, payload);
    
    // Publish ACK
    if (_mqtt && _mqtt->isConnected()) {
        String ackTopic = "v1/devices/" + _deviceId + "/schedules/ack";
        String ackPayload = result.toJson();
        _mqtt->publish(ackTopic.c_str(), ackPayload.c_str(), QOS_1, false);
    }
}
```

## MQTT Protocol Examples

### Create Recurring Schedule

**Topic**: `v1/devices/{token}/schedules` (QoS 1, Retained)

```json
{
  "t": "s",
  "a": "c",
  "d": {
    "id": "0001",
    "rid": "000001",
    "ad": 100,
    "rt": "H",
    "st": "R",
    "et": "08:00:00",
    "rp": "1111111",
    "at": "S",
    "sv": 1,
    "en": true
  }
}
```

**ACK Response** (Topic: `v1/devices/{token}/schedules/ack`):
```json
{
  "a": "c",
  "id": "0001",
  "success": true,
  "processingMs": 45
}
```

### Execution Report

**Topic**: `v1/devices/{token}/schedules/executed` (QoS 1)

```json
{
  "id": "0001",
  "ts": 1735286400,
  "status": "success",
  "value": "1"
}
```

## Testing Guide

### 1. NTP Sync Test

```cpp
void testNTPSync() {
    Serial.println("Testing NTP sync...");
    TimeManager::syncNTP();
    
    if (TimeManager::waitForSync(10)) {
        Serial.println("✅ NTP synced!");
        time_t now = TimeManager::getUTC();
        struct tm timeinfo;
        gmtime_r(&now, &timeinfo);
        Serial.printf("Current UTC: %04d-%02d-%02d %02d:%02d:%02d\n",
                     timeinfo.tm_year + 1900, timeinfo.tm_mon + 1,
                     timeinfo.tm_mday, timeinfo.tm_hour,
                     timeinfo.tm_min, timeinfo.tm_sec);
    } else {
        Serial.println("❌ NTP sync failed!");
    }
}
```

### 2. Storage Test

```cpp
void testStorage() {
    ScheduleConfig config;
    strcpy(config.id, "0001");
    strcpy(config.registerKey, "000001");
    config.address = 100;
    config.registerType = RegisterType::HOLDING;
    config.scheduleType = ScheduleType::RECURRING;
    strcpy(config.executeTime, "08:00:00");
    strcpy(config.repeatDays, "1111111");
    config.actionType = ActionType::SET;
    config.setValue = 1;
    config.enabled = true;
    
    if (ScheduleStorage::saveSchedule(config)) {
        Serial.println("✅ Schedule saved!");
    }
    
    std::vector<ScheduleConfig> schedules = ScheduleStorage::loadAllSchedules();
    Serial.printf("Loaded %d schedules\n", schedules.size());
}
```

### 3. Execution Test

```cpp
void testExecution() {
    ScheduleExecutor executor;
    
    if (!executor.begin()) {
        Serial.println("❌ Executor init failed!");
        return;
    }
    
    executor.onScheduleExecuted([](const String& id, ExecutionStatus status,
                                   int64_t value, const String& error) {
        Serial.printf("Schedule %s: %s (value=%lld)\n",
                     id.c_str(), executionStatusToString(status), value);
    });
    
    // Simulate loop
    for (int i = 0; i < 100; i++) {
        executor.loop();
        delay(1000);
    }
}
```

## Performance Considerations

### Memory Usage

- **Heap**: ~50-60 KB for 20 schedules
- **Flash**: ~40-50 KB code
- **LittleFS**: ~200-300 bytes per schedule file

### Execution Timing

- **Check Frequency**: 1 second (configurable)
- **Granularity**: 1 second (minimum)
- **Latency**: < 100ms (Modbus write enqueue)
- **Accuracy**: ±1 second (NTP sync dependent)

### Optimization Tips

1. **Reduce Check Frequency**: Increase from 1s to 5s if schedules are sparse
2. **Batch Writes**: Group multiple schedules with same timestamp
3. **Lazy Loading**: Don't load all schedules at boot, load on-demand
4. **State Caching**: Cache execution states in RAM instead of re-reading files

## Troubleshooting

### Issue: NTP sync fails

**Symptoms**: `isSynced()` returns false after 10 seconds

**Solutions**:
- Check WiFi connection
- Verify NTP server accessibility (ping pool.ntp.org)
- Increase timeout in `waitForSync()`
- Try alternative NTP servers

### Issue: Schedule not executing

**Symptoms**: Time passes but schedule doesn't trigger

**Debug Steps**:
1. Check if schedule enabled: `config.enabled == true`
2. Verify NTP synced: `TimeManager::isSynced()`
3. Print current UTC time vs. executeTime
4. Check repeat days match current day (tm_wday)
5. Verify executedToday flag reset at midnight

### Issue: Max schedules reached

**Symptoms**: Cannot create new schedule, ACK returns error

**Solutions**:
- Delete unused schedules
- Increase `MAX_SCHEDULES` constant
- Check LittleFS free space

### Issue: Interval schedule drifts

**Symptoms**: Interval execution not precise (e.g., 10s becomes 11s)

**Causes**:
- `millis()` overflow after 49 days
- Loop delay longer than 1 second
- Modbus write blocking loop

**Solutions**:
- Use overflow-safe comparison: `(millis() - last >= interval)`
- Ensure loop() runs at least once per second
- Non-blocking Modbus writes via queue

## Future Enhancements

### Planned Features

- [ ] **Timezone Support**: Add timezone offset to config
- [ ] **Conditional Execution**: Execute only if register value meets condition
- [ ] **Schedule Chains**: Execute multiple actions in sequence
- [ ] **Priority System**: High-priority schedules execute first
- [ ] **Web UI**: Local web interface for schedule management
- [ ] **Schedule Groups**: Organize schedules by room/device
- [ ] **Execution History**: Log last 100 executions in LittleFS
- [ ] **Calendar Integration**: Import schedules from iCal/Google Calendar

### API Extensions

```cpp
// Future API ideas
zeno.enableSchedule()
    .setTimezone("Asia/Ho_Chi_Minh")  // Timezone support
    .setCheckFrequency(500)            // Check every 500ms
    .setPriority("0001", 10)           // High priority
    .addCondition("0001", [](int64_t value) {
        return value < 100;            // Only if value < 100
    })
    .chainSchedule("0001", "0002");    // Execute 0002 after 0001
```

## References

- **MQTT Protocol**: `doc/ESP32_SCHEDULE_MQTT_PROTOCOL.md`
- **Example Code**: `examples/Schedule_MQTT/Schedule_MQTT.ino`
- **API Documentation**: `lib/ZenoPCB/src/schedule/README.md` (this file)
- **ZenoPCB Main**: `lib/ZenoPCB/src/ZenoPCB.h`

## Support

- **Issues**: [GitHub Issues](https://github.com/zenopcb/zenopcb-platform-iot/issues)
- **Email**: firmware@zenopcb.vn
- **Documentation**: [ZenoPCB Docs](https://docs.zenopcb.com)
