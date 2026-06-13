// SPDX-License-Identifier: MS-PL
#pragma once

#include <istream>
#include <memory>
#include <string>
#include <vector>

#include "Microsoft/Xna/Framework/Audio/AudioChannels.hpp"
#include "Microsoft/Xna/Framework/Audio/SoundEffectI.hpp"
#include "Microsoft/Xna/Framework/Audio/SoundEffectInstance.hpp"
#include "System/IDisposable.hpp"
#include "System/TimeSpan.hpp"
#include "SharpRuntime/SharpRuntimeHelper.hpp"

namespace Microsoft::Xna::Framework::Audio
{
    /** @brief Represents a loaded sound effect asset. */
    class SoundEffect : public SoundEffectI, public System::IDisposable
    {
        friend class SoundEffectInstance;

    private:
        class Impl;
        std::shared_ptr<Impl> impl_;

        static float MasterVolume_;
        static float DistanceScale_;
        static float DopplerScale_;
        static float SpeedOfSound_;

        std::string name_;
        bool isDisposed_ = false;
        SharpRuntime::uintcs loopStart_ = 0;
        SharpRuntime::uintcs loopLength_ = 0;

        [[nodiscard]] void* getNativeAudioHandle() const;

        /** @brief Internal constructor that wraps a preloaded Impl. */
        explicit SoundEffect(std::shared_ptr<Impl> impl, std::string name = {});

    public:
        /**
         * @brief Constructs a SoundEffect by loading from a file path.
         *
         * @param assetName Path to the audio file (WAV or other supported format).
         */
        explicit SoundEffect(const std::string& assetName);

        /**
         * @brief Constructs a SoundEffect from a raw 16-bit PCM buffer.
         *
         * @param buffer     PCM audio data.
         * @param sampleRate Sample rate in Hz.
         * @param channels   Channel layout (Mono or Stereo).
         */
        SoundEffect(const std::vector<SharpRuntime::bytecs>& buffer,
                    SharpRuntime::intcs sampleRate,
                    AudioChannels channels);

        /**
         * @brief Constructs a SoundEffect from a range within a PCM buffer with loop points.
         *
         * @param buffer      PCM audio data.
         * @param offset      Byte offset into the buffer.
         * @param count       Number of bytes to use.
         * @param sampleRate  Sample rate in Hz.
         * @param channels    Channel layout (Mono or Stereo).
         * @param loopStart   Sample index where the loop begins.
         * @param loopLength  Number of samples in the loop region.
         */
        SoundEffect(const std::vector<SharpRuntime::bytecs>& buffer,
                    SharpRuntime::intcs offset,
                    SharpRuntime::intcs count,
                    SharpRuntime::intcs sampleRate,
                    AudioChannels channels,
                    SharpRuntime::intcs loopStart,
                    SharpRuntime::intcs loopLength);

        /** @brief Destroys the sound effect and releases audio resources. */
        ~SoundEffect() override;

        SoundEffect(const SoundEffect&) = default;
        SoundEffect& operator=(const SoundEffect&) = default;
        SoundEffect(SoundEffect&&) noexcept = default;
        SoundEffect& operator=(SoundEffect&&) noexcept = default;

        // --- Properties ---

        /**
         * @brief Gets the playback duration of this sound effect.
         *
         * @return Duration as a TimeSpan.
         */
        [[nodiscard]] System::TimeSpan getDurationProperty() const;

        /**
         * @brief Gets whether this sound effect has been disposed.
         *
         * @return true if disposed; otherwise false.
         */
        [[nodiscard]] bool getIsDisposedProperty() const;

        /**
         * @brief Gets the display name of this sound effect.
         *
         * @return Name string.
         */
        [[nodiscard]] const std::string& getNameProperty() const;

        /**
         * @brief Sets the display name of this sound effect.
         *
         * @param value New name.
         */
        void setNameProperty(const std::string& value);

        /** @brief Sets the display name of this sound effect (move overload). */
        void setNameProperty(std::string&& value);

        // --- Static properties ---

