// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/GamerServices/FriendCollection.hpp"

namespace Microsoft::Xna::Framework::GamerServices
{
    FriendCollection::FriendCollection(std::vector<FriendGamer*> friends)
        : GamerCollection<FriendGamer>(std::move(friends))
    {
    }

    FriendCollection FriendCollection::CreateInternal(std::vector<FriendGamer*> friends)
    {
        return FriendCollection(std::move(friends));
    }

    bool FriendCollection::getIsDisposedProperty() const { return isDisposed_; }

    void FriendCollection::Dispose()
    {
        if (!isDisposed_)
        {
            collection_.clear();
            isDisposed_ = true;
        }
    }
}
