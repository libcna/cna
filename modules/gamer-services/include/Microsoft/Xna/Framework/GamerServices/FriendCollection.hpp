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
     * Task 7.12/10.2: a non-owning view, per `GamerCollection<T>`'s own doc comment (the single
     * canonical statement of this contract) - `Dispose()` never deletes the `FriendGamer*`
     * pointers it was constructed with, matching FNA's own real `FriendCollection.Dispose()`
     * (`collection.Clear(); IsDisposed = true;`). `SignedInGamer::GetFriends()` only ever
     * constructs an empty stub `FriendCollection` today, so no real leak is possible in practice
     * yet - whoever eventually implements real friend-list population must establish its own
     * ownership registry for the `FriendGamer` objects it creates, exactly like
     * `GamerServicesDispatcher::Initialize()` already does for `SignedInGamer`.
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
        CNAEXT static FriendCollection CreateInternal(std::vector<FriendGamer*> friends);

    private:
        explicit FriendCollection(std::vector<FriendGamer*> friends);

        bool isDisposed_{false};
    };
}
