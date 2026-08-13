// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Media/MediaPlayer.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <stdexcept>

#include "CNA/Internal/Media/VisualizationCapture.hpp"
#include "CNA/Internal/Media/VisualizationFFT.hpp"

#ifdef SOUND_ENABLED
#include "CNA/Internal/Audio/MixerEngine.hpp"
#endif

namespace Microsoft::Xna::Framework::Media
{
    System::EventHandler<System::EventArgs> MediaPlayer::ActiveSongChanged;
    System::EventHandler<System::EventArgs> MediaPlayer::MediaStateChanged;

    bool         MediaPlayer::isMuted_               = false;
    bool         MediaPlayer::isRepeating_           = false;
    bool         MediaPlayer::isShuffled_            = false;
    MediaState   MediaPlayer::state_                 = MediaState::Stopped;
    float        MediaPlayer::volume_                = 1.0f;
    bool         MediaPlayer::initialized_           = false;
    SharpRuntime::intcs MediaPlayer::numSongsInQueuePlayed_ = 0;
    MediaQueue   MediaPlayer::queue_;
    std::mt19937 MediaPlayer::random_(std::random_device{}());

    // Visualization capture state (plan_media.md MEDIA-186/188). Kept outside the SOUND_ENABLED
    // guard so IsVisualizationEnabled still round-trips in a no-audio build; only the actual
    // post-mix tap below is audio-renderer specific.
    namespace
    {
        bool g_visualizationEnabled = false;

        // Whether a post-mix callback is ACTUALLY installed right now. Tracked separately from
        // g_visualizationEnabled because the two can legitimately disagree: if uninstalling fails,
        // the user-visible flag goes false (the caller asked for off, and no data is served) while
        // a live callback is still writing. Without this, the early-return guard in the setter
        // would see "already false" and never retry the uninstall, leaving the tap installed
        // forever (found by external code review, plan_media.md MEDIA-226).
        bool g_visualizationTapInstalled = false;

        CNA::Internal::Media::VisualizationCapture g_visualizationCapture;
    }

    bool         MediaPlayer::timerRunning_          = false;
    std::chrono::steady_clock::time_point MediaPlayer::timerStart_;
    std::chrono::duration<double> MediaPlayer::accumulatedTime_ =
        std::chrono::duration<double>::zero();

#ifdef SOUND_ENABLED
    namespace
    {
        CNA::Internal::Audio::MixerTrack* g_musicTrack = nullptr;
        CNA::Internal::Audio::MixerAudioPtr g_musicAudio;

        std::atomic<bool> g_songEnded{false};

        void OnMusicTrackStopped(void* /*userdata*/,
                                 CNA::Internal::Audio::MixerTrack* /*track*/)
        {
            // Called from the audio thread — only set a flag.
            g_songEnded.store(true, std::memory_order_relaxed);
        }

        // Runs on the AUDIO THREAD for every mixed buffer: must stay real-time safe (no
        // allocation, no locking, no exceptions). VisualizationCapture::Push is written to that
        // contract. The audio facade supplies float32 PCM directly, so no format conversion is
        // needed -- only a downmix to mono (plan_media.md MEDIA-186).
        void OnPostMix(void* /*userdata*/, int channels, float* pcm, int samples)
        {
            g_visualizationCapture.Push(pcm, samples, channels > 0 ? channels : 1);
        }

        void DestroyMusicAudio()
        {
            g_musicAudio.reset();
        }

        void DestroyMusicTrack()
        {
            if (g_musicTrack)
            {
                CNA::Internal::Audio::StopMixerTrack(g_musicTrack);
                CNA::Internal::Audio::DestroyMixerTrack(g_musicTrack);
                g_musicTrack = nullptr;
            }
        }

        void ApplyMusicVolume(float vol, bool muted)
        {
            if (g_musicTrack)
            {
                CNA::Internal::Audio::SetMixerTrackGain(g_musicTrack, muted ? 0.0f : vol);
            }
        }
    }
#endif

    // --- properties ---

    bool MediaPlayer::getGameHasControlProperty()
    {
        return true;
    }

    bool MediaPlayer::getIsMutedProperty()
    {
        return isMuted_;
    }

    void MediaPlayer::setIsMutedProperty(bool value)
    {
        isMuted_ = value;
#ifdef SOUND_ENABLED
        ApplyMusicVolume(volume_, isMuted_);
#endif
    }

    bool MediaPlayer::getIsRepeatingProperty()
    {
        return isRepeating_;
    }

    void MediaPlayer::setIsRepeatingProperty(bool value)
    {
        isRepeating_ = value;
    }

    bool MediaPlayer::getIsShuffledProperty()
    {
        return isShuffled_;
    }

