// SPDX-License-Identifier: MS-PL
#include "CNA/Internal/Audio/AudioMixer.hpp"

#ifdef SOUND_ENABLED
#include <mutex>
#include <stdexcept>
#include <string>

namespace CNA::Internal::Audio
{
    namespace
    {
        // AUDIO-002: g_mixer's lazy-init check-then-create sequence, and DestroyMixer()'s
        // check-then-destroy sequence, now share this single mutex -- previously neither was
        // synchronized at all (only an assumed, unenforced main-thread-only contract), so two
        // concurrent first GetMixer() callers could both observe g_mixer == nullptr and both race
        // through MIX_Init()/MIX_CreateMixerDevice(), and a GetMixer() call concurrent with
        // DestroyMixer() could read a half-destroyed pointer. Holding the lock for the entire
        // function body below (not just around the null check) is what makes this safe -- there
        // is no unlocked window between "check g_mixer" and "create/destroy/return it" for a
        // second thread to slip into.
        std::mutex g_mixerMutex;
        MIX_Mixer* g_mixer = nullptr;
    }

    MIX_Mixer* GetMixer()
    {
        std::lock_guard<std::mutex> lock(g_mixerMutex);
        if (!g_mixer)
        {
            if (!MIX_Init())
            {
                throw std::runtime_error(std::string("MIX_Init failed: ") + SDL_GetError());
            }

            SDL_AudioSpec spec{};
            spec.format = SDL_AUDIO_S16;
            spec.channels = 2;
            spec.freq = 44100;

            g_mixer = MIX_CreateMixerDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec);
            if (!g_mixer)
            {
                // IN-11: MIX_Init()/MIX_Quit() are reference-counted; without this, every
                // failed retry (e.g. no audio hardware present) leaks another MIX_Init()
                // refcount that's never balanced by a matching MIX_Quit().
                MIX_Quit();
                throw std::runtime_error(std::string("MIX_CreateMixerDevice failed: ") + SDL_GetError());
            }
        }
        return g_mixer;
    }

    void DestroyMixer()
    {
        std::lock_guard<std::mutex> lock(g_mixerMutex);
        if (g_mixer)
        {
            MIX_DestroyMixer(g_mixer);
            g_mixer = nullptr;
            MIX_Quit();
        }
    }
}
#endif
