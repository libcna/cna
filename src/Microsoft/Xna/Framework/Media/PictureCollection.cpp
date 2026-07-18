// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Media/PictureCollection.hpp"

namespace Microsoft::Xna::Framework::Media
{
    PictureCollection::PictureCollection(std::vector<Picture*> pictures)
        : base_(std::move(pictures))
    {
    }

    void PictureCollection::Dispose()
    {
        base_.Dispose();
    }

    SharpRuntime::intcs PictureCollection::getCountProperty() const
    {
        return base_.Count();
    }

    bool PictureCollection::getIsDisposedProperty() const
    {
        return base_.IsDisposed();
    }

    Picture* PictureCollection::operator[](SharpRuntime::intcs index) const
    {
        return base_.At(index);
    }

    PictureCollection::iterator PictureCollection::begin()
    {
        return base_.begin();
    }

    PictureCollection::iterator PictureCollection::end()
    {
        return base_.end();
    }

    PictureCollection::const_iterator PictureCollection::begin() const
    {
        return base_.begin();
    }

    PictureCollection::const_iterator PictureCollection::end() const
    {
        return base_.end();
    }

    const std::string& PictureCollection::GetTypeName() const
    {
        static const std::string typeName = "Microsoft.Xna.Framework.Media.PictureCollection";
        return typeName;
    }
}
