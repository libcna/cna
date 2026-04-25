#include "Microsoft/Xna/Framework/GameWindow.hpp"

#include <SDL3/SDL.h>

#include <stdexcept>

namespace Microsoft::Xna::Framework {

    GameWindow::GameWindow(SDL_Window* window)
        : window_(window)
    {
    }

    std::string GameWindow::getTitleProperty() const
    {
        if (!window_) {
            return "";
        }

        const char* title = SDL_GetWindowTitle(window_);
        return title ? std::string(title) : "";
    }

    void GameWindow::setTitleProperty(const std::string& title)
    {
        if (!window_) {
            return;
        }

        if (!SDL_SetWindowTitle(window_, title.c_str())) {
            throw std::runtime_error(std::string("SDL_SetWindowTitle failed: ") + SDL_GetError());
        }
    }

    void GameWindow::setWindowInternal(SDL_Window* window)
    {
        window_ = window;
    }
}
