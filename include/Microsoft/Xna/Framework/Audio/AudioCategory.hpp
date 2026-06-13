// SPDX-License-Identifier: MS-PL
#pragma once

#include <string>

#include "System/IEquatable.hpp"
#include "Microsoft/Xna/Framework/Audio/AudioStopOptions.hpp"

namespace Microsoft::Xna::Framework::Audio
{
    class AudioEngine;

    /**
     * @brief Represents a named category of sounds managed by an AudioEngine.
     *
     * Category-level mixing is not available on the SDL3_mixer backend.
     * Pause, Resume, SetVolume, and Stop are accepted without error but have no
     * audible effect on that backend.
     */
    struct AudioCategory : public System::IEquatable<AudioCategory>
    {
        /** @brief Gets the name of this category. */
        [[nodiscard]] const std::string& getNameProperty() const;

        /** @brief Pauses all cues in this category. No-op on SDL3_mixer backend. */
        void Pause();

        /** @brief Resumes all cues in this category. No-op on SDL3_mixer backend. */
        void Resume();

        /**
         * @brief Sets the volume for this category. No-op on SDL3_mixer backend.
         *
         * @param volume New volume level.
         */
        void SetVolume(float volume);

        /**
         * @brief Stops all cues in this category.
         *
         * @param options Whether to stop immediately or let release phases finish.
         */
        void Stop(AudioStopOptions options);

        /**
         * @brief Returns whether this category is equal to another.
         *
         * @param other Category to compare with.
         * @return true if equal; otherwise false.
         */
        [[nodiscard]] bool Equals(const AudioCategory& other) const override;

        /** @brief Returns a hash code for this category. */
        [[nodiscard]] int GetHashCode() const;

        /** @brief Returns whether two categories are equal. */
        bool operator==(const AudioCategory& other) const;

        /** @brief Returns whether two categories are not equal. */
        bool operator!=(const AudioCategory& other) const;

    private:
        friend class AudioEngine;
        AudioCategory(AudioEngine* engine, unsigned short index, std::string name);

        AudioEngine* parent_;
        unsigned short index_;
        std::string name_;
    };
}
