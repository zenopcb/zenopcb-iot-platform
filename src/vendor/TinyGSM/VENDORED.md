# vshymanskyy/TinyGSM — VENDORED

**Upstream:** https://github.com/vshymanskyy/TinyGSM
**Version:** ~0.12.0 (pre-existing vendored copy)
**License:** **LGPL-3.0** (per upstream — see https://github.com/vshymanskyy/TinyGSM/blob/master/LICENSE)
**Author:** Volodymyr Shymanskyy
**Purpose:** Lean modem driver supporting many cellular / WiFi modem chipsets via AT-commands. Used by `lib/ZenoPCB/src/network/Zeno4GProvider.h` (under `-DZENOPCB_ENABLE_CELLULAR -DTINY_GSM_MODEM_SIM7600` etc.).

## License compliance

TinyGSM is licensed under **LGPL-3.0**, NOT MIT. Per LGPL-3.0:

- Modifications to TinyGSM sources must be documented.
- The class identity `TinyGsm` / `TinyGsmClient` is preserved — **NOT renamed** in Plan 06-2.5c (the MIT-licensed siblings `Preferences`, `StreamDebugger`, `PubSubClient` were renamed; TinyGSM was deliberately excluded from rename to honor LGPL-3.0 attribution + license-identity requirements).
- Users linking to ZenoPCB IoT Library inherit LGPL-3.0 obligations for the TinyGSM subtree when they enable `-DZENOPCB_ENABLE_CELLULAR`. The vendored snapshot keeps the upstream code identifiable for the LGPL "corresponding source" disclosure.

## Modifications from upstream

- **2026-06-02 (Plan 06-2.5c):** `TinyGsmClientXBee.h` — single comment block at line ~162 expanded to note the in-project rename of the MIT-licensed sibling `PubSubClient` → `ZenoPubSubClient`. This is a documentation-only change inside an upstream-style comment. **No TinyGSM source code, type, function, macro, or behavior was changed.** The TinyGSM logic is byte-identical to upstream modulo this 3-line comment expansion. The modification is minimal and falls within LGPL-3.0's allowed source modification with documentation requirement (this VENDORED.md file).
- **Other modifications:** none beyond the pre-Plan-06-2.5c upstream snapshot.

## Why no class rename

Unlike the MIT-licensed vendored siblings (`Preferences`, `StreamDebugger`, `PubSubClient`), TinyGSM is **NOT renamed** because:

1. LGPL-3.0 strongly couples class/library identity to license — renaming `TinyGsm` to `ZenoTinyGsm` would obscure the LGPL attribution chain.
2. User direction 2026-06-02 explicitly excluded TinyGSM from the rename pass.
3. There is no naming-collision risk: `TinyGsm` is the canonical type from the upstream library, and users who use this library always go through TinyGSM directly (no second source).

## Usage

Only `lib/ZenoPCB/src/network/Zeno4GProvider.h` references this, and only under `-DZENOPCB_ENABLE_CELLULAR`. Build env: `esp32_4g`, `esp32_multi`, `esp32_4g_1s`, `esp32_multi_1s`, `zf01_4g`, `zf01_multi`.

## File inventory

See `lib/ZenoPCB/src/vendor/TinyGSM/` — 40+ files (driver per modem chipset family). All files preserved verbatim from upstream modulo the single-comment modification noted above.
