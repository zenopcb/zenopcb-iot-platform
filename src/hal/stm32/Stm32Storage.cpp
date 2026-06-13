#include "Stm32Storage.h"

#if defined(STM32F1) || defined(STM32F4)

#include "../../core/ZenoPCBDebug.h"

namespace ZenoPCB {

bool Stm32Storage::begin() {
    // STM32duino default Arduino core has no LittleFS / SPIFFS. Surfaces
    // platform gap; Stm32Hal::capabilities() omits CAP_FS_FILES so
    // capability-gated callers should never reach this in practice.
    // Persistence on STM32 lives in Stm32NVS (ZenoFlashStorage wrapper).
    ZENO_LOG_CORE("[WARN] Stm32Storage: filesystem not supported (CAP_FS_FILES=0). Persistence via NVS (ZenoFlashStorage).");
    return false;
}

bool   Stm32Storage::exists(const char *)                          { return false; }
size_t Stm32Storage::readFile(const char *, char *, size_t)        { return 0; }
size_t Stm32Storage::writeFile(const char *, const char *, size_t) { return 0; }
bool   Stm32Storage::deleteFile(const char *)                      { return false; }
bool   Stm32Storage::mkdir(const char *)                           { return false; }

void Stm32Storage::listFiles(const char *,
                             std::function<void(const char *)>) {
    // No filesystem — emit zero callbacks (no-op body).
}

}  // namespace ZenoPCB

#endif  // defined(STM32F1) || defined(STM32F4)
