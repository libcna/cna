// SPDX-License-Identifier: MS-PL
#include "CNA/Internal/Audio/AudioMixer.hpp"
#include "CNA/Internal/Audio/MixerEngine.hpp"
#include "Microsoft/Xna/Framework/Audio/SoundEffect.hpp"
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <semaphore>
#include <string>
#include <vector>

using namespace CNA::Internal::Audio;
using namespace Microsoft::Xna::Framework::Audio;
static std::atomic<unsigned> stopped{0};
static std::atomic<bool> started{false};
static std::binary_semaphore firstAudio{0};
static std::uint64_t expectedGeneration;
static unsigned expectedStops;

static void Cooked(void*, MixerTrack*, int, float*, int)
{
    if (!started.exchange(true)) firstAudio.release();
}
static void Stopped(void*, MixerTrack*) { ++stopped; }
static void BeforeStaticCleanup()
{
    // Registered before the first mixer creation, as the pending-pan static's
    // destructor is. Exit must stop/join the device and invalidate tracks first.
    const auto generation = GetMixerGeneration();
    const bool pass = generation == expectedGeneration && stopped == expectedStops &&
                      GetMixerGeneratedByteCount() == 0;
    std::printf("EXIT_ORDER generation=%llu expected=%llu stopped=%u expectedStops=%u pass=%d\n",
        static_cast<unsigned long long>(generation), static_cast<unsigned long long>(expectedGeneration),
        stopped.load(), expectedStops, pass);
    std::fflush(stdout);
    if (!pass) std::_Exit(1);
}

int main(int argc, char** argv)
{
    if (std::atexit(BeforeStaticCleanup)) return 2;
    const std::string mode = argc > 1 ? argv[1] : "--active";
    if (mode == "--recreate") { EnsureMixer(); DestroyMixer(); }
    EnsureMixer();
    expectedGeneration = GetMixerGeneration() + 1;
    expectedStops = 1;
    std::vector<std::byte> pcm(44100 * 4 * 60);
    auto audio = LoadMixerRawAudio(pcm, {44100, 2, MixerSampleFormat::Signed16});
    MixerTrack* track = CreateMixerTrack();
    if (!audio || !track || !SetMixerTrackAudio(track, audio.get())) return 3;
    SetMixerTrackMixCallback(track, Cooked, nullptr);
    SetMixerTrackStoppedCallback(track, Stopped, nullptr);
    if (!PlayMixerTrack(track)) return 4;
    // Exercise actual fire-and-forget pan ownership at exit, both a completed
    // short sound and an active one. No test sleeps or replaces its callbacks.
    SoundEffect shortEffect(std::vector<unsigned char>(4 * 16), 44100, AudioChannels::Stereo);
    SoundEffect longEffect(std::vector<unsigned char>(44100 * 4 * 60), 44100, AudioChannels::Stereo);
    if (!shortEffect.Play(1.0f, 0.0f, 0.5f) || !longEffect.Play(1.0f, 0.0f, -0.5f)) return 5;
    if (!firstAudio.try_acquire_for(std::chrono::seconds(5))) return 6;
    if (mode == "--explicit-close") DestroyMixer();
    // Active track intentionally belongs to the mixer until process shutdown.
    return 0;
}
