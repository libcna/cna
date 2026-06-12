// SPDX-License-Identifier: MS-PL
#pragma once

#include <chrono>
#include <random>

#include "Microsoft/Xna/Framework/FrameworkDispatcher.hpp"
#include "Microsoft/Xna/Framework/MathHelper.hpp"
#include "Microsoft/Xna/Framework/Media/MediaQueue.hpp"
#include "Microsoft/Xna/Framework/Media/MediaState.hpp"
#include "Microsoft/Xna/Framework/Media/Song.hpp"
#include "Microsoft/Xna/Framework/Media/SongCollection.hpp"
#include "Microsoft/Xna/Framework/Media/VisualizationData.hpp"
#include "System/EventArgs.hpp"
#include "System/EventHandler.hpp"
#include "System/TimeSpan.hpp"

namespace Microsoft::Xna::Framework::Media
{
    /// Static media playback controller for songs and the active song queue.
    class MediaPlayer final
    {
    public:
        MediaPlayer() = delete;

        /// Raised when the active song changes.
        static System::EventHandler<System::EventArgs> ActiveSongChanged;

        /// Raised when the media playback state changes.
        static System::EventHandler<System::EventArgs> MediaStateChanged;

        /// Returns whether the game has control of its own music playback.
        [[nodiscard]] static bool getGameHasControlProperty();

        /// Gets whether song playback is muted.
        [[nodiscard]] static bool getIsMutedProperty();

        /// Sets whether song playback is muted.
        static void setIsMutedProperty(bool value);

        /// Gets whether the playback queue repeats.
        [[nodiscard]] static bool getIsRepeatingProperty();

        /// Sets whether the playback queue repeats.
        static void setIsRepeatingProperty(bool value);

        /// Gets whether the playback queue is shuffled.
        [[nodiscard]] static bool getIsShuffledProperty();

        /// Sets whether the playback queue is shuffled.
        static void setIsShuffledProperty(bool value);

        /// Gets the current play position.
        [[nodiscard]] static System::TimeSpan getPlayPositionProperty();

        /// Gets the media queue used by the media player.
        [[nodiscard]] static MediaQueue& getQueueProperty();

        /// Gets the current playback state.
        [[nodiscard]] static MediaState getStateProperty();

        /// Gets the current media volume.
        [[nodiscard]] static float getVolumeProperty();

        /// Sets the current media volume. Values are clamped to [0, 1].
        static void setVolumeProperty(float value);

        /// Gets whether visualization data collection is enabled.
        [[nodiscard]] static bool getIsVisualizationEnabledProperty();

        /// Enables or disables visualization data collection.
        static void setIsVisualizationEnabledProperty(bool value);

        /// Moves playback to the next song.
        static void MoveNext();

        /// Moves playback to the previous song.
        static void MovePrevious();

        /// Pauses playback when a song is playing.
        static void Pause();

        /// Clears the queue, queues one song and starts playback immediately.
        static void Play(Song* song);

        /// Clears the queue, queues the collection and starts at the first song.
        static void Play(const SongCollection& songs);

        /// Clears the queue, queues the collection and starts at the specified index.
        static void Play(const SongCollection& songs, SharpRuntime::intcs index);

        /// Resumes playback from a paused state.
        static void Resume();

        /// Stops playback and resets the active queue playback state.
        static void Stop();

        /// Fills visualization data for the current song when supported by the backend.
        static void GetVisualizationData(VisualizationData& data);

        /// Performs pending media-player maintenance.
        static void Update();

        /// Raises the deferred ActiveSongChanged event.
        static void OnActiveSongChanged();

        /// Raises the deferred MediaStateChanged event.
        static void OnMediaStateChanged();

        /// Releases backend media resources if initialized.
        static void ProgramExit();

    private:
        static bool isMuted_;
        static bool isRepeating_;
        static bool isShuffled_;
        static MediaState state_;
        static float volume_;
        static bool initialized_;
        static SharpRuntime::intcs numSongsInQueuePlayed_;
        static MediaQueue queue_;
        static std::mt19937 random_;

        static bool timerRunning_;
        static std::chrono::steady_clock::time_point timerStart_;
        static std::chrono::duration<double> accumulatedTime_;

        static void setStateProperty(MediaState value);
        static void LoadSong(Song* song);
        static void NextSong(SharpRuntime::intcs direction);
        static void PlaySong(Song* song);

        static void TimerStart();
        static void TimerStop();
        static void TimerReset();
        [[nodiscard]] static System::TimeSpan TimerElapsed();
    };
}
