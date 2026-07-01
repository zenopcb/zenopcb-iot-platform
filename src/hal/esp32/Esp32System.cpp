#include "Esp32System.h"

// (symmetric to Esp8266 mirror).
#if defined(ESP32)

#include <stdio.h>
#include <string.h>

namespace ZenoPCB {

void Esp32System::restart() {
    ESP.restart();
    // Defense for toolchains that drop [[noreturn]] on virtual methods
    // (/). ESP.restart resets the chip within ~1 ms;
    // this loop is unreachable in practice but proves to the compiler
    // that the function body cannot fall through.
    for (;;) {
        // Spin until the chip resets.
    }
}

uint32_t Esp32System::getFreeHeap() {
    return ESP.getFreeHeap();
}

uint32_t Esp32System::getMaxAllocHeap() {
    return ESP.getMaxAllocHeap();
}

uint32_t Esp32System::getTotalHeap() {
    // Matches DiagnosticsCollector.cpp:228 exactly so the used-heap
    // computation (getTotalHeap() - getFreeHeap()) is byte-identical
    // before and after.
    return ESP.getHeapSize();
}

size_t Esp32System::getUniqueId(char *out, size_t outSize) {
    if (!out || outSize < 9) return 0;

    uint64_t chipid = ESP.getEfuseMac();
    // Format matches WiFiProvisioning.cpp:1861 8 hex chars of the upper
    // 32 bits of the 48-bit MAC. snprintf only (CLAUDE.md).
    int written = snprintf(out, outSize, "%08X", (uint32_t)(chipid >> 16));
    if (written < 0) {
        out[0] = '\0';
        return 0;
    }
    // snprintf returns the would-be length if truncated; clamp.
    if ((size_t)written >= outSize) {
        return outSize - 1;
    }
    return (size_t)written;
}

uint32_t Esp32System::uptimeMs() {
    return millis();
}

void Esp32System::feedWatchdog() {
    // Arduino Core 3.x / ESP-IDF 5.x does NOT auto-register loopTask with
    // the task watchdog, so an unconditional esp_task_wdt_reset() floods
    // serial with "esp_task_wdt_reset(763): task not found" once per call
    // when the library feeds the WDT from inside loop()-driven code.
    // Only reset when the current task is actually registered (user opted
    // in via esp_task_wdt_add). Always yield() that feeds the IDLE
    // task WDT, which IS active by default and is the watchdog Arduino
    // sketches actually rely on.
    if (esp_task_wdt_status(NULL) == ESP_OK) {
        esp_task_wdt_reset();
    }
    yield();
}

}  // namespace ZenoPCB

#endif  // defined(ESP32)
