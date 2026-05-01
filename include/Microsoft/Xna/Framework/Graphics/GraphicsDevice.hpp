#pragma once

#include <memory>

#include "SharpRuntime/Prop.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Graphics/Viewport.hpp"

struct SDL_Window;
struct SDL_Renderer;

namespace Microsoft::Xna::Framework::Graphics {

    class GraphicsDeviceManager;
}

namespace CNA::Internal::Backends {
    class IGraphicsBackend;
}

namespace Microsoft::Xna::Framework::Graphics {
    using namespace CNA::Internal::Backends;

    /**
     * @brief Represents the main graphics device used by the game.
     *
     * This class uses a backend abstraction to handle the actual rendering,
     * such as SDL_Renderer or EasyGL.
     */
    class GraphicsDevice {
    private:
        SDL_Window* window_;
        std::unique_ptr<IGraphicsBackend> backend_;
        Viewport Viewport_;

    public:
        DEF_PROP(Microsoft::Xna::Framework::Graphics::Viewport, Viewport, getter1, setter0, member0, static0, constret0, ref1, constmet0)

        /**
         * @brief Creates the graphics device.
         */
        GraphicsDevice();

        /**
         * @brief Destroys the graphics device and releases native resources.
         */
        ~GraphicsDevice();

        GraphicsDevice(const GraphicsDevice&) = delete;
        GraphicsDevice& operator=(const GraphicsDevice&) = delete;
        GraphicsDevice(GraphicsDevice&&) = delete;
        GraphicsDevice& operator=(GraphicsDevice&&) = delete;

        /**
         * @brief Clears the current back buffer with the specified color.
         *
         * @param color Color used for clearing.
         */
        void Clear(const Color& color);

        /**
         * @brief Clears the current back buffer with RGBA float components.
         *
         * Each component is expected in the range 0.0f to 1.0f.
         *
         * @param r Red component.
         * @param g Green component.
         * @param b Blue component.
         * @param a Alpha component.
         */
        void Clear(float r, float g, float b, float a);

        /**
         * @brief Presents the rendered frame to the screen.
         */
        void Present();

        IGraphicsBackend& GetBackend() const { return *backend_; }

    private:
        /**
         * @brief Returns the internal SDL renderer for engine-internal use.
         *
         * This method is intentionally private to avoid exposing SDL
         * through the public graphics API.
         */
        [[nodiscard]] SDL_Renderer* GetRendererInternal() const;
        [[nodiscard]] SDL_Window* GetWindowInternal() const;

        void UpdateViewportFromWindow();

        friend class Texture2D;
        friend class SpriteBatch;
        friend class GraphicsDeviceManager;
    };
}