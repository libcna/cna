#include "CNA/Entrypoint.hpp"

#include <cstdio>
#include <exception>

#include "Microsoft/Xna/Framework/DisplayOrientation.hpp"
#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"

using Microsoft::Xna::Framework::DisplayOrientation;
using Microsoft::Xna::Framework::Game;
using Microsoft::Xna::Framework::GraphicsDeviceManager;

int main(int /*argc*/, char* /*argv*/[])
{
    try
    {
        // Construction initializes SDL video/audio, creates the selected renderer and attaches
        // its native window. One frame then exercises CNA's initialization, event, update, draw
        // and presentation path without leaving an automated smoke launch running indefinitely.
        Game game;
        GraphicsDeviceManager graphics(&game);
        graphics.setSupportedOrientationsProperty(DisplayOrientation::LandscapeLeft);
        game.RunOneFrame();
        std::puts("CNA_APPLE_SMOKE_OK");
        return 0;
    }
    catch (const std::exception& error)
    {
        std::fprintf(stderr, "CNA_APPLE_SMOKE_FAILED: %s\n", error.what());
        return 1;
    }
}
