// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/TestSupport/TestScratch.hpp"
#include <cstdlib>

namespace CNA::TestSupport
{
    // Tools called by a test may allocate through the standard temporary-directory API.
    // Redirect that API after claiming the sole process owner; no second allocator exists here.
    inline void UseProcessTempRoot()
    {
        const auto& root = FixtureRoot();
#if defined(_WIN32)
        if (::_wputenv_s(L"TMP", root.c_str()) != 0 || ::_wputenv_s(L"TEMP", root.c_str()) != 0)
#else
        if (::setenv("TMPDIR", root.c_str(), 1) != 0)
#endif
            throw std::runtime_error("cannot isolate the test temporary directory");
    }
}
