// SPDX-License-Identifier: MS-PL
#pragma once

#include <chrono>
#include <random>

#include "CNA/CNAHelper.hpp"
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
    /** @brief Static media playback controller for songs and the active song queue. */
    class MediaPlayer final
    {
    public:
        MediaPlayer() = delete;

        /** @brief Raised when the active song changes. */
        static System::EventHandler<System::EventArgs> ActiveSongChanged;

        /** @brief Raised when the playback state changes. */
        static System::EventHandler<System::EventArgs> MediaStateChanged;

        /**
         * @brief Gets whether the game has control of its own background music.
         *
         * @return true if the game controls music playback; otherwise false.
         */
        [[nodiscard]] static bool getGameHasControlProperty();

        /**
         * @brief Gets whether song playback is muted.
         *
         * @return true if muted; otherwise false.
         */
        [[nodiscard]] static bool getIsMutedProperty();

        /**
         * @brief Sets whether song playback is muted.
         *
         * @param value New muted state.
         */
        static void setIsMutedProperty(bool value);

        /**
         * @brief Gets whether the playback queue repeats.
         *
         * @return true if repeating; otherwise false.
         */
        [[nodiscard]] static bool getIsRepeatingProperty();

        /**
         * @brief Sets whether the playback queue repeats.
         *
         * @param value New repeating state.
         */
        static void setIsRepeatingProperty(bool value);

        /**
         * @brief Gets whether the playback queue is shuffled.
         *
         * @return true if shuffled; otherwise false.
         */
        [[nodiscard]] static bool getIsShuffledProperty();

        /**
         * @brief Sets whether the playback queue is shuffled.
         *
         * @param value New shuffled state.
         */
        static void setIsShuffledProperty(bool value);

        /**
         * @brief Gets the current playback position within the active song.
         *
         * @return Current play position as a TimeSpan.
         */
        [[nodiscard]] static System::TimeSpan getPlayPositionProperty();

        /**
         * @brief Gets the media queue managed by this player.
         *
         * @return Reference to the MediaQueue.
         */
        [[nodiscard]] static MediaQueue& getQueueProperty();

        /**
         * @brief Gets the current playback state.
         *
         * @return Current MediaState.
         */
        [[nodiscard]] static MediaState getStateProperty();

        /**
         * @brief Gets the current playback volume. Range [0, 1].
         *
         * @return Current volume.
         */
        [[nodiscard]] static float getVolumeProperty();

        /**
         * @brief Sets the current playback volume. Values are clamped to [0, 1].
         *
         * @param value New volume.
         */
        static void setVolumeProperty(float value);

        /**
         * @brief Gets whether visualization data collection is enabled.
         *
         * @return true if visualization is enabled; otherwise false.
         */
        [[nodiscard]] static bool getIsVisualizationEnabledProperty();

        /**
         * @brief Enables or disables visualization data collection.
         *
         * @param value New visualization enabled state.
         */
        static void setIsVisualizationEnabledProperty(bool value);

        /** @brief Advances playback to the next song in the queue. */
        static void MoveNext();

        /** @brief Moves playback back to the previous song in the queue. */
        static void MovePrevious();

        /** @brief Pauses playback when a song is currently playing. */
        static void Pause();

        /**
         * @brief Clears the queue, enqueues the given song, and starts playback.
         *
         * @param song Song to play.
         */
        static void Play(Song* song);

        /**
         * @brief Clears the queue, enqueues the collection, and starts at the first song.
         *
         * @param songs Collection of songs to enqueue.
         */
        static void Play(const SongCollection& songs);

        /**
         * @brief Clears the queue, enqueues the collection, and starts at the specified index.
         *
         * @param songs Collection of songs to enqueue.
         * @param index Zero-based index of the first song to play.
         */
        static void Play(const SongCollection& songs, SharpRuntime::intcs index);

        /** @brief Resumes playback from a paused state. */
        static void Resume();

        /** @brief Stops playback and resets the active queue playback state. */
        static void Stop();

        /**
         * @brief Fills visualization data for the current song when supported by the renderer.
         *
         * @param data Visualization data buffer to populate.
         */
        static void GetVisualizationData(VisualizationData& data);

        /** @brief Performs pending media-player maintenance (timer updates, state transitions). */
        NOXNA static void Update();

        /** @brief Raises the deferred ActiveSongChanged event. */
        NOXNA static void OnActiveSongChanged();

        /** @brief Raises the deferred MediaStateChanged event. */
        NOXNA static void OnMediaStateChanged();

        /** @brief Releases renderer media resources if initialized. Called at application exit. */
        NOXNA static void ProgramExit();

        /**
         * @brief Fallback song-end detector for builds without a native track-stopped signal
         * (i.e. without SOUND_ENABLED).
         *
         * Compares elapsed playback time against the song's known duration. Only reports "ended"
         * when duration is genuinely known (greater than zero) -- an unset/zero Duration (e.g. a
         * Song constructed without one) can't be detected this way, so playback simply never
         * auto-advances in that case, rather than false-triggering immediately at time zero.
         * Always compiled (not gated by SOUND_ENABLED) so it can be exercised directly by tests
         * regardless of which audio backend a given build has.
         *
         * @param activeSong The currently active song (may be nullptr).
         * @param elapsed    Elapsed playback time since the song started.
         * @return true if the song should be considered ended.
         */
        NOXNA [[nodiscard]] static bool DetectSongEndedByElapsedTime(
            Song* activeSong, System::TimeSpan elapsed);

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
