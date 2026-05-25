#include "Microsoft/Xna/Framework/Media/SongCollection.hpp"

#include <stdexcept>

namespace Microsoft::Xna::Framework::Media
{
    SongCollection::SongCollection(std::vector<Song*> songs)
        : innerList_(std::move(songs)),
          isDisposed_(false)
    {
    }

    Song* SongCollection::operator[](SharpRuntime::intcs index) const
    {
        if (index < 0 || index >= getCountProperty())
        {
            throw std::out_of_range("index");
        }

        return innerList_[static_cast<std::size_t>(index)];
    }

    SharpRuntime::intcs SongCollection::getCountProperty() const
    {
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
        return innerList_.begin();
    }

    SongCollection::iterator SongCollection::end()
    {
        return innerList_.end();
    }

    SongCollection::const_iterator SongCollection::begin() const
    {
        return innerList_.begin();
    }

    SongCollection::const_iterator SongCollection::end() const
    {
        return innerList_.end();
    }

    const std::string& SongCollection::GetTypeName() const
    {
        static const std::string typeName = "Microsoft.Xna.Framework.Media.SongCollection";
        return typeName;
    }
}
