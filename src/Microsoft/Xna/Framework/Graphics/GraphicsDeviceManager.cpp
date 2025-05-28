#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.h"
#include <SDL3/SDL.h>
#include <SDL3/SDL_render.h>
#include <iostream>
#include "Microsoft/Xna/Framework/Graphics/GraphicsDeviceManager.h"

namespace Microsoft::Xna::Framework::Graphics {
    GraphicsDeviceManager::GraphicsDeviceManager():
    GraphicsDevice( [this]() { return graphicsDevice; }),
    IMPL_PROP_AUTO(bool, IsFullScreen)
    {

    }


}
