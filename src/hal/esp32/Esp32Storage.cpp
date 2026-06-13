#include "Esp32Storage.h"

// Plan 06-03 Pattern B (symmetric to Plan 06-2.5d Esp8266 mirror).
#if defined(ESP32)

#include <string.h>

namespace ZenoPCB {

bool Esp32Storage::begin() {
    // true = format on first-mount failure (matches existing LittleFSManager
    // behaviour throughout the firmware).
    return LittleFS.begin(true);
}

bool Esp32Storage::exists(const char *path) {
    if (!path) return false;
    return LittleFS.exists(path);
}

size_t Esp32Storage::readFile(const char *path, char *out, size_t maxLen) {
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

size_t Esp32Storage::writeFile(const char *path, const char *data, size_t len) {
    if (!path || !data) return 0;
    if (len == 0) return 0;

    File f = LittleFS.open(path, "w");
    if (!f) return 0;

    size_t written = f.write(reinterpret_cast<const uint8_t *>(data), len);
    f.close();
    return written;
}

bool Esp32Storage::deleteFile(const char *path) {
    if (!path) return false;
    return LittleFS.remove(path);
}

bool Esp32Storage::mkdir(const char *path) {
    if (!path) return false;
    return LittleFS.mkdir(path);
}

void Esp32Storage::listFiles(const char *prefix,
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
    // (Arduino-ESP32 File RAII), but we also close explicitly for clarity
    // (Pitfall 2 — leaked LittleFS handles exhaust the ~5-handle table).
    while (true) {
        File entry = dir.openNextFile();
        if (!entry) break;

        const char *entryPath = entry.path();
        if (entryPath && strncmp(entryPath, effectivePrefix, strlen(effectivePrefix)) == 0) {
            callback(entryPath);
        }
        entry.close();
    }
    dir.close();
}

}  // namespace ZenoPCB

#endif  // defined(ESP32)
