// SPDX-License-Identifier: MS-PL

#include <gtest/gtest.h>

#if (defined(CNA_AUDIO_PLATFORM_SDL3) + defined(CNA_AUDIO_PLATFORM_SDL2) \
     + defined(CNA_AUDIO_PLATFORM_NULL)) != 1
#  error "Exactly one CNA audio-platform implementation must be selected."
#endif

namespace {

TEST(AudioPlatformSelectionCompileTests, ExactlyOneImplementedBackendIsSelected)
{
#if defined(CNA_AUDIO_PLATFORM_SDL3)
    SUCCEED() << "CNA_AUDIO_PLATFORM=SDL3";
#elif defined(CNA_AUDIO_PLATFORM_SDL2)
    SUCCEED() << "CNA_AUDIO_PLATFORM=SDL2";
#else
    SUCCEED() << "CNA_AUDIO_PLATFORM=NULL";
#endif
}

} // namespace
