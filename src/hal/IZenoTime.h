#ifndef ZENOPCB_IZENOTIME_H
#define ZENOPCB_IZENOTIME_H

/**
 * @file IZenoTime.h
 * @brief Pure-virtual interface for wall-clock time and NTP sync.
 *
 * Used for schedule execution, telemetry timestamps, and logs.
 *
 * `syncNTP` returns immediately; SNTP runs in background. Use
 * `isSynced()` to check sync status before relying on `now()`.
 *
 * On ESP32 the impl may delegate to existing TimeManager  leaving
 * TimeManager intact for per RESEARCH (avoid two
 * configTime callers fighting over global SNTP state). may
 * migrate all TimeManager callers to IZenoTime.
 *
 * `time_t` is C standard <time.h>  POSIX seconds-since-epoch (UTC).
 *
 * No exceptions  fallible methods return bool.
 */

#include <stddef.h>
#include <stdint.h>
#include <time.h>

namespace ZenoPCB {

struct IZenoTime {
    virtual ~IZenoTime() = default;

    /**
     * Initiate non-blocking NTP synchronisation. Returns immediately;
     * SNTP runs in the background. Use `isSynced()` to check status.
     *
     * @param server         NTP server hostname (e.g. "pool.ntp.org").
     * @param gmtOffsetSec   GMT offset in seconds (positive east of UTC).
     * @param daylightOffsetSec Additional DST offset in seconds.
     */
    virtual void syncNTP(const char *server,
                         long gmtOffsetSec,
                         int daylightOffsetSec) = 0;

    /**
     * Current wall-clock time as POSIX seconds-since-epoch (UTC).
     * Returns 0 if not yet synced (impl may also return last-known time).
     */
    virtual time_t now() = 0;

    /**
     * True once the platform clock has been synchronised at least once
     * since boot (i.e. has a plausible year > 2020 epoch).
     */
    virtual bool isSynced() = 0;
};

}  // namespace ZenoPCB

#endif  // ZENOPCB_IZENOTIME_H
