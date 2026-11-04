// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Media/MediaQueue.hpp"

#include "System/ArgumentOutOfRangeException.hpp"
#include <algorithm>

namespace Microsoft::Xna::Framework::Media
{
    MediaQueue::MediaQueue()
        : activeSongIndex_(-1)
    {
    }

    Song* MediaQueue::getActiveSongProperty() const
    {
        if (songs_.empty() || activeSongIndex_ < 0 || activeSongIndex_ >= getCountProperty())
        {
            return nullptr;
        }

        return songs_[static_cast<std::size_t>(activeSongIndex_)].get();
    }

    SharpRuntime::intcs MediaQueue::getActiveSongIndexProperty() const
    {
        return activeSongIndex_;
    }

    void MediaQueue::setActiveSongIndexProperty(SharpRuntime::intcs value)
    {
        // XNA set_ActiveSongIndex IL_0000..0020 clamps low first, then high (empty => -1).
        activeSongIndex_ = std::min(std::max(value, 0), getCountProperty() - 1);
    }

    SharpRuntime::intcs MediaQueue::getCountProperty() const
    {
        return static_cast<SharpRuntime::intcs>(songs_.size());
    }

    Song* MediaQueue::operator[](SharpRuntime::intcs index) const
    {
        // FNA throws ArgumentOutOfRangeException (implicitly, via the underlying List<T>
        // indexer -- MediaQueue.cs). Matches the majority project precedent (BoundingBox/
        // VertexBuffer/NetworkSessionProperties), not TouchCollection's std::out_of_range
        // outlier -- see CHECKLIST.md's project-wide out-of-range row.
        if (index < 0 || index >= getCountProperty())
        {
            throw System::ArgumentOutOfRangeException("index");
        }

        return songs_[static_cast<std::size_t>(index)].get();
    }

    void MediaQueue::Add(Song* song)
    {
        songs_.emplace_back(song);
    }

    void MediaQueue::Clear()
    {
        songs_.clear();
        activeSongIndex_ = -1;
    }

    const std::string& MediaQueue::GetTypeName() const
    {
        static const std::string typeName = "Microsoft.Xna.Framework.Media.MediaQueue";
        return typeName;
    }
}
