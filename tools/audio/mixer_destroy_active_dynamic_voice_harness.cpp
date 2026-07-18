// SPDX-License-Identifier: MS-PL
//
// Task AUD-04-009: the DynamicSoundEffectInstance counterpart to
// tools/audio/mixer_destroy_active_static_voice_harness.cpp (AUD-04-008) -- same hazard, same
// fix (SoundEffectInstance::GetLiveTrackHandle()'s generation check, which DynamicSoundEffect
// Instance shares via the inherited/shared `track_` member, P13-DYNAMIC-001), but exercised
// through DynamicSoundEffectInstance's own independent track_ access sites (getStateProperty(),
// Play()'s resume-from-Paused and new-track-creation paths, Stop(bool)'s early-return guard, and
// StopInternal()'s own inline track teardown -- none of these share code with the base class's
// Play()/Stop()/Pause()/Dispose(), so a regression in one does not imply a regression in the
// other). Needs its own process for the same reason as the static-voice harness: DestroyMixer()
// frees every MIX_Track the mixer owned, and a crash here must not corrupt the shared CnaTests
// binary's results.
//
// Exit codes: 0 = every operation on the orphaned instance completed without crashing AND
// getStateProperty() correctly reported Stopped after DestroyMixer(); 1 = an exception escaped
// (or playback never actually started, e.g. no audio device in this environment); 2 = state was
// wrong (regression in the generation check itself, even though nothing crashed).
#include "CNA/Internal/Audio/AudioMixer.hpp"
#include "Microsoft/Xna/Framework/Audio/AudioChannels.hpp"
#include "Microsoft/Xna/Framework/Audio/DynamicSoundEffectInstance.hpp"
#include "Microsoft/Xna/Framework/Audio/SoundState.hpp"
#include "System/Environment.hpp"

#include <cstdio>
#include <cstdlib>
#include <exception>
#include <vector>

using Microsoft::Xna::Framework::Audio::AudioChannels;
using Microsoft::Xna::Framework::Audio::DynamicSoundEffectInstance;
using Microsoft::Xna::Framework::Audio::SoundState;

int main()
{
    System::Environment::SetEnvironmentVariable("SDL_AUDIODRIVER", "dummy");

    try
    {
        DynamicSoundEffectInstance instance(44100, AudioChannels::Stereo);

        // Enough queued audio that the stream cannot run dry and self-stop during this harness
        // (10 buffers of ~6ms each at 44100Hz stereo S16 -- the dynamic stream halts only when
        // starved, not on a fixed duration, but keeping the queue well-fed avoids that entirely).
        std::vector<unsigned char> pcm(4 * 1024, 0);
        for (int i = 0; i < 10; ++i)
        {
            instance.SubmitBuffer(pcm);
        }

        instance.Play();
        if (instance.getStateProperty() != SoundState::Playing)
        {
            std::fprintf(stderr, "Play() did not report Playing -- no audio device in this environment?\n");
            return 1;
        }

        // The core of AUD-04-009: destroy the shared mixer -- and every MIX_Track/MIX_Audio it
        // owns -- out from under a still-alive, still-playing dynamic instance.
        CNA::Internal::Audio::DestroyMixer();

        if (instance.getStateProperty() != SoundState::Stopped)
        {
            std::fprintf(stderr, "state after DestroyMixer() was not Stopped -- generation check regressed\n");
            return 2;
        }

        // Every operation a real caller could still reach on `instance` after this point.
        instance.Pause();
        instance.Stop();
        instance.Dispose();
        // `instance` then goes out of scope at the end of this try block, exercising the
        // destructor path too.
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
