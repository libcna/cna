#pragma once

#include <memory>
#include <string>
#include <vector>

#include "Microsoft/Xna/Framework/Media/Song.hpp"
#include "SharpRuntime/SharpRuntimeHelper.hpp"
#include "System/Object.hpp"

namespace Microsoft::Xna::Framework::Media
{
    /// Playback queue used by MediaPlayer.
    class MediaQueue final : public System::Object
    {
    public:
        /// Creates an empty media queue with no active song.
        MediaQueue();

        MediaQueue(const MediaQueue&) = delete;
        MediaQueue& operator=(const MediaQueue&) = delete;
        MediaQueue(MediaQueue&&) = default;
        MediaQueue& operator=(MediaQueue&&) = default;
        ~MediaQueue() override = default;

        /// Gets the active song, or nullptr when the queue is empty or no active index is selected.
        [[nodiscard]] Song* getActiveSongProperty() const;

        /// Gets the active song index.
        [[nodiscard]] SharpRuntime::intcs getActiveSongIndexProperty() const;

        /// Sets the active song index.
        void setActiveSongIndexProperty(SharpRuntime::intcs value);

        /// Gets the number of songs in the queue.
        [[nodiscard]] SharpRuntime::intcs getCountProperty() const;

        /// Gets the song at the specified index.
        [[nodiscard]] Song* operator[](SharpRuntime::intcs index) const;

        /// Adds a song to the queue. The queue takes ownership of the pointer.
        void Add(Song* song);

        /// Clears the queue and resets ownership of queued song objects.
        void Clear();

        [[nodiscard]] const std::string& GetTypeName() const override;

    private:
        std::vector<std::unique_ptr<Song>> songs_;
        SharpRuntime::intcs activeSongIndex_;
    };
}
