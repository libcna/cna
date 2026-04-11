#pragma once

#include <memory>
#include <string>

#include "SoundEffectI.hpp"
#include "SoundEffectInstance.hpp"

namespace Microsoft::Xna::Framework::Audio {

    /**
     * @brief Represents a loaded sound effect asset.
     *
     * This class mirrors the XNA / MonoGame SoundEffect type.
     * A SoundEffect instance owns or shares the underlying native audio resource
     * and can be used to create playable SoundEffectInstance objects.
     *
     * The class uses shared internal state, so copying SoundEffect objects is cheap.
     */
    class SoundEffect : public SoundEffectI {
        friend class SoundEffectInstance;

    private:
        /**
         * @brief Internal implementation object.
         *
         * Stores platform-specific audio data and hides backend details from the public API.
         */
        class Impl;

        /**
         * @brief Shared pointer to the internal implementation.
         *
         * This makes SoundEffect cheap to copy while sharing the same loaded native audio asset.
         */
        std::shared_ptr<Impl> impl_;

        /**
         * @brief Global master volume for all sound effects.
         *
         * Expected range is from 0.0f to 1.0f.
         */
        static float MasterVolume_;

    private:
        /**
         * @brief Returns the native backend audio handle.
         *
         * This is intended for internal engine use only, for example by SoundEffectInstance.
         *
         * @return Pointer to the native audio object, or nullptr if no native audio is loaded.
         */
        [[nodiscard]] void* getNativeAudioHandle() const;

    public:
        /**
         * @brief Constructs a sound effect by loading it from an asset path.
         *
         * @param assetName Path or identifier of the sound asset to load.
         *
         * @throws std::runtime_error If loading fails when sound support is enabled.
         */
        explicit SoundEffect(const std::string& assetName);

        /**
         * @brief Destroys the sound effect.
         */
        ~SoundEffect() override;

        /**
         * @brief Copy constructor.
         *
         * Copies the SoundEffect as a shared handle to the same internal audio resource.
         */
        SoundEffect(const SoundEffect&) = default;

        /**
         * @brief Copy assignment operator.
         *
         * Assigns the SoundEffect as a shared handle to the same internal audio resource.
         *
         * @return Reference to this object.
         */
        SoundEffect& operator=(const SoundEffect&) = default;

        /**
         * @brief Move constructor.
         */
        SoundEffect(SoundEffect&&) noexcept = default;

        /**
         * @brief Move assignment operator.
         *
         * @return Reference to this object.
         */
        SoundEffect& operator=(SoundEffect&&) noexcept = default;

        /**
         * @brief Gets the global master volume for sound effects.
         *
         * @return Master volume in the range 0.0f to 1.0f.
         */
        [[nodiscard]] static float getMasterVolumeProperty();

        /**
         * @brief Sets the global master volume for sound effects.
         *
         * Values outside the range 0.0f to 1.0f are clamped.
         *
         * @param v New master volume.
         */
        static void setMasterVolumeProperty(const float& v);

        /**
         * @brief Sets the global master volume for sound effects.
         *
         * Values outside the range 0.0f to 1.0f are clamped.
         *
         * @param v New master volume.
         */
        static void setMasterVolumeProperty(float&& v);

        /**
         * @brief Creates a playable sound effect instance from this sound effect.
         *
         * @return A SoundEffectInstance bound to this sound effect.
         */
        [[nodiscard]] SoundEffectInstance CreateInstance() const override;
    };
}