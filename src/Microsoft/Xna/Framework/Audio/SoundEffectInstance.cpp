#include "Microsoft/Xna/Framework/Audio/SoundEffectInstance.hpp"
#include "Microsoft/Xna/Framework/Audio/SoundEffect.hpp"

#ifdef SOUND_ENABLED
#include <SDL3_mixer/SDL_mixer.h>
#endif

namespace Microsoft::Xna::Framework::Audio {

    SoundEffectInstance::SoundEffectInstance(const SoundEffect& soundEffect)
        : soundEffect_(&soundEffect)
    {
    }

    SoundEffectInstance::~SoundEffectInstance()
    {
        Stop();
    }

    void SoundEffectInstance::Play()
    {
#ifdef SOUND_ENABLED
        if (!soundEffect_) {
            State_ = SoundState::Stopped;
            playing_ = false;
            channel_ = -1;
            return;
        }

        auto* chunk = static_cast<Mix_Chunk*>(soundEffect_->getNativeChunkHandle());
        if (!chunk) {
            State_ = SoundState::Stopped;
            playing_ = false;
            channel_ = -1;
            return;
        }

        if (playing_) {
            return;
        }

        const int loops = IsLooped_ ? -1 : 0;
        channel_ = Mix_PlayChannel(-1, chunk, loops);
        if (channel_ == -1) {
            State_ = SoundState::Stopped;
            playing_ = false;
            channel_ = -1;
            return;
        }

        const float effectiveVolume = Volume_ * SoundEffect::getMasterVolumeProperty();
        Mix_Volume(channel_, static_cast<int>(effectiveVolume * 128.0f));

        Uint8 left = 255;
        Uint8 right = 255;

        if (Pan_ < 0.0f) {
            right = static_cast<Uint8>(255.0f * (1.0f + Pan_));
        } else if (Pan_ > 0.0f) {
            left = static_cast<Uint8>(255.0f * (1.0f - Pan_));
        }

        Mix_SetPanning(channel_, left, right);

        playing_ = true;
        State_ = SoundState::Playing;
#else
        State_ = SoundState::Stopped;
        playing_ = false;
        channel_ = -1;
#endif
    }

    void SoundEffectInstance::Stop()
    {
#ifdef SOUND_ENABLED
        if (channel_ != -1) {
            Mix_HaltChannel(channel_);
        }
#endif
        channel_ = -1;
        playing_ = false;
        State_ = SoundState::Stopped;
    }

    void SoundEffectInstance::Pause()
    {
#ifdef SOUND_ENABLED
        if (channel_ != -1 && playing_ && State_ == SoundState::Playing) {
            Mix_Pause(channel_);
            State_ = SoundState::Paused;
        }
#endif
    }

    void SoundEffectInstance::Resume()
    {
#ifdef SOUND_ENABLED
        if (channel_ != -1 && State_ == SoundState::Paused) {
            Mix_Resume(channel_);
            State_ = SoundState::Playing;
        }
#endif
    }

    float SoundEffectInstance::getVolumeProperty() const
    {
        return Volume_;
    }

    void SoundEffectInstance::setVolumeProperty(const float& volume)
    {
        Volume_ = (volume < 0.0f) ? 0.0f : ((volume > 1.0f) ? 1.0f : volume);

#ifdef SOUND_ENABLED
        if (channel_ != -1) {
            const float effectiveVolume = Volume_ * SoundEffect::getMasterVolumeProperty();
            Mix_Volume(channel_, static_cast<int>(effectiveVolume * 128.0f));
        }
#endif
    }

    void SoundEffectInstance::setVolumeProperty(float&& volume)
    {
        setVolumeProperty(volume);
    }

    float SoundEffectInstance::getPanProperty() const
    {
        return Pan_;
    }

    void SoundEffectInstance::setPanProperty(const float& pan)
    {
        Pan_ = (pan < -1.0f) ? -1.0f : ((pan > 1.0f) ? 1.0f : pan);

#ifdef SOUND_ENABLED
        if (channel_ != -1) {
            Uint8 left = 255;
            Uint8 right = 255;

            if (Pan_ < 0.0f) {
                right = static_cast<Uint8>(255.0f * (1.0f + Pan_));
            } else if (Pan_ > 0.0f) {
                left = static_cast<Uint8>(255.0f * (1.0f - Pan_));
            }

            Mix_SetPanning(channel_, left, right);
        }
#endif
    }

    void SoundEffectInstance::setPanProperty(float&& pan)
    {
        setPanProperty(pan);
    }

    float SoundEffectInstance::getPitchProperty() const
    {
        return Pitch_;
    }

    void SoundEffectInstance::setPitchProperty(const float& pitch)
    {
        Pitch_ = (pitch < -1.0f) ? -1.0f : ((pitch > 1.0f) ? 1.0f : pitch);
    }

    void SoundEffectInstance::setPitchProperty(float&& pitch)
    {
        setPitchProperty(pitch);
    }

    bool SoundEffectInstance::getIsLoopedProperty() const
    {
        return IsLooped_;
    }

    void SoundEffectInstance::setIsLoopedProperty(const bool& looped)
    {
        IsLooped_ = looped;

        if (playing_) {
            Stop();
            Play();
        }
    }

    void SoundEffectInstance::setIsLoopedProperty(bool&& looped)
    {
        setIsLoopedProperty(looped);
    }

    SoundState SoundEffectInstance::getStateProperty() const
    {
#ifdef SOUND_ENABLED
        if (channel_ != -1 && State_ == SoundState::Playing) {
            if (Mix_Playing(channel_) == 0) {
                return SoundState::Stopped;
            }
        }
#endif
        return State_;
    }

}