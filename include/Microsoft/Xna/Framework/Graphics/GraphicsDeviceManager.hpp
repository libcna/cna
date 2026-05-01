#pragma once

#include "SharpRuntime/Prop.hpp"

namespace Microsoft::Xna::Framework {
    class Game;
}

namespace Microsoft::Xna::Framework::Graphics {

    class GraphicsDevice;

    /**
     * @brief Manages high-level graphics device settings.
     *
     * This is currently a lightweight subset needed by the game.
     */
    class GraphicsDeviceManager {
    private:
        Microsoft::Xna::Framework::Game* game_ = nullptr;
        GraphicsDevice* GraphicsDevice_ = nullptr;
        bool IsFullScreen_ = false;

    public:
        DEF_PROP(Microsoft::Xna::Framework::Graphics::GraphicsDevice*, GraphicsDevice, getter1, setter0, member0, static0, constret0, ref1, constmet0)
        DEF_PROP(bool, IsFullScreen, getter1, setter1, member0, static0, constret1, ref1, constmet1)

        /**
         * @brief Constructs an empty graphics device manager.
         */
        GraphicsDeviceManager();

        /**
         * @brief Constructs a graphics device manager for a game.
         *
         * @param game Owning game instance.
         */
        explicit GraphicsDeviceManager(Game* game);

        /**
         * @brief Toggles fullscreen state.
         */
        void ToggleFullScreen();
    };
}