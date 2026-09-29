// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/GamerServices/GamerServicesComponent.hpp"
#include "Microsoft/Xna/Framework/GamerServices/GamerServicesDispatcher.hpp"
#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GameWindow.hpp"

namespace Microsoft::Xna::Framework::GamerServices
{
    GamerServicesComponent::GamerServicesComponent(Microsoft::Xna::Framework::Game& game)
        : GameComponent(game)
    {
    }

    void GamerServicesComponent::Initialize()
    {
        // FNA's override does not call base.Initialize() — matched here intentionally.
        GamerServicesDispatcher::setWindowHandleProperty(getGameProperty().getWindowProperty().getHandleProperty());
        // XNA IL (GamerServicesComponent.Initialize): a title update being installed exits the game.
        if (!installingTitleUpdate_)
            installingTitleUpdate_ = GamerServicesDispatcher::InstallingTitleUpdate.Add(
                [this](System::Object*, const System::EventArgs&) { getGameProperty().Exit(); });
        GamerServicesDispatcher::Initialize(getGameProperty().getServicesProperty());
    }

    GamerServicesComponent::~GamerServicesComponent()
    {
        if (installingTitleUpdate_)
            GamerServicesDispatcher::InstallingTitleUpdate.Remove(*installingTitleUpdate_);
    }

    void GamerServicesComponent::Update(Microsoft::Xna::Framework::GameTime& /*gameTime*/)
    {
        // FNA's override does not call base.Update() — matched here intentionally.
        GamerServicesDispatcher::Update();
    }
}
