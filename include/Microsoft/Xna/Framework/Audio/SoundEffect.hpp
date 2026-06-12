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
    /// Represents a loaded sound effect asset.
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

        /// Internal constructor that wraps a preloaded Impl.
        explicit SoundEffect(std::shared_ptr<Impl> impl, std::string name = {});

    public:
        /// Constructs a SoundEffect by loading from a file path.
        explicit SoundEffect(const std::string& assetName);

        /// Constructs a SoundEffect from a raw 16-bit PCM buffer.
        SoundEffect(const std::vector<SharpRuntime::bytecs>& buffer,
                    SharpRuntime::intcs sampleRate,
                    AudioChannels channels);

        /// Constructs a SoundEffect from a range within a 16-bit PCM buffer with loop points.
        SoundEffect(const std::vector<SharpRuntime::bytecs>& buffer,
                    SharpRuntime::intcs offset,
                    SharpRuntime::intcs count,
                    SharpRuntime::intcs sampleRate,
                    AudioChannels channels,
                    SharpRuntime::intcs loopStart,
                    SharpRuntime::intcs loopLength);

        ~SoundEffect() override;

        SoundEffect(const SoundEffect&) = default;
        SoundEffect& operator=(const SoundEffect&) = default;
        SoundEffect(SoundEffect&&) noexcept = default;
        SoundEffect& operator=(SoundEffect&&) noexcept = default;

        // --- Properties ---

        /// Gets the duration of the sound effect.
        [[nodiscard]] System::TimeSpan getDurationProperty() const;

        /// Gets whether this instance has been disposed.
        [[nodiscard]] bool getIsDisposedProperty() const;

        /// Gets the display name of the sound effect.
        [[nodiscard]] const std::string& getNameProperty() const;

        /// Sets the display name of the sound effect.
        void setNameProperty(const std::string& value);
        void setNameProperty(std::string&& value);

        // --- Static properties ---

        /// Gets the global master volume for all sound effects. Range [0, 1].
        [[nodiscard]] static float getMasterVolumeProperty();
        /// Sets the global master volume for all sound effects. Clamped to [0, 1].
        static void setMasterVolumeProperty(const float& v);
        static void setMasterVolumeProperty(float&& v);

        /// Gets the distance scaling factor for 3D attenuation.
        /// SDL3_mixer does not implement full 3D audio; this value is used in Apply3D
        /// approximations only.
        [[nodiscard]] static float getDistanceScaleProperty();
        static void setDistanceScaleProperty(float value);

        /// Gets the Doppler effect scale factor.
        /// SDL3_mixer does not implement Doppler effects; this is stored but not applied.
        [[nodiscard]] static float getDopplerScaleProperty();
        static void setDopplerScaleProperty(float value);

        /// Gets the speed of sound used in Doppler calculations.
        /// SDL3_mixer does not implement Doppler effects; this is stored but not applied.
        [[nodiscard]] static float getSpeedOfSoundProperty();
        static void setSpeedOfSoundProperty(float value);

        // --- Methods ---

        /// Creates a playable instance of this sound effect.
        [[nodiscard]] SoundEffectInstance CreateInstance() const override;

        /// Plays the sound effect once at full volume with default pitch and pan.
        bool Play();

        /// Plays the sound effect once with the specified volume, pitch and pan.
        bool Play(float volume, float pitch, float pan);

        /// Releases the underlying audio resource.
        void Dispose() override;

        // --- Static methods ---

        /// Returns the duration of a 16-bit PCM buffer.
        [[nodiscard]] static System::TimeSpan GetSampleDuration(
            SharpRuntime::intcs sizeInBytes,
            SharpRuntime::intcs sampleRate,
            AudioChannels channels);

        /// Returns the byte count for a 16-bit PCM buffer of the given duration.
        [[nodiscard]] static SharpRuntime::intcs GetSampleSizeInBytes(
            System::TimeSpan duration,
            SharpRuntime::intcs sampleRate,
            AudioChannels channels);

        /// Loads a SoundEffect from a WAV stream. The caller owns the returned object.
        [[nodiscard]] static SoundEffect* FromStream(std::istream& stream);
    };
}
