#pragma once

#include "Microsoft/Xna/Framework/Media/MediaState.hpp"
#include "Microsoft/Xna/Framework/Media/Video/Video.hpp"
#include "System/IDisposable.hpp"
#include "System/Object.hpp"
#include "System/TimeSpan.hpp"

namespace Microsoft::Xna::Framework::Graphics
{
    class Texture2D;
}

namespace Microsoft::Xna::Framework::Media
{
    /// Controls video playback.
    /// @note Status: Stub — video playback not implemented
    class VideoPlayer final : public System::Object, public System::IDisposable
    {
    public:
        VideoPlayer();
        ~VideoPlayer() override;

        void Dispose() override;

        [[nodiscard]] bool getIsDisposedProperty() const;

        [[nodiscard]] bool getIsLoopedProperty() const;
        void setIsLoopedProperty(bool value);

        [[nodiscard]] bool getIsMutedProperty() const;
        void setIsMutedProperty(bool value);

        /// Gets the current playback position.
        [[nodiscard]] System::TimeSpan getPlayPositionProperty() const;

        [[nodiscard]] MediaState getStateProperty() const;

        [[nodiscard]] Video* getVideoProperty() const;

        [[nodiscard]] float getVolumeProperty() const;
        void setVolumeProperty(float value);

        /// Gets the current video frame as a Texture2D.
        Graphics::Texture2D* GetTexture();

        void Play(Video* video);
        void Stop();
        void Pause();
        void Resume();

        [[nodiscard]] const std::string& GetTypeName() const override;

    private:
        bool isDisposed_;
        bool isLooped_;
        bool isMuted_;
        MediaState state_;
        float volume_;
        Video* video_;
    };
}
