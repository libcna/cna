// SPDX-License-Identifier: MS-PL
//
// Task P9-HARDWARE-005: AudioMixer::GetMixer()/DestroyMixer() require a real (or mocked) SDL3
// audio subsystem; this environment only ever runs the SDL "dummy" audio driver (IN-12), and
// g_mixer (AudioMixer.cpp) is a process-wide, once-ever-initialized cache -- once any earlier
// test in this shared CnaTests binary succeeds even once, every later attempt sees the already
// -cached mixer and can never exercise GetMixer()'s failure branch again (see plan_audio.md
// P9-HARDWARE-002's verification caveat). A real regression test for the no-audio-hardware path
// therefore needs a fresh, isolated OS process with an invalid SDL_AUDIODRIVER set before
// anything else in that process ever calls SDL_Init(SDL_INIT_AUDIO) -- provided here by spawning
// tools/audio/audio_no_hardware_harness.cpp, mirroring the precedent set by
// tests/CNA/Internal/Net/TwoProcessLoopbackTest.cpp for the same "needs a fresh process" problem.
#include <gtest/gtest.h>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <poll.h>
#include <spawn.h>
#include <string>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

#include "CNA/Internal/Audio/AudioMixer.hpp"
#include "System/Environment.hpp"

#include <SDL3_mixer/SDL_mixer.h>

extern char** environ;

namespace {
    constexpr int kWatchdogSeconds = 10;

    // Spawns the harness with no arguments, capturing its stderr for diagnostics on failure.
    // Returns {pid, readFd} on success, {-1, -1} on a spawn-side failure (already reported via
    // ADD_FAILURE).
    struct SpawnedProcess {
        pid_t pid{-1};
        int readFd{-1};
    };

    SpawnedProcess SpawnHarness() {
        int pipeFds[2];
        if (pipe(pipeFds) != 0) {
            ADD_FAILURE() << "pipe() failed: " << strerror(errno);
            return {};
        }

        posix_spawn_file_actions_t actions;
        posix_spawn_file_actions_init(&actions);
        posix_spawn_file_actions_adddup2(&actions, pipeFds[1], STDERR_FILENO);
        posix_spawn_file_actions_addclose(&actions, pipeFds[0]);

        char* argv[] = {const_cast<char*>(CNA_AUDIO_NO_HARDWARE_HARNESS_PATH), nullptr};

        pid_t pid = -1;
        int rc = posix_spawn(&pid, CNA_AUDIO_NO_HARDWARE_HARNESS_PATH, &actions, nullptr, argv, environ);
        posix_spawn_file_actions_destroy(&actions);
        close(pipeFds[1]); // the child has its own dup'd copy; the parent only needs the read end

        if (rc != 0) {
            ADD_FAILURE() << "posix_spawn(" << CNA_AUDIO_NO_HARDWARE_HARNESS_PATH << ") failed: " << strerror(rc);
            close(pipeFds[0]);
            return {};
        }
        return SpawnedProcess{pid, pipeFds[0]};
    }

