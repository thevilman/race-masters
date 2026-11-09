#ifndef TG_UNZIP_H
#define TG_UNZIP_H

// Unpacks a zip archive held in memory (stored or deflated files, RFC 1951), checking each
// file's CRC-32. The launcher uses it for the python.org embeddable Python on Windows, so it
// needs neither tar.exe (which mishandles folders with non-ASCII names) nor any library.

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Called for each file, in the archive's order: its path inside the archive ('/' separated,
// checked: no "..", no absolute path or drive) and its bytes. Return false to stop.
typedef bool (*TgUnzipWrite)(const char* name, const uint8_t* data, size_t size, void* user);

// true when every file was unpacked and checked; else err says why.
bool tg_unzip(const uint8_t* zip, size_t size, TgUnzipWrite write, void* user, char* err, size_t errSize);

// Raw DEFLATE data -> out (exactly outSize bytes expected). Returns false on bad data.
bool tg_inflate(const uint8_t* in, size_t inSize, uint8_t* out, size_t outSize);
uint32_t tg_crc32(const uint8_t* data, size_t n);

#endif
