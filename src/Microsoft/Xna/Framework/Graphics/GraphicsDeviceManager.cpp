#include "Microsoft/Xna/Framework/Graphics/GraphicsDeviceManager.hpp"

#include <SDL3/SDL.h>
#include <stdexcept>

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"

namespace Microsoft::Xna::Framework::Graphics {

    IMPL_PROP(Microsoft::Xna::Framework::Graphics::GraphicsDevice*, GraphicsDevice, getter1, setter0, member0, static0, constret0, ref1, constmet0, GraphicsDeviceManager, nothing)
    IMPL_PROP(bool, IsFullScreen, getter1, setter1, member0, static0, constret1, ref1, constmet1, GraphicsDeviceManager, nothing)

    GraphicsDeviceManager::GraphicsDeviceManager()
        : game_(nullptr),
          GraphicsDevice_(nullptr),
          IsFullScreen_(false)
    {
    }

    GraphicsDeviceManager::GraphicsDeviceManager(Game* game)
        : game_(game),
          GraphicsDevice_(game ? &game->getGraphicsDeviceProperty() : nullptr),
          IsFullScreen_(false)
    {
    }

    void GraphicsDeviceManager::ToggleFullScreen()
    {
        if (!GraphicsDevice_) {
            throw std::runtime_error("GraphicsDeviceManager::ToggleFullScreen failed: GraphicsDevice is null.");
        }

        SDL_Window* window = GraphicsDevice_->GetWindowInternal();
        if (!window) {
            throw std::runtime_error("GraphicsDeviceManager::ToggleFullScreen failed: SDL window is null.");
        }

        IsFullScreen_ = !IsFullScreen_;

        if (IsFullScreen_) {
            if (!SDL_SetWindowFullscreen(window, true)) {
                throw std::runtime_error(
                    std::string("SDL_SetWindowFullscreen(true) failed: ") + SDL_GetError()
                );
            }
        } else {
            if (!SDL_SetWindowFullscreen(window, false)) {
                throw std::runtime_error(
                    std::string("SDL_SetWindowFullscreen(false) failed: ") + SDL_GetError()
                );
            }
        }

        GraphicsDevice_->UpdateViewportFromWindow();
    }
}