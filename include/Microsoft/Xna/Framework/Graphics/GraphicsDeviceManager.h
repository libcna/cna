#ifndef GRAPHICSDEVICEMANAGER_H
#define GRAPHICSDEVICEMANAGER_H
#include "GraphicsDevice.h"

namespace Microsoft::Xna::Framework::Graphics {

    class GraphicsDeviceManager {
    private:
        GraphicsDevice graphicsDevice;
    public:
        NeoSdk::Property<GraphicsDevice> GraphicsDevice;
        GraphicsDeviceManager();

    };

}

#endif // GRAPHICSDEVICEMANAGER_H
