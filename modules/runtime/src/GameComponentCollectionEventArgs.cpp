// SPDX-License-Identifier: MS-PL

#include "Microsoft/Xna/Framework/GameComponentCollectionEventArgs.hpp"

namespace Microsoft::Xna::Framework
{
    GameComponentCollectionEventArgs::GameComponentCollectionEventArgs(IGameComponent* gameComponent)
        : gameComponent_(gameComponent)
    {
    }

    IGameComponent* GameComponentCollectionEventArgs::getGameComponentProperty() const
    {
        return gameComponent_;
    }
}