        /**
         * @brief Gets the global master volume applied to all sound effects. Range [0, 1].
         *
         * @return Master volume.
         */
        [[nodiscard]] static float getMasterVolumeProperty();

        /**
         * @brief Sets the global master volume applied to all sound effects. Clamped to [0, 1].
         *
         * @param v New master volume.
         */
        static void setMasterVolumeProperty(const float& v);

        /** @brief Sets the global master volume (move overload). */
        static void setMasterVolumeProperty(float&& v);

        /**
         * @brief Gets the distance scaling factor used in Apply3D attenuation approximations.
         *
         * SDL3_mixer does not implement full 3D audio; this value is used in Apply3D only.
         *
         * @return Distance scale factor.
         */
        [[nodiscard]] static float getDistanceScaleProperty();

        /**
         * @brief Sets the distance scaling factor used in Apply3D attenuation approximations.
         *
         * @param value New distance scale.
         */
        static void setDistanceScaleProperty(float value);

        /**
         * @brief Gets the Doppler effect scale factor.
         *
         * SDL3_mixer does not implement Doppler; this value is stored but not applied.
         *
         * @return Doppler scale factor.
         */
        [[nodiscard]] static float getDopplerScaleProperty();

        /**
         * @brief Sets the Doppler effect scale factor.
         *
         * @param value New Doppler scale.
         */
        static void setDopplerScaleProperty(float value);

        /**
         * @brief Gets the speed of sound used in Doppler calculations.
         *
         * SDL3_mixer does not implement Doppler; this value is stored but not applied.
         *
         * @return Speed of sound in units per second.
         */
        [[nodiscard]] static float getSpeedOfSoundProperty();

        /**
         * @brief Sets the speed of sound used in Doppler calculations.
         *
         * @param value New speed of sound.
         */
        static void setSpeedOfSoundProperty(float value);

        // --- Methods ---

        /**
         * @brief Creates a new SoundEffectInstance for this sound effect.
         *
         * @return A new SoundEffectInstance bound to this effect.
         */
        [[nodiscard]] SoundEffectInstance CreateInstance() const override;

        /**
         * @brief Plays the sound effect once at full volume with default pitch and pan.
         *
         * @return true if the sound started playing; false if the instance limit was reached.
         */
        bool Play();

        /**
         * @brief Plays the sound effect once with explicit volume, pitch, and pan.
         *
         * @param volume Volume in the range [0, 1].
         * @param pitch  Pitch adjustment in the range [-1, 1].
         * @param pan    Pan in the range [-1 (left), 1 (right)].
         * @return true if the sound started playing; false if the instance limit was reached.
         */
        bool Play(float volume, float pitch, float pan);

        /** @brief Releases the underlying audio resource. */
        void Dispose() override;

        // --- Static methods ---

        /**
         * @brief Returns the playback duration of a 16-bit PCM buffer.
         *
         * @param sizeInBytes Number of PCM data bytes.
         * @param sampleRate  Sample rate in Hz.
         * @param channels    Channel layout (Mono or Stereo).
         * @return Duration as a TimeSpan.
         */
        [[nodiscard]] static System::TimeSpan GetSampleDuration(
            SharpRuntime::intcs sizeInBytes,
            SharpRuntime::intcs sampleRate,
            AudioChannels channels);

        /**
         * @brief Returns the byte count for a 16-bit PCM buffer of the given duration.
         *
         * @param duration   Desired playback duration.
         * @param sampleRate Sample rate in Hz.
         * @param channels   Channel layout (Mono or Stereo).
         * @return Number of bytes required.
         */
        [[nodiscard]] static SharpRuntime::intcs GetSampleSizeInBytes(
            System::TimeSpan duration,
            SharpRuntime::intcs sampleRate,
            AudioChannels channels);

        /**
         * @brief Loads a SoundEffect from a WAV stream. The caller owns the returned object.
         *
         * @param stream Input stream containing WAV audio data.
         * @return Pointer to the newly created SoundEffect.
         */
        [[nodiscard]] static SoundEffect* FromStream(std::istream& stream);
    };
}
