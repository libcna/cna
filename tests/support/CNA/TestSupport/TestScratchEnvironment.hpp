// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/TestSupport/TestScratch.hpp"
#include <cstdlib>
#include <string>

namespace CNA::TestSupport
{
    // Sets one process environment variable; false when the C runtime refuses it.
    //
    // Test-support targets link nothing, sharp-runtime included, so they cannot use
    // System::Environment::SetEnvironmentVariable. This is their one portable spelling, and the
    // only POSIX setenv that scripts/check_no_posix_setenv.py accepts in CNA's own sources.
    [[nodiscard]] inline bool SetProcessEnvironmentVariable(const char* name, const std::string& value)
    {
#if defined(_WIN32)
        return ::_putenv_s(name, value.c_str()) == 0;
#else
        return ::setenv(name, value.c_str(), 1) == 0;
#endif
    }

    // Tools called by a test may allocate through the standard temporary-directory API.
    // Redirect that API after claiming the sole process owner; no second allocator exists here.
    inline void UseProcessTempRoot()
    {
        const auto& root = FixtureRoot();
#if defined(_WIN32)
        if (::_wputenv_s(L"TMP", root.c_str()) != 0 || ::_wputenv_s(L"TEMP", root.c_str()) != 0)
#else
        if (!SetProcessEnvironmentVariable("TMPDIR", root.string()))
#endif
            throw std::runtime_error("cannot isolate the test temporary directory");
    }
}
