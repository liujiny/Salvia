#ifndef SALVIA_STATEFILE_H
#define SALVIA_STATEFILE_H

#include <cstdio>
#include <cstring>
#include <string>
#include <stdint.h>
#include <zlib.h>
#if !defined(_WIN32) && !defined(_XBOX) && !defined(_XBOX360)
#include <fcntl.h>
#include <unistd.h>
#endif

/* State files retain the original gzip(core bytes + optional RCHV trailer).
 * I/O uses bounded chunks; a CV1000 state must never require a 151 MiB copy.
 * Replace only a completely closed temporary file, preserving the old slot
 * when writing, finalizing, or renaming fails. */
namespace SalviaStateFile {
static const size_t CHUNK = 65536;
static const uint32_t MAX_RA_BYTES = 16 * 1024 * 1024;

inline bool write(gzFile file, const void* data, size_t size) {
    const unsigned char* p = (const unsigned char*)data;
    while (size) {
        unsigned n = (unsigned)(size > CHUNK ? CHUNK : size);
        int done = gzwrite(file, p, n);
        if (done <= 0 || done > (int)n) return false;
        p += done; size -= done;
    }
    return true;
}

inline bool read(gzFile file, void* data, size_t size) {
    unsigned char* p = (unsigned char*)data;
    while (size) {
        unsigned n = (unsigned)(size > CHUNK ? CHUNK : size);
        int done = gzread(file, p, n);
        if (done <= 0 || done > (int)n) return false;
        p += done; size -= done;
    }
    return true;
}

inline bool end(gzFile file) {
    unsigned char byte;
    int n = gzread(file, &byte, 1), err = Z_OK;
    gzerror(file, &err);
    return n == 0 && gzeof(file) && (err == Z_OK || err == Z_STREAM_END);
}

/* Validate CRC, truncation, core length, and the complete optional trailer
 * before any streamed load writes live emulator memory. Rewind the SAME open
 * file for loading, so replacing the slot cannot switch files between passes.
 * A second-pass device read failure is still an error, never reported success. */
inline bool validate(gzFile file, size_t coreSize, uint32_t& raSize) {
    unsigned char scratch[8192];
    raSize = 0;
    size_t remaining = coreSize;
    while (remaining) {
        size_t n = remaining > sizeof(scratch) ? sizeof(scratch) : remaining;
        if (!read(file, scratch, n)) return false;
        remaining -= n;
    }
    int markerSize = gzread(file, scratch, 4);
    if (markerSize == 0) return end(file); // Legacy core-only state.
    if (markerSize != 4 || memcmp(scratch, "RCHV", 4) != 0 ||
        !read(file, &raSize, sizeof(raSize)) || raSize > MAX_RA_BYTES)
        return false;
    remaining = raSize;
    while (remaining) {
        size_t n = remaining > sizeof(scratch) ? sizeof(scratch) : remaining;
        if (!read(file, scratch, n)) return false;
        remaining -= n;
    }
    return end(file);
}

inline bool replace(const std::string& temporary, const std::string& target) {
#if defined(_WIN32) || defined(_XBOX) || defined(_XBOX360)
    return MoveFileExA(temporary.c_str(), target.c_str(),
                       MOVEFILE_REPLACE_EXISTING) != 0;
#else
    return std::rename(temporary.c_str(), target.c_str()) == 0;
#endif
}

inline bool commit(const std::string& path) {
#if defined(_WIN32) || defined(_XBOX) || defined(_XBOX360)
    HANDLE file = CreateFileA(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, NULL,
                              OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) return false;
    bool ok = FlushFileBuffers(file) != 0;
    if (!CloseHandle(file)) ok = false;
    return ok;
#else
    int fd = open(path.c_str(), O_RDONLY);
    if (fd < 0) return false;
    bool ok = fsync(fd) == 0;
    if (close(fd) != 0) ok = false;
    return ok;
#endif
}

inline bool finish(gzFile file, bool ok, const std::string& temporary,
                   const std::string& target) {
    const int closed = gzclose(file); // Checks final deflate/write errors.
    ok = ok && closed == Z_OK;
    if (ok) ok = commit(temporary);
    if (ok) ok = replace(temporary, target);
    if (!ok) std::remove(temporary.c_str());
    return ok;
}
} // namespace SalviaStateFile
#endif
