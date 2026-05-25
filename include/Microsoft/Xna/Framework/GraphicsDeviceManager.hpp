#pragma once

#include "SharpRuntime/Prop.hpp"

namespace Microsoft::Xna::Framework {
    class Game;
}

namespace Microsoft::Xna::Framework {

    namespace Graphics
    {
        class GraphicsDevice;
    }

    /**
     * @brief Presentation/scaling policy for the CNA rendering backend.
     *
     * Matches XNA/Windows Phone semantics when game virtual resolution
     * differs from the physical surface size.
     *
     * Values are intentionally identical to CnaPresentationMode so they
     * can be cast to int and forwarded to the backend without including
     * CNA internal headers from game-facing code.
     */
    enum class PresentationMode {
        Letterbox             = 0, ///< Adds bars; preserves aspect ratio.
        Overscan              = 1, ///< Fills surface, may crop edges. Matches Windows Phone.
        Stretch               = 2, ///< Fills surface by stretching, no crop or bars.
        NativeBackBuffer      = 3, ///< No scaling; renders at requested backbuffer size.
        FixedHeightDynamicWidth = 4 ///< Keeps preferred height fixed; derives logical width
                                   ///< from actual surface aspect ratio. Matches XNA/Windows
                                   ///< Phone behaviour where height=480 is fixed and wider
                                   ///< devices show more horizontal content.
    };

    /**
     * @brief Manages high-level graphics device settings.
     *
     * This is currently a lightweight subset needed by the game.
     */
    class GraphicsDeviceManager {
    private:
        Microsoft::Xna::Framework::Game* game_ = nullptr;
        Graphics::GraphicsDevice* GraphicsDevice_ = nullptr;
        bool IsFullScreen_ = false;
        /// XNA-compatible preferred back buffer dimensions.
        /// Defaults match the SDL_CreateWindow request in GraphicsDevice (800x480).
        /// Call ApplyChanges() after modifying to propagate to the backend.
        int PreferredBackBufferWidth_  = 800;
        int PreferredBackBufferHeight_ = 480;
        /// Presentation/scaling policy propagated to the backend on ApplyChanges().
        /// Defaults to FixedHeightDynamicWidth: keeps preferred height fixed and derives
        /// logical width from the actual surface aspect ratio, matching XNA/Windows Phone.
        PresentationMode PreferredPresentationMode_ = PresentationMode::FixedHeightDynamicWidth;

    public:
        DEF_PROP(Microsoft::Xna::Framework::Graphics::GraphicsDevice*, GraphicsDevice, getter1, setter0, member0, static0, constret0, ref1, constmet0)
        DEF_PROP(bool, IsFullScreen, getter1, setter1, member0, static0, constret1, ref1, constmet1)
        DEF_PROP(int, PreferredBackBufferWidth,  getter1, setter1, member0, static0, constret1, ref0, constmet1)
        DEF_PROP(int, PreferredBackBufferHeight, getter1, setter1, member0, static0, constret1, ref0, constmet1)
        DEF_PROP(PresentationMode, PreferredPresentationMode, getter1, setter1, member0, static0, constret1, ref0, constmet1)

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
         * @brief Applies pending graphics settings (e.g. PreferredBackBufferWidth/Height)
         * to the graphics device and backend.
         *
         * Call this after changing PreferredBackBufferWidth/Height to update
         * the SDL logical presentation and CNA viewport.
         */
        void ApplyChanges();

        /**
         * @brief Toggles fullscreen state.
         */
        void ToggleFullScreen();
    };
}