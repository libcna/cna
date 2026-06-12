// SPDX-License-Identifier: MS-PL
#pragma once

#include "Microsoft/Xna/Framework/Audio/SoundState.hpp"
#include "System/IDisposable.hpp"
#include "System/Object.hpp"
#include "SharpRuntime/SharpRuntimeHelper.hpp"

namespace Microsoft::Xna::Framework::Audio
{
    class AudioEmitter;
    class AudioListener;
    class SoundEffect;

    class SoundEffectInstance : public System::Object, public System::IDisposable
    {
        friend class SoundEffect;

    protected:
        /// Default constructor for use by DynamicSoundEffectInstance.
        SoundEffectInstance();

        // These members are protected so DynamicSoundEffectInstance can manage its own state.
        void* track_        = nullptr;
        bool  playing_      = false;
        SoundState State_   = SoundState::Stopped;

    private:
        const SoundEffect* soundEffect_ = nullptr;
        bool  IsLooped_     = false;
        bool  isDisposed_   = false;
        float Volume_       = 1.0f;
        float Pan_          = 0.0f;
        float Pitch_        = 0.0f;

    public:
        explicit SoundEffectInstance(const SoundEffect& soundEffect);
        ~SoundEffectInstance() override;

        SoundEffectInstance(const SoundEffectInstance&) = delete;
        SoundEffectInstance& operator=(const SoundEffectInstance&) = delete;
        SoundEffectInstance(SoundEffectInstance&& other) noexcept;
        SoundEffectInstance& operator=(SoundEffectInstance&& other) noexcept;

        virtual void Play();
        virtual void Stop();
        void Stop(bool immediate);
        void Pause();
        void Resume();

        /// Releases the sound effect instance.
        void Dispose() override;

        /// Applies 3D spatial audio properties using listener/emitter positions.
        /// SDL3_mixer does not support full 3D audio (Doppler, HRTF, orientation);
        /// this is a distance/pan approximation only.
        void Apply3D(const AudioListener& listener, const AudioEmitter& emitter);

        /// Multi-listener overload; only a single listener is supported (throws for >1).
        void Apply3D(const AudioListener* listeners, int listenerCount, const AudioEmitter& emitter);

        [[nodiscard]] bool getIsDisposedProperty() const;

        [[nodiscard]] float getVolumeProperty() const;
        void setVolumeProperty(const float& volume);
        void setVolumeProperty(float&& volume);

        [[nodiscard]] float getPanProperty() const;
        void setPanProperty(const float& pan);
        void setPanProperty(float&& pan);

        [[nodiscard]] float getPitchProperty() const;
        void setPitchProperty(const float& pitch);
        void setPitchProperty(float&& pitch);

        [[nodiscard]] virtual bool getIsLoopedProperty() const;
        virtual void setIsLoopedProperty(const bool& looped);
        virtual void setIsLoopedProperty(bool&& looped);

        [[nodiscard]] virtual SoundState getStateProperty() const;

        GetTypeNameHPP()
    };
}
