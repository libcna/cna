// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Net/AvailableNetworkSessionCollection.hpp"

namespace Microsoft::Xna::Framework::Net
{
    AvailableNetworkSessionCollection::AvailableNetworkSessionCollection(std::vector<AvailableNetworkSession> sessions)
        : ReadOnlyCollection<AvailableNetworkSession>(std::move(sessions))
    {
    }

    AvailableNetworkSessionCollection AvailableNetworkSessionCollection::CreateInternal(
        std::vector<AvailableNetworkSession> sessions
    ) {
        return AvailableNetworkSessionCollection(std::move(sessions));
    }

    bool AvailableNetworkSessionCollection::getIsDisposedProperty() const { return isDisposed_; }

    void AvailableNetworkSessionCollection::Dispose()
    {
        isDisposed_ = true;
    }
}
