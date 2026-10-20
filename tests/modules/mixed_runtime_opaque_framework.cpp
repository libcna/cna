// SPDX-License-Identifier: MS-PL
#include <cstdio>
#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GamerServices/GamerServicesComponent.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"

namespace
{
    void ReferenceGamerServices(Microsoft::Xna::Framework::Game& game)
    {
        Microsoft::Xna::Framework::GamerServices::GamerServicesComponent component(game);
    }
}

int CnaMixedRuntimeOpaqueFramework()
{
    auto* volatile reference = &ReferenceGamerServices;
    if (reference == nullptr) return 2;
    if (Microsoft::Xna::Framework::Graphics::SamplerState::PointWrap.getIsDisposedProperty())
        return 3;
    std::puts("opaque framework reached; no game or service constructed");
    return 0;
}
