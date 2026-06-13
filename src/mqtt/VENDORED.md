# knolleary/PubSubClient — VENDORED (lives in `lib/ZenoPCB/src/mqtt/`)

**Upstream:** https://github.com/knolleary/pubsubclient
**Version:** ~v2.8 (Knolleary fork, vendored pre-Phase-6)
**License:** MIT (per upstream `LICENSE.txt`)
**Author:** Nicholas O'Leary
**Purpose:** MQTT v3.1.1 TCP client. Used by `lib/ZenoPCB/src/ZenoPCB.cpp`, `lib/ZenoPCB/src/mqtt/MQTTClient.{h,cpp}`, `lib/ZenoPCB/src/mqtt/ZenoPCBMQTT.cpp`.

## Why vendored (in `mqtt/`, not `vendor/`)

PubSubClient was vendored well before the dedicated `lib/ZenoPCB/src/vendor/` directory pattern was established. It currently lives alongside the project's own MQTT wrappers (`MQTTClient`, `ZenoPCBMQTT`) in `lib/ZenoPCB/src/mqtt/`. The file location is preserved for git-history continuity; classification as "vendored" is documented here.

## Modifications from upstream

- **Pre-Plan-06-2.5c:** Various small mods to the original knolleary fork (buffer-size tuning, ESP32/ESP8266 callback signature handling, etc.) — see `git log lib/ZenoPCB/src/mqtt/PubSubClient.{h,cpp}` for the historical record.
- **2026-06-02 (Plan 06-2.5c):**
  - SPDX MIT header prepended to `PubSubClient.h` + `PubSubClient.cpp`.
  - Class `PubSubClient` → `ZenoPubSubClient` (incl. all 16 ctor signatures + dtor + 50+ qualified method definitions).
  - File names (`PubSubClient.h/.cpp`) + header guard (`#ifndef PubSubClient_h`) preserved to minimize git-history churn.
- **Brand consistency:** rename claims this fork as a ZenoPCB internal component and avoids collision with any user-installed PubSubClient library (a common Arduino dependency).

## Usage

After Plan 06-2.5c the class is `ZenoPubSubClient`. Callers (all updated in Plan 06-2.5c):

- `lib/ZenoPCB/src/mqtt/MQTTClient.{h,cpp}` — primary wrapper, holds a `ZenoPubSubClient _mqttClient` member.
- `lib/ZenoPCB/src/mqtt/ZenoPCBMQTT.cpp` — comment refs only.
- `lib/ZenoPCB/src/ZenoPCB.cpp` — two ephemeral `ZenoPubSubClient` instances for provisioning MQTT-test + claim flows (lines ~1741, ~1788).
- `lib/ZenoPCB/src/vendor/TinyGSM/TinyGsmClientXBee.h` — single comment block (line ~162) updated to mention the rename per LGPL-3.0 modification-documentation requirement (see `lib/ZenoPCB/src/vendor/TinyGSM/VENDORED.md`).

## File inventory

| File | Role |
|------|------|
| `PubSubClient.h` | Public `ZenoPubSubClient` class declaration + MQTT protocol constants |
| `PubSubClient.cpp` | Method implementations |
| `VENDORED.md` | This file — provenance + license + modification log |