    void MediaPlayer::setIsShuffledProperty(bool value)
    {
        isShuffled_ = value;
    }

    System::TimeSpan MediaPlayer::getPlayPositionProperty()
    {
        return TimerElapsed();
    }

    MediaQueue& MediaPlayer::getQueueProperty()
    {
        return queue_;
    }

    MediaState MediaPlayer::getStateProperty()
    {
        return state_;
    }

    void MediaPlayer::setStateProperty(MediaState value)
    {
        if (state_ != value)
        {
            state_ = value;
            Microsoft::Xna::Framework::FrameworkDispatcher::MediaStateChanged = true;
        }
    }

    float MediaPlayer::getVolumeProperty()
    {
        return volume_;
    }

    void MediaPlayer::setVolumeProperty(float value)
    {
        volume_ = Microsoft::Xna::Framework::MathHelper::Clamp(value, 0.0f, 1.0f);
#ifdef SOUND_ENABLED
        ApplyMusicVolume(volume_, isMuted_);
#endif
    }

    bool MediaPlayer::getIsVisualizationEnabledProperty()
    {
        return g_visualizationEnabled;
    }

    void MediaPlayer::setIsVisualizationEnabledProperty(bool value)
    {
        // Early-out only when the request is genuinely already satisfied. Checking the flag alone
        // was not enough: after a FAILED uninstall the flag is false while a callback is still
        // installed, and a later set(false) would return here and never retry it (plan_media.md
        // MEDIA-226).
        if (g_visualizationEnabled == value && g_visualizationTapInstalled == value)
        {
            return;
        }

        // The flag is assigned only from what ACTUALLY happened, never up front. An earlier version
        // set it before even obtaining the mixer, so a mixer-creation failure left
        // IsVisualizationEnabled reporting true with no mixer and no callback (plan_media.md
        // MEDIA-222).
#ifdef SOUND_ENABLED
        try
        {
            if (!value)
            {
                // Disabling always ends with the user-visible flag false: the caller asked for off
                // and no data will be served either way.
                g_visualizationEnabled = false;

                if (CNA::Internal::Audio::SetMixerPostMixCallback(nullptr, nullptr))
                {
                    // Uninstall confirmed -- nothing can be writing, so clearing is safe.
                    g_visualizationTapInstalled = false;
                    g_visualizationCapture.Reset();
                }
                // Uninstall FAILED: the callback may still be live, so deliberately do NOT Reset()
                // -- zeroing a buffer another thread is writing is the exact race MEDIA-216 fixed.
                // g_visualizationTapInstalled stays true so a later call retries this.
                return;
            }

            // Enabling: only report enabled once a tap is genuinely installed.
            // Clear BEFORE installing: once the callback is live the audio thread owns the buffer.
            g_visualizationCapture.Reset();
            if (CNA::Internal::Audio::SetMixerPostMixCallback(OnPostMix, nullptr))
            {
                g_visualizationTapInstalled = true;
                g_visualizationEnabled = true;
            }
            // Install failed -> both stay false, matching reality.
        }
        catch (const std::exception&)
        {
            // Mixer creation throws when no audio device can be created at all (e.g. a headless
            // machine). No callback was installed, so clearing is safe -- but enabling must not
            // be reported as successful.
            g_visualizationCapture.Reset();
            g_visualizationTapInstalled = false;
            g_visualizationEnabled = false;
        }
#else
        // No audio backend compiled in: there is no audio thread, so the flag round-trips and the
        // buffer simply stays empty.
        g_visualizationEnabled = value;
        g_visualizationTapInstalled = false;
        g_visualizationCapture.Reset();
#endif
    }

    // --- public methods ---

    void MediaPlayer::MoveNext()
    {
        NextSong(1);
    }

    void MediaPlayer::MovePrevious()
    {
        NextSong(-1);
    }

    void MediaPlayer::Pause()
    {
        if (getStateProperty() != MediaState::Playing || queue_.getActiveSongProperty() == nullptr)
        {
            return;
        }

#ifdef SOUND_ENABLED
        if (g_musicTrack)
        {
            CNA::Internal::Audio::PauseMixerTrack(g_musicTrack);
        }
#endif
        TimerStop();
        setStateProperty(MediaState::Paused);
    }

    void MediaPlayer::Play(Song* song)
    {
        Song* previousSong = queue_.getCountProperty() > 0 ? queue_[0] : nullptr;

        queue_.Clear();
        numSongsInQueuePlayed_ = 0;
        LoadSong(song);
        queue_.setActiveSongIndexProperty(0);

        PlaySong(song);

        if (previousSong != song)
        {
            Microsoft::Xna::Framework::FrameworkDispatcher::ActiveSongChanged = true;
        }
    }

