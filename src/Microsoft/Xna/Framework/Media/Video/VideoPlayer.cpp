// SPDX-License-Identifier: MIT
// Copyright (c) Robert Vokac and contributors
#include "Microsoft/Xna/Framework/Media/Video/VideoPlayer.hpp"

#include <algorithm>
#include <SDL3/SDL.h>

#include "CNA/Internal/Media/VideoDecoder.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"

namespace Microsoft::Xna::Framework::Media
{
    VideoPlayer::VideoPlayer() = default;

    VideoPlayer::~VideoPlayer()
    {
        Dispose();
    }

    void VideoPlayer::Dispose()
    {
        if (isDisposed_) return;
        isDisposed_ = true;
        CloseDecoder();
    }

    // -------------------------------------------------------------------------

    bool VideoPlayer::getIsDisposedProperty()  const { return isDisposed_; }
    bool VideoPlayer::getIsLoopedProperty()    const { return isLooped_; }
    void VideoPlayer::setIsLoopedProperty(bool v)    { isLooped_ = v; }
    bool VideoPlayer::getIsMutedProperty()     const { return isMuted_; }
    void VideoPlayer::setIsMutedProperty(bool v)     { isMuted_ = v; ApplyVolume(); }
    MediaState VideoPlayer::getStateProperty() const { return state_; }
    Video*     VideoPlayer::getVideoProperty() const { return video_; }
    float      VideoPlayer::getVolumeProperty() const { return volume_; }
    void VideoPlayer::setVolumeProperty(float v) { volume_ = std::clamp(v, 0.0f, 1.0f); ApplyVolume(); }

    System::TimeSpan VideoPlayer::getPlayPositionProperty() const
    {
        if (state_ == MediaState::Stopped) return System::TimeSpan::Zero;
        return System::TimeSpan::FromMilliseconds(GetElapsedSeconds() * 1000.0);
    }

    // -------------------------------------------------------------------------

    double VideoPlayer::GetElapsedSeconds() const
    {
        if (state_ == MediaState::Paused)
            return pauseOffset_;
        if (state_ == MediaState::Playing)
        {
            auto now = Clock::now();
            double elapsed = std::chrono::duration<double>(now - startTime_).count();
            return pauseOffset_ + elapsed;
        }
        return 0.0;
    }

    void VideoPlayer::ApplyVolume()
    {
        if (!audioStream_) return;
        SDL_SetAudioStreamGain(audioStream_, isMuted_ ? 0.0f : volume_);
    }

    // -------------------------------------------------------------------------

    void VideoPlayer::OpenDecoder(Video* video)
    {
        CloseDecoder();

        decoder_ = std::make_unique<CNA::Internal::Media::VideoDecoder>();
        if (!decoder_->Open(video->getFileNameProperty()))
        {
            decoder_.reset();
            return;
        }

        // Create frame texture sized to video dimensions
        Graphics::GraphicsDevice* device = video->getGraphicsDeviceProperty();
        if (device)
        {
            frameTexture_ = std::make_unique<Graphics::Texture2D>(
                *device,
                decoder_->GetWidth(),
                decoder_->GetHeight()
            );
        }

        // Open SDL audio stream if video has audio
        if (decoder_->HasAudio())
        {
            SDL_AudioSpec spec{};
            spec.format   = SDL_AUDIO_F32;
            spec.channels = decoder_->GetChannels();
            spec.freq     = decoder_->GetSampleRate();
            audioStream_  = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK,
                                                      &spec, nullptr, nullptr);
            if (audioStream_)
            {
                SDL_SetAudioStreamGain(audioStream_, isMuted_ ? 0.0f : volume_);
                SDL_ResumeAudioStreamDevice(audioStream_);
            }
        }

