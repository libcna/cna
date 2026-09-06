// SPDX-License-Identifier: MS-PL
#pragma once

#include <memory>
#include <string>
#include <string_view>

#include "CNA/CNAHelper.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/ContentProcessor.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/VideoContent.hpp"
#include "Microsoft/Xna/Framework/Media/VideoSoundtrackType.hpp"

namespace Microsoft::Xna::Framework::Content::Pipeline::Processors
{
    /**
     * @brief Records what kind of audio a video carries, and passes the video on.
     *
     * The one processor whose input and output are the same type, and the same object: XNA's
     * answers its own input rather than a copy, having set the soundtrack type on it. The video
     * itself is not re-encoded -- an `.xnb` for a video is a header naming an external media file,
     * and the media file is the source, copied.
     */
    class VideoProcessor
        : public ContentProcessor<std::shared_ptr<VideoContent>, std::shared_ptr<VideoContent>>
    {
    public:
        /** @brief .NET full name of this type. */
        CNAEXT static constexpr std::string_view XnaTypeName =
            "Microsoft.Xna.Framework.Content.Pipeline.Processors.VideoProcessor";

        /** @brief Initializes a processor with XNA's own default: `Music`. */
        VideoProcessor() = default;

        /**
         * @brief Gets the kind of audio the processed video is to declare.
         *
         * @return The soundtrack type; `Music` unless it was set (measured,
         *         tests/reference/xna40/media case videoprocessor/defaults).
         */
        [[nodiscard]] Media::VideoSoundtrackType getVideoSoundtrackTypeProperty() const noexcept;

        /**
         * @brief Sets the kind of audio the processed video is to declare.
         *
         * @param value The soundtrack type.
         */
        void setVideoSoundtrackTypeProperty(Media::VideoSoundtrackType value) noexcept;

        /**
         * @brief Records the soundtrack type on the video and answers it.
         *
         * @param input The video to process; the same object comes back.
         * @param context The processor context; the source is added as a dependency and as an
         *        output file, because the built asset streams from it.
         * @return The same video, with its soundtrack type set.
         * @throws System::ArgumentNullException when the input is null, naming `input` as XNA's
         *         does.
         */
        [[nodiscard]] std::shared_ptr<VideoContent> Process(const std::shared_ptr<VideoContent>& input,
                                                            ContentProcessorContext& context) override;

        /** @brief Returns the type's stable name. */
        CNAEXT [[nodiscard]] const std::string& GetTypeName() const;

    private:
        Media::VideoSoundtrackType soundtrack_ = Media::VideoSoundtrackType::Music;
    };
}
