// SPDX-License-Identifier: MS-PL
#pragma once

#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "CNA/CNAHelper.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/Audio/AudioContent.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/ContentProcessor.hpp"
#include "SharpRuntime/SharpRuntimeHelper.hpp"
#include "System/Object.hpp"

namespace Microsoft::Xna::Framework::Content::Pipeline::Processors
{
    /**
     * @brief A processed sound effect, ready for the writer.
     *
     * XNA declares no public member on this type; what it carries is the converted audio, which
     * CNA exposes through CNAEXT accessors so its writer can reach it.
     */
    class SoundEffectContent final : public System::Object
    {
    public:
        /** @brief .NET full name of this type. */
        CNAEXT static constexpr std::string_view XnaTypeName =
            "Microsoft.Xna.Framework.Content.Pipeline.Processors.SoundEffectContent";

        /** @brief Initializes empty content, which is what a default-constructed one carries. */
        CNAEXT SoundEffectContent() = default;

        /**
         * @brief Initializes the content from a converted audio.
         *
         * @param format The encoding the samples are in.
         * @param data The samples.
         * @param loopStart The first frame of the loop region.
         * @param loopLength The frames the loop region spans.
         * @param duration How long the sound plays for.
         */
        CNAEXT SoundEffectContent(std::shared_ptr<Audio::AudioFormat> format,
                                  std::vector<SharpRuntime::bytecs> data, SharpRuntime::intcs loopStart,
                                  SharpRuntime::intcs loopLength, System::TimeSpan duration);

        /** @brief The encoding the samples are in. */
        CNAEXT [[nodiscard]] const std::shared_ptr<Audio::AudioFormat>& Format() const noexcept;

        /** @brief The samples. */
        CNAEXT [[nodiscard]] const std::vector<SharpRuntime::bytecs>& Data() const noexcept;

        /** @brief The first frame of the loop region. */
        CNAEXT [[nodiscard]] SharpRuntime::intcs LoopStart() const noexcept;

        /** @brief The frames the loop region spans. */
        CNAEXT [[nodiscard]] SharpRuntime::intcs LoopLength() const noexcept;

        /** @brief How long the sound plays for. */
        CNAEXT [[nodiscard]] System::TimeSpan Duration() const noexcept;

        /** @brief Returns the type's stable name. */
        CNAEXT [[nodiscard]] const std::string& GetTypeName() const override;

    private:
        std::shared_ptr<Audio::AudioFormat> format_;
        std::vector<SharpRuntime::bytecs> data_;
        SharpRuntime::intcs loopStart_ = 0;
        SharpRuntime::intcs loopLength_ = 0;
        System::TimeSpan duration_;
    };

    /**
     * @brief A processed song: the file it was written to and how long it plays.
     *
     * XNA declares no public member on this type either.
     */
    class SongContent final : public System::Object
    {
    public:
        /** @brief .NET full name of this type. */
        CNAEXT static constexpr std::string_view XnaTypeName =
            "Microsoft.Xna.Framework.Content.Pipeline.Processors.SongContent";

        /** @brief Initializes empty content. */
        CNAEXT SongContent() = default;

        /**
         * @brief Initializes the content from a written song file.
         *
         * @param fileName The file the song was written to, relative to the output directory.
         * @param duration How long the song plays for.
         */
        CNAEXT SongContent(std::string fileName, System::TimeSpan duration);

        /** @brief The file the song was written to. */
        CNAEXT [[nodiscard]] const std::string& FileName() const noexcept;

        /** @brief How long the song plays for. */
        CNAEXT [[nodiscard]] System::TimeSpan Duration() const noexcept;

        /** @brief Returns the type's stable name. */
        CNAEXT [[nodiscard]] const std::string& GetTypeName() const override;

    private:
        std::string fileName_;
        System::TimeSpan duration_;
    };

    /**
     * @brief Converts an audio source into the sound effect a game plays.
     */
    class SoundEffectProcessor
        : public ContentProcessor<std::shared_ptr<Audio::AudioContent>, std::shared_ptr<SoundEffectContent>>
    {
    public:
        /** @brief .NET full name of this type. */
        CNAEXT static constexpr std::string_view XnaTypeName =
            "Microsoft.Xna.Framework.Content.Pipeline.Processors.SoundEffectProcessor";

        /** @brief Initializes a processor with XNA's own defaults. */
        SoundEffectProcessor() = default;

        /**
         * @brief Gets how much of the source the conversion keeps.
         *
         * @return The quality.
         */
        [[nodiscard]] Audio::ConversionQuality getQualityProperty() const noexcept;

        /**
         * @brief Sets how much of the source the conversion keeps.
         *
         * @param value The quality.
         */
        void setQualityProperty(Audio::ConversionQuality value) noexcept;

        /**
         * @brief Converts the audio and answers the sound effect.
         *
         * @param input The audio to convert; it is converted in place.
         * @param context The processor context.
         * @return The sound effect.
         * @throws System::ArgumentNullException when the input is null.
         */
        [[nodiscard]] std::shared_ptr<SoundEffectContent> Process(
            const std::shared_ptr<Audio::AudioContent>& input, ContentProcessorContext& context) override;

        /** @brief Returns the type's stable name. */
        CNAEXT [[nodiscard]] const std::string& GetTypeName() const;

    private:
        Audio::ConversionQuality quality_ = Audio::ConversionQuality::Best;
    };

    /**
     * @brief Converts an audio source into the song a game streams.
     */
    class SongProcessor
        : public ContentProcessor<std::shared_ptr<Audio::AudioContent>, std::shared_ptr<SongContent>>
    {
    public:
        /** @brief .NET full name of this type. */
        CNAEXT static constexpr std::string_view XnaTypeName =
            "Microsoft.Xna.Framework.Content.Pipeline.Processors.SongProcessor";

        /** @brief Initializes a processor with XNA's own defaults. */
        SongProcessor() = default;

        /**
         * @brief Gets how much of the source the conversion keeps.
         *
         * @return The quality.
         */
        [[nodiscard]] Audio::ConversionQuality getQualityProperty() const noexcept;

        /**
         * @brief Sets how much of the source the conversion keeps.
         *
         * @param value The quality.
         */
        void setQualityProperty(Audio::ConversionQuality value) noexcept;

        /**
         * @brief Converts the audio into a song.
         *
         * @param input The audio to convert.
         * @param context The processor context.
         * @return The song.
         * @throws System::ArgumentNullException when the input is null.
         * @throws InvalidContentException always otherwise: a song is Windows Media audio, and
         *         that encoder is not available outside the platform that owns it.
         */
        [[nodiscard]] std::shared_ptr<SongContent> Process(const std::shared_ptr<Audio::AudioContent>& input,
                                                           ContentProcessorContext& context) override;

        /** @brief Returns the type's stable name. */
        CNAEXT [[nodiscard]] const std::string& GetTypeName() const;

    private:
        Audio::ConversionQuality quality_ = Audio::ConversionQuality::Best;
    };
}
