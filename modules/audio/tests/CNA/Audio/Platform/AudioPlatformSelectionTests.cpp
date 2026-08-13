// SPDX-License-Identifier: MS-PL

#include <gtest/gtest.h>

#if defined(CNA_AUDIO_PLATFORM_SDL3) && defined(CNA_AUDIO_PLATFORM_NULL)
#  error "Exactly one CNA audio-platform implementation must be selected."
#endif

#if !defined(CNA_AUDIO_PLATFORM_SDL3) && !defined(CNA_AUDIO_PLATFORM_NULL)
#  error "CNA_AUDIO_PLATFORM did not publish its implementation compile definition."
#endif

namespace {

TEST(AudioPlatformSelectionCompileTests, ExactlyOneImplementedBackendIsSelected)
{
#if defined(CNA_AUDIO_PLATFORM_SDL3)
    SUCCEED() << "CNA_AUDIO_PLATFORM=SDL3";
#else
    SUCCEED() << "CNA_AUDIO_PLATFORM=NULL";
#endif
}

} // namespace
