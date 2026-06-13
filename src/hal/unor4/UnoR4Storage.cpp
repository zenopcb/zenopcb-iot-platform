#include "UnoR4Storage.h"

#if defined(ARDUINO_UNOR4_WIFI)

#include "../../core/ZenoPCBDebug.h"

namespace ZenoPCB {

bool UnoR4Storage::begin() {
    // CAP_FS_FILES=0 — no LittleFS / SPIFFS on UNO R4 RA4M1. Log once to
    // surface the platform gap; UnoR4Hal::capabilities() omits CAP_FS_FILES
    // so capability-gated callers should never reach this in practice.
    ZENO_LOG_CORE("[WARN] UnoR4Storage: filesystem not supported (CAP_FS_FILES=0)");
    return false;
}

bool   UnoR4Storage::exists(const char *)                          { return false; }
size_t UnoR4Storage::readFile(const char *, char *, size_t)        { return 0; }
size_t UnoR4Storage::writeFile(const char *, const char *, size_t) { return 0; }
bool   UnoR4Storage::deleteFile(const char *)                      { return false; }
bool   UnoR4Storage::mkdir(const char *)                           { return false; }
void   UnoR4Storage::listFiles(const char *, std::function<void(const char *)>) {}

}  // namespace ZenoPCB

#endif  // defined(ARDUINO_UNOR4_WIFI)
