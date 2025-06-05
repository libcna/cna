#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.h"
#include <SDL3/SDL.h>
#include <SDL3/SDL_render.h>
#include <iostream>
#include "Microsoft/Xna/Framework/Graphics/GraphicsDeviceManager.h"

namespace Microsoft::Xna::Framework::Graphics {
    igetter(Microsoft::Xna::Framework::Graphics::GraphicsDevice, GraphicsDevice, GraphicsDeviceManager)
    idata(bool, IsFullScreen, GraphicsDeviceManager)

    GraphicsDeviceManager::GraphicsDeviceManager(): IsFullScreen_(false) {
    }

    GraphicsDeviceManager::GraphicsDeviceManager(Game *game): IsFullScreen_(false) {
    }

    void GraphicsDeviceManager::ToggleFullScreen() {
    }
}
