#include "Esp8266Storage.h"

#if defined(ESP8266)

#include <string.h>

namespace ZenoPCB {

namespace {

// Pitfall 1 — ESP8266 LittleFS requires every parent directory to
// exist before opening a file in write mode under a sub-directory.
// (ESP32 LittleFS auto-creates parents; ESP8266 silently returns an
// invalid File handle if a parent is missing.) Split `path` on '/'
// and mkdir each intermediate segment. No-op for paths without
// sub-directories (root-relative files like "/foo.json").
//
// `char buf[128]` stays well under the 512 B per-frame stack budget
// (06-CONTEXT D-08) and matches the upper-bound used by `listFiles`
// in the ESP32 analog.
void ensureParentDirs(const char *path) {
    if (!path || path[0] != '/') return;
    char buf[128];
    size_t pathLen = strnlen(path, sizeof(buf) - 1);
    if (pathLen >= sizeof(buf)) return;
    memcpy(buf, path, pathLen + 1);
    for (size_t i = 1; i < pathLen; ++i) {
        if (buf[i] == '/') {
            buf[i] = '\0';
            LittleFS.mkdir(buf);   // no-op if already exists
            buf[i] = '/';
        }
    }
}

}  // namespace

bool Esp8266Storage::begin() {
    // ESP8266 LittleFS.begin() has no boolean overload — it auto-formats
    // on first-mount failure natively (per ESP8266 Arduino Core 3.x docs).
    // Behaviour matches `LittleFS.begin(true)` on ESP32.
    return LittleFS.begin();
}

bool Esp8266Storage::exists(const char *path) {
    if (!path) return false;
    return LittleFS.exists(path);
}

size_t Esp8266Storage::readFile(const char *path, char *out, size_t maxLen) {
    if (!path || !out || maxLen == 0) return 0;

    File f = LittleFS.open(path, "r");
    if (!f) return 0;
    // Guard against directories.
    if (f.isDirectory()) {
        f.close();
        return 0;
    }

    // Reserve one byte for the trailing NUL.
    size_t toRead = maxLen - 1;
    size_t available = (size_t)f.size();
    if (available < toRead) toRead = available;

    size_t bytesRead = f.readBytes(out, toRead);
    out[bytesRead] = '\0';
    f.close();
    return bytesRead;
}

size_t Esp8266Storage::writeFile(const char *path, const char *data, size_t len) {
    if (!path || !data || len == 0) return 0;

    ensureParentDirs(path);    // PITFALL 1 fix — ESP8266-only auto-mkdir.

    File f = LittleFS.open(path, "w");
    if (!f) return 0;

    size_t written = f.write(reinterpret_cast<const uint8_t *>(data), len);
    f.close();
    return written;
}

bool Esp8266Storage::deleteFile(const char *path) {
    if (!path) return false;
    return LittleFS.remove(path);
}

bool Esp8266Storage::mkdir(const char *path) {
    if (!path) return false;
    return LittleFS.mkdir(path);
}

void Esp8266Storage::listFiles(const char *prefix,
                               std::function<void(const char *)> callback) {
    if (!callback) return;

    const char *effectivePrefix = (prefix && prefix[0] != '\0') ? prefix : "/";

    // Determine the directory portion of `prefix`. If the prefix points to a
    // directory directly (ends with '/') open it; otherwise open the parent
    // directory and filter entries that begin with `prefix`.
    char dirPath[128];
    const char *lastSlash = strrchr(effectivePrefix, '/');
    if (!lastSlash) {
        // No slash at all — treat root as the search directory.
        dirPath[0] = '/';
        dirPath[1] = '\0';
    } else {
        // Copy up to and including the slash (or the whole prefix if it ends
        // with one). Guard against overrun.
        size_t copyLen = static_cast<size_t>(lastSlash - effectivePrefix);
        if (copyLen == 0) {
            // Prefix starts with '/', last slash is the first char — root.
            dirPath[0] = '/';
            dirPath[1] = '\0';
        } else {
            if (copyLen >= sizeof(dirPath)) copyLen = sizeof(dirPath) - 1;
            memcpy(dirPath, effectivePrefix, copyLen);
            dirPath[copyLen] = '\0';
        }
    }

    File dir = LittleFS.open(dirPath, "r");
    if (!dir) return;
    if (!dir.isDirectory()) {
        dir.close();
        return;
    }

    // Iterate every direct child entry. Each `entry` is closed automatically
    // when it goes out of scope at the bottom of each loop iteration
    // (Arduino-ESP8266 File RAII), but we also close explicitly for clarity
    // (Pitfall 2 — leaked LittleFS handles exhaust the handle table).
    //
    // NOTE: ESP8266 LittleFS exposes a different directory iteration API
    // than ESP32 (Dir/openNextFile vs File/openNextFile). The analog
    // ESP32 path API uses `entry.path()`; on ESP8266 the analogous call
    // is `entry.fullName()` (entries from a `Dir` provide `fileName()` +
    // `dir.next()`). This .cpp keeps the ESP32 call site verbatim; the
    // backport divergence is resolved at compile time inside the
    // `<LittleFS.h>` headers on ESP8266 if the API names match
    // sufficiently. If they diverge at compile-time in Plan 06-02, the
    // executor of 06-02 will narrow this one body to use `Dir` /
    // `dir.next()` / `dir.fileName()` per ESP8266 LittleFS API.
    while (true) {
        File entry = dir.openNextFile();
        if (!entry) break;

        // Plan 06-03 Rule 3 — ESP8266 fs::File lacks path(); use
        // fullName() which returns the absolute path within LittleFS
        // root. ESP32 Storage uses entry.path(); both produce the
        // same "/dir/file.ext" shape so the strncmp() comparison
        // against `effectivePrefix` stays correct.
        const char *entryPath = entry.fullName();
        if (entryPath && strncmp(entryPath, effectivePrefix, strlen(effectivePrefix)) == 0) {
            callback(entryPath);
        }
        entry.close();
    }
    dir.close();
}

}  // namespace ZenoPCB

#endif  // defined(ESP8266)
