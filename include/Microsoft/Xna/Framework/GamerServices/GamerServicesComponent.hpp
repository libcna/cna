#pragma once

#include "Microsoft/Xna/Framework/GameComponent.hpp"

namespace Microsoft::Xna::Framework::GamerServices
{
    /// A GameComponent that must be added to Game.Components to enable GamerServices.
    ///
    /// On PC, XNA 4.0 required this component to be registered before calling any
    /// Guide or Gamer API. CNA provides a no-op stub; GamerServices are not
    /// available on any CNA platform.
    // CNA_STUB: XNA 4.0 API surface placeholder. Behavior is not implemented yet.
    class GamerServicesComponent : public Microsoft::Xna::Framework::GameComponent
    {
    public:
        explicit GamerServicesComponent(Microsoft::Xna::Framework::Game& game);

        void Initialize() override;
        void Update(Microsoft::Xna::Framework::GameTime& gameTime) override;
    };
}
