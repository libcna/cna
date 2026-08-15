// SPDX-License-Identifier: MS-PL
#pragma once

#include <cstddef>
#include <istream>
#include <memory>
#include <string>
#include <vector>

#include "CNA/CNAHelper.hpp"
#include "Microsoft/Xna/Framework/Audio/AudioChannels.hpp"
#include "Microsoft/Xna/Framework/Audio/SoundEffectInstance.hpp"
#include "System/IDisposable.hpp"
#include "System/Object.hpp"
#include "System/TimeSpan.hpp"
#include "SharpRuntime/SharpRuntimeHelper.hpp"

namespace Microsoft::Xna::Framework::Audio
{
    /** @brief Represents a loaded sound effect asset. */
    class SoundEffect final : public System::Object, public System::IDisposable
    {
        friend class SoundEffectInstance;
        CNAEXT friend struct SoundEffectTestAccess;

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

        // Instance-tracking + Dispose cascade (T-3G, matches FNA's SoundEffect.Instances):
        // SoundEffectInstance registers itself here on construction against the type-erased
        // keep-alive it holds (see SoundEffectInstance::soundEffectKeepAlive_), unregisters on
        // Dispose(), and re-points registration when moved (its own address changes). Static and
        // keyed by the keep-alive pointer, not a SoundEffect&, because an instance must be able
        // to unregister itself long after the original SoundEffect object is gone.
        static void RegisterInstance(const std::shared_ptr<void>& keepAlive, SoundEffectInstance* instance);
        static void UnregisterInstance(const std::shared_ptr<void>& keepAlive, SoundEffectInstance* instance);

        // AUD-15-005: test-only introspection into Impl::instances' real size, so a stress test
        // can directly verify the live-instance registry actually shrinks back down instead of
        // only inferring it indirectly (e.g. via wall-clock timing, which turned out not to
        // reliably catch a deliberately-broken UnregisterInstance() at a few thousand entries).
        [[nodiscard]] std::size_t GetLiveInstanceCountInternal() const;

    public:
        /**
         * @brief Constructs a SoundEffect by loading from a file path.
         *
         * @param assetName Path to the audio file (WAV or other supported format).
         */
        CNAEXT explicit SoundEffect(const std::string& assetName);

        /**
         * @brief Constructs a SoundEffect from a raw 16-bit PCM buffer.
         *
         * AUD-05-005: `buffer` must be headerless, little-endian, signed 16-bit PCM samples --
         * NOT a WAV/RIFF file, not Ogg/MP3-compressed data, and not an XNB asset. Passing an
         * entire file's bytes here (rather than just its raw sample data) is a common mistake:
         * the leading container header (e.g. a WAV `RIFF`/`WAVE`/`fmt ` chunk) is misread as
         * audio samples, producing loud noise or silence rather than a clean failure. Use
         * `SoundEffect(const std::string&)` (the file-path constructor) to load a real audio
         * file, including WAV, instead.
         *
         * @param buffer     Raw signed 16-bit PCM sample data (no container/file header).
         * @param sampleRate Sample rate in Hz.
         * @param channels   Channel layout (Mono or Stereo).
         */
        SoundEffect(const std::vector<SharpRuntime::bytecs>& buffer,
                    SharpRuntime::intcs sampleRate,
                    AudioChannels channels);

        /**
         * @brief Constructs a SoundEffect from a range within a PCM buffer with loop points.
         *
         * AUD-05-005: see the other raw-buffer constructor's doc comment -- `buffer` (or rather,
         * the `[offset, offset + count)` range within it) must be headerless, little-endian,
         * signed 16-bit PCM samples, not a WAV/container file's raw bytes.
         *
         * @param buffer      Raw signed 16-bit PCM sample data (no container/file header).
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

        /**
         * @brief SoundEffect is move-only (T-3G): matching FNA's single-object instance
         * tracking (SoundEffect::Dispose() cascades to every live SoundEffectInstance created
         * via CreateInstance()) requires a single, unambiguous owner per underlying resource --
         * two independent copies could otherwise disagree about which one's Dispose() call
         * is authoritative.
         */
        SoundEffect(const SoundEffect&) = delete;

        /** @brief Deleted; see the copy constructor's rationale. */
        SoundEffect& operator=(const SoundEffect&) = delete;

        /** @brief Move-constructs a SoundEffect, transferring ownership of the underlying resource. */
        SoundEffect(SoundEffect&&) noexcept = default;

        /** @brief Move-assigns a SoundEffect, transferring ownership of the underlying resource. */
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
        CNAEXT void setNameProperty(std::string&& value);

        // --- Static properties ---

        /**
         * @brief Gets the global master volume applied to all sound effects. Range [0, 1].
         *
         * @return Master volume.
         */
        [[nodiscard]] static float getMasterVolumeProperty();

        /**
         * @brief Sets the global master volume applied to all sound effects.
         *
         * Values are passed through unclamped (matching FNA).
         *
         * @param v New master volume.
         */
        static void setMasterVolumeProperty(const float& v);

        /** @brief Sets the global master volume (move overload). */
        CNAEXT static void setMasterVolumeProperty(float&& v);

        /**
         * @brief Gets the distance scaling factor used in Apply3D attenuation approximations.
         *
         * The current mixer engine does not implement full 3D audio; Apply3D uses this value.
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
         * Applied as a real closed-form pitch-shift factor in Apply3D (matches FAudio's
         * F3DAudio.c CalculateDoppler exactly), not a native mixer Doppler feature.
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
         * Applied as a real closed-form pitch-shift factor in Apply3D (matches FAudio's
         * F3DAudio.c CalculateDoppler exactly), not a native mixer Doppler feature.
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
        [[nodiscard]] SoundEffectInstance CreateInstance() const;

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
         * @param pitch  Pitch adjustment; clamped to [-1, 1].
         * @param pan    Pan in the range [-1 (left), 1 (right)].
         * @return true if the sound started playing; false if the instance limit was reached.
         * @throws System::ArgumentOutOfRangeException if @p pan is outside [-1, 1].
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

        GetTypeNameHPP()
    };
}
