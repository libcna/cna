// SPDX-License-Identifier: MS-PL
#include "CNA/Internal/Media/AudioDurationProbe.hpp"

namespace CNA::Internal::Media
{
    SharpRuntime::intcs AudioDurationProbe::ProbeDurationMS(const std::string&)
    {
        // Duration metadata remains optional when the FFmpeg video backend is disabled. Callers
        // already treat zero as unknown, matching the established non-FFmpeg platform behavior.
        return 0;
    }
}
