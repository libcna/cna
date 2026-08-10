// SPDX-License-Identifier: MS-PL
#pragma once

#include <string>

#include "System/IEquatable.hpp"
#include "Microsoft/Xna/Framework/Audio/AudioStopOptions.hpp"
#include "SharpRuntime/SharpRuntimeHelper.hpp"

namespace Microsoft::Xna::Framework::Audio
{
    class AudioEngine;

    /**
     * @brief Represents a named category of sounds managed by an AudioEngine.
     *
     * Pause, Resume, Stop, and SetVolume all route to every currently active Cue in
     * this category (see AudioEngine::PauseCategoryInternal / ResumeCategoryInternal /
     * StopCategoryInternal / SetCategoryVolumeInternal) and have a real, immediate
     * effect on playback -- including SetVolume, which retroactively re-applies to
     * cues already playing, not just future Play() calls (T-4D).
     */
    struct AudioCategory : public System::IEquatable<AudioCategory>
    {
        /** @brief Gets the name of this category. */
        [[nodiscard]] const std::string& getNameProperty() const;

        /** @brief Pauses every currently playing Cue in this category. */
        void Pause();

        /** @brief Resumes every currently paused Cue in this category. */
        void Resume();

        /**
         * @brief Sets the baseline volume for this category, applied both to future
         * Play() calls and retroactively to every cue in this category already playing.
         *
         * @param volume New volume level.
         */
        void SetVolume(float volume);

        /**
         * @brief Stops every currently active Cue in this category.
         *
         * @param options Whether to stop immediately or let release phases finish.
         */
        void Stop(AudioStopOptions options);

        /**
         * @brief Returns whether this category has the same name as another.
         *
         * @param other Category to compare with.
         * @return true if both have the same Name; otherwise false.
         */
        [[nodiscard]] bool Equals(const AudioCategory& other) const override;

        /**
         * @brief Returns a hash code based on this category's name.
         *
         * @return Hash code consistent with Equals and operator==.
         */
        [[nodiscard]] int GetHashCode() const;

        /**
         * @brief Returns whether two categories have the same name.
         *
         * @param other Category to compare with.
         * @return true if both have the same Name; otherwise false.
         */
        bool operator==(const AudioCategory& other) const;

        /**
         * @brief Returns whether two categories have different names.
         *
         * @param other Category to compare with.
         * @return true if the Name values differ; otherwise false.
         */
        bool operator!=(const AudioCategory& other) const;

    private:
        friend class AudioEngine;
        AudioCategory(AudioEngine* engine, SharpRuntime::ushortcs index, std::string name);

        AudioEngine* parent_;
        SharpRuntime::ushortcs index_;
        std::string name_;
    };
}
