# Porting the ZenoPCB STM32 HAL into STM32CubeIDE / HAL / LL Projects

**Status:** Downstream user recipe (D-25 NEW user direction 2026-06-03 #3).
**Scope:** Phase 7 STILL builds via STM32duino + PlatformIO per D-01 — this
document describes how a downstream user can lift `lib/ZenoPCB/src/hal/stm32/`
into a raw STM32CubeIDE project (without ArduinoCore-STM32) by providing a
single `arduino_compat.c` stub file.

The CubeIDE port is **NOT** a Phase 7 build target. The
STM32duino + PlatformIO build is the primary deliverable; this recipe simply
keeps the future port cost low.

---

## Why this recipe exists

The Arduino-minimal abstraction layer (`arduino_compat.h`) was added in
Plan 07-04 Task 3 (D-25) so the only Arduino-specific symbols used by the
STM32 HAL `*.cpp` bodies are `millis()`, `Serial.println()`, and `delay()`.
All three are routed through the `ZenoPCB::stm32::compat::` namespace, with
two compile-time backends:

| `ARDUINO` defined                   | `ARDUINO` undefined (CubeIDE) |
| ----------------------------------- | ----------------------------- |
| `compat::now_ms()` → `::millis()`   | `compat::now_ms()` → `arduino_compat_millis()` |
| `compat::log(s)` → `Serial.println` | `compat::log(s)` → `arduino_compat_log(s)`     |
| `compat::delay_ms(n)` → `::delay(n)`| `compat::delay_ms(n)` → `arduino_compat_delay_ms(n)` |

The three `arduino_compat_*` functions are declared `extern "C"` and have
no default implementation — the CubeIDE user provides a single `.c` file
implementing all three, then the linker resolves the symbols.

Beyond `arduino_compat.h`, the STM32 HAL still depends on:

- `<IWatchdog.h>` from ArduinoCore-STM32 (replace with native
  `HAL_IWDG_Refresh()` in CubeIDE).
- The CMSIS device header (`NVIC_SystemReset()`, `HAL_GetUIDw0()`) — these
  are provided by every STM32 CubeIDE project natively, no port work needed.
- The vendored `ZenoFlashStorage` (Plan 07-03) — its
  `eeprom_buffered_*` primitives wrap the STM32 HAL FLASH API and work
  inside CubeIDE without modification because the upstream library
  was designed for non-Arduino STM32 targets too. No port work needed.
- The platform-neutral `TimeManager` (`lib/ZenoPCB/src/core/`) — uses only
  POSIX `<time.h>` + `configTime()`. The `configTime` symbol is provided by
  newlib + lwIP SNTP; if you use the STM32CubeMX lwIP middleware, no port
  work needed.

---

## Recipe (one-time setup)

### Step 1 — Copy the library tree into your CubeIDE project

Suggested layout (mirrors how STM32CubeIDE organizes middleware):

```
YourProject/
  Middlewares/
    Third_Party/
      ZenoPCB/
        src/
          hal/
            IZenoHal.h
            IZenoStorage.h
            IZenoNVS.h
            IZenoOTA.h
            IZenoTime.h
            IZenoSystem.h
            stm32/                 # this directory — all 12 files + arduino_compat.h
          core/                    # TimeManager, ZenoPCBDebug, ZenoPCBCloud
          vendor/
            FlashStorage/          # Plan 07-03 vendored MIT
            ArduinoJson/           # Plan 06-2.5b vendored MIT (ZenoJson)
            Preferences/           # Plan 06-2.5a/c — NOT NEEDED on STM32 (kept for completeness; ESP8266-only header guards)
          mqtt/                    # PubSubClient + ZenoPCBMQTT
        Stub/
          arduino_compat.c         # NEW — you author this (template below)
```

Add the following to your CubeIDE project's C/C++ include paths
(*Project → Properties → C/C++ General → Paths and Symbols*):

```
Middlewares/Third_Party/ZenoPCB/src
Middlewares/Third_Party/ZenoPCB/src/hal
Middlewares/Third_Party/ZenoPCB/src/hal/stm32
Middlewares/Third_Party/ZenoPCB/src/vendor
Middlewares/Third_Party/ZenoPCB/src/vendor/FlashStorage
```

### Step 2 — Set compile flags

Add the following to *Project → Properties → C/C++ Build → Settings →*
*MCU GCC Compiler / MCU G++ Compiler → Preprocessor*:

```
-DSTM32F1        (or -DSTM32F4 depending on your target)
-DZENOPCB_BROKER_HOST="\"your.mqtt.broker.example.com\""   # D-26 broker integration (Plan 07-06)
-DZENOPCB_VERSION="\"1.0.0-cubeide\""
```

Do **NOT** define `-DARDUINO`. The compat layer relies on its absence to
route through the user stub.

### Step 3 — Author `arduino_compat.c`

Place this stub at `Middlewares/Third_Party/ZenoPCB/Stub/arduino_compat.c`:

```c
// arduino_compat.c — CubeIDE port stub for the ZenoPCB STM32 HAL (D-25).
// See PORTING_TO_CUBEIDE.md.

#include <stdint.h>
#include <stdio.h>

#include "stm32f4xx_hal.h"   // or stm32f1xx_hal.h for STM32F1 targets

// ---- now_ms ----------------------------------------------------------------

uint32_t arduino_compat_millis(void)
{
    // HAL_GetTick() is incremented every 1 ms by SysTick_Handler — same
    // semantics as Arduino's millis(). If your project uses FreeRTOS,
    // you may prefer xTaskGetTickCount() / portTICK_PERIOD_MS instead.
    return HAL_GetTick();
}

// ---- log -------------------------------------------------------------------

void arduino_compat_log(const char *msg)
{
    // Route to your debug transport. Three common options:
    //   1. UART:        HAL_UART_Transmit(&huart2, (uint8_t*)msg, strlen(msg), HAL_MAX_DELAY);
    //   2. SWO / ITM:   ITM_SendChar() loop
    //   3. printf:      requires retargeting _write() to your transport
    // The example below assumes you have retargeted printf to UART.
    printf("%s\r\n", msg);
}

// ---- delay_ms --------------------------------------------------------------

void arduino_compat_delay_ms(uint32_t ms)
{
    HAL_Delay(ms);
}
```

### Step 4 — Replace `<IWatchdog.h>` references

The `Stm32System.cpp` file `#include <IWatchdog.h>` and calls
`IWatchdog.reload()`. In CubeIDE, replace these with:

```c
#include "stm32f4xx_hal_iwdg.h"          // or stm32f1xx_hal_iwdg.h

extern IWDG_HandleTypeDef hiwdg;          // provided by main.c, CubeMX-generated

// ... and in feedWatchdog():
HAL_IWDG_Refresh(&hiwdg);
```

You can do this with a `#if !defined(ARDUINO)` guard around the include in
`Stm32System.cpp` if you want the file to remain dual-build-compatible
without forking.

### Step 5 — Initialise the HAL in `main.c`

After your CubeMX-generated `MX_GPIO_Init()`, `MX_USART2_UART_Init()`,
`MX_IWDG_Init()`, `MX_LWIP_Init()`, etc., add:

```c
extern "C" {
#include "Middlewares/Third_Party/ZenoPCB/src/hal/stm32/Stm32Hal.h"
}

// In main() after MX_*_Init() calls:
ZenoPCB::IZenoHal& hal = ZenoPCB::getStm32Hal();
hal.nvs().begin("zeno_creds");
// ... wire up the Zeno facade per your application.
```

(Adjust the namespace usage if your `main.c` is plain C — wrap the calls
in a small `.cpp` shim.)

---

## Build verification

After the steps above, a clean CubeIDE rebuild should:

- Compile every `Middlewares/Third_Party/ZenoPCB/src/hal/stm32/*.cpp` file.
- Link without unresolved-symbol errors for `arduino_compat_millis` /
  `_log` / `_delay_ms` (your stub).
- Produce a firmware image that calls `HAL_GetTick()` / `printf()` /
  `HAL_Delay()` instead of Arduino primitives.

---

## What is **not** covered by this recipe

- **NTP via lwIP SNTP**: requires CubeMX → LwIP middleware enabled with
  SNTP option. ZenoPCB's `TimeManager::syncNTP` calls the standard
  `configTime()` from `<time.h>`; lwIP provides the symbol.
- **MQTT**: ZenoPCB ships the vendored `PubSubClient` which expects an
  Arduino `Client*` interface. Provide an `Arduino::WiFiClient`-style
  adaptor over your `mbedTLS` / `socket` API, or vendor a CubeIDE-native
  MQTT client (e.g., paho-mqtt-embedded-c).
- **OTA**: STM32 OTA requires a custom dual-bank bootloader — explicitly
  out of v1.0.0 scope per 07-CONTEXT D-12. `Stm32OTA` returns `Unavailable`
  in both build modes; the CubeIDE port inherits this behaviour unchanged.

For these surfaces, see the matching upstream STM32CubeMX middleware or a
RTOS-aware vendored client per the licenses-acceptable list in
07-RESEARCH §"Package Legitimacy Audit".

---

## Reference

- D-25 user direction (2026-06-03 #3): `lib/ZenoPCB/src/hal/stm32/arduino_compat.h`
- Plan 07-04 Task 3 specification: `.planning/phases/07-uno-r4-stm32-ports-capability-matrix/07-04-PLAN.md`
- Phase 7 architecture context: `.planning/phases/07-uno-r4-stm32-ports-capability-matrix/07-CONTEXT.md`
