#ifndef GRAPHICSDEVICEMANAGER_H
#define GRAPHICSDEVICEMANAGER_H
#include "GraphicsDevice.h"
#include "Microsoft/Xna/Framework/Game.h"

namespace Microsoft::Xna::Framework::Graphics {
    class GraphicsDeviceManager {
    private:
        Microsoft::Xna::Framework::Graphics::GraphicsDevice graphicsDevice;

    public:

    public:
        Microsoft::Xna::Framework::Graphics::GraphicsDevice GraphicsDeviceProperty();

    private:
        bool IsFullScreenProperty_ = false;

    public:
        [[nodiscard]] bool IsFullScreenProperty() const;

    public:
        void IsFullScreenProperty(bool v);;

        GraphicsDeviceManager();

        explicit GraphicsDeviceManager(Game *game);

        void ToggleFullScreen();
    };
}

#endif // GRAPHICSDEVICEMANAGER_H
