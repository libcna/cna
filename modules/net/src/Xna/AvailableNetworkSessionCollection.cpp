// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Net/AvailableNetworkSessionCollection.hpp"

namespace Microsoft::Xna::Framework::Net
{
    AvailableNetworkSessionCollection::AvailableNetworkSessionCollection(
        std::vector<AvailableNetworkSession> sessions,
        std::shared_ptr<bool> disposed
    )
        : ReadOnlyCollection<AvailableNetworkSession>(std::move(sessions))
        , disposed_(std::move(disposed))
    {
    }

    AvailableNetworkSessionCollection AvailableNetworkSessionCollection::CreateInternal(
        std::vector<AvailableNetworkSession> sessions
    ) {
        // Reference: each listing keeps its parent collection, and disposing that collection makes
        // the listing unjoinable (BeginJoin throws ObjectDisposedException).
        auto disposed = std::make_shared<bool>(false);
        for (auto& session : sessions)
            session.collectionDisposed_ = disposed;
        return AvailableNetworkSessionCollection(std::move(sessions), std::move(disposed));
    }

    bool AvailableNetworkSessionCollection::getIsDisposedProperty() const { return *disposed_; }

    void AvailableNetworkSessionCollection::Dispose()
    {
        *disposed_ = true;
    }
}
