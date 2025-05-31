#ifndef GRAPHICSDEVICEMANAGER_H
#define GRAPHICSDEVICEMANAGER_H
#include "GraphicsDevice.h"
#include "Microsoft/Xna/Framework/Game.h"

namespace Microsoft::Xna::Framework::Graphics {
    class GraphicsDeviceManager {
        dgetter(Microsoft::Xna::Framework::Graphics::GraphicsDevice, GraphicsDevice)
        ddata(bool, IsFullScreen)

        GraphicsDeviceManager();

        explicit GraphicsDeviceManager(Game *game);

        void ToggleFullScreen();
    };
}

#endif // GRAPHICSDEVICEMANAGER_H
