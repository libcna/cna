// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Media/Video/VideoPlayer.hpp"

#include <algorithm>
#include <cstddef>
#include <cmath>
#include <span>

#ifdef SOUND_ENABLED
#include "CNA/Internal/Audio/MixerEngine.hpp"
#endif
#include "CNA/Internal/Media/VideoDecoder.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "System/InvalidOperationException.hpp"
#include "System/ObjectDisposedException.hpp"

namespace Microsoft::Xna::Framework::Media
{
    namespace
    {
        // plans/plan_platform.md PLAT-SDL2-8. The mixer engine is the SDL3_mixer implementation and is
        // excluded from the archive for every other CNA_AUDIO_PLATFORM value, so this file linked
        // only under SDL3 audio -- and only ever got built at all where FFmpeg is present, which
        // is why no CI cell caught it. MediaPlayer.cpp already solved the same problem with
        // `#ifdef SOUND_ENABLED` around each use; doing that here would put a guard around
        // fourteen call sites, so the seam is one thin shim layer instead and the playback logic
        // below stays a single readable version.
        //
        // Without the engine `OpenAudioStream` returns null, and every other operation is already
        // guarded by `if (audioStream_)` at its call site, so a video plays with its audio track
        // silent -- exactly what MediaPlayer does in this profile, and exactly what
        // docs/platform-sdl2.md documents. Video decoding and presentation are unaffected.
#ifdef SOUND_ENABLED
        using AudioStream = CNA::Internal::Audio::MixerStream;

        AudioStream* OpenAudioStream(int sampleRate, int channels)
        {
            return CNA::Internal::Audio::CreateMixerPlaybackStream(
                {sampleRate, channels, CNA::Internal::Audio::MixerSampleFormat::Float32});
        }
        void CloseAudioStream(AudioStream* stream) { CNA::Internal::Audio::DestroyMixerStream(stream); }
        void SetAudioGain(AudioStream* stream, float gain)
        {
            CNA::Internal::Audio::SetMixerStreamGain(stream, gain);
        }
        void PauseAudio(AudioStream* stream) { CNA::Internal::Audio::PauseMixerStream(stream); }
        void ResumeAudio(AudioStream* stream) { CNA::Internal::Audio::ResumeMixerStream(stream); }
        void SubmitAudio(AudioStream* stream, std::span<const std::byte> pcm)
        {
            (void)CNA::Internal::Audio::PutMixerStreamData(stream, pcm);
        }
        std::size_t QueuedAudioBytes(const AudioStream* stream)
        {
            return CNA::Internal::Audio::GetMixerStreamQueuedBytes(stream);
        }
#else
        using AudioStream = CNA::Internal::Audio::MixerStream;

