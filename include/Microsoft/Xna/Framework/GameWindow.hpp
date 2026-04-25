#pragma once

#include <string>

struct SDL_Window;

namespace Microsoft::Xna::Framework {

    class Game;

    class GameWindow {
    private:
        SDL_Window* window_ = nullptr;

    public:
        GameWindow() = default;
        explicit GameWindow(SDL_Window* window);

        [[nodiscard]] std::string getTitleProperty() const;
        void setTitleProperty(const std::string& title);

    private:
        void setWindowInternal(SDL_Window* window);

        friend class Game;
    };
}
