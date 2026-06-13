#include "Stm32System.h"

#if defined(STM32F1) || defined(STM32F4)

#include <stdio.h>
#include <string.h>

// D-25 Arduino-minimal abstraction: route time / logging / blocking-wait
// primitives through the compat layer instead of bare <Arduino.h>.
// arduino_compat.h pulls <Arduino.h> internally when ARDUINO is defined
// (i.e. STM32duino + PlatformIO default build), which transitively
// brings in CMSIS device headers, NVIC_SystemReset(), HAL_GetUIDw0(),
// and the STM32duino HAL wrappers used by IWatchdog.
//
// CubeIDE porters: see PORTING_TO_CUBEIDE.md — replace this single
// include + the IWatchdog one with native CMSIS / HAL includes.
#include "arduino_compat.h"
#include <IWatchdog.h>     // ArduinoCore-STM32 IWatchdog lib (CubeIDE: use HAL_IWDG_Refresh()).

namespace ZenoPCB {

void Stm32System::restart() {
    NVIC_SystemReset();
    // Defense for toolchains that drop [[noreturn]] on virtual methods.
    // NVIC_SystemReset() resets the chip within ~1 ms; this loop is
    // unreachable in practice but proves to the compiler that the
    // function body cannot fall through.
    for (;;) {
        // Spin until the chip resets.
    }
}

uint32_t Stm32System::getFreeHeap() {
    // PLACEHOLDER per 07-PATTERNS §"Stm32System" — newlib-nano on
    // STM32duino exposes `mallinfo().fordblks` for free-block tracking,
    // but the exact include path varies per ArduinoCore-STM32 version.
    // Plan 07-09 hardware UAT validates and replaces with the real
    // mallinfo call once F4 + F1 board availability is confirmed.
    // Returning 0 keeps the IZenoSystem contract intact (uint32_t free
    // heap; consumer-side DiagnosticsCollector ignores 0 as "unknown").
    return 0;
}

uint32_t Stm32System::getMaxAllocHeap() {
    // PLACEHOLDER — same mallinfo route as getFreeHeap(). Plan 07-09 UAT.
    return 0;
}

uint32_t Stm32System::getTotalHeap() {
    // Per-family static const — STM32 SRAM is statically partitioned at
    // link time (07-RESEARCH §F1 + F4 spike).
#if defined(STM32F4)
    return 196608;   // 192 KB SRAM on Nucleo-F429ZI / F407VG.
#elif defined(STM32F1)
    return 20480;    // 20 KB SRAM on Blue Pill F103C8.
#endif
}

size_t Stm32System::getUniqueId(char *out, size_t outSize) {
    if (!out || outSize < 9) return 0;

    // STM32 has a 96-bit unique ID exposed via HAL_GetUIDw0/1/2 (CMSIS
    // device header). The 8-hex-char output width matches Esp32 +
    // Esp8266 surfaces so existing WiFiProvisioning + DeviceCredentials
    // call sites read an identical ID format across all four ports.
    // snprintf only (CLAUDE.md memory rule — no sprintf).
    uint32_t w0 = HAL_GetUIDw0();
    int written = snprintf(out, outSize, "%08lX", (unsigned long)w0);
    if (written < 0) {
        out[0] = '\0';
        return 0;
    }
    if ((size_t)written >= outSize) {
        return outSize - 1;
    }
    return (size_t)written;
}

uint32_t Stm32System::uptimeMs() {
    // D-25: route through arduino_compat::now_ms() so a CubeIDE port can
    // swap to HAL_GetTick() via the user-provided arduino_compat.c stub.
    // On the STM32duino + PlatformIO default build this resolves inline
    // to the standard Arduino time primitive (which itself wraps
    // HAL_GetTick()) — same 32-bit ms counter that wraps every ~49.7
    // days, identical semantics to ESP32 / ESP8266.
    return stm32::compat::now_ms();
}

void Stm32System::feedWatchdog() {
    // ArduinoCore-STM32 bundles IWatchdog. The IWatchdog instance must
    // be IWatchdog.begin(timeout_us) by the application before reload()
    // takes effect. If the user hasn't called IWatchdog.begin() in
    // setup(), reload() is a silent no-op — acceptable behaviour per
    // 07-PATTERNS §"Stm32System" line 781.
    IWatchdog.reload();
}

}  // namespace ZenoPCB

#endif  // defined(STM32F1) || defined(STM32F4)
