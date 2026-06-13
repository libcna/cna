// SPDX-License-Identifier: MS-PL

#pragma once

#include "Microsoft/Xna/Framework/IGameComponent.hpp"
#include "System/EventArgs.hpp"

namespace Microsoft::Xna::Framework
{
    /** @brief Event arguments used when a game component is added to or removed from a collection. */
    class GameComponentCollectionEventArgs : public System::EventArgs
    {
    public:
        /**
         * @brief Creates event arguments for the specified game component.
         * @param gameComponent Pointer to the component associated with the event.
         */
        explicit GameComponentCollectionEventArgs(IGameComponent* gameComponent);

        /**
         * @brief Gets the game component associated with the collection event.
         * @return Pointer to the game component that was added or removed.
         */
        [[nodiscard]] IGameComponent* getGameComponentProperty() const;

    private:
        IGameComponent* gameComponent_;
    };
}
