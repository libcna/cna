// SPDX-License-Identifier: MS-PL
//
// Task P9-HARDWARE-005 / PLAT-99: a small standalone (non-GTest) executable that forces
// SDL_AUDIODRIVER to a nonexistent name before anything in this fresh process touches audio,
// then calls a real XNA-facing entry point. The selected SDL3 device must translate the failed
// open to NoAudioHardwareException; the selected NULL device must succeed silently without
// consulting SDL at all. The shared CnaTests binary cannot reliably exercise the SDL3 failure
// path because AudioMixer's device and SDL's driver selection are process-wide caches.
//
// SoundEffect::getMasterVolumeProperty() is used as the trigger: a static property getter that
// calls GetMixerOrThrowXna() as its very first action, needing no file/buffer/instance setup at
// all (SoundEffect.cpp, wired by P9-HARDWARE-002).
//
// Exit codes: 0 = selection-appropriate behavior, 1 = SDL3 unexpectedly succeeded or NULL
// unexpectedly reported no hardware, 2 = another exception type was thrown.
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
#if defined(CNA_AUDIO_PLATFORM_NULL)
        std::fprintf(stderr, "NULL audio unexpectedly reported missing physical hardware\n");
        return 1;
#else
        return 0;
#endif
    }
    catch (const std::exception& ex)
    {
        std::fprintf(stderr, "wrong exception type thrown: %s\n", ex.what());
        return 2;
    }

#if defined(CNA_AUDIO_PLATFORM_NULL)
    return 0;
#else
    std::fprintf(stderr, "no exception thrown -- SDL3 unexpectedly opened an audio device\n");
    return 1;
#endif
}
