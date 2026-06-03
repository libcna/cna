#include "Microsoft/Xna/Framework/Media/Video/VideoPlayer.hpp"

namespace Microsoft::Xna::Framework::Media
{
    VideoPlayer::VideoPlayer()
        : isDisposed_(false),
          isLooped_(false),
          isMuted_(false),
          state_(MediaState::Stopped),
          volume_(1.0f),
          video_(nullptr)
    {
    }

    VideoPlayer::~VideoPlayer()
    {
        Dispose();
    }

    void VideoPlayer::Dispose()
    {
        isDisposed_ = true;
        state_ = MediaState::Stopped;
        video_ = nullptr;
    }

    bool VideoPlayer::getIsDisposedProperty() const
    {
        return isDisposed_;
    }

    bool VideoPlayer::getIsLoopedProperty() const
    {
        return isLooped_;
    }

    void VideoPlayer::setIsLoopedProperty(bool value)
    {
        isLooped_ = value;
    }

    bool VideoPlayer::getIsMutedProperty() const
    {
        return isMuted_;
    }

    void VideoPlayer::setIsMutedProperty(bool value)
    {
        isMuted_ = value;
    }

    System::TimeSpan VideoPlayer::getPlayPositionProperty() const
    {
        // TODO: return actual playback position
        return System::TimeSpan::Zero;
    }

    MediaState VideoPlayer::getStateProperty() const
    {
        return state_;
    }

    Video* VideoPlayer::getVideoProperty() const
    {
        return video_;
    }

    float VideoPlayer::getVolumeProperty() const
    {
        return volume_;
    }

    void VideoPlayer::setVolumeProperty(float value)
    {
        volume_ = value;
    }

    Graphics::Texture2D* VideoPlayer::GetTexture()
    {
        // TODO: return current decoded video frame as Texture2D
        return nullptr;
    }

    void VideoPlayer::Play(Video* video)
    {
        video_ = video;
        state_ = MediaState::Playing;
        // TODO: start video decoding and audio playback
    }

    void VideoPlayer::Stop()
    {
        state_ = MediaState::Stopped;
        // TODO: stop decoder and release resources
    }

    void VideoPlayer::Pause()
    {
        state_ = MediaState::Paused;
        // TODO: pause decoder
    }

    void VideoPlayer::Resume()
    {
        state_ = MediaState::Playing;
        // TODO: resume decoder
    }

    const std::string& VideoPlayer::GetTypeName() const
    {
        static const std::string typeName = "Microsoft.Xna.Framework.Media.VideoPlayer";
        return typeName;
    }
}
