#include "Esp32OTA.h"

// Plan 06-03 Pattern B (symmetric to Plan 06-2.5d Esp8266 mirror).
#if defined(ESP32)

namespace ZenoPCB {

bool Esp32OTA::begin(size_t expectedSize, const char *expectedMd5) {
    if (!Update.begin(expectedSize)) return false;
    // Pitfall 2 (RESEARCH §"ESP32 API Wrapping Strategy") — MD5 via begin().
    if (expectedMd5 && expectedMd5[0] != '\0') {
        Update.setMD5(expectedMd5);
    }
    return true;
}

size_t Esp32OTA::write(const uint8_t *data, size_t len) {
    if (!data || len == 0) return 0;
    // Update.write takes a non-const uint8_t* in Arduino-ESP32 Core 3.x.
    // The data is read-only from our perspective; const_cast is safe here.
    return Update.write(const_cast<uint8_t *>(data), len);
}

bool Esp32OTA::end() {
    // true = set new partition as bootable. Caller is responsible for the
    // reboot via IZenoSystem::restart() (interface contract from Plan 04-01).
    return Update.end(true);
}

void Esp32OTA::abort() {
    Update.abort();
}

const char *Esp32OTA::errorString() {
    // Arduino-ESP32 Core 3.x: UpdateClass::errorString() returns
    // `const char *`. Existing ZenoPCBOTA.cpp:142 concatenates the result
    // with String, which is compatible with const char*.
    return Update.errorString();
}

bool Esp32OTA::canRollBack() {
    return Update.canRollBack();
}

bool Esp32OTA::rollBack() {
    return Update.rollBack();
}

}  // namespace ZenoPCB

#endif  // defined(ESP32)
