#include "Microsoft/Xna/Framework/Media/Video/Video.hpp"

#include <utility>

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
        // TODO: implement codec detection and metadata reading from file
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
