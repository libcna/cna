// SPDX-License-Identifier: MS-PL
#pragma once

#include <vector>

#include "CNA/Internal/Media/MediaObjectDisposal.hpp"
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
        /// typeName is the public collection's own name, which a disposed refusal reports.
        MediaCollectionBase(std::vector<T*> items, const char* typeName)
            : items_(std::move(items)), typeName_(typeName) {}

        /// Count, the indexer and enumeration are refused once disposed, as XNA's are.
        [[nodiscard]] SharpRuntime::intcs Count() const
        {
            ThrowIfDisposed(isDisposed_, typeName_);
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
            ThrowIfDisposed(isDisposed_, typeName_);
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

        iterator begin() { ThrowIfDisposed(isDisposed_, typeName_); return items_.begin(); }
        iterator end() { ThrowIfDisposed(isDisposed_, typeName_); return items_.end(); }
        [[nodiscard]] const_iterator begin() const
        {
            ThrowIfDisposed(isDisposed_, typeName_);
            return items_.begin();
        }
        [[nodiscard]] const_iterator end() const
        {
            ThrowIfDisposed(isDisposed_, typeName_);
            return items_.end();
        }

    private:
        std::vector<T*> items_;
        const char* typeName_;
        bool isDisposed_ = false;
    };
}
