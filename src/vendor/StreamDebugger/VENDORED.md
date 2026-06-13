# vshymanskyy/StreamDebugger — VENDORED

**Upstream:** https://github.com/vshymanskyy/StreamDebugger
**Version:** ~1.0.1 (vendored well before Phase 6; original snapshot kept verbatim until Plan 06-2.5c rename)
**License:** MIT (per upstream `StreamDebugger.h` file header — `@license This project is released under the MIT License (MIT)`, `@copyright Copyright (c) 2016 Volodymyr Shymanskyy`)
**Author:** Volodymyr Shymanskyy
**Purpose:** Single-header AT-command dialog tap that mirrors a `Stream` for both data + dump streams. Used by `lib/ZenoPCB/src/network/Zeno4GProvider.h` when `-DZENOPCB_DEBUG_4G=1` is set, to log every AT command exchange with the 4G modem to Serial.

## Why vendored

Vendored well before Phase 6 (pre-existing in the repo when Plan 06-2.5c started). Per user direction 2026-06-02 (Phase 6, ZenoPCB IoT Library):

- ZenoPCB Arduino library must be self-contained (no `lib_deps` external pulls during Arduino IDE compile).
- Avoid collision with any external StreamDebugger library a user may have installed globally.

## Note on `vendor/README.md` license row

The repo-level `lib/ZenoPCB/src/vendor/README.md` table lists StreamDebugger as `LGPL-3.0` — this is incorrect against the upstream file header which self-declares MIT (`@license This project is released under the MIT License (MIT)`). The upstream repo at https://github.com/vshymanskyy/StreamDebugger likewise carries a MIT LICENSE. The VENDORED.md you are reading + the file header inside `StreamDebugger.h` are the authoritative license declarations.

## Modifications from upstream

- **2026-06-02 (Plan 06-2.5c):** SPDX MIT header prepended; class `StreamDebugger` → `ZenoStreamDebugger` (incl. constructor signature). The upstream `@author`/`@license`/`@copyright`/`@date` Doxygen block preserved verbatim above the class. Header guard `StreamDebugger_h` kept (unique token).
- **Other modifications:** none beyond the rename above.

## Usage

Only `lib/ZenoPCB/src/network/Zeno4GProvider.h` references this, and only under `#if ZENOPCB_DEBUG_4G` (off by default). The class is now `ZenoStreamDebugger`.

## File inventory

| File | Role |
|------|------|
| `StreamDebugger.h` | Public `ZenoStreamDebugger` class (single-header) |
| `VENDORED.md` | This file — provenance + license + modification log |
