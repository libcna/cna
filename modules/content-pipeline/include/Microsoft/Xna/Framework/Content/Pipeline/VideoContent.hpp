// SPDX-License-Identifier: MS-PL
#pragma once

#include <string>
#include <string_view>

#include "CNA/CNAHelper.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/ContentItem.hpp"
#include "Microsoft/Xna/Framework/Media/VideoSoundtrackType.hpp"
#include "SharpRuntime/SharpRuntimeHelper.hpp"
#include "System/IDisposable.hpp"
#include "System/TimeSpan.hpp"

namespace Microsoft::Xna::Framework::Content::Pipeline
{
    /**
     * @brief One video file as the pipeline holds it: where it is and what shape it has.
     *
     * The constructor is **eager**: it opens the file and reads its declared shape there, so a
     * source that cannot be read is refused at construction and not at a later property read.
     * Measured, and it matters: the same missing file is a `FileNotFoundException` through
     * `WmvImporter` and an `InvalidContentException` through this constructor, because the
     * importer checks existence first and the constructor does not (see
     * `docs/xna-content-pipeline-media.md` section 4).
     *
     * Every property is read-only. The soundtrack type is the one thing a processor sets, and it
     * does so on its own input, which is also its output.
     */
    class VideoContent final : public ContentItem, public System::IDisposable
    {
    public:
        /** @brief .NET full name of this type. */
        CNAEXT static constexpr std::string_view XnaTypeName =
            "Microsoft.Xna.Framework.Content.Pipeline.VideoContent";

        /**
         * @brief Opens a video file and reads what it declares about itself.
         *
         * @param filename Path to the video source.
         * @throws InvalidContentException when the file is not there, is not a video, or cannot
         *         be read -- all three carry the one sentence XNA gives, and a null or empty name
         *         reaches it as an empty file name rather than being refused as null.
         */
        explicit VideoContent(const std::string& filename);

        /** @brief Releases what the probe held. */
        ~VideoContent() override;

        VideoContent(const VideoContent&) = delete;
        VideoContent& operator=(const VideoContent&) = delete;

        /**
         * @brief Gets the video's bit rate.
         *
         * @return Bits per second, as the container declares them.
         */
        [[nodiscard]] SharpRuntime::intcs getBitsPerSecondProperty() const noexcept;

        /**
         * @brief Gets how long the video plays for.
         *
         * @return The duration.
         */
        [[nodiscard]] System::TimeSpan getDurationProperty() const noexcept;

        /**
         * @brief Gets the path the video was read from.
         *
         * @return The file name, as it was given.
         */
        [[nodiscard]] const std::string& getFilenameProperty() const noexcept;

        /**
         * @brief Gets the video's frame rate.
         *
         * @return Frames per second.
         */
        [[nodiscard]] SharpRuntime::Single getFramesPerSecondProperty() const noexcept;

        /**
         * @brief Gets the frame height.
         *
         * @return Height in pixels.
         */
        [[nodiscard]] SharpRuntime::intcs getHeightProperty() const noexcept;

        /**
         * @brief Gets the kind of audio the video carries.
         *
         * @return The soundtrack type; `Music` until a processor sets another.
         */
        [[nodiscard]] Media::VideoSoundtrackType getVideoSoundtrackTypeProperty() const noexcept;

        /**
         * @brief Sets the kind of audio the video carries.
         *
         * XNA declares this setter `internal`, and `VideoProcessor` is the only thing that reaches
         * it; C++ has no assembly boundary, so it stays public and marked as the extension it is.
         *
         * @param value The soundtrack type to record.
         */
        CNAEXT void setVideoSoundtrackTypeProperty(Media::VideoSoundtrackType value) noexcept;

        /**
         * @brief Gets the frame width.
         *
         * @return Width in pixels.
         */
        [[nodiscard]] SharpRuntime::intcs getWidthProperty() const noexcept;

        /** @brief Releases what the probe held; calling it again does nothing. */
        void Dispose() override;

        /** @brief Returns the type's stable name. */
        CNAEXT [[nodiscard]] const std::string& GetTypeName() const override;

        /**
         * @brief Tells whether the source carries an audio stream at all.
         *
         * Not an XNA member: `VideoProcessor` needs it to decide what a soundtrack type means for
         * a silent source, and a game that writes its own video processor needs the same answer.
         *
         * @return true when the file has an audio stream.
         */
        CNAEXT [[nodiscard]] bool HasSoundtrackEXT() const noexcept;

    private:
        std::string filename_;
        SharpRuntime::intcs width_ = 0;
        SharpRuntime::intcs height_ = 0;
        SharpRuntime::Single framesPerSecond_ = 0.0f;
        SharpRuntime::intcs bitsPerSecond_ = 0;
        System::TimeSpan duration_{};
        Media::VideoSoundtrackType soundtrack_ = Media::VideoSoundtrackType::Music;
        bool hasSoundtrack_ = false;
        bool disposed_ = false;
    };
}
