#include "UnoR4OTA.h"

#if defined(ARDUINO_UNOR4_WIFI)

#include "../../core/ZenoPCBDebug.h"

#ifdef ZENOPCB_ENABLE_UNOR4_OTA
// Opt-in dependencies (only pulled when the build flag is on so the
// disabled-default build does not pay the link / flash cost):
//   - WiFiS3 — WiFiClient for HTTP firmware download from the OTA server.
//   - Renesas RA4M1 FSP Flash API — symbols `R_FLASH_LP_Open`,
//     `R_FLASH_LP_Write`, `R_FLASH_LP_Close` exposed by ArduinoCore-renesas
//     via the umbrella `<Arduino.h>` include. Wave 1 spike (Plan 07-09
//     UAT) confirms exact header path and call sequence.
#include <WiFiS3.h>
#endif

namespace ZenoPCB {

#ifdef ZENOPCB_ENABLE_UNOR4_OTA

// ============================================================================
// ENABLED branch — custom WiFiClient + Renesas FSP Flash impl per D-16 RESCOPED
// ============================================================================
//
// Bodies are PLACEHOLDER for the Wave 1 compile-clean baseline. The 3-5 day
// implementation work tracked by D-16 RESCOPED + D-18 PENDING precedent
// fills these bodies with the live Renesas FSP `R_FLASH_LP_Open` /
// `R_FLASH_LP_Write` / `R_FLASH_LP_Close` sequence + WiFiClient HTTP
// download streaming + MD5 verification. Plan 07-09 UAT hardware validation
// is the release gate.
//
// Cached error string for `errorString()` — sized at < 64 B to honour the
// CLAUDE.md memory rule (stack arrays < 1 KB, no String concat in loops).
static const char *s_lastError = "";

bool UnoR4OTA::begin(size_t expectedSize, const char *expectedMd5) {
    // TODO (Plan 07-09 UAT spike):
    //   1. Open the RA4M1 OTA flash region via `R_FLASH_LP_Open()`.
    //   2. Validate `expectedSize` fits inside the OTA partition budget
    //      (RA4M1 has 256 KB Flash; partition layout TBD by spike).
    //   3. If `expectedMd5 != nullptr`, initialise a streaming MD5
    //      context for incremental verification during `write()`.
    //   4. Return true on successful open; cache error via s_lastError
    //      and return false otherwise.
    (void)expectedSize;
    (void)expectedMd5;
    s_lastError = "UnoR4OTA::begin PLACEHOLDER — Renesas FSP Flash spike pending";
    ZENO_LOG_CORE("[WARN] UnoR4OTA::begin: PLACEHOLDER body — Plan 07-09 UAT spike pending");
    return false;
}

size_t UnoR4OTA::write(const uint8_t *data, size_t len) {
    // TODO (Plan 07-09 UAT spike):
    //   1. Stream `len` bytes from `data` via `R_FLASH_LP_Write()`.
    //   2. Update the running MD5 context if MD5 verification is active.
    //   3. Return the number of bytes successfully written (caller
    //      compares to `len` to detect a short write).
    (void)data;
    (void)len;
    return 0;
}

bool UnoR4OTA::end() {
    // TODO (Plan 07-09 UAT spike):
    //   1. Finalize the Renesas FSP Flash write (commit any buffered
    //      sector via `R_FLASH_LP_Close()` after final `R_FLASH_LP_Write`).
    //   2. If MD5 verification active, compare computed MD5 to
    //      `expectedMd5` from begin(); on mismatch, cache the error
    //      and return false.
    //   3. Mark the new OTA slot as bootable (RA4M1 dual-bank Flash
    //      bank-swap is a separate FSP call — spike confirms semantics).
    return false;
}

void UnoR4OTA::abort() {
    // TODO (Plan 07-09 UAT spike):
    //   - `R_FLASH_LP_Close()` and reset region without committing.
    //   - Equivalent of Esp8266OTA's no-op semantics in early eboot
    //     contexts: any partial write is harmless until the new slot
    //     is marked bootable.
}

const char *UnoR4OTA::errorString() {
    // Pointer remains valid until the next OTA call (IZenoOTA contract).
    // Cached into the `s_lastError` static; PLACEHOLDER bodies set it
    // on each failure path.
    return s_lastError;
}

bool UnoR4OTA::canRollBack() {
    // RA4M1 has dual-bank Flash, which in principle supports rollback by
    // swapping the active bank pointer. Plan 07-09 UAT spike confirms the
    // exact Renesas FSP API for bank-swap + rollback semantics; until
    // then we report false (capability-honest).
    return false;
}

bool UnoR4OTA::rollBack() {
    // TODO (Plan 07-09 UAT spike): bank-swap to the previous OTA slot.
    return false;
}

#else  // !ZENOPCB_ENABLE_UNOR4_OTA

// ============================================================================
// DISABLED-DEFAULT branch — opt-in gate not set; all methods log + fail
// ============================================================================
//
// `begin()` logs a single warn-line surfacing the platform gap so consumers
// have a discoverable hint pointing at the build flag. The remaining
// methods return their interface failure values silently (no log spam if a
// caller polls write() in a loop).

bool UnoR4OTA::begin(size_t, const char *) {
    ZENO_LOG_CORE("[WARN] UnoR4OTA: not enabled — build with -DZENOPCB_ENABLE_UNOR4_OTA");
    return false;
}

size_t UnoR4OTA::write(const uint8_t *, size_t) {
    return 0;
}

bool UnoR4OTA::end() {
    return false;
}

void UnoR4OTA::abort() {
    // No-op — nothing to abort when the feature is compiled out.
}

const char *UnoR4OTA::errorString() {
    return "OTA disabled at build time";
}

bool UnoR4OTA::canRollBack() {
    return false;
}

bool UnoR4OTA::rollBack() {
    return false;
}

#endif  // ZENOPCB_ENABLE_UNOR4_OTA

}  // namespace ZenoPCB

#endif  // defined(ARDUINO_UNOR4_WIFI)