        // Decode and display first frame immediately
        double pts = 0.0;
        if (decoder_->NextFrame(rgbaBuffer_, pts) && frameTexture_)
        {
            frameTexture_->SetDataRGBA(rgbaBuffer_.data(),
                                       static_cast<int>(rgbaBuffer_.size() / 4));
            lastFramePts_ = pts;
        }
        decoder_->DrainAudio(audioBuffer_);
        if (audioStream_ && !audioBuffer_.empty())
        {
            SDL_PutAudioStreamData(audioStream_,
                                   audioBuffer_.data(),
                                   static_cast<int>(audioBuffer_.size() * sizeof(float)));
            audioBuffer_.clear();
        }
    }

    void VideoPlayer::CloseDecoder()
    {
        if (audioStream_)
        {
            SDL_DestroyAudioStream(audioStream_);
            audioStream_ = nullptr;
        }
        frameTexture_.reset();
        decoder_.reset();
        state_        = MediaState::Stopped;
        video_        = nullptr;
        pauseOffset_  = 0.0;
        lastFramePts_ = -1.0;
    }

    // -------------------------------------------------------------------------

    void VideoPlayer::Play(Video* video)
    {
        if (!video) return;
        video_ = video;
        OpenDecoder(video);
        if (!decoder_) return;

        state_       = MediaState::Playing;
        pauseOffset_ = 0.0;
        startTime_   = Clock::now();
    }

    void VideoPlayer::Stop()
    {
        CloseDecoder();
        state_ = MediaState::Stopped;
    }

    void VideoPlayer::Pause()
    {
        if (state_ != MediaState::Playing) return;
        pauseOffset_ += std::chrono::duration<double>(Clock::now() - startTime_).count();
        state_ = MediaState::Paused;
        if (audioStream_) SDL_PauseAudioStreamDevice(audioStream_);
    }

    void VideoPlayer::Resume()
    {
        if (state_ != MediaState::Paused) return;
        startTime_ = Clock::now();
        state_     = MediaState::Playing;
        if (audioStream_) SDL_ResumeAudioStreamDevice(audioStream_);
    }

    // -------------------------------------------------------------------------

    Graphics::Texture2D* VideoPlayer::GetTexture()
    {
        if (state_ == MediaState::Stopped || !decoder_ || !frameTexture_)
            return frameTexture_.get();

        if (state_ == MediaState::Paused)
            return frameTexture_.get();

        const double elapsed  = GetElapsedSeconds();
        const double frameDt  = (decoder_->GetFPS() > 0.0f)
                                ? 1.0 / decoder_->GetFPS()
                                : 1.0 / 24.0;

        // Decode frames until we are caught up to the current play position
        while (lastFramePts_ < elapsed - frameDt * 0.5)
        {
            double pts = 0.0;
            if (!decoder_->NextFrame(rgbaBuffer_, pts))
            {
                // EOF
                if (isLooped_)
                {
                    decoder_->SeekToStart();
                    pauseOffset_ = 0.0;
                    startTime_   = Clock::now();
                    lastFramePts_= -1.0;
                }
                else
                {
                    state_ = MediaState::Stopped;
                    if (audioStream_) SDL_PauseAudioStreamDevice(audioStream_);
                }
                break;
            }

            frameTexture_->SetDataRGBA(rgbaBuffer_.data(),
                                       static_cast<int>(rgbaBuffer_.size() / 4));
            lastFramePts_ = pts;

            // Feed decoded audio to SDL stream
            decoder_->DrainAudio(audioBuffer_);
            if (audioStream_ && !audioBuffer_.empty())
            {
                SDL_PutAudioStreamData(audioStream_,
                                       audioBuffer_.data(),
                                       static_cast<int>(audioBuffer_.size() * sizeof(float)));
                audioBuffer_.clear();
            }
        }

        return frameTexture_.get();
    }

    const std::string& VideoPlayer::GetTypeName() const
    {
        static const std::string name = "Microsoft.Xna.Framework.Media.VideoPlayer";
        return name;
    }
}
