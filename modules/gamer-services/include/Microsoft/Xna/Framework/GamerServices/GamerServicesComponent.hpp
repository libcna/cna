// SPDX-License-Identifier: MS-PL
#pragma once

#include "Microsoft/Xna/Framework/GameComponent.hpp"
#include "System/EventArgs.hpp"
#include "System/EventHandler.hpp"
#include <optional>

namespace Microsoft::Xna::Framework::GamerServices
{
    /**
     * @brief A GameComponent that must be added to Game.Components to enable GamerServices.
     *
     * On PC, XNA 4.0 required this component to be registered before calling any
     * Guide or Gamer API.
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

        /** @brief Stops listening for title updates. */
        ~GamerServicesComponent() override;

        /**
         * @brief Initializes GamerServicesDispatcher with the owning game's window handle and
         * service container, and exits the game when a title update is being installed.
         */
        void Initialize() override;

        /**
         * @brief Drives GamerServicesDispatcher's per-frame update.
         *
         * @param gameTime Current game timing state.
         */
        void Update(Microsoft::Xna::Framework::GameTime& gameTime) override;

    private:
        std::optional<System::EventHandler<System::EventArgs>::Token> installingTitleUpdate_;
    };
}
