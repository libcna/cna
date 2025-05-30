#ifndef GRAPHICSDEVICEMANAGER_H
#define GRAPHICSDEVICEMANAGER_H
#include "GraphicsDevice.h"
#include "Microsoft/Xna/Framework/Game.h"

namespace Microsoft::Xna::Framework::Graphics {

    class GraphicsDeviceManager {
    private:
        Microsoft::Xna::Framework::Graphics::GraphicsDevice graphicsDevice;
    public:
        NeoSdk::Property<Microsoft::Xna::Framework::Graphics::GraphicsDevice> GraphicsDevice;
        DEF_PROP_AUTO(bool, IsFullScreen, false);
        GraphicsDeviceManager();

        explicit GraphicsDeviceManager(Game* game);

        void ToggleFullScreen();
    };

}

#endif // GRAPHICSDEVICEMANAGER_H