    void MediaPlayer::Play(const SongCollection& songs)
    {
        Play(songs, 0);
    }

    void MediaPlayer::Play(const SongCollection& songs, SharpRuntime::intcs index)
    {
        queue_.Clear();
        numSongsInQueuePlayed_ = 0;

        for (Song* song : songs)
        {
            LoadSong(song);
        }

        queue_.setActiveSongIndexProperty(index);
        PlaySong(queue_.getActiveSongProperty());
    }

    void MediaPlayer::Resume()
    {
        if (getStateProperty() != MediaState::Paused)
        {
            return;
        }

#ifdef SOUND_ENABLED
        if (g_musicTrack)
        {
            CNA::Internal::Audio::ResumeMixerTrack(g_musicTrack);
        }
#endif
        TimerStart();
        setStateProperty(MediaState::Playing);
    }

    void MediaPlayer::Stop()
    {
        if (getStateProperty() == MediaState::Stopped)
        {
            return;
        }

#ifdef SOUND_ENABLED
        DestroyMusicTrack();
        DestroyMusicAudio();
        g_songEnded.store(false, std::memory_order_relaxed);
#endif
        TimerStop();
        TimerReset();

        for (SharpRuntime::intcs i = 0; i < queue_.getCountProperty(); ++i)
        {
            if (queue_[i] != nullptr)
            {
                queue_[i]->setPlayCountProperty(0);
            }
        }

        setStateProperty(MediaState::Stopped);
    }

    void MediaPlayer::GetVisualizationData(VisualizationData& data)
    {
        // XNA only produces visualization data while it is switched on; with it off (or with
        // nothing captured yet) the arrays stay zeroed, matching VisualizationData's own
        // zero-initialized construction rather than throwing (plan_media.md MEDIA-189).
        if (!g_visualizationEnabled || !g_visualizationCapture.HasData())
        {
            data.samp.fill(0.0f);
            data.freq.fill(0.0f);
            return;
        }

        // Sample domain: the most recent VisualizationData::Size mono samples.
        g_visualizationCapture.Read(data.samp.data(), data.samp.size());

        // Frequency domain: FFT over a larger window ending at the same point, so the 256 bins
        // XNA exposes come from a 512-sample transform rather than a needlessly short one.
        std::array<float, CNA::Internal::Media::VisualizationFFT::InputSize> window{};
        g_visualizationCapture.Read(window.data(), window.size());

        std::array<float, CNA::Internal::Media::VisualizationFFT::BinCount> bins{};
        CNA::Internal::Media::VisualizationFFT::ComputeMagnitudes(window, bins);

        static_assert(CNA::Internal::Media::VisualizationFFT::BinCount ==
                        static_cast<std::size_t>(VisualizationData::Size),
                        "FFT bin count must match VisualizationData::Size");
        std::copy(bins.begin(), bins.end(), data.freq.begin());
    }

    bool MediaPlayer::DetectSongEndedByElapsedTime(Song* activeSong, System::TimeSpan elapsed)
    {
        if (activeSong == nullptr)
        {
            return false;
        }
        System::TimeSpan duration = activeSong->getDurationProperty();
        return duration > System::TimeSpan::Zero && elapsed >= duration;
    }

    void MediaPlayer::Update()
    {
        Song* activeSong = queue_.getActiveSongProperty();
        if (activeSong == nullptr || getStateProperty() != MediaState::Playing)
        {
            return;
        }

#ifdef SOUND_ENABLED
        bool songEnded = g_songEnded.exchange(false, std::memory_order_relaxed);
#else
        // No native track-stopped signal in this build configuration -- fall back to comparing
        // wall-clock elapsed time against the song's known duration (plan_media.md MEDIA-32).
        bool songEnded = DetectSongEndedByElapsedTime(activeSong, TimerElapsed());
#endif
        if (!songEnded)
        {
            return;
        }

        numSongsInQueuePlayed_ += 1;

        if (numSongsInQueuePlayed_ >= queue_.getCountProperty())
        {
            numSongsInQueuePlayed_ = 0;
            if (!isRepeating_)
            {
                Stop();
                Microsoft::Xna::Framework::FrameworkDispatcher::ActiveSongChanged = true;
                return;
            }
        }

        MoveNext();
    }

    void MediaPlayer::OnActiveSongChanged()
    {
        ActiveSongChanged.Raise(nullptr, System::EventArgs::Empty);
    }

    void MediaPlayer::OnMediaStateChanged()
    {
        MediaStateChanged.Raise(nullptr, System::EventArgs::Empty);
    }

