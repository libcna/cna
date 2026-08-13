#include "CNA/Entrypoint.hpp"

#include <cstdio>
#include <exception>

#include "Microsoft/Xna/Framework/Game.hpp"

using Microsoft::Xna::Framework::Game;

int main(int /*argc*/, char* /*argv*/[])
{
    try
    {
        // Construction initializes SDL video/audio, creates the selected renderer and attaches
        // its native window. One frame then exercises CNA's initialization, event, update, draw
        // and presentation path without leaving a build-only CI launch running indefinitely.
        Game game;
        game.RunOneFrame();
        std::puts("CNA_IOS_SMOKE_OK");
        return 0;
    }
    catch (const std::exception& error)
    {
        std::fprintf(stderr, "CNA_IOS_SMOKE_FAILED: %s\n", error.what());
        return 1;
    }
}
