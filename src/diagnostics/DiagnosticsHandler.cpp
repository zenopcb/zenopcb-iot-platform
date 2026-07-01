// TU guard for ZENOPCB_MICRO_BASIC profile.
#if !defined(ZENOPCB_DISABLE_DIAGNOSTICS)

#include "DiagnosticsHandler.h"
#include "../mqtt/ZenoPCBMQTT.h"
#include "../core/ZenoPCBDebug.h"

#if ZENOPCB_DEBUG_VERBOSE
#define ZENO_LOG_DIAG_V(fmt, ...) ZENO_LOG("ZenoPCB", fmt, ##__VA_ARGS__)
#else
#define ZENO_LOG_DIAG_V(fmt, ...)
#endif

namespace ZenoPCB
{

    DiagnosticsHandler::DiagnosticsHandler(DiagnosticsCollector *collector, ZenoPCBMQTT *mqtt)
        : _collector(collector),
          _mqtt(mqtt),
          _maxRetries(2),
          _requestCallback(nullptr),
          _sentCallback(nullptr),
          _errorCallback(nullptr)
    {
    }

    void DiagnosticsHandler::setMaxRetries(uint8_t maxRetries)
    {
        _maxRetries = maxRetries;
    }

    void DiagnosticsHandler::setRequestCallback(DiagnosticsRequestCallback callback)
    {
        _requestCallback = callback;
    }

    void DiagnosticsHandler::setSentCallback(DiagnosticsSentCallback callback)
    {
        _sentCallback = callback;
    }

    void DiagnosticsHandler::setErrorCallback(DiagnosticsErrorCallback callback)
    {
        _errorCallback = callback;
    }

    bool DiagnosticsHandler::sendPassive()
    {
        if (_collector == nullptr || _mqtt == nullptr)
        {
            ZENO_LOG_CORE("[DIAG] Cannot send: collector or MQTT is null");
            return false;
        }

        // Collect data without requestId
        DiagnosticsData data = _collector->collect(nullptr);

        // Format to JSON
        String payload = _formatJson(data);

        ZENO_LOG_DIAG_V("[DIAG-PASSIVE] Sending diagnostics");
        ZENO_LOG_DIAG_V("[DIAG-PASSIVE] Payload: %s", payload.c_str());

        // Send via MQTT (fire-and-forget, no retry)
        bool success = _mqtt->sendDiagnostics(payload);

        if (success)
        {
            ZENO_LOG_DIAG_V("[DIAG-PASSIVE] Sent successfully");
            if (_sentCallback)
            {
                _sentCallback(true); // isPassive = true
            }
        }
        else
        {
            ZENO_LOG_CORE("[DIAG-PASSIVE] Send failed (no retry for passive)");
            if (_errorCallback)
            {
                _errorCallback("Failed to send passive diagnostics", true);
            }
        }

        return success;
    }

    bool DiagnosticsHandler::sendOnDemand(const String &requestId)
    {
        if (_collector == nullptr || _mqtt == nullptr)
        {
            ZENO_LOG_CORE("[DIAG-RESPONSE] Cannot send: collector or MQTT is null");
            return false;
        }

        if (requestId.length() == 0)
        {
            ZENO_LOG_CORE("[DIAG-RESPONSE] RequestId is empty");
            return false;
        }

        // Collect data with requestId
        DiagnosticsData data = _collector->collect(requestId.c_str());

        // Format to JSON
        String payload = _formatJson(data);

        ZENO_LOG_DIAG_V("[DIAG-RESPONSE] Sending on-demand response for requestId: %s", requestId.c_str());
        ZENO_LOG_DIAG_V("[DIAG-RESPONSE] Payload: %s", payload.c_str());

        // Send with retry logic
        bool success = _sendWithRetry("diagnostics/response", payload, false);

        if (success)
        {
            ZENO_LOG_DIAG_V("[DIAG-RESPONSE] Sent successfully");
            if (_sentCallback)
            {
                _sentCallback(false); // isPassive = false
            }
        }
        else
        {
            ZENO_LOG_CORE("[DIAG-RESPONSE] Failed after %d retries", _maxRetries);
            if (_errorCallback)
            {
                _errorCallback("Failed to send on-demand response after retries", false);
            }
        }

        return success;
    }

