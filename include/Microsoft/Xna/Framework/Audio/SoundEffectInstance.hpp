// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/CNAHelper.hpp"
#include "Microsoft/Xna/Framework/Audio/SoundState.hpp"
#include "System/IDisposable.hpp"
#include "System/Object.hpp"
#include "SharpRuntime/SharpRuntimeHelper.hpp"

namespace Microsoft::Xna::Framework::Audio
{
    class AudioEmitter;
    class AudioListener;
    class SoundEffect;

    /** @brief Controls playback of a sound effect instance, including volume, pitch, pan, and looping. */
    class SoundEffectInstance : public System::Object, public System::IDisposable
    {
        friend class SoundEffect;
        // Tests need read access to the underlying MIX_Track handle to verify Play() idempotency
        // (that a repeated call while already playing doesn't restart the track).
        NOXNA friend struct SoundEffectInstanceTestAccess;

    protected:
        /** @brief Default constructor for use by DynamicSoundEffectInstance. */
        SoundEffectInstance();

        // These members are protected so DynamicSoundEffectInstance can manage its own state.
        void* track_        = nullptr;
        bool  playing_      = false;
        bool  hasStarted_   = false; // true once Play() has been called; never reset (gates IsLooped)
        SoundState State_   = SoundState::Stopped;

    private:
        const SoundEffect* soundEffect_ = nullptr;
        bool  IsLooped_     = false;
        bool  isDisposed_   = false;
        float Volume_       = 1.0f;
        float Pan_          = 0.0f;
        float Pitch_        = 0.0f;

    public:
        /**
         * @brief Constructs a SoundEffectInstance bound to the given sound effect.
         *
         * @param soundEffect The sound effect to bind this instance to.
         */
        explicit SoundEffectInstance(const SoundEffect& soundEffect);

        /** @brief Destroys the instance and releases its audio track. */
        ~SoundEffectInstance() override;

        SoundEffectInstance(const SoundEffectInstance&) = delete;
        SoundEffectInstance& operator=(const SoundEffectInstance&) = delete;

        /** @brief Move-constructs a SoundEffectInstance, transferring ownership of the audio track. */
        NOXNA SoundEffectInstance(SoundEffectInstance&& other) noexcept;

        /** @brief Move-assigns a SoundEffectInstance, transferring ownership of the audio track. */
        NOXNA SoundEffectInstance& operator=(SoundEffectInstance&& other) noexcept;

        /** @brief Starts or resumes playback of this instance. */
        virtual void Play();

        /** @brief Stops playback of this instance immediately. */
        virtual void Stop();

        /**
         * @brief Stops playback of this instance.
         *
         * @param immediate If true, cuts off immediately; if false, allows release tails.
         */
        void Stop(bool immediate);

        /** @brief Pauses playback of this instance. */
        void Pause();

        /** @brief Resumes a paused instance. */
        void Resume();

        /** @brief Releases this sound effect instance. */
        void Dispose() override;

        /**
         * @brief Applies 3D spatial audio properties using listener and emitter positions.
         *
         * SDL3_mixer does not support full 3D audio; this is a distance and pan approximation
         * applied directly to the underlying track. It does not modify the Volume or Pan
         * properties, which continue to report only what was last set through their setters.
         *
         * @param listener Position and orientation of the audio listener.
         * @param emitter  Position and orientation of the sound emitter.
         * @throws System::ObjectDisposedException if the instance has been disposed.
         */
        void Apply3D(const AudioListener& listener, const AudioEmitter& emitter);

        /**
         * @brief Multi-listener overload; only a single listener is supported.
         *
         * @param listeners     Array of listener descriptions.
         * @param listenerCount Number of listeners (must be 1).
         * @param emitter       Position and orientation of the sound emitter.
         * @throws System::ArgumentNullException if @p listeners is null.
         * @throws System::NotSupportedException if @p listenerCount is not 1.
         */
        void Apply3D(const AudioListener* listeners, int listenerCount, const AudioEmitter& emitter);

        /**
         * @brief Gets whether this instance has been disposed.
         *
         * @return true if disposed; otherwise false.
         */
        [[nodiscard]] virtual bool getIsDisposedProperty() const;

        /**
         * @brief Gets the playback volume. Range [0, 1].
         *
         * @return Current volume.
         */
        [[nodiscard]] float getVolumeProperty() const;

        /**
         * @brief Sets the playback volume. Values are passed through unclamped (matching FNA).
         *
         * @param volume New volume value.
         */
        void setVolumeProperty(const float& volume);

        /** @brief Sets the playback volume (move overload). */
        NOXNA void setVolumeProperty(float&& volume);

        /**
         * @brief Gets the stereo pan. Range [-1 (left), 1 (right)].
         *
         * @return Current pan value.
         */
        [[nodiscard]] float getPanProperty() const;

        /**
         * @brief Sets the stereo pan. Range [-1 (left), 1 (right)].
         *
         * @param pan New pan value.
         * @throws System::ObjectDisposedException if the instance has been disposed.
         * @throws System::ArgumentOutOfRangeException if @p pan is outside [-1, 1].
         */
        void setPanProperty(const float& pan);

        /** @brief Sets the stereo pan (move overload). */
        NOXNA void setPanProperty(float&& pan);

        /**
         * @brief Gets the pitch adjustment. Range [-1, 1].
         *
         * @return Current pitch adjustment.
         */
        [[nodiscard]] float getPitchProperty() const;

        /**
         * @brief Sets the pitch adjustment. Range [-1, 1].
         *
         * @param pitch New pitch value.
         */
        void setPitchProperty(const float& pitch);

        /** @brief Sets the pitch adjustment (move overload). */
        NOXNA void setPitchProperty(float&& pitch);

        /**
         * @brief Gets whether the sound loops continuously.
         *
         * @return true if looping; otherwise false.
         */
        [[nodiscard]] virtual bool getIsLoopedProperty() const;

        /**
         * @brief Sets whether the sound loops continuously.
         *
         * @param looped New loop flag.
         * @throws System::InvalidOperationException if the instance has already been played.
         */
        virtual void setIsLoopedProperty(const bool& looped);

        /** @brief Sets whether the sound loops (move overload). */
        NOXNA virtual void setIsLoopedProperty(bool&& looped);

        /**
         * @brief Gets the current playback state.
         *
         * @return Current SoundState.
         */
        [[nodiscard]] virtual SoundState getStateProperty() const;

        GetTypeNameHPP()
    };
}
