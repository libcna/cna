#ifndef GRAPHICSDEVICEMANAGER_H
#define GRAPHICSDEVICEMANAGER_H
#include "GraphicsDevice.h"
#include "Microsoft/Xna/Framework/Game.h"

namespace Microsoft::Xna::Framework::Graphics {
    class GraphicsDeviceManager {

        DEF_PROP(Microsoft::Xna::Framework::Graphics::GraphicsDevice, GraphicsDevice, getter1, setter0, member1, static0, constret1, ref1, constmet1)
        DEF_PROP(bool, IsFullScreen, getter1, setter1, member1, static0, constret1, ref1, constmet1)


        GraphicsDeviceManager();

        explicit GraphicsDeviceManager(Game *game);

        void ToggleFullScreen();
    };
}

#endif // GRAPHICSDEVICEMANAGER_H
