// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Media/Video/Video.hpp"
#include "Microsoft/Xna/Framework/Media/Video/VideoPlayer.hpp"
#include "CNA/Internal/Media/VideoDecoder.hpp"

#include <filesystem>
#include <utility>

#include "System/IO/FileNotFoundException.hpp"

namespace Microsoft::Xna::Framework::Media
{
    Video::Video(std::string fileName, Graphics::GraphicsDevice* device)
        : fileName_(std::move(fileName)),
          device_(device),
          width_(0),
          height_(0),
          framesPerSecond_(0.0f),
          soundtrackType_(VideoSoundtrackType::MusicAndDialog),
          duration_(System::TimeSpan::Zero)
    {
        // FNA's raw-file constructor explicitly checks File.Exists and throws
        // FileNotFoundException before ever touching the file (Video.cs). CNA's equivalent
        // previously just probed via VideoDecoder::Open and silently left width_/height_/
        // duration_ at 0 on failure, with no exception at all -- a real fidelity gap
        // (plans/plan_media.md MEDIA-44).
        if (!std::filesystem::exists(fileName_))
        {
            throw System::IO::FileNotFoundException(
                "Could not find file '" + fileName_ + "'.", fileName_);
        }

        // CNA's deliberate decoder-free build profile has no FNA equivalent: keep the XNA type
        // link-complete, but reject the first operation that needs native codec services instead
        // of making every Game executable carry FFmpeg (plans/plan_media.md MEDIA-233).
        CNA::Internal::Media::RequireVideoDecoderAvailable();
        CNA::Internal::Media::VideoDecoder probe;
        if (probe.Open(fileName_))
        {
            width_          = probe.GetWidth();
            height_         = probe.GetHeight();
            framesPerSecond_= probe.GetFPS();
            duration_       = System::TimeSpan::FromMilliseconds(
                                  static_cast<double>(probe.GetDuration() * 1000.0));
        }
    }

    Video::Video(std::string fileName, Graphics::GraphicsDevice* device,
                 SharpRuntime::intcs durationMS, SharpRuntime::intcs width,
                 SharpRuntime::intcs height, float framesPerSecond,
                 VideoSoundtrackType soundtrackType)
        : fileName_(std::move(fileName)),
          device_(device),
          width_(width),
          height_(height),
          framesPerSecond_(framesPerSecond),
          soundtrackType_(soundtrackType),
          duration_(System::TimeSpan::FromMilliseconds(durationMS))
    {
    }

    SharpRuntime::intcs Video::getWidthProperty() const
    {
        return width_;
    }

    SharpRuntime::intcs Video::getHeightProperty() const
    {
        return height_;
    }

    float Video::getFramesPerSecondProperty() const
    {
        return framesPerSecond_;
    }

    VideoSoundtrackType Video::getVideoSoundtrackTypeProperty() const
    {
        return soundtrackType_;
    }

    System::TimeSpan Video::getDurationProperty() const
    {
        return duration_;
    }

    void Video::setDurationProperty(System::TimeSpan value)
    {
        duration_ = value;
    }

    const std::string& Video::getFileNameProperty() const { return fileName_; }

    Graphics::GraphicsDevice* Video::getGraphicsDeviceProperty() const { return device_; }

    void Video::SetAudioTrackEXT(SharpRuntime::intcs track)
    {
        audioTrack_ = track;
        if (parent_) parent_->SetAudioTrackEXT(track);
    }

    void Video::SetVideoTrackEXT(SharpRuntime::intcs track)
    {
        videoTrack_ = track;
        if (parent_) parent_->SetVideoTrackEXT(track);
    }

    Video* Video::FromUriEXT(const std::string& uri, Graphics::GraphicsDevice* device)
    {
        return new Video(uri, device);
    }

    const std::string& Video::GetTypeName() const
    {
        static const std::string typeName = "Microsoft.Xna.Framework.Media.Video";
        return typeName;
    }
}
