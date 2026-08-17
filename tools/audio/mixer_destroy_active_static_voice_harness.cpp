// SPDX-License-Identifier: MS-PL
//
// Tasks AUD-04-008 / PLAT-96: a small standalone (non-GTest) executable that plays a
// SoundEffectInstance and a fire-and-forget SoundEffect, then calls
// CNA::Internal::Audio::DestroyMixer() directly while both are still alive and playing. It then
// exercises every operation a caller could still reach on the now-orphaned instance
// (state query, Pause(), Stop(), Dispose(), and finally natural destruction). Needs its own
// process, not the shared CnaTests binary: DestroyMixer() frees every MIX_Track/MIX_Audio the
// mixer owned (confirmed against real SDL3_mixer source, MIX_DestroyMixer -> MIX_DestroyTrack ->
// SDL_aligned_free), so if SoundEffectInstance::GetLiveTrackHandle()'s generation check
// (AUD-04-008/009) ever regressed, a later access would dereference freed memory -- a crash here
// must not take down (or silently corrupt the results of) every other test in the shared binary,
// same isolation rationale as tools/audio/audio_no_hardware_harness.cpp.
//
// Exit codes: 0 = every operation on the orphaned instance completed without crashing,
// getStateProperty() correctly reported Stopped after DestroyMixer(), and the surviving
// SoundEffect played again on the next mixer generation; 1 = an exception escaped; 2 = state was
// wrong; 3 = the surviving audio resource could not be rebuilt
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
        if (!effect.Play())
        {
            std::fprintf(stderr, "fire-and-forget Play() failed\n");
            return 1;
        }

        // AUD-04-008: this invalidates the instance's borrowed track. PLAT-96: the
        // fire-and-forget track's stopped callback runs from inside native shutdown and must not
        // synchronously destroy a stream whose lock is still owned by the native StopTrack frame.
        CNA::Internal::Audio::DestroyMixer();

        // Every operation a real caller could still reach on `instance` after this point, in the
        // order a game would plausibly hit them.
        if (instance.getStateProperty() != SoundState::Stopped)
        {
            std::fprintf(stderr, "state after DestroyMixer() was not Stopped -- generation check regressed\n");
            return 2;
        }
        if (!effect.Play())
        {
            std::fprintf(stderr, "surviving SoundEffect did not play after mixer recreation\n");
            return 3;
        }
        CNA::Internal::Audio::DestroyMixer();
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
