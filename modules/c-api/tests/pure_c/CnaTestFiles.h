/* SPDX-License-Identifier: MS-PL */

/*
 * UTF-8 file names for the pure-C suites on every C library.
 *
 * The C API takes every path as UTF-8, and a suite that writes a fixture with a non-ASCII name
 * must create the file the C API will look for. POSIX fopen and remove take the bytes as they are;
 * the Windows C runtime reads them in the ANSI code page, which spells the name differently. On
 * Windows these helpers convert to UTF-16 and use _wfopen and _wremove instead.
 */

#ifndef CNA_C_API_TEST_FILES_H
#define CNA_C_API_TEST_FILES_H

#include <stdio.h>

#if defined(_WIN32)

#include <wchar.h>
#include <windows.h>

static inline int cna_test_widen(const char* const text, wchar_t* const wide, const int capacity)
{
    return MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, -1, wide, capacity) != 0;
}

static inline FILE* cna_test_fopen(const char* const path, const char* const mode)
{
    wchar_t widePath[1024];
    wchar_t wideMode[16];
    if (!cna_test_widen(path, widePath, 1024) || !cna_test_widen(mode, wideMode, 16)) {
        return NULL;
    }
    return _wfopen(widePath, wideMode);
}

static inline int cna_test_remove(const char* const path)
{
    wchar_t widePath[1024];
    if (!cna_test_widen(path, widePath, 1024)) {
        return -1;
    }
    return _wremove(widePath);
}

#else

static inline FILE* cna_test_fopen(const char* const path, const char* const mode)
{
    return fopen(path, mode);
}

static inline int cna_test_remove(const char* const path)
{
    return remove(path);
}

#endif

#endif
