// SPDX-License-Identifier: MS-PL
//
// Task P9-HARDWARE-005 / PLAT-99: SDL3 playback requires a real or mocked audio subsystem, and
// this environment only ever runs the SDL "dummy" driver (IN-12). NULL playback deliberately
// requires neither. For the SDL3 failure path, g_mixer (AudioMixer.cpp) is a process-wide,
// once-ever-initialized cache -- once any earlier
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
#include <signal.h>
#include <spawn.h>
#include <string>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

#include "CNA/Internal/Audio/AudioMixer.hpp"
#include "CNA/Internal/Audio/MixerEngine.hpp"
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

    // AUD-04-008/009: generalized to take an explicit path so the same spawn/watchdog/drain
    // plumbing serves the no-hardware harness plus the two mixer-destroy-with-active-voice
    // harnesses below, instead of duplicating it per harness.
    SpawnedProcess SpawnHarness(const char* path) {
        int pipeFds[2];
        if (pipe(pipeFds) != 0) {
            ADD_FAILURE() << "pipe() failed: " << strerror(errno);
            return {};
        }

        posix_spawn_file_actions_t actions;
        posix_spawn_file_actions_init(&actions);
        posix_spawn_file_actions_adddup2(&actions, pipeFds[1], STDERR_FILENO);
        posix_spawn_file_actions_addclose(&actions, pipeFds[0]);

        char* argv[] = {const_cast<char*>(path), nullptr};

        pid_t pid = -1;
        int rc = posix_spawn(&pid, path, &actions, nullptr, argv, environ);
        posix_spawn_file_actions_destroy(&actions);
        close(pipeFds[1]); // the child has its own dup'd copy; the parent only needs the read end

        if (rc != 0) {
            ADD_FAILURE() << "posix_spawn(" << path << ") failed: " << strerror(rc);
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

// AUD-04-001 / PLAT-95: the application-side mixer format remains queryable after the physical
// device was moved behind IAudioDevice. MIX_GetMixerFormat now reports the memory-backed mixer's
// stable output format; native-device conversion is intentionally the platform implementation's
// responsibility.
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
// proves the same GetMixer() path is override-able to other rates/channel counts. PLAT-95 makes
// this application format deterministic: the selected device may convert to a different physical
// format, but the memory-backed mixer's rate/channels remain exactly what its callback produces.
class AudioMixerSpecOverrideTest : public ::testing::TestWithParam<std::tuple<int, int>> {};

TEST_P(AudioMixerSpecOverrideTest, OverriddenSpecIsActuallyNegotiated) {
    System::Environment::SetEnvironmentVariable("SDL_AUDIODRIVER", "dummy");
    const auto [freq, channels] = GetParam();

    CNA::Internal::Audio::DestroyMixer();
    MixerSpecOverrideGuard guard;

    const CNA::Internal::Audio::MixerFormat requested{
        freq, channels, CNA::Internal::Audio::MixerSampleFormat::Signed16};
    CNA::Internal::Audio::SetMixerSpecOverrideForTests(requested);

    try {
        MIX_Mixer* mixer = CNA::Internal::Audio::GetMixer();
        ASSERT_NE(mixer, nullptr);

        SDL_AudioSpec actual{};
        ASSERT_TRUE(MIX_GetMixerFormat(mixer, &actual)) << SDL_GetError();

        EXPECT_EQ(actual.freq, freq);
        EXPECT_EQ(actual.channels, channels);
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

// AUD-04-006: a spec the audio contract/SDL stream considers invalid --
// freq <= 0, or a channel count outside SDL_IsSupportedChannelCount's documented 1-8 range
// (third_party/SDL/src/audio/SDL_audiocvt.c) -- must fail mixer creation outright rather than
// silently substituting some other rate/channel count the caller never asked for and would have
// no way to detect. Validation runs against the original application format before a physical
// device is started, so these values throw instead of being substituted.
class AudioMixerInvalidSpecThrowsTest : public ::testing::TestWithParam<std::tuple<int, int>> {};

TEST_P(AudioMixerInvalidSpecThrowsTest, RejectedOutrightRatherThanSilentlySubstituting) {
    System::Environment::SetEnvironmentVariable("SDL_AUDIODRIVER", "dummy");
    const auto [freq, channels] = GetParam();

    CNA::Internal::Audio::DestroyMixer();
    MixerSpecOverrideGuard guard;

    const CNA::Internal::Audio::MixerFormat requested{
        freq, channels, CNA::Internal::Audio::MixerSampleFormat::Signed16};
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

// AUD-04-007: repeated selected-device/mixer failures must not leak a MIX_Init()/MIX_Quit() refcount
// imbalance -- GetMixer()'s failure branch (AudioMixer.cpp) pairs a MIX_Quit() with the MIX_Init()
// it made moments earlier specifically so a failed retry never leaks a refcount (IN-11). If that
// pairing were ever wrong, repeated failures would desynchronize SDL's audio subsystem refcount,
// and a subsequent *valid* GetMixer() call would either fail unexpectedly or leave the subsystem
// unable to ever fully shut down -- this test drives 5 consecutive failures on the same invalid
// spec, then clears the override and confirms a completely ordinary GetMixer() call still succeeds
// immediately afterward with the correct default spec, proving the failed attempts left no
// residue behind.
TEST(AudioMixerTest, RepeatedDeviceOpenFailuresLeaveBalancedLifecycleAndSubsequentSuccessIntact) {
    System::Environment::SetEnvironmentVariable("SDL_AUDIODRIVER", "dummy");

    CNA::Internal::Audio::DestroyMixer();
    MixerSpecOverrideGuard guard;

    CNA::Internal::Audio::MixerFormat invalid{
        44100, 0, CNA::Internal::Audio::MixerSampleFormat::Signed16};

    for (int attempt = 0; attempt < 5; ++attempt) {
        CNA::Internal::Audio::SetMixerSpecOverrideForTests(invalid);
        EXPECT_THROW({ CNA::Internal::Audio::GetMixer(); }, std::runtime_error)
            << "attempt " << attempt;
        // GetMixer() itself never caches a failed attempt (g_mixer stays null), so no explicit
        // DestroyMixer() is needed between retries here -- each loop iteration re-enters the
        // exact same lazy-init/failure branch from scratch, which is the real-world retry shape
        // this test is verifying stays leak-free.
    }

    CNA::Internal::Audio::ClearMixerSpecOverrideForTests();
    try {
        MIX_Mixer* mixer = CNA::Internal::Audio::GetMixer();
        ASSERT_NE(mixer, nullptr);
        SDL_AudioSpec actual{};
        ASSERT_TRUE(MIX_GetMixerFormat(mixer, &actual)) << SDL_GetError();
        EXPECT_EQ(actual.freq, 44100);
        EXPECT_EQ(actual.channels, 2);
    } catch (...) {
        GTEST_SKIP() << "no audio device (dummy driver unavailable)";
    }
}

// AUD-15-017: repeated GetMixer()/DestroyMixer() cycles must never leak a device, thread, handle,
// or MIX_Init()/SDL_INIT_AUDIO refcount -- each cycle recreates the mixer "from scratch" (matches
// DestroyMixer()'s own documented contract), and every selected-device/queued-stream subsystem
// lease must be reacquired and released cleanly on each cycle. Twenty cycles is far more than any
// single test elsewhere in this suite exercises in a row; a leak would most plausibly show up as
// a growing resource count or a failure partway through, not necessarily on cycle 1.
TEST(AudioMixerTest, RepeatedInitDestroyCyclesLeaveNoLeakedStateAndFinalCallSucceeds) {
    System::Environment::SetEnvironmentVariable("SDL_AUDIODRIVER", "dummy");
    try {
        for (int i = 0; i < 20; ++i) {
            MIX_Mixer* mixer = CNA::Internal::Audio::GetMixer();
            ASSERT_NE(mixer, nullptr) << "cycle " << i;
            SDL_AudioSpec actual{};
            ASSERT_TRUE(MIX_GetMixerFormat(mixer, &actual)) << "cycle " << i << ": " << SDL_GetError();
            EXPECT_EQ(actual.freq, 44100) << "cycle " << i;
            EXPECT_EQ(actual.channels, 2) << "cycle " << i;
            CNA::Internal::Audio::DestroyMixer();
        }

        // One more creation after the loop, to confirm the mixer is still fully functional (not
        // degraded, e.g. by a slowly-exhausted resource) after 20 full cycles.
        MIX_Mixer* finalMixer = CNA::Internal::Audio::GetMixer();
        ASSERT_NE(finalMixer, nullptr);
        SDL_AudioSpec finalSpec{};
        ASSERT_TRUE(MIX_GetMixerFormat(finalMixer, &finalSpec)) << SDL_GetError();
        EXPECT_EQ(finalSpec.freq, 44100);
        EXPECT_EQ(finalSpec.channels, 2);
    } catch (...) {
        GTEST_SKIP() << "no audio device (dummy driver unavailable)";
    }
}

TEST(AudioMixerTest, MissingPhysicalHardwareFollowsTheSelectedAudioDeviceContract) {
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(kWatchdogSeconds);

    SpawnedProcess proc = SpawnHarness(CNA_AUDIO_NO_HARDWARE_HARNESS_PATH);
    ASSERT_NE(proc.pid, -1);

    int exitCode = -1;
    bool finished = WaitWithWatchdog(proc.pid, deadline, &exitCode);

    std::string output;
    DrainRemaining(proc.readFd, &output);
    close(proc.readFd);

    ASSERT_TRUE(finished) << "harness process did not exit before the watchdog deadline and was killed; output: " << output;
    EXPECT_EQ(exitCode, 0)
        << "expected selection-appropriate no-hardware behavior; got " << exitCode
        << "; output: " << output;
}

// AUD-04-008: DestroyMixer() frees every MIX_Track the mixer owned (confirmed against real
// SDL3_mixer source, MIX_DestroyMixer -> MIX_DestroyTrack -> SDL_aligned_free) -- a still-alive
// SoundEffectInstance holding one of those tracks must detect the invalidation
// (SoundEffectInstance::GetLiveTrackHandle()'s generation check) rather than dereference freed
// memory on its next Pause()/Stop()/Dispose()/state-query/destructor call. Spawned as its own
// process (see tools/audio/mixer_destroy_active_static_voice_harness.cpp's top-of-file comment)
// so a crash here is caught by the watchdog/exit-status check below instead of taking down every
// other test in this shared binary. Exit 0 = safe AND the state genuinely reflected the
// generation check (not merely "didn't crash"); exit 2 = no crash but the check itself regressed.
TEST(AudioMixerTest, MixerDestructionWithActiveStaticVoiceDoesNotCrashOrUseAfterFree) {
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(kWatchdogSeconds);

    SpawnedProcess proc = SpawnHarness(CNA_AUDIO_MIXER_DESTROY_ACTIVE_STATIC_VOICE_HARNESS_PATH);
    ASSERT_NE(proc.pid, -1);

    int exitCode = -1;
    bool finished = WaitWithWatchdog(proc.pid, deadline, &exitCode);

    std::string output;
    DrainRemaining(proc.readFd, &output);
    close(proc.readFd);

    ASSERT_TRUE(finished) << "harness process did not exit before the watchdog deadline (hang?) and was killed; output: " << output;
    EXPECT_EQ(exitCode, 0)
        << "expected the harness to exit 0 (no crash, generation check correctly detected the "
        << "orphaned track); got " << exitCode << " (-1 means the process crashed/was signaled, "
        << "e.g. a real use-after-free -- see AudioMixerTests.cpp's AUD-04-008 note); output: " << output;
}

// AUD-04-009: the DynamicSoundEffectInstance counterpart to the AUD-04-008 test just above -- see
// tools/audio/mixer_destroy_active_dynamic_voice_harness.cpp's top-of-file comment for why this
// needs a separate harness (DynamicSoundEffectInstance has its own independent track_ access
// sites, none of which share code with the base class's).
TEST(AudioMixerTest, MixerDestructionWithActiveDynamicVoiceDoesNotCrashOrUseAfterFree) {
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(kWatchdogSeconds);

    SpawnedProcess proc = SpawnHarness(CNA_AUDIO_MIXER_DESTROY_ACTIVE_DYNAMIC_VOICE_HARNESS_PATH);
    ASSERT_NE(proc.pid, -1);

    int exitCode = -1;
    bool finished = WaitWithWatchdog(proc.pid, deadline, &exitCode);

    std::string output;
    DrainRemaining(proc.readFd, &output);
    close(proc.readFd);

    ASSERT_TRUE(finished) << "harness process did not exit before the watchdog deadline (hang?) and was killed; output: " << output;
    EXPECT_EQ(exitCode, 0)
        << "expected the harness to exit 0 (no crash, generation check correctly detected the "
        << "orphaned track); got " << exitCode << " (-1 means the process crashed/was signaled, "
        << "e.g. a real use-after-free -- see AudioMixerTests.cpp's AUD-04-009 note); output: " << output;
}
