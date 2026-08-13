// SPDX-License-Identifier: MS-PL
//
// Task P9-HARDWARE-005: a small standalone (non-GTest) executable that forces SDL_AUDIODRIVER to
// a driver name that does not exist, before anything in this fresh process ever touches SDL
// audio, then calls a real XNA-facing audio entry point to prove NoAudioHardwareException is
// genuinely thrown when SDL3_mixer's device can't be opened. The shared CnaTests binary cannot
// exercise this path: CNA::Internal::Audio::AudioMixer.cpp's g_mixer is a process-wide,
// once-ever-initialized cache, and SDL only reads SDL_AUDIODRIVER the first time SDL_Init
// (SDL_INIT_AUDIO) runs in a process (see plan_audio.md P9-HARDWARE-002/005's notes). PLAT-95
// moved that open from SDL_mixer to the selected Sdl3AudioDevice; MIX_Init itself still never
// touches the audio subsystem.
//
// SoundEffect::getMasterVolumeProperty() is used as the trigger: a static property getter that
// calls GetMixerOrThrowXna() as its very first action, needing no file/buffer/instance setup at
// all (SoundEffect.cpp, wired by P9-HARDWARE-002).
//
// Exit codes: 0 = NoAudioHardwareException thrown as expected, 1 = no exception thrown (audio
// device was unexpectedly available in this environment, or a real regression), 2 = the wrong
// exception type was thrown.
#include "Microsoft/Xna/Framework/Audio/NoAudioHardwareException.hpp"
#include "Microsoft/Xna/Framework/Audio/SoundEffect.hpp"
#include "System/Environment.hpp"

#include <cstdio>
#include <cstdlib>
#include <exception>

int main()
{
    // Must be set before any SDL subsystem in this process ever touches audio -- SDL only reads
    // SDL_AUDIODRIVER on the first SDL_Init(SDL_INIT_AUDIO) call, so this only works because this
    // harness is a brand-new process with no prior audio initialization.
    System::Environment::SetEnvironmentVariable("SDL_AUDIODRIVER", "cna_p9hw005_nonexistent_driver");

    try
    {
        (void)Microsoft::Xna::Framework::Audio::SoundEffect::getMasterVolumeProperty();
    }
    catch (const Microsoft::Xna::Framework::Audio::NoAudioHardwareException&)
    {
        return 0;
    }
    catch (const std::exception& ex)
    {
        std::fprintf(stderr, "wrong exception type thrown: %s\n", ex.what());
        return 2;
    }

    std::fprintf(stderr, "no exception thrown -- an audio device was available in this environment\n");
    return 1;
}
