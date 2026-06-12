// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Media/PictureCollection.hpp"

#include <stdexcept>

namespace Microsoft::Xna::Framework::Media
{
    void PictureCollection::Dispose()
    {
        // TODO: implement collection cleanup
        throw std::runtime_error("not implemented");
    }

    SharpRuntime::intcs PictureCollection::getCountProperty() const
    {
        // TODO: implement media library catalog access
        throw std::runtime_error("not implemented");
    }

    bool PictureCollection::getIsDisposedProperty() const
    {
        // TODO: implement media library catalog access
        throw std::runtime_error("not implemented");
    }

    Picture* PictureCollection::operator[](SharpRuntime::intcs index) const
    {
        // TODO: implement media library catalog access
        throw std::runtime_error("not implemented");
    }

    PictureCollection::iterator PictureCollection::begin()
    {
        return innerList_.begin();
    }

    PictureCollection::iterator PictureCollection::end()
    {
        return innerList_.end();
    }

    PictureCollection::const_iterator PictureCollection::begin() const
    {
        return innerList_.begin();
    }

    PictureCollection::const_iterator PictureCollection::end() const
    {
        return innerList_.end();
    }

    const std::string& PictureCollection::GetTypeName() const
    {
        static const std::string typeName = "Microsoft.Xna.Framework.Media.PictureCollection";
        return typeName;
    }
}
