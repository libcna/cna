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
         * @brief Clears the collection and marks it as disposed.
         */
        void Dispose() override;

        /** @brief Creates a FriendCollection for CNA internal use. */
        NOXNA static FriendCollection CreateInternal(std::vector<FriendGamer*> friends);

    private:
        explicit FriendCollection(std::vector<FriendGamer*> friends);

        bool isDisposed_{false};
    };
}
