// SPDX-License-Identifier: MS-PL
// Minimal-link probe for CNA::Runtime (plans/MODULARIZATION_PLAN.md §4): the Game layer's symbols
// must resolve without GamerServices/Net, devices or CNAEXT. Referencing Game's members pulls
// the runtime archive without constructing a Game (construction is exercised by the demo and
// test corpus; this probe is a pure link-closure fixture that must stay side-effect free).
#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GameTime.hpp"
#include "Microsoft/Xna/Framework/LaunchParameters.hpp"

#include <cstdio>

int main()
{
    using namespace Microsoft::Xna::Framework;

    GameTime time;
    LaunchParameters params;
    const auto exitMember = &Game::Exit;
    (void)exitMember;

    std::printf("runtime probe: elapsed=%f params=%zu\n",
                time.getElapsedGameTimeProperty().getTotalSecondsProperty(),
                params.size());
    return 0;
}
