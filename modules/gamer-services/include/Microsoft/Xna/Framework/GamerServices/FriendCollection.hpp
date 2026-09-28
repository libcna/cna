// SPDX-License-Identifier: MS-PL
#pragma once
#include "CNA/CNAHelper.hpp"
#include "Microsoft/Xna/Framework/GamerServices/GamerCollection.hpp"
#include "Microsoft/Xna/Framework/GamerServices/FriendGamer.hpp"
#include "System/IDisposable.hpp"
#include <vector>
#include <memory>

namespace Microsoft::Xna::Framework::GamerServices
{
    /**
     * @brief A disposable read-only collection of FriendGamer objects.
     *
     * Service snapshots own their FriendGamer objects for the collection lifetime. Explicit
     * internal factory views retain caller ownership. Dispose clears the visible collection;
     * pointers borrowed from a service snapshot remain alive until the snapshot is destroyed.
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
         * @brief Clears the visible collection and marks it as disposed.
         * Object ownership follows the snapshot/factory contract described above.
         */
        void Dispose() override;

        /** @brief Creates a FriendCollection for CNA internal use. */
        CNAEXT static FriendCollection CreateInternal(std::vector<FriendGamer*> friends);

    private:
        explicit FriendCollection(std::vector<FriendGamer*> friends);

        friend class SignedInGamer;
        std::vector<std::shared_ptr<FriendGamer>> ownedFriends_;
        bool isDisposed_{false};
    };
}
