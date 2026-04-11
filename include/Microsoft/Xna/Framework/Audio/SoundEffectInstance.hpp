#pragma once

#include "SoundState.hpp"

namespace Microsoft::Xna::Framework::Audio {
    class SoundEffect;

    class SoundEffectInstance {
    private:
        const SoundEffect* soundEffect_ = nullptr;
        void* track_ = nullptr;
        bool playing_ = false;
        float Volume_ = 1.0f;
        float Pan_ = 0.0f;
        float Pitch_ = 0.0f;
        bool IsLooped_ = false;
        SoundState State_ = SoundState::Stopped;

    public:
        explicit SoundEffectInstance(const SoundEffect& soundEffect);
        ~SoundEffectInstance();

        SoundEffectInstance(const SoundEffectInstance&) = delete;
        SoundEffectInstance& operator=(const SoundEffectInstance&) = delete;
        SoundEffectInstance(SoundEffectInstance&& other) noexcept;
        SoundEffectInstance& operator=(SoundEffectInstance&& other) noexcept;

        void Play();
        void Stop();
        void Pause();
        void Resume();

        [[nodiscard]] float getVolumeProperty() const;
        void setVolumeProperty(const float& volume);
        void setVolumeProperty(float&& volume);

        [[nodiscard]] float getPanProperty() const;
        void setPanProperty(const float& pan);
        void setPanProperty(float&& pan);

        [[nodiscard]] float getPitchProperty() const;
        void setPitchProperty(const float& pitch);
        void setPitchProperty(float&& pitch);

        [[nodiscard]] bool getIsLoopedProperty() const;
        void setIsLoopedProperty(const bool& looped);
        void setIsLoopedProperty(bool&& looped);

        [[nodiscard]] SoundState getStateProperty() const;
    };
}