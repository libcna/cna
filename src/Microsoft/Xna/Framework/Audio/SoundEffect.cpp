#include "Microsoft/Xna/Framework/Audio/SoundEffect.hpp"

#include <stdexcept>

#ifdef SOUND_ENABLED
#include <SDL3/SDL.h>
#include <SDL3_mixer/SDL_mixer.h>
#endif

namespace Microsoft::Xna::Framework::Audio
{
#ifdef SOUND_ENABLED
    namespace
    {
        MIX_Mixer* g_mixer = nullptr;

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
    }
#endif
    class SoundEffect::Impl
    {
    public:
#ifdef SOUND_ENABLED
        std::shared_ptr<MIX_Audio> audio{};
#endif
    };

    float SoundEffect::MasterVolume_ = 1.0f;

    float SoundEffect::getMasterVolumeProperty()
    {
        return MasterVolume_;
    }

    void SoundEffect::setMasterVolumeProperty(const float& v)
    {
        MasterVolume_ = (v < 0.0f) ? 0.0f : ((v > 1.0f) ? 1.0f : v);
    }

    void SoundEffect::setMasterVolumeProperty(float&& v)
    {
        setMasterVolumeProperty(v);
    }

    SoundEffect::SoundEffect(const std::string& assetName)
        : impl_(std::make_shared<Impl>())
    {
        if (assetName.empty()) return;
#ifdef SOUND_ENABLED
        MIX_Mixer* mixer = GetMixer();

        MIX_Audio* rawAudio = MIX_LoadAudio(mixer, assetName.c_str(), true);

        if (!rawAudio)
        {
            throw std::runtime_error(
                "Failed to load sound: " + assetName + " Error: " + SDL_GetError()
            );
        }

        impl_->audio = std::shared_ptr<MIX_Audio>(
            rawAudio,
            [](MIX_Audio* ptr)
            {
                if (ptr)
                {
                    MIX_DestroyAudio(ptr);
                }
            }
        );
#else
        (void)assetName;
#endif
    }

    SoundEffect::~SoundEffect() = default;

    void* SoundEffect::getNativeAudioHandle() const
    {
#ifdef SOUND_ENABLED
        if (!impl_ || !impl_->audio)
        {
            return nullptr;
        }
        return impl_->audio.get();
#else
        return nullptr;
#endif
    }

    SoundEffectInstance SoundEffect::CreateInstance() const
    {
        return SoundEffectInstance(*this);
    }
}
