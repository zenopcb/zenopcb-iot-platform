# bblanchon/ArduinoJson — VENDORED + RENAMED

**Upstream:** https://github.com/bblanchon/ArduinoJson
**Version:** 7.4.3 (vendored 2026-06-02 from PlatformIO Registry; upstream `.pio/libdeps/esp32_multi/ArduinoJson` snapshot — actual release pulled by `^7.4.2` constraint was 7.4.3)
**License:** MIT (see `LICENSE.md` — verbatim copy of upstream `LICENSE.txt`)
**Author:** Benoit Blanchon (https://blog.benoitblanchon.fr)

## Why vendored

Per user direction 2026-06-02 (Phase 6, ZenoPCB IoT Library):

- ZenoPCB Arduino library must be self-contained (no `lib_deps` external pulls during Arduino IDE / PlatformIO compile).
- **Avoid conflict** if a user has another version of `ArduinoJson` already installed in their Arduino IDE or PIO setup. Renaming the root namespace `ArduinoJson` → `ZenoJson` ensures coexistence — both copies can be `#include`d without `JsonDocument` ambiguity errors or surprising silent header-guard suppression.

## Modifications from upstream

- **2026-06-02 (Plan 06-2.5b):** Renamed root namespace `ArduinoJson` → `ZenoJson` via `sed` across all `.hpp`/`.h` files. Specifically rewritten:
  - `namespace ArduinoJson { ... }` (inside `Namespace.hpp` macro bodies) → `namespace ZenoJson { ... }`
  - `using namespace ArduinoJson` (root `ArduinoJson.h` line 11 plus 6 occurrences inside `MsgPackSerializer.hpp` + `PrettyJsonSerializer.hpp`) → `using namespace ZenoJson`
  - `ArduinoJson::` qualified references (across 9 files: `compatibility.hpp`, `MsgPackSerializer.hpp`, `PrettyJsonSerializer.hpp`, `JsonInteger.hpp`, `serialize.hpp`, `measure.hpp`, `pgmspace.hpp`, `VariantRefBase.hpp`, `VariantCompare.hpp`) → `ZenoJson::`
  - `ARDUINOJSON_NAMESPACE` deprecated macro target token: `... ArduinoJson` → `... ZenoJson` (the macro itself remains; only its expansion target points at the new namespace name)
- **Preserved unchanged:**
  - `inline namespace ARDUINOJSON_VERSION_NAMESPACE` (ArduinoJson's own ABI versioning machinery — internal)
  - `ARDUINOJSON_*` preprocessor macros (config knobs)
  - Class names (`JsonDocument`, `JsonObject`, `JsonArray`, `JsonVariant`, `JsonString`, etc.) — only the wrapping namespace differs
  - File names — every header keeps its original name (`ArduinoJson.h`, `ArduinoJson.hpp`, plus the `ArduinoJson/` subdirectory)
  - `#include "ArduinoJson/..."` paths — directory name preserved (filesystem path, not namespace token)
  - Original MIT copyright/license header on every source file
  - Comments and diagnostic strings (e.g. `#error ArduinoJson requires a C++ compiler ...`, `ARDUINOJSON_NAMESPACE is deprecated, use ArduinoJson instead`) — user-facing strings that mention the original library name
- **NOT modified** (CMake build files dropped from snapshot — not needed by Arduino / PIO):
  - `CMakeLists.txt` from upstream `src/` (not copied)

Future modifications must be documented here with date + rationale.

## Usage in ZenoPCB

- **Library internal code** (`lib/ZenoPCB/src/**/*.{h,cpp}`): include via relative path
  ```cpp
  #include "../vendor/ArduinoJson/ArduinoJson.h"  // ArduinoJson API from vendored copy (namespace ZenoJson; see vendor/ArduinoJson/LICENSE.md)
  ```
  Because the vendored root `ArduinoJson.h` performs a top-level `using namespace ZenoJson;`, unqualified `JsonDocument` / `JsonObject` / `JsonArray` / `JsonVariant` / `JsonString` / `serializeJson` / `deserializeJson` continue to resolve as they did before the rename — no per-call-site requalification needed.
- **User-facing alias header:** `lib/ZenoPCB/src/core/ZenoJson.h` provides `namespace ZenoPCB::Json` with type aliases (`using Document = ::ZenoJson::JsonDocument;`) for future Phase 1 plan 01-08 wrapper API surface. Library internals do not depend on this alias; it exists as a forward-compatible carrying point.

## File inventory

| Component | Path | Count |
|-----------|------|-------|
| Entry-point headers | `ArduinoJson.h`, `ArduinoJson.hpp` | 2 |
| Internal headers | `ArduinoJson/**/*.hpp` (Array, Collection, Deserialization, Document, Json, Memory, Misc, MsgPack, Numbers, Object, Polyfills, Serialization, Strings, Variant subdirs + top-level `Configuration.hpp`, `Namespace.hpp`, `version.hpp`, `compatibility.hpp`) | 138 |
| License | `LICENSE.md` (verbatim from upstream `LICENSE.txt`) | 1 |
| Provenance | `VENDORED.md` (this file) | 1 |
| **Total** | | **142** |
