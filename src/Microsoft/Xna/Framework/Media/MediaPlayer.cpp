#include "Microsoft/Xna/Framework/Media/MediaPlayer.hpp"

#include <algorithm>
#include <stdexcept>

namespace Microsoft::Xna::Framework::Media
{
    System::EventHandler<System::EventArgs> MediaPlayer::ActiveSongChanged;
    System::EventHandler<System::EventArgs> MediaPlayer::MediaStateChanged;

    bool MediaPlayer::isMuted_ = false;
    bool MediaPlayer::isRepeating_ = false;
    bool MediaPlayer::isShuffled_ = false;
    MediaState MediaPlayer::state_ = MediaState::Stopped;
    float MediaPlayer::volume_ = 1.0f;
    bool MediaPlayer::initialized_ = false;
    SharpRuntime::intcs MediaPlayer::numSongsInQueuePlayed_ = 0;
    MediaQueue MediaPlayer::queue_;
    std::mt19937 MediaPlayer::random_(std::random_device{}());

    bool MediaPlayer::timerRunning_ = false;
    std::chrono::steady_clock::time_point MediaPlayer::timerStart_;
    std::chrono::duration<double> MediaPlayer::accumulatedTime_ = std::chrono::duration<double>::zero();

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

        // Backend hook: apply song volume as either 0.0f or volume_.
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

        // Backend hook: apply song volume as either 0.0f or volume_.
    }

    bool MediaPlayer::getIsVisualizationEnabledProperty()
    {
        // Backend hook: query visualization state.
        return false;
    }

    void MediaPlayer::setIsVisualizationEnabledProperty(bool value)
    {
        (void)value;
        // Backend hook: enable or disable visualization.
    }

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

        // Backend hook: pause current song.
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

        // Backend hook: resume current song.
        TimerStart();
        setStateProperty(MediaState::Playing);
    }

    void MediaPlayer::Stop()
    {
        if (getStateProperty() == MediaState::Stopped)
        {
            return;
        }

        // Backend hook: stop current song.
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
        (void)data;
        // Backend hook: fill frequency/sample visualization arrays.
    }

    void MediaPlayer::Update()
    {
        if (queue_.getActiveSongProperty() == nullptr || getStateProperty() != MediaState::Playing)
        {
            return;
        }

        // Backend hook: return here while the current song has not ended.
        // Until backend song-end detection exists, there is no automatic advancement.
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
            // Backend hook: quit song subsystem.
            initialized_ = false;
        }
    }

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

        if (getIsRepeatingProperty() && queue_.getActiveSongIndexProperty() >= queue_.getCountProperty() - 1)
        {
            queue_.setActiveSongIndexProperty(0);
            direction = 0;
        }

        if (getIsShuffledProperty())
        {
            std::uniform_int_distribution<SharpRuntime::intcs> distribution(0, queue_.getCountProperty() - 1);
            queue_.setActiveSongIndexProperty(distribution(random_));
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
            // Backend hook: initialize song subsystem.
            initialized_ = true;
        }

        // Backend hook: play song and use returned duration.
        // song->setDurationProperty(System::TimeSpan::FromSeconds(...));

        TimerStart();
        setStateProperty(MediaState::Playing);
    }

    void MediaPlayer::TimerStart()
    {
        if (!timerRunning_)
        {
            timerStart_ = std::chrono::steady_clock::now();
            timerRunning_ = true;
        }
    }

    void MediaPlayer::TimerStop()
    {
        if (timerRunning_)
        {
            accumulatedTime_ += std::chrono::steady_clock::now() - timerStart_;
            timerRunning_ = false;
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
