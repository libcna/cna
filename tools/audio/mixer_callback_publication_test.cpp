// SPDX-License-Identifier: MS-PL
#include "CNA/Internal/Audio/MixerEngine.hpp"
#include "CNA/Internal/Audio/AudioMixer.hpp"
#include <SDL3_mixer/SDL_mixer.h>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <future>
#include <semaphore>
#include <thread>
#include <vector>

using namespace CNA::Internal::Audio;
static const auto owner = std::this_thread::get_id();
static std::promise<void> boundary;
static std::promise<void> releaseSetter;
static auto releaseFuture = releaseSetter.get_future();
static bool announced = false; // accessed only by the setter thread
static std::atomic<int> first{0}, second{0}, invalid{0};
static int firstToken, secondToken;
static bool stress = false;
static std::atomic<bool> audioStarted{false};
static std::binary_semaphore firstAudio{0};

static void Announce()
{
    if (!announced) { announced = true; boundary.set_value(); }
}
extern "C" void __real_MIX_LockMixer(MIX_Mixer*);
extern "C" void __wrap_MIX_LockMixer(MIX_Mixer* mixer)
{
    if (!stress && std::this_thread::get_id() != owner) Announce();
    __real_MIX_LockMixer(mixer);
}
extern "C" bool __real_MIX_SetTrackCookedCallback(MIX_Track*, MIX_TrackMixCallback, void*);
extern "C" bool __wrap_MIX_SetTrackCookedCallback(MIX_Track* track, MIX_TrackMixCallback callback, void* data)
{
    if (!stress && std::this_thread::get_id() != owner) { Announce(); releaseFuture.wait(); }
    return __real_MIX_SetTrackCookedCallback(track, callback, data);
}
extern "C" bool __real_MIX_SetTrackStoppedCallback(MIX_Track*, MIX_TrackStoppedCallback, void*);
extern "C" bool __wrap_MIX_SetTrackStoppedCallback(MIX_Track* track, MIX_TrackStoppedCallback callback, void* data)
{
    if (!stress && std::this_thread::get_id() != owner) { Announce(); releaseFuture.wait(); }
    return __real_MIX_SetTrackStoppedCallback(track, callback, data);
}
static void First(void* data) { ++first; if (data != &firstToken) ++invalid; }
static void Second(void* data) { ++second; if (data != &secondToken) ++invalid; }
static void FirstMix(void* data, MixerTrack*, int, float*, int) {
    First(data);
    if (stress && !audioStarted.exchange(true)) firstAudio.release();
}
static void SecondMix(void* data, MixerTrack*, int, float*, int) { Second(data); }
static void FirstStopped(void* data, MixerTrack*) { First(data); }
static void SecondStopped(void* data, MixerTrack*) { Second(data); }
static void ReplacingStopped(void* data, MixerTrack* track)
{
    First(data);
    SetMixerTrackStoppedCallback(track, SecondStopped, &secondToken);
    SetMixerTrackMixCallback(track, SecondMix, &secondToken);
}

int main(int argc, char** argv)
{
    const bool stopped = argc > 1 && std::string(argv[1]) == "--stopped";
    stress = argc > 1 && std::string(argv[1]) == "--stress";
    MIX_Mixer* mixer = GetMixer();
    std::vector<std::byte> bytes(44100 * 4);
    auto audio = LoadMixerRawAudio(bytes, {44100, 2, MixerSampleFormat::Signed16});
    MixerTrack* track = CreateMixerTrack();
    if (!audio || !track || !SetMixerTrackAudio(track, audio.get())) return 2;
    if (argc > 1 && std::string(argv[1]) == "--reentrant") {
        SetMixerTrackStoppedCallback(track, ReplacingStopped, &firstToken);
        if (!PlayMixerTrack(track)) return 3;
        StopMixerTrack(track); // callback replaces both tuples, recursively
        if (!PlayMixerTrack(track)) return 3;
        StopMixerTrack(track);
        DestroyMixerTrack(track);
        audio.reset();
        DestroyMixer();
        std::printf("reentrant first=%d second=%d invalid=%d\n", first.load(), second.load(), invalid.load());
        return first == 1 && second >= 1 && invalid == 0 ? 0 : 1;
    }
    MIX_LockMixer(mixer);
    if (stopped) SetMixerTrackStoppedCallback(track, FirstStopped, &firstToken);
    else SetMixerTrackMixCallback(track, FirstMix, &firstToken);
    MixerPlayOptions options;
    options.loopCount = stress ? -1 : 0;
    if (!PlayMixerTrack(track, options)) return 3;
    if (stress) {
        MIX_UnlockMixer(mixer);
        if (!firstAudio.try_acquire_for(std::chrono::seconds(5))) return 4;
        // A fixed workload, not retries until the oracle passes. The actual audio
        // thread reads the callback tuple concurrently with these control updates.
        for (int i=0; i<200000; ++i) {
            SetMixerTrackMixCallback(track, SecondMix, &secondToken);
            SetMixerTrackMixCallback(track, FirstMix, &firstToken);
        }
        MIX_LockMixer(mixer);
        SetMixerTrackMixCallback(track, nullptr, nullptr);
        MIX_UnlockMixer(mixer);
        DestroyMixerTrack(track);
        audio.reset();
        DestroyMixer();
        std::printf("stress first=%d second=%d invalid=%d\n", first.load(), second.load(), invalid.load());
        return invalid == 0 ? 0 : 1;
    }
    std::thread writer([&] {
        if (stopped) SetMixerTrackStoppedCallback(track, SecondStopped, &secondToken);
        else SetMixerTrackMixCallback(track, SecondMix, &secondToken);
    });
    if (boundary.get_future().wait_for(std::chrono::seconds(5)) != std::future_status::ready)
        std::terminate();
    std::byte output[4096];
    if (stopped) StopMixerTrack(track);
    else if (MIX_Generate(mixer, output, sizeof(output)) < 0) std::terminate();
    const bool protectedState = first > 0 && second == 0 && invalid == 0;
    MIX_UnlockMixer(mixer);
    releaseSetter.set_value();
    writer.join();
    MIX_LockMixer(mixer);
    if (stopped) {
        if (!PlayMixerTrack(track)) std::terminate();
        StopMixerTrack(track);
    } else if (MIX_Generate(mixer, output, sizeof(output)) < 0) std::terminate();
    const bool publishedState = second > 0 && invalid == 0;
    SetMixerTrackMixCallback(track, nullptr, nullptr);
    SetMixerTrackStoppedCallback(track, nullptr, nullptr);
    MIX_UnlockMixer(mixer);
    DestroyMixerTrack(track);
    audio.reset();
    DestroyMixer();
    std::printf("protected=%d published=%d first=%d second=%d invalid=%d\n",
                protectedState, publishedState, first.load(), second.load(), invalid.load());
    return protectedState && publishedState ? 0 : 1;
}
