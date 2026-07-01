#include "Esp8266OTA.h"

#if defined(ESP8266)

#include <string.h>
#include <WString.h>   // Arduino String for the errorString() cache copy.

namespace ZenoPCB {

bool Esp8266OTA::begin(size_t expectedSize, const char *expectedMd5) {
    if (!Update.begin(expectedSize)) return false;
    // (RESEARCH "ESP32 API Wrapping Strategy") MD5 via begin.
    // The ESP8266 UpdaterClass exposes the same `setMD5(const char*)` overload.
    if (expectedMd5 && expectedMd5[0] != '\0') {
        Update.setMD5(expectedMd5);
    }
    return true;
}

size_t Esp8266OTA::write(const uint8_t *data, size_t len) {
    if (!data || len == 0) return 0;
    // UpdaterClass::write takes a non-const uint8_t* on both ESP32 Arduino
    // Core 3.x and ESP8266 Arduino Core 3.x. The data is read-only from
    // our perspective; const_cast is safe here.
    return Update.write(const_cast<uint8_t *>(data), len);
}

bool Esp8266OTA::end() {
    // true = set new partition as bootable. Caller is responsible for the
    // reboot via IZenoSystem::restart (interface contract from).
    return Update.end(true);
}

void Esp8266OTA::abort() {
    // PITFALL 2 ESP8266 UpdaterClass has no Update.abort(). Partial
    // writes are already in flash; eboot detects a malformed sketch at
    // next boot and refuses to swap. No-op is the correct behaviour.
}

const char *Esp8266OTA::errorString() {
    // PITFALL 2 ESP8266 `UpdaterClass::getErrorString()` returns
    // `String` by value (not `const char*` like Arduino-ESP32 Core 3.x).
    // Cache into a static char buffer so the IZenoOTA contract's
    // "pointer remains valid until the next OTA call" promise holds.
    //
    // `char buf[64]` 512 B per-frame budget; function-local
    // static is initialised once and zero-filled at first call.
    static char buf[64];
    String s = Update.getErrorString();
    size_t copyLen = s.length();
    if (copyLen >= sizeof(buf)) copyLen = sizeof(buf) - 1;
    memcpy(buf, s.c_str(), copyLen);
    buf[copyLen] = '\0';
    return buf;
}

// PITFALL 2 ESP8266 eboot is single-slot (no spare bootable
// partition). Rollback is unsupported at the bootloader level on
// both calls; one-liner bodies keep the structural grep gate happy.
bool Esp8266OTA::canRollBack() { return false; }
bool Esp8266OTA::rollBack()    { return false; }

}  // namespace ZenoPCB

#endif  // defined(ESP8266)
