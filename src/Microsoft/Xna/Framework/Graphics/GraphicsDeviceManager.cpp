#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.h"
#include <SDL3/SDL.h>
#include <SDL3/SDL_render.h>
#include <iostream>
#include "Microsoft/Xna/Framework/Graphics/GraphicsDeviceManager.h"

namespace Microsoft::Xna::Framework::Graphics {
    Microsoft::Xna::Framework::Graphics::GraphicsDevice GraphicsDeviceManager::GraphicsDeviceProperty() {
        return graphicsDevice;
    }

    bool GraphicsDeviceManager::IsFullScreenProperty() const { return IsFullScreenProperty_; }
    void GraphicsDeviceManager::IsFullScreenProperty(bool v) { IsFullScreenProperty_ = v; }

    GraphicsDeviceManager::GraphicsDeviceManager() {
    }
}
