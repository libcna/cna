// SPDX-License-Identifier: MS-PL
// Linker wrappers belong only to the isolated regression executables.
#include <cerrno>
#include <cstdlib>
#include <cstring>

namespace {
bool fault(const char* name) {
    const auto* value = std::getenv("CNA_CNBF_PROBE_FAULT");
    return value != nullptr && std::strcmp(value, name) == 0;
}
}

#if defined(CNA_PROBE_COMPRESSION_FAULTS)
#include <zlib.h>
#include <zstd.h>
extern "C" {
size_t __real_ZSTD_compress(void*, size_t, const void*, size_t, int);
size_t __real_ZSTD_decompress(void*, size_t, const void*, size_t);
int __real_compress2(Bytef*, uLongf*, const Bytef*, uLong, int);
int __real_uncompress(Bytef*, uLongf*, const Bytef*, uLong);

size_t __wrap_ZSTD_compress(void* dst, size_t cap, const void* src, size_t len, int level) {
    static int calls = 0;
    ++calls;
    if (fault("zstd-compress") || (calls > 1 && fault("zstd-timed-compress"))) return size_t(-1);
    const auto result = __real_ZSTD_compress(dst, cap, src, len, level);
    if (!ZSTD_isError(result) && result != 0 && calls > 1 && fault("zstd-timed-output"))
        static_cast<unsigned char*>(dst)[result - 1] ^= 1;
    return result;
}
size_t __wrap_ZSTD_decompress(void* dst, size_t cap, const void* src, size_t len) {
    static int calls = 0;
    ++calls;
    if (fault("zstd-decompress") || (calls > 1 && fault("zstd-timed-decompress"))) return size_t(-1);
    const auto result = __real_ZSTD_decompress(dst, cap, src, len);
    if (!ZSTD_isError(result) && result != 0) {
        if (fault("zstd-length")) return result - 1;
        if (fault("zstd-mismatch") || (calls > 1 && fault("zstd-timed-mismatch")))
            static_cast<unsigned char*>(dst)[result - 1] ^= 1;
    }
    return result;
}
int __wrap_compress2(Bytef* dst, uLongf* cap, const Bytef* src, uLong len, int level) {
    static int calls = 0;
    ++calls;
    if (fault("zlib-compress") || (calls > 1 && fault("zlib-timed-compress"))) return Z_MEM_ERROR;
    const auto result = __real_compress2(dst, cap, src, len, level);
    if (result == Z_OK && *cap != 0 && calls > 1 && fault("zlib-timed-output")) dst[*cap - 1] ^= 1;
    return result;
}
int __wrap_uncompress(Bytef* dst, uLongf* cap, const Bytef* src, uLong len) {
    static int calls = 0;
    ++calls;
    if (fault("zlib-decompress") || (calls > 1 && fault("zlib-timed-decompress"))) return Z_DATA_ERROR;
    const auto result = __real_uncompress(dst, cap, src, len);
    if (result == Z_OK && *cap != 0) {
        if (fault("zlib-length")) --*cap;
        if (fault("zlib-mismatch") || (calls > 1 && fault("zlib-timed-mismatch"))) dst[*cap - 1] ^= 1;
    }
    return result;
}
}
#else
#include <cstdarg>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
extern "C" {
int __real_open(const char*, int, ...);
int __real_open64(const char*, int, ...);
ssize_t __real_read(int, void*, size_t);
void* __real_mmap(void*, size_t, int, int, int, off_t);
void* __real_mmap64(void*, size_t, int, int, int, off64_t);
int __wrap_open(const char* path, int flags, ...) {
    if (fault("open")) { errno = EACCES; return -1; }
    return __real_open(path, flags);
}
int __wrap_open64(const char* path, int flags, ...) {
    if (fault("open")) { errno = EACCES; return -1; }
    return __real_open64(path, flags);
}
ssize_t __wrap_read(int fd, void* data, size_t size) {
    static bool interrupted = false;
    if (fault("read")) { errno = EIO; return -1; }
    if (fault("short-read")) return 0;
    if (fault("eintr") && !interrupted) { interrupted = true; errno = EINTR; return -1; }
    return __real_read(fd, data, size);
}
void* corruptMapping(void* result, size_t size) {
    if (result != MAP_FAILED && fault("mapped-tail")) {
        static_cast<unsigned char*>(result)[size - 1] ^= 1;
        mprotect(result, size, PROT_READ);
    }
    return result;
}
void* __wrap_mmap(void* address, size_t size, int prot, int flags, int fd, off_t offset) {
    if (fault("mmap")) { errno = ENOMEM; return MAP_FAILED; }
    return corruptMapping(__real_mmap(address, size, prot | (fault("mapped-tail") ? PROT_WRITE : 0), flags, fd, offset), size);
}
void* __wrap_mmap64(void* address, size_t size, int prot, int flags, int fd, off64_t offset) {
    if (fault("mmap")) { errno = ENOMEM; return MAP_FAILED; }
    return corruptMapping(__real_mmap64(address, size, prot | (fault("mapped-tail") ? PROT_WRITE : 0), flags, fd, offset), size);
}
}
#endif
