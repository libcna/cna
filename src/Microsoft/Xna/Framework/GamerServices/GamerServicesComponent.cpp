#include "Microsoft/Xna/Framework/GamerServices/GamerServicesComponent.hpp"

namespace Microsoft::Xna::Framework::GamerServices
{
    GamerServicesComponent::GamerServicesComponent(Microsoft::Xna::Framework::Game& game)
        : GameComponent(game)
    {
    }

    void GamerServicesComponent::Initialize()
    {
        // CNA_STUB: GamerServices not available — no-op.
        GameComponent::Initialize();
    }

    void GamerServicesComponent::Update(Microsoft::Xna::Framework::GameTime& gameTime)
    {
        // CNA_STUB: GamerServices not available — no-op.
        GameComponent::Update(gameTime);
    }
}
