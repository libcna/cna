#pragma once
#include "GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Game.hpp"

namespace Microsoft::Xna::Framework::Graphics {
    class GraphicsDeviceManager {

        DEF_PROP(Microsoft::Xna::Framework::Graphics::GraphicsDevice, GraphicsDevice, getter1, setter0, member1, static0, constret0, ref1, constmet0)
        DEF_PROP(bool, IsFullScreen, getter1, setter1, member1, static0, constret1, ref1, constmet1)


        GraphicsDeviceManager();

        explicit GraphicsDeviceManager(Game *game);

        void ToggleFullScreen();
    };
}