        AudioStream* OpenAudioStream(int, int) { return nullptr; }
        void CloseAudioStream(AudioStream*) {}
        void SetAudioGain(AudioStream*, float) {}
        void PauseAudio(AudioStream*) {}
        void ResumeAudio(AudioStream*) {}
        void SubmitAudio(AudioStream*, std::span<const std::byte>) {}
        std::size_t QueuedAudioBytes(const AudioStream*) { return 0; }
#endif
    }

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
        SetAudioGain(audioStream_, isMuted_ ? 0.0f : volume_);
    }

    void VideoPlayer::ReconfigureVideoOutputForCurrentTrack()
    {
        if (!decoder_) return;

        // Frame texture, sized to whichever video track is currently active.
        Graphics::GraphicsDevice* device = video_ ? video_->getGraphicsDeviceProperty() : nullptr;
        if (device)
        {
            frameTexture_ = std::make_unique<Graphics::Texture2D>(
                *device,
                decoder_->GetWidth(),
                decoder_->GetHeight()
            );
        }
        else
        {
            frameTexture_.reset();
        }
    }

    void VideoPlayer::ReconfigureAudioOutputForCurrentTrack()
    {
        if (!decoder_) return;

        // Audio stream, sized to whichever audio track is currently active. Always torn down
        // and recreated rather than reused -- a stale stream opened for a different sample
        // rate/channel count would otherwise keep playing decoded audio at the wrong speed/pitch.
        if (audioStream_)
        {
            CloseAudioStream(audioStream_);
            audioStream_ = nullptr;
        }
        if (decoder_->HasAudio())
        {
            audioStream_ = OpenAudioStream(decoder_->GetSampleRate(), decoder_->GetChannels());
            if (audioStream_)
            {
                SetAudioGain(audioStream_, isMuted_ ? 0.0f : volume_);
                if (state_ == MediaState::Playing)
                {
                    ResumeAudio(audioStream_);
                }
            }
        }
    }

    // -------------------------------------------------------------------------

    void VideoPlayer::OpenDecoder(Video* video)
    {
        CloseDecoder();
        // CloseDecoder() unconditionally resets video_ to nullptr (it's also the standalone
        // Stop()/Dispose() path) -- restore it here so ReconfigureVideoOutputForCurrentTrack()
        // below (and getVideoProperty(), for the rest of this call) see the real Video being
        // opened, not a stale null.
        video_ = video;

        decoder_ = std::make_unique<CNA::Internal::Media::VideoDecoder>();
        if (!decoder_->Open(video->getFileNameProperty()))
        {
            decoder_.reset();
            video_ = nullptr;
            return;
        }

        // FNA's VideoPlayerAV1/VideoPlayerTheora both validate the video's declared metadata
        // (trusted upfront, e.g. from an XNB) against what the real file actually reports before
        // ever playing it, throwing InvalidOperationException on mismatch (~1.0f fps tolerance,
        // matching FNA's own check) -- plans/plan_media.md MEDIA-42. For the raw-file constructor this
        // is a trivially-true self-check (Video's own properties already came from an identical
        // probe); for the XNB-sourced constructor it is the real, meaningful validation.
        if (video->getWidthProperty() != decoder_->GetWidth() ||
            video->getHeightProperty() != decoder_->GetHeight() ||
            std::abs(video->getFramesPerSecondProperty() - decoder_->GetFPS()) > 1.0f)
        {
            decoder_.reset();
            video_ = nullptr;
            throw System::InvalidOperationException(
                "Video metadata (width/height/framesPerSecond) does not match the decoded file.");
        }

        // Apply stored track preferences BEFORE creating the frame texture / audio stream --
        // both are sized/formatted from decoder_'s current state, so switching tracks first
        // (rather than after, as this used to do) ensures they're built for the track that will
        // actually be used, not always the file's default track (plans/plan_media.md MEDIA-90, a real
        // bug found by external code review: a caller that set a track preference via
        // SetAudioTrackEXT()/SetVideoTrackEXT() before Play() had that preference silently
        // ignored for the texture/audio-stream's own format/size).
        if (audioTrack_ >= 0) decoder_->SetAudioStream(audioTrack_);
        if (videoTrack_ >= 0) decoder_->SetVideoStream(videoTrack_);
        video->parent_ = this;

        // Set state_ to Playing BEFORE ReconfigureAudioOutputForCurrentTrack() runs -- that
        // function only resumes the stream when state_ == Playing (so a
        // mid-playback track switch while genuinely Paused doesn't wrongly resume audio), but
        // state_ is still Stopped here (CloseDecoder() just reset it) since Play() itself doesn't
        // set it to Playing until after this whole function returns. Left as Stopped, this
        // resulted in a real regression: the native backend opens every new stream
        // paused by default, and nothing ever resumed it for a fresh Play() call, so every video
        // with audio played completely silently (found by external code review).
        state_ = MediaState::Playing;

        // The whole rest of this function -- texture/audio-stream (re)creation AND the first-frame
        // decode -- is wrapped in one try block. MEDIA-149's own fix only wrapped the first-frame
        // decode, leaving an exception thrown by ReconfigureVideoOutputForCurrentTrack()'s
        // Texture2D construction (or, in principle, a future throwing path inside the audio-side
        // reconfigure) to bypass CloseDecoder() entirely: state_ was already Playing and
        // decoder_/video_/video->parent_ were already live at that point, same half-open-player
        // problem MEDIA-149 fixed for the narrower first-frame-decode case (found by external code
        // review, plans/plan_media.md MEDIA-152).
        try
        {
            ReconfigureVideoOutputForCurrentTrack();
            ReconfigureAudioOutputForCurrentTrack();

            double pts = 0.0;
            if (decoder_->NextFrame(rgbaBuffer_, pts) && frameTexture_)
            {
                frameTexture_->SetDataRGBA(rgbaBuffer_.data(),
                                           static_cast<int>(rgbaBuffer_.size() / 4));
                lastFramePts_ = pts;
            }
            DrainAndFlushAudioBuffer();
        }
        catch (...)
        {
            CloseDecoder();
            throw;
        }
    }

    void VideoPlayer::DrainAndFlushAudioBuffer()
    {
        decoder_->DrainAudio(audioBuffer_);
        if (audioStream_ && !audioBuffer_.empty())
        {
            const auto* bytes = reinterpret_cast<const std::byte*>(audioBuffer_.data());
            const auto byteCount = audioBuffer_.size() * sizeof(float);
            SubmitAudio(audioStream_, std::span<const std::byte>(bytes, byteCount));
        }
        audioBuffer_.clear();
    }

    void VideoPlayer::CloseDecoder()
    {
        if (audioStream_)
        {
            CloseAudioStream(audioStream_);
            audioStream_ = nullptr;
        }
        // audioBuffer_ can hold undrained decoded samples if the player is being torn down with no
        // audio device open (plans/plan_media.md MEDIA-153) -- clear it so a later, successful Play() on
        // a real device never gets stale audio from a previous, unrelated playback prepended to it.
        audioBuffer_.clear();
        if (video_) video_->parent_ = nullptr;
        frameTexture_.reset();
        decoder_.reset();
        state_        = MediaState::Stopped;
        video_        = nullptr;
        pauseOffset_  = 0.0;
        lastFramePts_ = -1.0;
    }

    // -------------------------------------------------------------------------

    namespace
    {
        // FNA's real checkDisposed() throws ObjectDisposedException("VideoPlayer") -- a hardcoded
        // literal type-name string, not nameof/reflection -- reproduced verbatim here for message
        // fidelity (plans/plan_media.md MEDIA-43). Deliberately NOT applied to Dispose() itself: FNA's
        // own Dispose() also calls checkDisposed() (throws on a second call), but ~VideoPlayer()
        // unconditionally calls Dispose() -- replicating that would make a second explicit
        // Dispose() followed by normal destruction throw from inside the destructor, which is
        // undefined behavior in C++ (destructors are implicitly noexcept) rather than a merely
        // surprising API quirk as it is in C#. Kept safely idempotent instead; documented, not
        // silently diverged.
        void CheckDisposed(bool isDisposed)
        {
            if (isDisposed)
            {
                throw System::ObjectDisposedException("VideoPlayer");
            }
        }
    }

    void VideoPlayer::Play(Video* video)
    {
        CheckDisposed(isDisposed_);
        if (!video) return;
        // CNA's optional-backend profile intentionally keeps VideoPlayer's state/configuration API
        // usable without FFmpeg. Reject before assigning video_ so a failed Play is transactional
        // and leaves the observable stopped/no-video state intact (plans/plan_media.md MEDIA-233).
        CNA::Internal::Media::RequireVideoDecoderAvailable();
        video_ = video;
        OpenDecoder(video);
        if (!decoder_) return;

        state_       = MediaState::Playing;
        pauseOffset_ = 0.0;
        startTime_   = Clock::now();
    }

    void VideoPlayer::Stop()
    {
        CheckDisposed(isDisposed_);
        CloseDecoder();
        state_ = MediaState::Stopped;
    }

    void VideoPlayer::Pause()
    {
        CheckDisposed(isDisposed_);
        if (state_ != MediaState::Playing) return;
        pauseOffset_ += std::chrono::duration<double>(Clock::now() - startTime_).count();
        state_ = MediaState::Paused;
        if (audioStream_) PauseAudio(audioStream_);
    }

    void VideoPlayer::Resume()
    {
        CheckDisposed(isDisposed_);
        if (state_ != MediaState::Paused) return;
        startTime_ = Clock::now();
        state_     = MediaState::Playing;
        if (audioStream_) ResumeAudio(audioStream_);
    }

    void VideoPlayer::SetAudioTrackEXT(SharpRuntime::intcs track)
    {
        CheckDisposed(isDisposed_);
        audioTrack_ = track;
        if (decoder_)
        {
            // A mid-playback switch can change sample rate/channel count -- the already-open
            // audio stream (opened for the previous track) must be recreated to match, not left
            // stale (plans/plan_media.md MEDIA-90, a real bug found by external code review). Only the
            // audio side is touched -- reconfiguring the video texture too (as a single combined
            // helper used to do) would be a needless texture reallocation for a change that has no
            // effect on it (plans/plan_media.md MEDIA-148, found by external code review). Only run at
            // all if SetAudioStream() reports a genuine switch happened -- re-selecting the
            // already-active track (or an out-of-range index, which the decoder also treats as a
            // no-op) used to still tear down and reopen the stream for nothing, discarding
            // whatever audio was already queued (plans/plan_media.md MEDIA-154, found by external code
            // review).
            if (decoder_->SetAudioStream(track))
            {
                ReconfigureAudioOutputForCurrentTrack();
            }
        }
    }

    void VideoPlayer::SetVideoTrackEXT(SharpRuntime::intcs track)
    {
        CheckDisposed(isDisposed_);
        videoTrack_ = track;
        if (decoder_)
        {
            // A mid-playback switch can change frame dimensions -- the already-created texture
            // (sized for the previous track) must be recreated to match (plans/plan_media.md MEDIA-90).
            // Only the video side is touched -- reconfiguring the audio stream too (as a single
            // combined helper used to do) tore down and reopened it on every video-only track
            // switch, discarding whatever audio was already queued for playback for no reason
            // (plans/plan_media.md MEDIA-148, found by external code review). Only run at all if
            // SetVideoStream() reports a genuine switch happened -- re-selecting the already-active
            // track (or an out-of-range index) used to still reallocate the texture for nothing
            // (plans/plan_media.md MEDIA-154, found by external code review).
            if (decoder_->SetVideoStream(track))
            {
                ReconfigureVideoOutputForCurrentTrack();
            }
        }
    }

    // -------------------------------------------------------------------------

    Graphics::Texture2D* VideoPlayer::GetTexture()
    {
        CheckDisposed(isDisposed_);

        // plans/plan_media.md MEDIA-45: FNA's own GetTexture() dereferences its impl unguarded, so
        // calling it before any Play() is a raw NullReferenceException in real XNA/FNA -- not a
        // bug to silently improve there. CNA instead returns nullptr gracefully (frameTexture_ is
        // simply still unset), a documented, deliberate deviation: C++ has no safe equivalent to
        // "let it NRE" the way a managed runtime does.
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
            const bool gotFrame = decoder_->NextFrame(rgbaBuffer_, pts);

            // Drain and feed decoded audio to the stream unconditionally, whether or not a
            // video frame came back. NextFrame()'s own internal packet-reading loop can decode
            // trailing audio packets (found while searching for either the next video packet or
            // true EOF) even on the call that ultimately returns false -- draining only in the
            // success branch below would silently strand that final batch of audio, undermining
            // the very "wait for queued audio to drain" check right below this
            // (plans/plan_media.md MEDIA-41 -- a real, confirmed bug found by external code review).
            DrainAndFlushAudioBuffer();

            if (!gotFrame)
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
                    // FNA's VideoPlayerTheora explicitly waits for the audio stream's own
                    // PendingBufferCount to reach 0 (in addition to the codec's EOS) before
                    // declaring State == Stopped, so queued audio isn't cut off abruptly at
                    // video EOF (plans/plan_media.md MEDIA-41). Re-checked on each GetTexture() call
                    // until the audio device has actually finished playing what was queued.
                    if (audioStream_
                        && QueuedAudioBytes(audioStream_) > 0)
                    {
                        break;
                    }
                    state_ = MediaState::Stopped;
                    if (audioStream_) PauseAudio(audioStream_);
                }
                break;
            }

            frameTexture_->SetDataRGBA(rgbaBuffer_.data(),
                                       static_cast<int>(rgbaBuffer_.size() / 4));
            lastFramePts_ = pts;
        }

        return frameTexture_.get();
    }

    const std::string& VideoPlayer::GetTypeName() const
    {
        static const std::string name = "Microsoft.Xna.Framework.Media.VideoPlayer";
        return name;
    }
}
