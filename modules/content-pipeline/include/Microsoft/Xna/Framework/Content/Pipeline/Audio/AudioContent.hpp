// SPDX-License-Identifier: MS-PL
#pragma once

#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "CNA/CNAHelper.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/Audio/AudioEnums.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/Audio/AudioFormat.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/ContentItem.hpp"
#include "SharpRuntime/SharpRuntimeHelper.hpp"
#include "System/IDisposable.hpp"
#include "System/TimeSpan.hpp"

namespace Microsoft::Xna::Framework::Content::Pipeline::Audio
{
    /**
     * @brief One audio file as the pipeline holds it: its encoding, its samples and its loop.
     */
    class AudioContent final : public ContentItem, public System::IDisposable
    {
    public:
        /** @brief .NET full name of this type. */
        CNAEXT static constexpr std::string_view XnaTypeName =
            "Microsoft.Xna.Framework.Content.Pipeline.Audio.AudioContent";

        /**
         * @brief Reads an audio file into memory.
         *
         * @param audioFileName Path to the source file.
         * @param audioFileType The file's format.
         * @throws InvalidContentException when the file cannot be read as audio of that type.
         */
        AudioContent(const std::string& audioFileName, AudioFileType audioFileType);

        /** @brief Releases the samples. */
        ~AudioContent() override;

        AudioContent(const AudioContent&) = delete;
        AudioContent& operator=(const AudioContent&) = delete;

        /**
         * @brief Gets the samples, in the encoding `Format` names.
         *
         * @return The sample bytes.
         * @throws InvalidContentException after `Dispose`.
         */
        [[nodiscard]] const std::vector<SharpRuntime::bytecs>& getDataProperty() const;

        /**
         * @brief Gets how long the audio plays for, truncated to whole milliseconds.
         *
         * @return The duration.
         */
        [[nodiscard]] System::TimeSpan getDurationProperty() const noexcept;

        /**
         * @brief Gets the path the audio was read from.
         *
         * @return The file name, as it was given.
         */
        [[nodiscard]] const std::string& getFileNameProperty() const noexcept;

        /**
         * @brief Gets the format of the source file.
         *
         * @return The file type.
         */
        [[nodiscard]] AudioFileType getFileTypeProperty() const noexcept;

        /**
         * @brief Gets the encoding of the samples.
         *
         * @return The format.
         */
        [[nodiscard]] const std::shared_ptr<AudioFormat>& getFormatProperty() const noexcept;

        /**
         * @brief Gets the number of sample frames the loop region spans.
         *
         * @return The loop length in frames.
         */
        [[nodiscard]] SharpRuntime::intcs getLoopLengthProperty() const noexcept;

        /**
         * @brief Gets the first sample frame of the loop region.
         *
         * @return The loop start in frames.
         */
        [[nodiscard]] SharpRuntime::intcs getLoopStartProperty() const noexcept;

        /**
         * @brief Re-encodes the samples.
         *
         * @param formatType The encoding to produce.
         * @param quality How much of the source to keep.
         * @param targetFileName Where a format that writes a file should write it; may be empty.
         * @throws InvalidContentException after `Dispose`, or when the encoding is not available
         *         in this build.
         */
        void ConvertFormat(ConversionFormat formatType, ConversionQuality quality,
                           const std::string& targetFileName);

        /** @brief Releases the samples; calling it again does nothing. */
        void Dispose() override;

        /** @brief Returns the type's stable name. */
        CNAEXT [[nodiscard]] const std::string& GetTypeName() const override;

    private:
        /**
         * @brief Refuses an operation that needs the samples after they were released.
         *
         * @param member The member being called, for the message.
         * @throws InvalidContentException when the content has been disposed.
         */
        void RequireNotDisposed(const std::string& member) const;

        std::string fileName_;
        AudioFileType fileType_ = AudioFileType::Wav;
        std::shared_ptr<AudioFormat> format_;
        std::vector<SharpRuntime::bytecs> data_;
        SharpRuntime::intcs loopStart_ = 0;
        SharpRuntime::intcs loopLength_ = 0;
        System::TimeSpan duration_;
        bool disposed_ = false;
    };
}
