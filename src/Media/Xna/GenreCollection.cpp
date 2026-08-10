// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Media/GenreCollection.hpp"

namespace Microsoft::Xna::Framework::Media
{
    GenreCollection::GenreCollection(std::vector<Genre*> genres)
        : base_(std::move(genres))
    {
    }

    void GenreCollection::Dispose()
    {
        base_.Dispose();
    }

    SharpRuntime::intcs GenreCollection::getCountProperty() const
    {
        return base_.Count();
    }

    bool GenreCollection::getIsDisposedProperty() const
    {
        return base_.IsDisposed();
    }

    Genre* GenreCollection::operator[](SharpRuntime::intcs index) const
    {
        return base_.At(index);
    }

    GenreCollection::iterator GenreCollection::begin()
    {
        return base_.begin();
    }

    GenreCollection::iterator GenreCollection::end()
    {
        return base_.end();
    }

    GenreCollection::const_iterator GenreCollection::begin() const
    {
        return base_.begin();
    }

    GenreCollection::const_iterator GenreCollection::end() const
    {
        return base_.end();
    }

    const std::string& GenreCollection::GetTypeName() const
    {
        static const std::string typeName = "Microsoft.Xna.Framework.Media.GenreCollection";
        return typeName;
    }
}
