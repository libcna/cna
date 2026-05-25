#pragma once

#include "Microsoft/Xna/Framework/IGameComponent.hpp"
#include "System/EventArgs.hpp"

namespace Microsoft::Xna::Framework
{
    /// Event arguments used when a game component is added to or removed from a collection.
    class GameComponentCollectionEventArgs : public System::EventArgs
    {
    public:
        /// Creates event arguments for the specified game component.
        explicit GameComponentCollectionEventArgs(IGameComponent* gameComponent);

        /// Gets the game component associated with the collection event.
        [[nodiscard]] IGameComponent* getGameComponentProperty() const;

    private:
        IGameComponent* gameComponent_;
    };
}
