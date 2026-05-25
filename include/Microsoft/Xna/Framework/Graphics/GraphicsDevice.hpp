#pragma once

#include <memory>

#include "SharpRuntime/Prop.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Graphics/Viewport.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"

struct SDL_Window;
struct SDL_Renderer;

namespace Microsoft::Xna::Framework {
    class GraphicsDeviceManager;
}

namespace Microsoft::Xna::Framework::Graphics {

    class VertexBuffer;
    class IndexBuffer;
    class BasicEffect;
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
        const VertexBuffer* currentVertexBuffer_ = nullptr;
        const IndexBuffer*  currentIndexBuffer_  = nullptr;
        BasicEffect*        currentEffect_       = nullptr;
        /// The game's virtual (logical) resolution in pixels.
        /// Initialized to 0 (unspecified). The authoritative values come from
        /// GraphicsDeviceManager::PreferredBackBufferWidth/Height propagated
        /// via ApplyChanges() → SetVirtualResolution(). The SDL backend then
        /// derives a physical-to-logical scale from these XNA-layer values.
        int virtualWidth_  = 0;
        int virtualHeight_ = 0;

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

        // ---- 3D pipeline (XNA-like minimal subset) ----

        /**
         * @brief Clears color and depth buffers in one call.
         *
         * @note Status: PARTIAL. Only the EasyGL backend honors this; other
         *       backends throw "3D not supported".
         */
        void Clear(const Color& color, float depth);

        /**
         * @brief Enables or disables the 3D depth test.
         */
        void SetDepthTestEnabled(bool enabled);

        /**
         * @brief Binds a vertex buffer as the current vertex source.
         *
         * Mirrors `GraphicsDevice.SetVertexBuffer` from XNA 4.0. Pass
         * `nullptr` to clear the binding.
         *
         * The buffer is **not** owned by the device; the caller must keep
         * it alive while it remains bound.
         */
        void SetVertexBuffer(const VertexBuffer* vertexBuffer);

        /**
         * @brief Binds an index buffer as the current index source.
         *
         * XNA-equivalent of assigning `GraphicsDevice.Indices`. Pass
         * `nullptr` to clear the binding.
         */
        void SetIndexBuffer(const IndexBuffer* indexBuffer);

        /** Currently bound vertex buffer, or nullptr. */
        [[nodiscard]] const VertexBuffer* GetVertexBuffer() const { return currentVertexBuffer_; }
        /** Currently bound index buffer, or nullptr. XNA name: `Indices`. */
        [[nodiscard]] const IndexBuffer* GetIndexBuffer() const { return currentIndexBuffer_; }

        /**
         * @brief XNA-style getter for the `Indices` property.
         *
         * Equivalent to `GraphicsDevice.Indices` in XNA 4.0. Returns a
         * pointer to the currently bound index buffer, or `nullptr`.
         *
         * @note Status: IMPLEMENTED.
         */
        [[nodiscard]] const IndexBuffer* Indices() const { return currentIndexBuffer_; }

        /**
         * @brief XNA-style setter for the `Indices` property.
         *
         * Equivalent to `GraphicsDevice.Indices = indexBuffer;` in XNA 4.0.
         * Forwards to `SetIndexBuffer(...)`. Pass `nullptr` to clear.
         *
         * @note Status: IMPLEMENTED.
         */
        void Indices(const IndexBuffer* indexBuffer) { SetIndexBuffer(indexBuffer); }

        /**
         * @brief XNA-shaped `DrawUserPrimitives` (data supplied by the caller).
         *
         * @note Status: STUB. Always throws `std::runtime_error` with a
         *       clear message. Use `SetVertexBuffer` + `DrawPrimitives`
         *       instead in CNA 3D for now.
         */
        void DrawUserPrimitives(PrimitiveType primitiveType,
                                const void* vertexData,
                                int vertexOffset,
                                int primitiveCount);

        /**
         * @brief XNA-shaped `DrawUserIndexedPrimitives`.
         *
         * @note Status: STUB. Always throws `std::runtime_error` with a
         *       clear message. Use `SetVertexBuffer` + `SetIndexBuffer` +
         *       `DrawIndexedPrimitives` instead in CNA 3D for now.
         */
        void DrawUserIndexedPrimitives(PrimitiveType primitiveType,
                                       const void* vertexData,
                                       int vertexOffset,
                                       int numVertices,
                                       const void* indexData,
                                       int indexOffset,
                                       int primitiveCount);

        /**
         * @brief Draws non-indexed primitives from the currently bound
         *        vertex buffer using the currently applied effect.
         *
         * XNA 4.0 signature equivalent. Call `BasicEffect::Apply()` and
         * `SetVertexBuffer(...)` before this method.
         *
         * @param primitiveType Topology.
         * @param vertexStart   First vertex to read (XNA: `startVertex`).
         *                      Must be 0 in the current CNA backend.
         * @param primitiveCount Number of primitives.
         */
        void DrawPrimitives(PrimitiveType primitiveType,
                            int vertexStart,
                            int primitiveCount);

        /**
         * @brief Draws indexed primitives from the currently bound vertex
         *        and index buffers using the currently applied effect.
         *
         * Matches the XNA 4.0 `DrawIndexedPrimitives` signature.
         *
         * @param primitiveType   Topology.
         * @param baseVertex      Offset added to every index. Must be 0 in
         *                        the current CNA backend.
         * @param minVertexIndex  Minimum index value (driver hint, ignored
         *                        by the current CNA backend).
         * @param numVertices     Number of vertices that will be referenced
         *                        (driver hint, ignored).
         * @param startIndex      First index to read. Must be 0 in the
         *                        current CNA backend.
         * @param primitiveCount  Number of primitives to draw.
         */
        void DrawIndexedPrimitives(PrimitiveType primitiveType,
                                   int baseVertex,
                                   int minVertexIndex,
                                   int numVertices,
                                   int startIndex,
                                   int primitiveCount);

        [[nodiscard]] IGraphicsBackend& GetBackend() const { return *backend_; }

        /**
         * @brief Internal hook used by `BasicEffect::Apply` to register
         *        itself as the active effect for the next draw call.
         *
         * Not part of the public XNA API.
         */
        void SetCurrentEffect(BasicEffect* effect) { currentEffect_ = effect; }

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

        /// Called by GraphicsDeviceManager::ApplyChanges() to propagate
        /// PreferredBackBufferWidth/Height into the backend logical presentation.
        void SetVirtualResolution(int width, int height);

        /// Called by GraphicsDeviceManager::ApplyChanges() to propagate
        /// PreferredPresentationMode into the backend.
        /// mode is the int value of PresentationMode / CnaPresentationMode.
        void SetPresentationMode(int mode);

        friend class Texture2D;
        friend class SpriteBatch;
        friend class Microsoft::Xna::Framework::GraphicsDeviceManager;
    };
}