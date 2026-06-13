/**
 * @file ModbusIntegration.h
 * @brief Main header to include all Modbus components
 *
 * Usage:
 * #include "modbus/ModbusIntegration.h"
 * using namespace ZenoPCB;
 *
 * // Initialize
 * ModbusConnectionManager::getInstance().begin();
 * ModbusDataBuffer::getInstance().begin();
 * RegisterPollingEngine::getInstance().begin();
 *
 * // Add connection
 * ConnectionConfig connConfig;
 * // ... configure ...
 * ModbusConnectionManager::getInstance().addConnection(connConfig);
 *
 * // Add register monitoring
 * DataMonitorConfig regConfig;
 * // ... configure ...
 * RegisterPollingEngine::getInstance().addRegister(regConfig);
 *
 * // In loop()
 * ModbusConnectionManager::getInstance().loop();
 * RegisterPollingEngine::getInstance().loop();
 */

#ifndef ZENOPCB_MODBUS_INTEGRATION_H
#define ZENOPCB_MODBUS_INTEGRATION_H

#include "ModbusConnectionManager.h"
#include "ModbusDataBuffer.h"
#include "RegisterPollingEngine.h"
#include "../storage/ConnectionConfig.h"
#include "../storage/DataMonitorConfig.h"
#include "../storage/LittleFSManager.h"

namespace ZenoPCB
{

    /**
     * @brief Initialize all Modbus components
     */
    inline bool initializeModbusSystem()
    {
        bool success = true;
        success &= ModbusConnectionManager::getInstance().begin();
        success &= ModbusDataBuffer::getInstance().begin();
        success &= RegisterPollingEngine::getInstance().begin();

        // ⭐ Set callback to check if connection is enabled
        // Used by buildTelemetryJson to skip registers of disabled connections
        ModbusDataBuffer::getInstance().setConnectionEnabledCallback([](const String &connectionId) -> bool
                                                                     {
            ConnectionConfig conn;
            if (LittleFSManager::readConfig(connectionId, conn))
            {
                return conn.enabled;
            }
            // If can't read config, assume enabled (don't skip)
            return true; });

        return success;
    }

    /**
     * @brief Load saved Modbus configs from LittleFS and add to managers
     * Call this AFTER initializeModbusSystem() and AFTER LittleFSManager::initialize()
     *
     * @return Number of configs loaded successfully
     */
    inline size_t loadSavedModbusConfigs()
    {
        size_t loadedCount = 0;

        // 1. Load all saved connections
        std::vector<String> connIds = LittleFSManager::listAllConfigs();
        Serial.printf("[Modbus] Found %d saved connection configs\n", connIds.size());

        for (const String &shortId : connIds)
        {
            ConnectionConfig conn;
            if (LittleFSManager::readConfig(shortId, conn))
            {
                if (conn.enabled)
                {
                    if (ModbusConnectionManager::getInstance().addConnection(conn))
                    {
                        Serial.printf("[Modbus] ✅ Loaded connection: %s (protocol=%s)\n",
                                      conn.shortId,
                                      conn.protocol == ConnectionProtocol::MODBUS_RTU ? "RTU" : "TCP");
                        loadedCount++;
                    }
                    else
                    {
                        Serial.printf("[Modbus] ❌ Failed to add connection: %s\n", conn.shortId);
                    }
                }
                else
                {
                    Serial.printf("[Modbus] ⏭️ Skipped disabled connection: %s\n", conn.shortId);
                }
            }
            else
            {
                Serial.printf("[Modbus] ❌ Failed to read connection: %s\n", shortId.c_str());
            }
        }

        // 2. Load all saved data monitors
        std::vector<String> monitorIds = LittleFSManager::listAllDataMonitors();
        Serial.printf("[Modbus] Found %d saved data monitor configs\n", monitorIds.size());

        for (const String &mqttKey : monitorIds)
        {
            DataMonitorConfig monitor;
            if (LittleFSManager::readDataMonitor(mqttKey, monitor))
            {
                if (monitor.enabled)
                {
                    // ⭐ Check if connection is enabled before adding register
                    String connId(monitor.connectionId);
                    ConnectionConfig conn;
                    bool connectionEnabled = true;

                    if (LittleFSManager::readConfig(connId, conn))
                    {
                        connectionEnabled = conn.enabled;
                    }

                    if (!connectionEnabled)
                    {
                        Serial.printf("[Modbus] ⏭️ Skipped monitor: %s (connection %s is disabled)\n",
                                      monitor.mqttKey, monitor.connectionId);
                        continue;
                    }

                    // Add to RegisterPollingEngine (which also adds to ModbusDataBuffer)
                    if (RegisterPollingEngine::getInstance().addRegister(monitor))
                    {
                        Serial.printf("[Modbus] ✅ Loaded monitor: %s (conn=%s, addr=%d)\n",
                                      monitor.mqttKey, monitor.connectionId, monitor.address);
                        loadedCount++;
                    }
                    else
                    {
                        Serial.printf("[Modbus] ❌ Failed to add monitor: %s (connection %s not found?)\n",
                                      monitor.mqttKey, monitor.connectionId);
                    }
                }
                else
                {
                    Serial.printf("[Modbus] ⏭️ Skipped disabled monitor: %s\n", monitor.mqttKey);
                }
            }
            else
            {
                Serial.printf("[Modbus] ❌ Failed to read monitor: %s\n", mqttKey.c_str());
            }
        }

        Serial.printf("[Modbus] 📊 Loaded %d configs total\n", loadedCount);
        return loadedCount;
    }

    /**
     * @brief Main loop for all Modbus components
     */
    inline void loopModbusSystem()
    {
        ModbusConnectionManager::getInstance().loop();
        RegisterPollingEngine::getInstance().loop();
    }

    /**
     * @brief Stop all Modbus components
     */
    inline void stopModbusSystem()
    {
        RegisterPollingEngine::getInstance().stop();
        ModbusConnectionManager::getInstance().stop();
        // ModbusDataBuffer is just storage, no stop() method needed
    }

} // namespace ZenoPCB

#endif // ZENOPCB_MODBUS_INTEGRATION_H
