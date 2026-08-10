// SPDX-License-Identifier: MS-PL
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "CNA/CNAHelper.hpp"
#include "Microsoft/Xna/Framework/Media/Song.hpp"
#include "SharpRuntime/SharpRuntimeHelper.hpp"
#include "System/Object.hpp"

namespace Microsoft::Xna::Framework::Media
{
    /** @brief Manages the ordered list of songs used by MediaPlayer. */
    class MediaQueue final : public System::Object
    {
    public:
        /** @brief Creates an empty media queue with no active song. */
        CNAEXT MediaQueue();

        CNAEXT MediaQueue(const MediaQueue&) = delete;
        CNAEXT MediaQueue& operator=(const MediaQueue&) = delete;
        CNAEXT MediaQueue(MediaQueue&&) = default;
        CNAEXT MediaQueue& operator=(MediaQueue&&) = default;

        /** @brief Destroys the media queue. */
        CNAEXT ~MediaQueue() override = default;

        /**
         * @brief Gets the currently active song, or nullptr when the queue is empty.
         *
         * @return Pointer to the active Song, or nullptr.
         */
        [[nodiscard]] Song* getActiveSongProperty() const;

        /**
         * @brief Gets the zero-based index of the active song.
         *
         * @return Active song index.
         */
        [[nodiscard]] SharpRuntime::intcs getActiveSongIndexProperty() const;

        /**
         * @brief Sets the zero-based index of the active song.
         *
         * @param value New active song index.
         */
        void setActiveSongIndexProperty(SharpRuntime::intcs value);

        /**
         * @brief Gets the number of songs in the queue.
         *
         * @return Song count.
         */
        [[nodiscard]] SharpRuntime::intcs getCountProperty() const;

        /**
         * @brief Gets the song at the specified index.
         *
         * @param index Zero-based index.
         * @return Pointer to the Song at that index.
         */
        [[nodiscard]] Song* operator[](SharpRuntime::intcs index) const;

        /**
         * @brief Adds a song to the end of the queue. The queue takes ownership of the pointer.
         *
         * @param song Song to add.
         */
        CNAEXT void Add(Song* song);

        /** @brief Clears all songs from the queue and resets the active index. */
        CNAEXT void Clear();

        /** @brief Returns the fully-qualified .NET type name. */
        CNAEXT [[nodiscard]] const std::string& GetTypeName() const override;

    private:
        std::vector<std::unique_ptr<Song>> songs_;
        SharpRuntime::intcs activeSongIndex_;
    };
}
