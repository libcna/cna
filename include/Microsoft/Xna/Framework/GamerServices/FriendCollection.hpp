// SPDX-License-Identifier: MS-PL
#pragma once
#include "CNA/CNAHelper.hpp"
#include "Microsoft/Xna/Framework/GamerServices/GamerCollection.hpp"
#include "Microsoft/Xna/Framework/GamerServices/FriendGamer.hpp"
#include "System/IDisposable.hpp"
#include <vector>

namespace Microsoft::Xna::Framework::GamerServices
{
    /**
     * @brief A disposable read-only collection of FriendGamer objects.
     *
     * Task 7.12: like `GamerCollection<T>` in general, this is a non-owning view over whatever
     * `FriendGamer*` pointers it was constructed with - `Dispose()` never deletes them, matching
     * FNA's own real `FriendCollection.Dispose()` (`collection.Clear(); IsDisposed = true;`,
     * relying on .NET's GC for the actual `FriendGamer` objects, which this C++ port has no
     * equivalent for). `SignedInGamer::GetFriends()` only ever constructs an empty stub
     * `FriendCollection` today, so no real leak is possible in practice yet - but whoever
     * eventually implements real friend-list population is responsible for its own explicit
     * ownership story for the `FriendGamer` objects it creates (mirroring
     * `NetworkSession::ownedGamers_`/`ENetBackend::SessionState::OwnedRemoteGamers`'s pattern, or
     * `GamerServicesDispatcher::Initialize()`'s explicit free-before-replace pattern), not this
     * collection.
     */
    class FriendCollection : public GamerCollection<FriendGamer>, public System::IDisposable
    {
    public:
        /**
         * @brief Gets whether this collection has been disposed.
         *
         * @return true if disposed.
         */
        [[nodiscard]] bool getIsDisposedProperty() const;

        /**
         * @brief Clears the collection and marks it as disposed. Does not free the FriendGamer
         * pointers themselves - see the class's own doc comment for the ownership contract.
         */
        void Dispose() override;

        /** @brief Creates a FriendCollection for CNA internal use. */
        NOXNA static FriendCollection CreateInternal(std::vector<FriendGamer*> friends);

    private:
        explicit FriendCollection(std::vector<FriendGamer*> friends);

        bool isDisposed_{false};
    };
}
