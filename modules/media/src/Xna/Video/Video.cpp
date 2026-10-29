// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Media/Video/Video.hpp"
#include "Microsoft/Xna/Framework/Media/Video/VideoPlayer.hpp"
#include "CNA/Internal/Media/VideoDecoder.hpp"

#include <filesystem>
#include <utility>

#include "CNA/Internal/PathUtf8.hpp"
#include "CNA/Internal/TitlePath.hpp"
#include "CNA/Platform/CurrentPlatform.hpp"
#include "System/IO/File.hpp"
#include "System/IO/FileNotFoundException.hpp"
#include "System/InvalidOperationException.hpp"
#include "System/Uri.hpp"

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
        // Existence means a file, not a directory or a truncated invalid filename.
        if (!System::IO::File::Exists(fileName_))
        {
            throw System::IO::FileNotFoundException(
                "Could not find file '" + fileName_ + "'.", fileName_);
        }

        // CNA's deliberate decoder-free build profile has no FNA equivalent: keep the XNA type
        // link-complete, but reject the first operation that needs native codec services instead
        // of making every Game executable carry FFmpeg (MEDIA-233).
        CNA::Internal::Media::RequireVideoDecoderAvailable();
        CNA::Internal::Media::VideoDecoder probe;
        // WINPORT: third-party boundary, see docs/filesystem-path-model.md -- libavformat widens
        // this filename from UTF-8 itself, so it is handed the UTF-8 text unchanged.
        if (probe.Open(fileName_))
        {
            width_          = probe.GetWidth();
            height_         = probe.GetHeight();
            framesPerSecond_= probe.GetFPS();
            duration_       = System::TimeSpan::FromMilliseconds(
                                  static_cast<double>(probe.GetDuration() * 1000.0));
        }
    }

    Video::~Video()
    {
        // Includes stopped and disposed players: retaining XNA's Video identity must not
        // retain a dangling pointer when its independent C++ owner destroys the asset.
        while (!playerReferences_.players.empty())
            playerReferences_.players.back()->OnVideoDestroyed();
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
        if (playerReferences_.activeParent) playerReferences_.activeParent->SetAudioTrackEXT(track);
    }

    void Video::SetVideoTrackEXT(SharpRuntime::intcs track)
    {
        videoTrack_ = track;
        if (playerReferences_.activeParent) playerReferences_.activeParent->SetVideoTrackEXT(track);
    }

    Video* Video::FromUriEXT(const std::string& uri, Graphics::GraphicsDevice* device)
    {
        // Preserve the string overload's plain native absolute-path input; percent signs
        // in a native filename are literal, unlike escapes in an explicit file URI.
        const auto nativePath = CNA::Internal::TryPathFromUtf8(uri);
        if (nativePath && nativePath->is_absolute()) return new Video(uri, device);

        const System::Uri parsed(uri, System::UriKind::RelativeOrAbsolute);
        if (parsed.getIsAbsoluteUriProperty())
        {
            if (!parsed.getIsFileProperty())
                throw System::InvalidOperationException("Only local file URIs are supported for now.");
            return new Video(parsed.getLocalPathProperty(), device);
        }

        // Media is below the runtime module that owns TitleLocation; use its existing bridge.
        const auto configuredTitle = CNA::Internal::TryGetTitlePath();
        std::string title = configuredTitle ? *configuredTitle :
            CNA::Platform::GetCurrentPlatform().GetFileSystem()->GetBasePath();
        if (!configuredTitle && title.empty())
            title = CNA::Internal::PathToGenericUtf8(std::filesystem::current_path());
        return new Video(CNA::Internal::PathToGenericUtf8(
            CNA::Internal::PathFromUtf8(title) / CNA::Internal::PathFromUtf8(parsed.ToString())), device);
    }

    const std::string& Video::GetTypeName() const
    {
        static const std::string typeName = "Microsoft.Xna.Framework.Media.Video";
        return typeName;
    }
}
