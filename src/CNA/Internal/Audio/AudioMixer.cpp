#include "CNA/Internal/Audio/AudioMixer.hpp"

#ifdef SOUND_ENABLED
#include <stdexcept>
#include <string>

namespace CNA::Internal::Audio
{
    namespace
    {
        MIX_Mixer* g_mixer = nullptr;
    }

    MIX_Mixer* GetMixer()
    {
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
                throw std::runtime_error(std::string("MIX_CreateMixerDevice failed: ") + SDL_GetError());
            }
        }
        return g_mixer;
    }

    void DestroyMixer()
    {
        if (g_mixer)
        {
            MIX_DestroyMixer(g_mixer);
            g_mixer = nullptr;
            MIX_Quit();
        }
    }
}
#endif
