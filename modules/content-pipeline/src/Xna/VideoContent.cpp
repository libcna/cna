// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Content/Pipeline/VideoContent.hpp"

#include <exception>
#include <filesystem>

#include "CNA/Content/Pipeline/BuildTimeMediaDecoder.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/InvalidContentException.hpp"

namespace Microsoft::Xna::Framework::Content::Pipeline
{
    namespace
    {
        /** @brief The one sentence XNA gives for every video it cannot read. */
        [[nodiscard]] std::string InvalidVideo(const std::string& filename)
        {
            std::string name;
            try
            {
                name = std::filesystem::path(filename).filename().string();
            }
            catch (const std::exception&)
            {
                name = filename;
            }
            return "Video file " + name +
                   " is invalid. Please make sure that the video is not DRM protected and is a "
                   "valid single-pass CBR encoded video file.";
        }
    }

    VideoContent::VideoContent(const std::string& filename) : filename_(filename)
    {
        namespace Media = CNA::Content::Pipeline::BuildTimeMedia;
        if (!Media::IsAvailable())
        {
            // A build with no decoder says so, rather than telling a user their file is invalid.
            throw InvalidContentException(InvalidVideo(filename) + " " + Media::UnavailableReason());
        }
        Media::ProbedVideo probed;
        try
        {
            if (filename.empty())
            {
                throw std::runtime_error("empty name");
            }
            probed = Media::ProbeVideo(filename);
        }
        catch (const std::exception&)
        {
            // Everything is this one refusal: a file that is not there, one that is not a video,
            // an empty name and a null one alike (measured, tests/reference/xna40/media cases
            // videocontent/*). A missing file is not a FileNotFoundException here -- that is the
            // importer's check, not the constructor's.
            throw InvalidContentException(InvalidVideo(filename));
        }
        width_ = static_cast<SharpRuntime::intcs>(probed.width);
        height_ = static_cast<SharpRuntime::intcs>(probed.height);
        framesPerSecond_ = probed.framesPerSecond;
        bitsPerSecond_ = static_cast<SharpRuntime::intcs>(probed.bitsPerSecond);
        // Whole milliseconds with the remainder dropped, as every other duration in this pipeline.
        duration_ = System::TimeSpan(probed.durationTicks / 10000 * 10000);
        hasSoundtrack_ = probed.hasAudio;
    }

    VideoContent::~VideoContent() = default;

    SharpRuntime::intcs VideoContent::getBitsPerSecondProperty() const noexcept { return bitsPerSecond_; }

    System::TimeSpan VideoContent::getDurationProperty() const noexcept { return duration_; }

    const std::string& VideoContent::getFilenameProperty() const noexcept { return filename_; }

    SharpRuntime::Single VideoContent::getFramesPerSecondProperty() const noexcept
    {
        return framesPerSecond_;
    }

    SharpRuntime::intcs VideoContent::getHeightProperty() const noexcept { return height_; }

    Media::VideoSoundtrackType VideoContent::getVideoSoundtrackTypeProperty() const noexcept
    {
        return soundtrack_;
    }

    void VideoContent::setVideoSoundtrackTypeProperty(const Media::VideoSoundtrackType value) noexcept
    {
        soundtrack_ = value;
    }

    SharpRuntime::intcs VideoContent::getWidthProperty() const noexcept { return width_; }

    bool VideoContent::HasSoundtrackEXT() const noexcept { return hasSoundtrack_; }

    void VideoContent::Dispose()
    {
        // Nothing is held open past the constructor's probe, and every property keeps answering
        // afterwards, which is what the genuine one does with its own handle (a second Dispose is
        // accepted too).
        disposed_ = true;
    }

    const std::string& VideoContent::GetTypeName() const
    {
        static const std::string name(XnaTypeName);
        return name;
    }
}
