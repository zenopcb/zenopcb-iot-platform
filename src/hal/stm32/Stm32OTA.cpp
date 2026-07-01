#include "Stm32OTA.h"

#if defined(STM32F1xx) || defined(STM32F4xx)

#include "../../core/ZenoPCBDebug.h"

namespace ZenoPCB {

bool Stm32OTA::begin(size_t, const char *) {
    // CAP_OTA=0 STM32 OTA requires a custom bootloader (dual-bank flash
    // + IAP write + reset-to-bootloader handshake) which is explicitly
    // out of v1.0.0 scope per. Surfaces platform gap;
    // capability-gated callers (post- in) receive
    // ZenoCapability::Unavailable before reaching this stub.
    ZENO_LOG_CORE("[WARN] Stm32OTA: not available custom bootloader required");
    return false;
}

size_t Stm32OTA::write(const uint8_t *, size_t)   { return 0; }
bool   Stm32OTA::end()                            { return false; }
void   Stm32OTA::abort()                          {}
const char *Stm32OTA::errorString()               { return "OTA not supported on STM32 default build"; }
bool   Stm32OTA::canRollBack()                    { return false; }
bool   Stm32OTA::rollBack()                       { return false; }

}  // namespace ZenoPCB

#endif  // defined(STM32F1xx) || defined(STM32F4xx)
