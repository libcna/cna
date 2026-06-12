// SPDX-License-Identifier: MS-PL
#pragma once

#include <string>

#include "Microsoft/Xna/Framework/Audio/AudioStopOptions.hpp"

namespace Microsoft::Xna::Framework::Audio
{
    class AudioEngine;

    /// Represents a named category of sounds managed by an AudioEngine.
    ///
    /// NOTE: Category-level mixing is not available on the SDL3_mixer backend.
    /// Pause/Resume/SetVolume/Stop are accepted without error but have no
    /// audible effect.
    struct AudioCategory
    {
        /// Gets the name of this category.
        [[nodiscard]] const std::string& getNameProperty() const;

        /// Pauses all cues in this category. No-op on SDL3_mixer backend.
        void Pause();

        /// Resumes all cues in this category. No-op on SDL3_mixer backend.
        void Resume();

        /// Sets the volume for this category. No-op on SDL3_mixer backend.
        void SetVolume(float volume);

        /// Stops all cues in this category. No-op on SDL3_mixer backend.
        void Stop(AudioStopOptions options);

        [[nodiscard]] bool Equals(const AudioCategory& other) const;
        [[nodiscard]] int GetHashCode() const;

        bool operator==(const AudioCategory& other) const;
        bool operator!=(const AudioCategory& other) const;

    private:
        friend class AudioEngine;
        AudioCategory(AudioEngine* engine, unsigned short index, std::string name);

        AudioEngine* parent_;
        unsigned short index_;
        std::string name_;
    };
}
