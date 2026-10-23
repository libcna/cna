// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Media/SongCollection.hpp"

#include "CNA/Internal/Media/MediaObjectDisposal.hpp"
#include "System/ArgumentOutOfRangeException.hpp"

namespace Microsoft::Xna::Framework::Media
{
    using CNA::Internal::Media::ThrowIfDisposed;

    SongCollection::SongCollection(std::vector<Song*> songs)
        : innerList_(std::move(songs)),
          isDisposed_(false)
    {
    }

    Song* SongCollection::operator[](SharpRuntime::intcs index) const
    {
        // FNA throws ArgumentOutOfRangeException (implicitly, via the underlying List<T>
        // indexer -- SongCollection.cs). Matches the majority project precedent (BoundingBox/
        // VertexBuffer/NetworkSessionProperties), not TouchCollection's std::out_of_range
        // outlier -- see plans/plan_media.md Section 2 item 7.
        ThrowIfDisposed(isDisposed_, "SongCollection");
        if (index < 0 || index >= getCountProperty())
        {
            throw System::ArgumentOutOfRangeException("index");
        }

        return innerList_[static_cast<std::size_t>(index)];
    }

    SharpRuntime::intcs SongCollection::getCountProperty() const
    {
        ThrowIfDisposed(isDisposed_, "SongCollection");
        return static_cast<SharpRuntime::intcs>(innerList_.size());
    }

    bool SongCollection::getIsDisposedProperty() const
    {
        return isDisposed_;
    }

    void SongCollection::Dispose()
    {
        innerList_.clear();
        isDisposed_ = true;
    }

    SongCollection::iterator SongCollection::begin()
    {
        ThrowIfDisposed(isDisposed_, "SongCollection");
        return innerList_.begin();
    }

    SongCollection::iterator SongCollection::end()
    {
        ThrowIfDisposed(isDisposed_, "SongCollection");
        return innerList_.end();
    }

    SongCollection::const_iterator SongCollection::begin() const
    {
        ThrowIfDisposed(isDisposed_, "SongCollection");
        return innerList_.begin();
    }

    SongCollection::const_iterator SongCollection::end() const
    {
        ThrowIfDisposed(isDisposed_, "SongCollection");
        return innerList_.end();
    }

    const std::string& SongCollection::GetTypeName() const
    {
        static const std::string typeName = "Microsoft.Xna.Framework.Media.SongCollection";
        return typeName;
    }
}
