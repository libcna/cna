#include "Microsoft/Xna/Framework/Audio/SoundEffect.hpp"

#include <stdexcept>

#ifdef SOUND_ENABLED
#include <SDL3/SDL.h>
#include <SDL3_mixer/SDL_mixer.h>
#endif

namespace Microsoft::Xna::Framework::Audio {

    class SoundEffect::Impl {
    public:
#ifdef SOUND_ENABLED
        std::shared_ptr<Mix_Chunk> chunk{};
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
#ifdef SOUND_ENABLED
        Mix_Chunk* rawChunk = Mix_LoadWAV(assetName.c_str());
        if (!rawChunk) {
            throw std::runtime_error("Failed to load sound: " + assetName + " Error: " + SDL_GetError());
        }

        impl_->chunk = std::shared_ptr<Mix_Chunk>(
            rawChunk,
            [](Mix_Chunk* ptr) {
                if (ptr) {
                    Mix_FreeChunk(ptr);
                }
            });
#else
        (void)assetName;
#endif
    }

    SoundEffect::~SoundEffect() = default;

    void* SoundEffect::getNativeChunkHandle() const
    {
#ifdef SOUND_ENABLED
        if (!impl_ || !impl_->chunk) {
            return nullptr;
        }
        return impl_->chunk.get();
#else
        return nullptr;
#endif
    }

    SoundEffectInstance SoundEffect::CreateInstance() const
    {
        return SoundEffectInstance(*this);
    }
}