    void DiagnosticsHandler::handleRequest(const String &payload)
    {
        ZENO_LOG_DIAG_V("[DIAG-REQUEST] Received request: %s", payload.c_str());

        // Extract requestId from JSON payload
        String requestId = _extractRequestId(payload);

        if (requestId.length() == 0)
        {
            ZENO_LOG_CORE("[DIAG-REQUEST] Invalid request: missing requestId");
            return;
        }

        // Notify via callback
        if (_requestCallback)
        {
            _requestCallback(requestId);
        }

        // Send on-demand response (with retry)
        sendOnDemand(requestId);
    }

    // ============================================
    // Private helper methods
    // ============================================

    String DiagnosticsHandler::_formatJson(const DiagnosticsData &data)
    {
        // Use elastic JsonDocument (ArduinoJson v7 capacity grows from heap)
        JsonDocument doc;

        // Add requestId only if present (on-demand response)
        if (data.requestId.length() > 0)
        {
            doc["requestId"] = data.requestId;
        }

        // Required fields
        doc["connectionType"] = connectionTypeToString(data.connectionType);
        doc["deviceIp"] = data.deviceIp;
        doc["signalStrength"] = data.signalStrength;
        doc["firmwareVersion"] = data.firmwareVersion;
        doc["uptime"] = data.uptime;
        doc["memoryUsage"] = data.memoryUsage;

        // Optional fields (recommended)
        doc["freeHeap"] = data.freeHeap;
        doc["rssi"] = data.rssi;
        doc["macAddress"] = data.macAddress;

        // Ethernet-specific fields
        if (data.linkSpeed > 0)
        {
            doc["linkSpeed"] = data.linkSpeed;
            doc["fullDuplex"] = data.fullDuplex;
        }

        // Cellular-specific fields (4G/5G)
        if (data.connectionType == DiagnosticsConnectionType::CELLULAR_4G ||
            data.connectionType == DiagnosticsConnectionType::CELLULAR_5G)
        {
            if (data.operatorName.length() > 0)
                doc["operator"] = data.operatorName;
            if (data.networkType.length() > 0)
                doc["networkType"] = data.networkType;
            if (data.imei.length() > 0)
                doc["imei"] = data.imei;
            if (data.csq > 0)
                doc["csq"] = data.csq;
        }

        // Serialize to string
        String output;
        serializeJson(doc, output);

        return output;
    }

    bool DiagnosticsHandler::_sendWithRetry(const String &topic, const String &payload, bool isPassive)
    {
        uint8_t attempts = 0;
        uint8_t maxAttempts = isPassive ? 1 : (_maxRetries + 1); // Passive: no retry, On-demand: 1 + maxRetries

        while (attempts < maxAttempts)
        {
            bool success = false;

            if (topic == "diagnostics/response")
            {
                success = _mqtt->sendDiagnosticsResponse(payload);
            }
            else
            {
                success = _mqtt->sendDiagnostics(payload);
            }

            if (success)
            {
                if (attempts > 0)
                {
                    ZENO_LOG_DIAG_V("[DIAG] Sent successfully after %d retries", attempts);
                }
                return true;
            }

            attempts++;

            // Retry delay (exponential backoff: 100ms, 200ms, 400ms)
            if (attempts < maxAttempts)
            {
                uint32_t delayMs = 100 * (1 << (attempts - 1)); // 100, 200, 400
                ZENO_LOG_DIAG_V("[DIAG] Retry %d/%d after %dms", attempts, maxAttempts - 1, delayMs);
                delay(delayMs);
            }
        }

        return false; // All attempts failed
    }

    String DiagnosticsHandler::_extractRequestId(const String &jsonPayload)
    {
        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, jsonPayload);

        if (error)
        {
            ZENO_LOG_CORE("[DIAG] JSON parse error: %s", error.c_str());
            return "";
        }

        if (!doc.containsKey("requestId"))
        {
            ZENO_LOG_CORE("[DIAG] Missing requestId in payload");
            return "";
        }

        return doc["requestId"].as<String>();
    }

} // namespace ZenoPCB

#endif // !ZENOPCB_DISABLE_DIAGNOSTICS
