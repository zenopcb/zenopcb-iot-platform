#include "Esp8266System.h"

#if defined(ESP8266)

#include <stdio.h>
#include <string.h>

namespace ZenoPCB {

void Esp8266System::restart() {
    ESP.restart();
    // Defense for toolchains that drop [[noreturn]] on virtual methods
    // (/). ESP.restart resets the chip within ~1 ms;
    // this loop is unreachable in practice but proves to the compiler
    // that the function body cannot fall through.
    for (;;) {
        // Spin until the chip resets.
    }
}

uint32_t Esp8266System::getFreeHeap() {
    return ESP.getFreeHeap();
}

uint32_t Esp8266System::getMaxAllocHeap() {
    // PITFALL 4 ESP8266 Arduino Core spells this `getMaxFreeBlockSize()`
    // (vs ESP32's `getMaxAllocHeap()`). Same semantics: largest contiguous
    // free block in bytes.
    return ESP.getMaxFreeBlockSize();
}

uint32_t Esp8266System::getTotalHeap() {
    // PITFALL 4 ESP8266 DRAM is statically partitioned at link time.
    // There is no `ESP.getHeapSize()` equivalent on ESP8266 Arduino
    // Core 3.x. 81920 is the standard ESP-12E (NodeMCU v3 / Wemos D1
    // Mini) DRAM total before the WiFi stack reservation. The
    // DiagnosticsCollector formula `used = getTotalHeap() - getFreeHeap()`
    // therefore over-counts by the WiFi stack share (~30 KB on a
    // connected WiFi STA). Acceptable for diagnostics purposes see
    // RESEARCH and in the plan threat model.
    return 81920;
}

size_t Esp8266System::getUniqueId(char *out, size_t outSize) {
    if (!out || outSize < 9) return 0;

    // PITFALL 4 ESP8266 chip ID is a 32-bit value from `ESP.getChipId()`
    // (vs ESP32's 48-bit MAC from `ESP.getEfuseMac()` truncated to its
    // upper 32 bits). Format as 8 hex chars + NUL to match the ESP32
    // analog output width (existing WiFiProvisioning + DeviceCredentials
    // call sites expect an 8-hex-char unique identifier). snprintf only
    // (CLAUDE.md memory rule no sprintf).
    uint32_t chipid = ESP.getChipId();
    int written = snprintf(out, outSize, "%08X", chipid);
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

uint32_t Esp8266System::uptimeMs() {
    return millis();
}

void Esp8266System::feedWatchdog() {
    // PITFALL 3 ESP8266 uses the in-core `ESP.wdtFeed()` (software
    // WDT) instead of the ESP-IDF `esp_task_wdt_reset()` (task WDT).
    // Both reset the platform watchdog timer for the calling context.
    ESP.wdtFeed();
}

}  // namespace ZenoPCB

#endif  // defined(ESP8266)
