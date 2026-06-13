// SPDX-License-Identifier: MIT
//
// khoih-prog/FlashStorage_STM32 — vendored from upstream v1.2.0 into ZenoPCB IoT Library.
// MIT — see ./LICENSE. Renamed `class EEPROMClass` → `class ZenoFlashStorage` per Plan 07-03.
//
// =============================================================================
// Vendor structure note (read before editing):
// =============================================================================
// Upstream khoih-prog/FlashStorage_STM32 is a HEADER-ONLY library. The class
// definition lives in `FlashStorage_STM32.hpp` and the implementation lives in
// `utility/stm32_eeprom_Impl.h`. The upstream `.h` is the single-TU entry point
// (`#include "FlashStorage_STM32.h"` — exactly once per binary, in the sketch's
// main TU) that pulls in both. Upstream documents this in every file header:
//
//   "The .hpp contains only definitions, and can be included as many times as
//    necessary, without `Multiple Definitions` Linker Error.
//    The .h contains implementations, and can be included only in main(), .ino
//    with setup() to avoid `Multiple Definitions` Linker Error."
//
// This .cpp file exists ONLY to satisfy ZenoPCB CLAUDE.md ("every class has
// paired .h + .cpp in the same directory") + Plan 07-03 `<automated>` gate
// (`test -f FlashStorage_STM32.cpp`). It is intentionally empty of executable
// content — including FlashStorage_STM32.h here would cause multiple-definitions
// linker errors (the symbols would land in both this TU and Stm32NVS.cpp).
//
// The actual STM32 binary that consumes ZenoFlashStorage is built in Plan 07-04
// via Stm32NVS.cpp which does `#include "../../vendor/FlashStorage/FlashStorage_STM32.h"`
// exactly once per binary, mirroring upstream's intended single-TU pattern.
// =============================================================================

// Intentionally empty translation unit — see header comment above.
