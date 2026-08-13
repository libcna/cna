// SPDX-License-Identifier: MS-PL

#include "CNA/Internal/Audio/AudioMixer.hpp"

#include <SDL3/SDL_init.h>
#include <SDL3/SDL_properties.h>
#include <SDL3/SDL_stdinc.h>

#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <stdexcept>
#include <thread>

namespace {

struct MixerCleanup final
{
    ~MixerCleanup()
    {
        CNA::Internal::Audio::DestroyMixer();
        CNA::Internal::Audio::ClearMixerSpecOverrideForTests();
    }
};

TEST(AudioMixerPlatformContractTests, SelectedDeviceOwnsPlaybackAndDrivesMemoryMixer)
{
    CNA::Internal::Audio::DestroyMixer();
    MixerCleanup cleanup;

#if defined(CNA_AUDIO_PLATFORM_SDL3)
    SDL_SetEnvironmentVariable(SDL_GetEnvironment(), "SDL_AUDIODRIVER", "dummy", true);

    MIX_Mixer* mixer = CNA::Internal::Audio::GetMixer();
    ASSERT_NE(mixer, nullptr);

    SDL_AudioSpec format{};
    ASSERT_TRUE(MIX_GetMixerFormat(mixer, &format)) << SDL_GetError();
    EXPECT_EQ(format.format, SDL_AUDIO_S16);
    EXPECT_EQ(format.channels, 2);
    EXPECT_EQ(format.freq, 44100);

    const SDL_PropertiesID properties = MIX_GetMixerProperties(mixer);
    ASSERT_NE(properties, 0u);
    EXPECT_EQ(SDL_GetNumberProperty(properties, MIX_PROP_MIXER_DEVICE_NUMBER, -1), 0);

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (CNA::Internal::Audio::GetMixerGeneratedByteCount() == 0
           && std::chrono::steady_clock::now() < deadline)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    EXPECT_GT(CNA::Internal::Audio::GetMixerGeneratedByteCount(), 0u);
    EXPECT_FALSE(CNA::Internal::Audio::HasMixerOutputError());
#elif defined(CNA_AUDIO_PLATFORM_NULL)
    const SDL_InitFlags audioBefore = SDL_WasInit(SDL_INIT_AUDIO) & SDL_INIT_AUDIO;
    EXPECT_THROW((void)CNA::Internal::Audio::GetMixer(), std::runtime_error);
    EXPECT_EQ(SDL_WasInit(SDL_INIT_AUDIO) & SDL_INIT_AUDIO, audioBefore)
        << "NULL selection must not initialize or fall back to SDL3 audio";
#else
#error "CNA audio platform selection did not define an implementation"
#endif
}

TEST(AudioMixerPlatformContractTests, Float32ApplicationFormatUsesTheSameWholeBufferPath)
{
#if defined(CNA_AUDIO_PLATFORM_SDL3)
    CNA::Internal::Audio::DestroyMixer();
    MixerCleanup cleanup;
    SDL_SetEnvironmentVariable(SDL_GetEnvironment(), "SDL_AUDIODRIVER", "dummy", true);

    SDL_AudioSpec requested{};
    requested.format = SDL_AUDIO_F32;
    requested.channels = 2;
    requested.freq = 48000;
    CNA::Internal::Audio::SetMixerSpecOverrideForTests(requested);

    MIX_Mixer* mixer = CNA::Internal::Audio::GetMixer();
    ASSERT_NE(mixer, nullptr);
    SDL_AudioSpec actual{};
    ASSERT_TRUE(MIX_GetMixerFormat(mixer, &actual)) << SDL_GetError();
    EXPECT_EQ(actual.format, requested.format);
    EXPECT_EQ(actual.channels, requested.channels);
    EXPECT_EQ(actual.freq, requested.freq);

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (CNA::Internal::Audio::GetMixerGeneratedByteCount() == 0
           && std::chrono::steady_clock::now() < deadline)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    EXPECT_GT(CNA::Internal::Audio::GetMixerGeneratedByteCount(), 0u);
    EXPECT_FALSE(CNA::Internal::Audio::HasMixerOutputError());
#else
    GTEST_SKIP() << "requires the implemented SDL3 playback device";
#endif
}

} // namespace
