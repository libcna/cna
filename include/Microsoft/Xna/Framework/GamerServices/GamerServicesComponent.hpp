// SPDX-License-Identifier: MS-PL
#pragma once

#include "Microsoft/Xna/Framework/GameComponent.hpp"

namespace Microsoft::Xna::Framework::GamerServices
{
    /**
     * @brief A GameComponent that must be added to Game.Components to enable GamerServices.
     *
     * On PC, XNA 4.0 required this component to be registered before calling any
     * Guide or Gamer API. CNA provides a no-op stub; GamerServices are not available
     * on any CNA platform.
     *
     * @note CNA_STUB: XNA 4.0 API surface placeholder. Behavior is not implemented yet.
     */
    class GamerServicesComponent : public Microsoft::Xna::Framework::GameComponent
    {
    public:
        /**
         * @brief Constructs a GamerServicesComponent for the given game.
         *
         * @param game The Game instance that owns this component.
         */
        explicit GamerServicesComponent(Microsoft::Xna::Framework::Game& game);

        /** @brief No-op stub. Does not initialize any real GamerServices infrastructure. */
        void Initialize() override;

        /**
         * @brief No-op stub. Does not process any GamerServices updates.
         *
         * @param gameTime Current game timing state.
         */
        void Update(Microsoft::Xna::Framework::GameTime& gameTime) override;
    };
}
