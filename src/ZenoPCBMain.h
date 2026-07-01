/**
 * @file ZenoPCBMain.h
 * @brief Main include file for ZenoPCB IoT Library (v0.4.0+)
 *
 * Include this file to use the ZenoPCB IoT Library:
 *
 *     #include <ZenoPCBMain.h>
 *
 * Quick start:
 *
 *     CLOUD_TO_DEVICE(Z0) {
 *         digitalWrite(LED, param.toBool() ? HIGH : LOW);
 *         DEVICE_TO_CLOUD(Z0, param.toBool());   // echo state back
 *     }
 *
 *     ZENO_EVERY(5000) {
 *         DEVICE_TO_CLOUD(Z1, analogRead(A0) * 0.1f);   // temperature every 5s
 *     }
 *
 *     void setup() {
 *         Serial.begin(115200);
 *         zeno.wifi(SSID, PASS).device(ID, TOKEN).begin();
 *     }
 *
 *     void loop() { zeno.loop(); }
 */

#ifndef ZENOPCB_MAIN_H
#define ZENOPCB_MAIN_H

#include "ZenoPCB.h"
#include "core/DeviceCredentials.h"
#include "core/ZenoTimer.h"

// ============================================================================
// DEVICE_TO_CLOUD(Zx, value)
//
// Send a value UP to the cloud. Stores the value in the Z key buffer and
// marks it dirty; the next publish cycle picks it up.
//
// Usage anywhere (inside CLOUD_TO_DEVICE body, ZENO_EVERY body, setup(), etc.):
//
//     DEVICE_TO_CLOUD(Z0, readTemperature());
//     DEVICE_TO_CLOUD(Z1, true);
//     DEVICE_TO_CLOUD(Z2, "running");
// ============================================================================
#define DEVICE_TO_CLOUD(key, value) zeno.set(ZenoPCB::ZKey::key, value)

// ============================================================================
// CLOUD_TO_DEVICE(Zx) { ... }
//
// Receive a value DOWN from the cloud. The block runs whenever the cloud
// publishes a new value for Zx on the /control topic. Self-registers at
// static-init time — NO need to call .onZKeyChange() in setup().
//
// Inside the block you have:
//     param        — ZValue (use param.toFloat(), .toInt(), .toString(), .toBool())
//     param.type   — ZValueType (INT, FLOAT, STRING, BOOL, NONE)
//     _zkey        — the ZKey enum value (== ZKey::Zx)
//
// Usage at FILE SCOPE (outside any function):
//
//     CLOUD_TO_DEVICE(Z0) {
//         digitalWrite(RELAY, param.toBool() ? HIGH : LOW);
//     }
//
// IMPORTANT: must be at file scope. Inside a function the compiler will
// reject the anonymous namespace expansion (intentional safety guard).
// ============================================================================
#define CLOUD_TO_DEVICE(key)                                                                          \
    static void _ctd_##key(ZenoPCB::ZKey, const ZenoPCB::ZValue &);                                   \
    namespace                                                                                         \
    {                                                                                                 \
        struct _CtdReg_##key                                                                          \
        {                                                                                             \
            _CtdReg_##key() { ZenoPCB::ZKeyHandlerRegistrar reg(ZenoPCB::ZKey::key, _ctd_##key); (void)reg; } \
        };                                                                                            \
        static _CtdReg_##key _ctd_reg_##key;                                                          \
    }                                                                                                 \
    static void _ctd_##key(ZenoPCB::ZKey _zkey, const ZenoPCB::ZValue &param)

// ============================================================================
// ZENO_EVERY(intervalMs) { ... }
//
// Run + publish the block every `intervalMs` milliseconds. Self-registers at
// static-init time; place at FILE SCOPE only.
//
//     ZENO_EVERY(5000) { DEVICE_TO_CLOUD(Z0, analogRead(A0)); }
//     ZENO_EVERY(1000) { DEVICE_TO_CLOUD(Z1, digitalRead(2));  }
//
// Minimum interval: 1000ms.
// GET_ALL: cloud requests fire every registered block immediately.
// ============================================================================

#define _ZENO_PASTE_INNER(a, b) a##b
#define _ZENO_PASTE(a, b) _ZENO_PASTE_INNER(a, b)

#define ZENO_EVERY(intervalMs)                                                                                                  \
    static void _ZENO_PASTE(_zeno_every_, __LINE__)();                                                                          \
    namespace                                                                                                                   \
    {                                                                                                                           \
        struct _ZENO_PASTE(_ZenoEveryReg_, __LINE__)                                                                            \
        {                                                                                                                       \
            _ZENO_PASTE(_ZenoEveryReg_, __LINE__)() { ZenoPCB::ZenoTimer::queuePending((intervalMs), _ZENO_PASTE(_zeno_every_, __LINE__)); } \
        };                                                                                                                      \
        static _ZENO_PASTE(_ZenoEveryReg_, __LINE__) _ZENO_PASTE(_zeno_every_reg_, __LINE__);                                   \
    }                                                                                                                           \
    static void _ZENO_PASTE(_zeno_every_, __LINE__)()

// ============================================================================
// Backward-compat aliases (v0.3.x → v0.4.0 migration helper)
//
// ZENO_WRITE is a safe alias — same expansion, just under a friendlier name.
// ZENO_READ is NOT aliased: redirecting it to CLOUD_TO_DEVICE would cause
// double-registration when old code also calls .onZKeyChange(). User upgrade
// path: replace `ZENO_READ(Zx)` with `CLOUD_TO_DEVICE(Zx)` and drop the
// manual .onZKeyChange() call.
//
// ZENO_READ_ALL is REMOVED in v0.4.0 — replace with one or more ZENO_EVERY
// blocks. See doc/RELEASE.md for migration examples.
// ============================================================================
#define ZENO_WRITE(key, value) DEVICE_TO_CLOUD(key, value)

#endif // ZENOPCB_MAIN_H
