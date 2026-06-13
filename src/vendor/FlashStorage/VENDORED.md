# khoih-prog/FlashStorage_STM32 — VENDORED + RENAMED

**Upstream:** https://github.com/khoih-prog/FlashStorage_STM32
**Version:** 1.2.0 (git tag `v1.2.0`, upstream commit `017141fbae13b9b08865511800566ad6ef61dceb`)
**Vendored:** 2026-06-03 (Plan 07-03, Phase 7)
**License:** MIT (see `LICENSE`)
**Author:** Khoi Hoang <khoih58@gmail.com> (https://github.com/khoih-prog)
**Note:** Upstream archived 2023-02 but binary-stable. RESEARCH §Package Legitimacy Audit row 274 confirms stable + no critical bugs in 4 years. Vendoring snapshots the v1.2.0 release; the upstream archive state is acceptable because the STM32duino HAL surface (`HAL_FLASH_*`, `eeprom_*`) the library wraps is itself stable since STM32 Core 2.0.

## Why vendored

- ZenoPCB Arduino library must be self-contained (no `lib_deps` external pulls for MIT libs).
- Avoid conflict if user has `FlashStorage_STM32` installed globally.
- Per Phase 6 Plan 06-2.5c policy: vendor + rename only MIT libraries; LGPL/GPL stay external `lib_deps`.

## Modifications from upstream

- **2026-06-03 (vendor time):** SPDX MIT header (`// SPDX-License-Identifier: MIT`) prepended as the first line of every `.h`, `.hpp`, `.cpp` file for license-scan compliance. Existing upstream author / copyright blocks preserved verbatim below the SPDX header.
- **2026-06-03 (Plan 07-03 — class rename):** Renamed upstream `class EEPROMClass` → `class ZenoFlashStorage` in `FlashStorage_STM32.hpp` for brand consistency and to avoid colliding with the Arduino `EEPROM` symbol the STM32duino Core also exposes. Public API surface is identical (`read`, `write`, `update`, `get<T>`, `put<T>`, `commit`, `isValid`, `length`, `setCommitASAP`, `getCommitASAP`). File names KEPT (`FlashStorage_STM32.h/.hpp/.cpp`) to minimize git history churn — Phase 6 Plan 06-2.5c precedent.
- **2026-06-03 (Plan 07-03 — instance rename):** Renamed upstream `static EEPROMClass EEPROM` → `static ZenoFlashStorage ZenoEEPROM` to avoid shadowing the STM32duino Core's `EEPROM` global. Consumer in Plan 07-04 (`Stm32NVS`) will reference `ZenoEEPROM` directly, NOT the Arduino `EEPROM` symbol.
- **2026-06-03 (deviation from PLAN.md assumption — Rule 1):** PLAN.md `<action>` STEP 4 assumed upstream class was `class FlashStorageClass`. The actual upstream class name (verified at commit 017141f) is `class EEPROMClass`. Rename target adjusted from `FlashStorageClass → ZenoFlashStorage` to `EEPROMClass → ZenoFlashStorage`. PLAN.md `<behavior>` straggler check (`grep -c "FlashStorageClass" ... = 0`) still passes trivially because `FlashStorageClass` never existed upstream. Equivalent straggler check `grep -c "EEPROMClass" lib/ZenoPCB/src/vendor/FlashStorage/*.{h,hpp,cpp} = 0` is the meaningful verification.
- **2026-06-03 (deviation from PLAN.md assumption — Rule 1):** PLAN.md `<files_modified>` listed `.h + .cpp`. Upstream is header-only (`.h + .hpp + utility/stm32_eeprom.hpp + utility/stm32_eeprom_Impl.h`). Added all four upstream headers + an intentionally-empty `FlashStorage_STM32.cpp` shim to satisfy ZenoPCB CLAUDE.md "every class has paired `.h + .cpp`" convention + PLAN.md `<automated>` gate (`test -f FlashStorage_STM32.cpp`). The `.cpp` does NOT include `FlashStorage_STM32.h` — that would cause multiple-definitions linker errors because the upstream `.h` pulls in `utility/stm32_eeprom_Impl.h` which defines `eeprom_*` functions and the static `eeprom_buffer` array. Plan 07-04's `Stm32NVS.cpp` is the single TU that performs `#include "FlashStorage_STM32.h"`, mirroring the upstream single-TU pattern documented in every upstream file header.
- **2026-06-03 (utility headers unchanged):** `utility/stm32_eeprom.hpp` and `utility/stm32_eeprom_Impl.h` copied verbatim from upstream `src/utility/`. Body unmodified — only the SPDX MIT header line + 5-line ZenoPCB attribution block prepended above the existing STMicroelectronics BSD-3-Clause notice (preserved inline). The STMicro notice is upstream-provided context for the emulated-EEPROM driver code; ZenoPCB does NOT claim authorship.

Future modifications must be documented here with date + rationale.

## Usage

Only `lib/ZenoPCB/src/hal/stm32/Stm32NVS.{h,cpp}` (Plan 07-04) references this. The include path is relative:

```cpp
#include "../../vendor/FlashStorage/FlashStorage_STM32.h"   // exactly ONCE per binary
// then use: ZenoFlashStorage _flash;
//           _flash.put(addr, value);
//           _flash.commit();
```

Plan 07-04 Stm32NVS.{h,cpp} TU is wrapped in `#if defined(STM32F1) || defined(STM32F4)` so ESP32 / ESP8266 / UNO R4 builds never reach this vendor directory regardless. Defense-in-depth: `FlashStorage_STM32.h` retains upstream's `#error This code is intended to run on STM32F/L/H/G/WB/MP1 platform!` guard verbatim.

## File inventory

| File | Role |
|------|------|
| `FlashStorage_STM32.h`   | Single-TU entry point (pulls in `.hpp` + utility impl). Include EXACTLY ONCE per binary. |
| `FlashStorage_STM32.hpp` | `class ZenoFlashStorage` definition + `static ZenoFlashStorage ZenoEEPROM` instance. Multi-include safe. |
| `FlashStorage_STM32.cpp` | Intentionally empty shim — satisfies ZenoPCB `.h + .cpp` pairing convention. See header comment. |
| `utility/stm32_eeprom.hpp`     | STM32 emulated-EEPROM C-API declarations (verbatim from STMicro). |
| `utility/stm32_eeprom_Impl.h`  | STM32 emulated-EEPROM C-API implementations (verbatim from STMicro; `HAL_FLASH_*` + `eeprom_buffer`). |
| `LICENSE`               | MIT license text (verbatim from upstream commit 017141f). |

## Supported MCU families

Per upstream `#error` guard in `FlashStorage_STM32.h`: STM32F0, STM32F1, STM32F2, STM32F3, STM32F4, STM32F7, STM32L0, STM32L1, STM32L4, STM32H7, STM32G0, STM32G4, STM32WB, STM32MP1, STM32L5.

ZenoPCB Phase 7 production targets: **STM32F1** (BluePill) and **STM32F4** (BlackPill) per CONTEXT D-22. Other families are upstream-supported but untested by ZenoPCB.

## License compliance

`LICENSE` is the verbatim MIT text from upstream commit `017141f`. ZenoPCB modifications listed above are MIT-compatible (the MIT license explicitly permits modification + redistribution under the same terms). The combined work continues to ship under MIT. SPDX-License-Identifier headers added to every `.h/.hpp/.cpp` for automated license scanning (REUSE / SPDX tooling).

Plan 07-07 (GOV-04 / THIRD_PARTY_LICENSES.md) will reference this file as the source of truth for the `khoih-prog/FlashStorage_STM32 — MIT` row.
