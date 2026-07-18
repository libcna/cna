// SPDX-License-Identifier: MS-PL
//
// Task AUD-04-008: a small standalone (non-GTest) executable that plays a SoundEffectInstance,
// then calls CNA::Internal::Audio::DestroyMixer() directly while that instance is still alive and
// playing, then exercises every operation a caller could still reach on the now-orphaned instance
// (state query, Pause(), Stop(), Dispose(), and finally natural destruction). Needs its own
// process, not the shared CnaTests binary: DestroyMixer() frees every MIX_Track/MIX_Audio the
// mixer owned (confirmed against real SDL3_mixer source, MIX_DestroyMixer -> MIX_DestroyTrack ->
// SDL_aligned_free), so if SoundEffectInstance::GetLiveTrackHandle()'s generation check
// (AUD-04-008/009) ever regressed, a later access would dereference freed memory -- a crash here
// must not take down (or silently corrupt the results of) every other test in the shared binary,
// same isolation rationale as tools/audio/audio_no_hardware_harness.cpp.
//
// Exit codes: 0 = every operation on the orphaned instance completed without crashing AND
// getStateProperty() correctly reported Stopped after DestroyMixer() (proving the generation
// check, not luck, is what kept this safe); 1 = an exception escaped; 2 = state was wrong
// (regression in the generation check itself, even though nothing crashed). A crash is detected
// by the parent (AudioMixerTests.cpp) via an abnormal (non-WIFEXITED) process termination, not by
// this harness's own exit code.
#include "CNA/Internal/Audio/AudioMixer.hpp"
#include "Microsoft/Xna/Framework/Audio/SoundEffect.hpp"
#include "Microsoft/Xna/Framework/Audio/SoundEffectInstance.hpp"
#include "Microsoft/Xna/Framework/Audio/SoundState.hpp"
#include "System/Environment.hpp"

#include <cstdio>
#include <cstdlib>
#include <exception>
#include <vector>

using Microsoft::Xna::Framework::Audio::AudioChannels;
using Microsoft::Xna::Framework::Audio::SoundEffect;
using Microsoft::Xna::Framework::Audio::SoundEffectInstance;
using Microsoft::Xna::Framework::Audio::SoundState;

int main()
{
    System::Environment::SetEnvironmentVariable("SDL_AUDIODRIVER", "dummy");

    try
    {
        // 10s of stereo S16 silence -- long enough that it cannot finish playing naturally
        // during this harness, so DestroyMixer() always races a genuinely still-active track.
        std::vector<unsigned char> pcm(4 * 441000, 0);
        SoundEffect effect(pcm, 44100, AudioChannels::Stereo);
        SoundEffectInstance instance = effect.CreateInstance();

        instance.Play();
        if (instance.getStateProperty() != SoundState::Playing)
        {
            std::fprintf(stderr, "Play() did not report Playing -- no audio device in this environment?\n");
            return 1;
        }

        // The core of AUD-04-008: destroy the shared mixer -- and every MIX_Track/MIX_Audio it
        // owns -- out from under a still-alive, still-playing instance. Nothing above this line
        // exercises the risk; everything below does.
        CNA::Internal::Audio::DestroyMixer();

        // Every operation a real caller could still reach on `instance` after this point, in the
        // order a game would plausibly hit them.
        if (instance.getStateProperty() != SoundState::Stopped)
        {
            std::fprintf(stderr, "state after DestroyMixer() was not Stopped -- generation check regressed\n");
            return 2;
        }
        instance.Pause();
        instance.Stop();
        instance.Dispose();
        // `instance` then goes out of scope at the end of this try block, exercising the
        // destructor path too (Dispose() is idempotent -- guarded by isDisposed_ -- so this is
        // the "already disposed" branch of ~SoundEffectInstance(), not a second live teardown).
    }
    catch (const std::exception& ex)
    {
        std::fprintf(stderr, "exception escaped: %s\n", ex.what());
        return 1;
    }
    catch (...)
    {
        std::fprintf(stderr, "non-std exception escaped\n");
        return 1;
    }

    return 0;
}