    // Polls non-blockingly for pid to exit, up to deadline; SIGKILLs and reaps it on timeout.
    bool WaitWithWatchdog(pid_t pid, std::chrono::steady_clock::time_point deadline, int* exitCode) {
        for (;;) {
            int status = 0;
            pid_t rv = waitpid(pid, &status, WNOHANG);
            if (rv == pid) {
                *exitCode = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
                return true;
            }
            if (std::chrono::steady_clock::now() >= deadline) {
                kill(pid, SIGKILL);
                waitpid(pid, &status, 0);
                *exitCode = -1;
                return false;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
    }

    void DrainRemaining(int fd, std::string* out) {
        char buffer[256];
        for (;;) {
            ssize_t n = read(fd, buffer, sizeof(buffer));
            if (n <= 0) {
                break;
            }
            out->append(buffer, static_cast<std::size_t>(n));
        }
    }
}

// AUD-04-001 (2026-07-17 deep audit): the actual negotiated mixer format must be queryable after
// GetMixer() returns, not just the requested spec CNA itself asked for -- this is what
// AudioMixer.cpp's own new diagnostic logging (requested vs. actual) depends on being real and
// available, not merely printed once and forgotten. Also implicitly proves the SDL_AUDIO_S16
// format actually round-trips through MIX_CreateMixerDevice under the dummy driver (this
// environment's only available driver), since MIX_GetMixerFormat would otherwise report whatever
// the (absent) physical device negotiated instead.
TEST(AudioMixerTest, ActualMixerFormatIsQueryableAfterCreation) {
    System::Environment::SetEnvironmentVariable("SDL_AUDIODRIVER", "dummy");
    try {
        MIX_Mixer* mixer = CNA::Internal::Audio::GetMixer();
        ASSERT_NE(mixer, nullptr);

        SDL_AudioSpec actual{};
        ASSERT_TRUE(MIX_GetMixerFormat(mixer, &actual)) << SDL_GetError();
        EXPECT_GT(actual.freq, 0);
        EXPECT_GT(actual.channels, 0);
    } catch (...) {
        GTEST_SKIP() << "no audio device (dummy driver unavailable)";
    }
}

namespace {
    // AUD-04-004: restores the production-default mixer spec on scope exit, regardless of how
    // the scope is left (assertion failure or normal return) -- necessary because g_mixer is a
    // single process-wide singleton shared by every other test in this binary; leaving an
    // override or a destroyed mixer behind would silently corrupt unrelated later tests.
    struct MixerSpecOverrideGuard {
        ~MixerSpecOverrideGuard() {
            CNA::Internal::Audio::ClearMixerSpecOverrideForTests();
            CNA::Internal::Audio::DestroyMixer();
        }
    };
}

// AUD-04-004: production code always requests the fixed S16 stereo 44100 Hz default; this
// proves the same GetMixer() path is override-able to other rates/channel counts (foundational
// for later device-negotiation-adjacent tests, e.g. AUD-04-002/003), and that the SDL dummy
// driver used throughout this suite actually honors an arbitrary requested spec rather than
// silently clamping every mixer to one fixed rate regardless of what's requested.
class AudioMixerSpecOverrideTest : public ::testing::TestWithParam<std::tuple<int, int>> {};

TEST_P(AudioMixerSpecOverrideTest, OverriddenSpecIsActuallyNegotiated) {
    System::Environment::SetEnvironmentVariable("SDL_AUDIODRIVER", "dummy");
    const auto [freq, channels] = GetParam();

    CNA::Internal::Audio::DestroyMixer();
    MixerSpecOverrideGuard guard;

    SDL_AudioSpec requested{};
    requested.format = SDL_AUDIO_S16;
    requested.channels = channels;
    requested.freq = freq;
    CNA::Internal::Audio::SetMixerSpecOverrideForTests(requested);

    try {
        MIX_Mixer* mixer = CNA::Internal::Audio::GetMixer();
        ASSERT_NE(mixer, nullptr);

        SDL_AudioSpec actual{};
        ASSERT_TRUE(MIX_GetMixerFormat(mixer, &actual)) << SDL_GetError();

        // AUD-04-004 finding: SDL itself (OpenPhysicalAudioDevice, third_party/SDL/src/audio/
        // SDL_audio.c) imposes a floor of S16 stereo 44100 Hz on every physical playback device
        // it opens -- "We impose a simple minimum on device formats. This prevents something
        // low quality ... from ruining a music thing playing at CD quality that tries to open
        // later." (DEFAULT_AUDIO_PLAYBACK_CHANNELS=2, DEFAULT_AUDIO_PLAYBACK_FREQUENCY=44100,
        // SDL_sysaudio.h). Requests at/above the floor pass through exactly; requests below it
        // are raised to the floor. This is genuine, documented SDL3 device-open behavior, not a
        // CNA bug or a resampling step -- CNA's own hard-coded production request (S16 stereo
        // 44100 Hz) already sits exactly on this floor, so this confirms there is no
        // *downward* device-negotiation risk in this SDL build; the only device-negotiation
        // pitch risk direction is upward (e.g. an OS default device that only offers
        // 48/96/192 kHz -- AUD-04-002/003 territory, still open).
        EXPECT_EQ(actual.freq, std::max(freq, 44100));
        EXPECT_EQ(actual.channels, std::max(channels, 2));
    } catch (...) {
        GTEST_SKIP() << "no audio device (dummy driver unavailable)";
    }
}

INSTANTIATE_TEST_SUITE_P(
    AUD04004, AudioMixerSpecOverrideTest,
    ::testing::Values(
        std::make_tuple(22050, 1),
        std::make_tuple(22050, 2),
        std::make_tuple(44100, 1),
        std::make_tuple(48000, 2),
        std::make_tuple(96000, 2)));

// AUD-04-006: a spec SDL considers genuinely invalid at the application-stream level --
// freq <= 0, or a channel count outside SDL_IsSupportedChannelCount's documented 1-8 range
// (third_party/SDL/src/audio/SDL_audiocvt.c) -- must fail mixer creation outright rather than
// silently substituting some other rate/channel count the caller never asked for and would have
// no way to detect. This is a DIFFERENT code path from AUD-04-004's device-open floor-clamp
// (OpenPhysicalAudioDevice, which only raises too-small-but-still-positive/nonzero values): the
// app-side stream validation runs against the ORIGINAL unclamped request and rejects it before
// the floor-clamped physical-device spec ever becomes observable, so these values throw --
// they do not quietly become 44100 Hz stereo the way e.g. 22050 Hz mono does (AUD-04-004).
class AudioMixerInvalidSpecThrowsTest : public ::testing::TestWithParam<std::tuple<int, int>> {};

TEST_P(AudioMixerInvalidSpecThrowsTest, RejectedOutrightRatherThanSilentlySubstituting) {
    System::Environment::SetEnvironmentVariable("SDL_AUDIODRIVER", "dummy");
    const auto [freq, channels] = GetParam();

    CNA::Internal::Audio::DestroyMixer();
    MixerSpecOverrideGuard guard;

    SDL_AudioSpec requested{};
    requested.format = SDL_AUDIO_S16;
    requested.channels = channels;
    requested.freq = freq;
    CNA::Internal::Audio::SetMixerSpecOverrideForTests(requested);

    EXPECT_THROW({ CNA::Internal::Audio::GetMixer(); }, std::runtime_error);
}

INSTANTIATE_TEST_SUITE_P(
    AUD04006, AudioMixerInvalidSpecThrowsTest,
    ::testing::Values(
        std::make_tuple(0, 2),      // freq == 0
        std::make_tuple(-1, 2),     // freq negative
        std::make_tuple(44100, 0),  // channels == 0
        std::make_tuple(44100, -1), // channels negative
        std::make_tuple(44100, 9))); // channels above SDL's 8-channel support ceiling

TEST(AudioMixerTest, GetMixerThrowsNoAudioHardwareExceptionWhenSdlAudioDriverIsInvalid) {
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(kWatchdogSeconds);

    SpawnedProcess proc = SpawnHarness();
    ASSERT_NE(proc.pid, -1);

    int exitCode = -1;
    bool finished = WaitWithWatchdog(proc.pid, deadline, &exitCode);

    std::string output;
    DrainRemaining(proc.readFd, &output);
    close(proc.readFd);

    ASSERT_TRUE(finished) << "harness process did not exit before the watchdog deadline and was killed; output: " << output;
    EXPECT_EQ(exitCode, 0)
        << "expected the harness to exit 0 (NoAudioHardwareException thrown); got " << exitCode
        << "; output: " << output;
}
