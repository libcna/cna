// SPDX-License-Identifier: MS-PL
#pragma once

#include <vector>

#include "SharpRuntime/SharpRuntimeHelper.hpp"
#include "System/ArgumentOutOfRangeException.hpp"

namespace CNA::Internal::Media
{
    /// Shared storage/indexer/enumerator/dispose renderer for the 6 public XNA read-only
    /// collection types (AlbumCollection/ArtistCollection/GenreCollection/PictureCollection/
    /// PictureAlbumCollection/PlaylistCollection), which are structurally identical in FNA except
    /// for their element type. Public class names, constructors, and exception contracts stay
    /// fully distinct and XNA-faithful in Phase 4 -- only the private implementation is shared
    /// here (plans/plan_media.md MEDIA-55/D9).
    template <typename T>
    class MediaCollectionBase
    {
    public:
        MediaCollectionBase() = default;
        explicit MediaCollectionBase(std::vector<T*> items) : items_(std::move(items)) {}

        [[nodiscard]] SharpRuntime::intcs Count() const
        {
            return static_cast<SharpRuntime::intcs>(items_.size());
        }

        [[nodiscard]] bool IsDisposed() const { return isDisposed_; }

        void Dispose()
        {
            items_.clear();
            isDisposed_ = true;
        }

        [[nodiscard]] T* At(SharpRuntime::intcs index) const
        {
            if (index < 0 || index >= Count())
            {
                throw System::ArgumentOutOfRangeException("index");
            }
            return items_[static_cast<std::size_t>(index)];
        }

        /// Internal-only append (e.g. MediaLibrary::SavePicture growing SavedPictures at
        /// runtime) -- not part of the public XNA read-only-collection contract; only reachable
        /// through a concrete collection class's own friend-gated private members.
        void Add(T* item) { items_.push_back(item); }

        using iterator = typename std::vector<T*>::iterator;
        using const_iterator = typename std::vector<T*>::const_iterator;

        iterator begin() { return items_.begin(); }
        iterator end() { return items_.end(); }
        [[nodiscard]] const_iterator begin() const { return items_.begin(); }
        [[nodiscard]] const_iterator end() const { return items_.end(); }

    private:
        std::vector<T*> items_;
        bool isDisposed_ = false;
    };
}