    void MediaPlayer::ProgramExit()
    {
        if (initialized_)
        {
#ifdef SOUND_ENABLED
            DestroyMusicTrack();
            DestroyMusicAudio();
#endif
            initialized_ = false;
        }
    }

    // --- private helpers ---

    void MediaPlayer::LoadSong(Song* song)
    {
        if (song == nullptr)
        {
            return;
        }
        queue_.Add(new Song(song->getHandle(), song->getNameProperty()));
    }

    void MediaPlayer::NextSong(SharpRuntime::intcs direction)
    {
        Stop();

        if (queue_.getCountProperty() == 0)
        {
            return;
        }

        if (isRepeating_ && queue_.getActiveSongIndexProperty() >= queue_.getCountProperty() - 1)
        {
            queue_.setActiveSongIndexProperty(0);
            direction = 0;
        }

        if (isShuffled_)
        {
            std::uniform_int_distribution<SharpRuntime::intcs> dist(
                0, queue_.getCountProperty() - 1
            );
            queue_.setActiveSongIndexProperty(dist(random_));
        }
        else
        {
            queue_.setActiveSongIndexProperty(
                static_cast<SharpRuntime::intcs>(
                    Microsoft::Xna::Framework::MathHelper::Clamp(
                        queue_.getActiveSongIndexProperty() + direction,
                        0,
                        queue_.getCountProperty() - 1
                    )
                )
            );
        }

        Song* nextSong = queue_[queue_.getActiveSongIndexProperty()];
        if (nextSong != nullptr)
        {
            PlaySong(nextSong);
        }

        Microsoft::Xna::Framework::FrameworkDispatcher::ActiveSongChanged = true;
    }

    void MediaPlayer::PlaySong(Song* song)
    {
        if (song == nullptr)
        {
            return;
        }

        if (!initialized_)
        {
            initialized_ = true;
        }

#ifdef SOUND_ENABLED
        // Stop and release any previously playing music.
        DestroyMusicTrack();
        DestroyMusicAudio();
        g_songEnded.store(false, std::memory_order_relaxed);

        // Load the song file (streaming, no full predecode for long music tracks).
        g_musicAudio = CNA::Internal::Audio::LoadMixerAudioFile(
            song->getHandle(), /*predecode=*/false);
        if (!g_musicAudio)
        {
            return;
        }

        g_musicTrack = CNA::Internal::Audio::CreateMixerTrack();
        if (!g_musicTrack)
        {
            DestroyMusicAudio();
            return;
        }

        if (!CNA::Internal::Audio::SetMixerTrackAudio(g_musicTrack, g_musicAudio.get()))
        {
            DestroyMusicTrack();
            DestroyMusicAudio();
            return;
        }

        ApplyMusicVolume(volume_, isMuted_);
        CNA::Internal::Audio::SetMixerTrackStoppedCallback(
            g_musicTrack, OnMusicTrackStopped, nullptr);

        // Report duration from the audio asset.
        const auto format = CNA::Internal::Audio::GetMixerAudioFormat(g_musicAudio.get());
        if (format.sampleRate > 0)
        {
            const std::int64_t frames =
                CNA::Internal::Audio::GetMixerAudioDuration(g_musicAudio.get());
            if (frames > 0)
            {
                song->setDurationProperty(
                    System::TimeSpan::FromSeconds(
                        static_cast<double>(frames) / format.sampleRate
                    )
                );
            }
        }

        if (!CNA::Internal::Audio::PlayMixerTrack(g_musicTrack))
        {
            DestroyMusicTrack();
            DestroyMusicAudio();
            return;
        }

        song->setPlayCountProperty(song->getPlayCountProperty() + 1);
#endif

        TimerReset();
        TimerStart();
        setStateProperty(MediaState::Playing);
    }

    // --- timer ---

    void MediaPlayer::TimerStart()
    {
        if (!timerRunning_)
        {
            timerStart_    = std::chrono::steady_clock::now();
            timerRunning_  = true;
        }
    }

    void MediaPlayer::TimerStop()
    {
        if (timerRunning_)
        {
            accumulatedTime_ += std::chrono::steady_clock::now() - timerStart_;
            timerRunning_    = false;
        }
    }

    void MediaPlayer::TimerReset()
    {
        accumulatedTime_ = std::chrono::duration<double>::zero();
        if (timerRunning_)
        {
            timerStart_ = std::chrono::steady_clock::now();
        }
    }

    System::TimeSpan MediaPlayer::TimerElapsed()
    {
        auto elapsed = accumulatedTime_;
        if (timerRunning_)
        {
            elapsed += std::chrono::steady_clock::now() - timerStart_;
        }
        return System::TimeSpan::FromSeconds(elapsed.count());
    }
}
