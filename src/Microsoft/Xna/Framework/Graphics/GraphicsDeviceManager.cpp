#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include <SDL3/SDL.h>
#include <SDL3/SDL_render.h>
#include <iostream>
#include "Microsoft/Xna/Framework/Graphics/GraphicsDeviceManager.hpp"

namespace Microsoft::Xna::Framework::Graphics {

    IMPL_PROP(Microsoft::Xna::Framework::Graphics::GraphicsDevice, GraphicsDevice, getter1, setter0, member0, static0, constret0, ref1, constmet0, GraphicsDeviceManager, nothing)
    IMPL_PROP(bool, IsFullScreen, getter1, setter1, member0, static0, constret1, ref1, constmet1, GraphicsDeviceManager, nothing)

    GraphicsDeviceManager::GraphicsDeviceManager(): IsFullScreen_(false) {
    }

    GraphicsDeviceManager::GraphicsDeviceManager(Game *game): IsFullScreen_(false) {
    }

    void GraphicsDeviceManager::ToggleFullScreen() {
    }
}
