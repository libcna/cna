#include "Microsoft/Xna/Framework/Graphics/GraphicsDeviceManager.hpp"

#include <SDL3/SDL.h>
#include <stdexcept>

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"

namespace Microsoft::Xna::Framework::Graphics {

    namespace
    {
        void LogWindowDebugState(SDL_Window* window, const char* context)
        {
            if (!window) {
                SDL_Log("[WindowDebug] %s: window=null", context);
                return;
            }
            const Uint64 flags = SDL_GetWindowFlags(window);
            const bool borderless = (flags & SDL_WINDOW_BORDERLESS) != 0;
            const bool fullscreen = (flags & SDL_WINDOW_FULLSCREEN) != 0;
            SDL_Log(
                "[WindowDebug] %s: flags=0x%llx borderless=%s bordered=%s fullscreen=%s",
                context,
                static_cast<unsigned long long>(flags),
                borderless ? "true" : "false",
                borderless ? "false" : "true",
                fullscreen ? "true" : "false"
            );
        }
    }

    IMPL_PROP(Microsoft::Xna::Framework::Graphics::GraphicsDevice*, GraphicsDevice, getter1, setter0, member0, static0, constret0, ref1, constmet0, GraphicsDeviceManager, nothing)
    IMPL_PROP(bool, IsFullScreen, getter1, setter1, member0, static0, constret1, ref1, constmet1, GraphicsDeviceManager, nothing)
    IMPL_PROP(int, PreferredBackBufferWidth,  getter1, setter1, member0, static0, constret1, ref0, constmet1, GraphicsDeviceManager, nothing)
    IMPL_PROP(int, PreferredBackBufferHeight, getter1, setter1, member0, static0, constret1, ref0, constmet1, GraphicsDeviceManager, nothing)
    IMPL_PROP(PresentationMode, PreferredPresentationMode, getter1, setter1, member0, static0, constret1, ref0, constmet1, GraphicsDeviceManager, nothing)

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
        // Apply default preferred settings (backbuffer size, presentation mode)
        // immediately so the backend has a valid logical resolution from the start.
        // In XNA, GraphicsDeviceManager owns device creation and applies settings
        // automatically; games should not need to call ApplyChanges() manually
        // just to get a working initial viewport.
        ApplyChanges();
    }

    void GraphicsDeviceManager::ApplyChanges()
    {
        if (!GraphicsDevice_) {
            SDL_Log("[WindowDebug] ApplyChanges: GraphicsDevice is null, skipping");
            return;
        }
        SDL_Log("[Presentation] ApplyChanges: PreferredBackBufferWidth=%d PreferredBackBufferHeight=%d PresentationMode=%d",
                PreferredBackBufferWidth_, PreferredBackBufferHeight_,
                static_cast<int>(PreferredPresentationMode_));
        GraphicsDevice_->SetPresentationMode(static_cast<int>(PreferredPresentationMode_));
        GraphicsDevice_->SetVirtualResolution(PreferredBackBufferWidth_, PreferredBackBufferHeight_);
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
        SDL_Log(
            "[WindowDebug] ToggleFullScreen requested -> target_fullscreen=%s",
            IsFullScreen_ ? "true" : "false"
        );
        LogWindowDebugState(window, "before SDL_SetWindowFullscreen");

        if (IsFullScreen_) {
            if (!SDL_SetWindowFullscreen(window, true)) {
                throw std::runtime_error(
                    std::string("SDL_SetWindowFullscreen(true) failed: ") + SDL_GetError()
                );
            }
            LogWindowDebugState(window, "after SDL_SetWindowFullscreen(true)");
        } else {
            if (!SDL_SetWindowFullscreen(window, false)) {
                throw std::runtime_error(
                    std::string("SDL_SetWindowFullscreen(false) failed: ") + SDL_GetError()
                );
            }
            LogWindowDebugState(window, "after SDL_SetWindowFullscreen(false)");
        }

        GraphicsDevice_->UpdateViewportFromWindow();
    }
}
