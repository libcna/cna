#pragma once

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Graphics/Viewport.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteEffects.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include <cstddef>
#include <string>
#include <memory>
#include "CNA/Internal/Graphics/ImageData.hpp"

struct SDL_Window;
struct SDL_Renderer;
struct SDL_Texture;

namespace CNA::Internal::Backends
{
    using Color = Microsoft::Xna::Framework::Color;
    using Rectangle = Microsoft::Xna::Framework::Rectangle;
    using Vector2 = Microsoft::Xna::Framework::Vector2;
    using SpriteEffects = Microsoft::Xna::Framework::Graphics::SpriteEffects;
    using PrimitiveType = Microsoft::Xna::Framework::Graphics::PrimitiveType;
    using Matrix = Microsoft::Xna::Framework::Matrix;
    using ImageData = CNA::Internal::Graphics::ImageData;

    /**
     * @brief Backend handle for a vertex buffer of `VertexPositionColor` data.
     *
     * Owned by `Microsoft::Xna::Framework::Graphics::VertexBuffer`. The
     * concrete type is backend-specific (e.g. an OpenGL VBO+VAO pair) and
     * intentionally hidden from the public XNA-like API.
     *
     * @note Status: PARTIAL. Only vertex layouts compatible with
     *       `VertexPositionColor` are supported.
     */
    class IVertexBufferBackend
    {
    public:
        virtual ~IVertexBufferBackend() = default;
        /**
         * @brief Uploads `vertex_count` `VertexPositionColor` vertices.
         *
         * @param data         Pointer to a contiguous array of vertices,
         *                     each of size `stride_in_bytes`.
         * @param vertex_count Number of vertices.
         * @param stride_in_bytes Size of one vertex in bytes.
         */
        virtual void SetData(const void* data,
                             int vertex_count,
                             std::size_t stride_in_bytes) = 0;
        [[nodiscard]] virtual int GetVertexCount() const = 0;
    };

    /**
     * @brief Backend handle for a 16- or 32-bit index buffer.
     *
     * @note Status: PARTIAL. The minimal CNA 3D pipeline currently only
     *       calls `IGraphicsBackend::DrawIndexedColoredPrimitives` with
     *       16-bit indices.
     */
    class IIndexBufferBackend
    {
    public:
        virtual ~IIndexBufferBackend() = default;
        virtual void SetData16(const void* data, int index_count) = 0;
        [[nodiscard]] virtual int GetIndexCount() const = 0;
    };

    class ITextureBackend
    {
    public:
        virtual ~ITextureBackend() = default;
        virtual int GetWidth() const = 0;
        virtual int GetHeight() const = 0;
        // TODO: SDL dependency should be abstracted later
        virtual SDL_Texture* GetNativeTexture() const = 0;
    };

    class ISpriteBatchBackend
    {
    public:
        virtual ~ISpriteBatchBackend() = default;
        virtual void Begin() = 0;
        virtual void End() = 0;
        virtual void Draw(const ITextureBackend& texture, float x, float y) = 0;
        virtual void Draw(const ITextureBackend& texture,
                          const Rectangle& destinationRectangle,
                          const Rectangle& sourceRectangle,
                          const Color& color) = 0;
        virtual void Draw(const ITextureBackend& texture,
                          const Rectangle& destinationRectangle,
                          const Rectangle& sourceRectangle,
                          const Color& color,
                          float rotation,
                          const Vector2& origin,
                          SpriteEffects effects,
                          float layerDepth) = 0;
    };

    class IGraphicsBackend
    {
    public:
        virtual ~IGraphicsBackend() = default;
        virtual void Clear(float r, float g, float b, float a) = 0;
        virtual void Present() = 0;
        virtual void GetViewportSize(int& width, int& height) = 0;
        /// Updates the backend logical presentation size at runtime.
        /// Called by GraphicsDevice::SetVirtualResolution() when
        /// GraphicsDeviceManager::ApplyChanges() propagates a new
        /// PreferredBackBufferWidth/Height from the game.
        virtual void SetVirtualResolution(int width, int height) = 0;
        /// Updates the backend presentation/scaling mode at runtime.
        /// Called by GraphicsDevice when GraphicsDeviceManager::ApplyChanges() is used.
        virtual void SetPresentationMode(int mode) = 0;
        // TODO: SDL dependency should be abstracted later
        virtual SDL_Window* GetWindowInternal() const = 0;
        virtual SDL_Renderer* GetRendererInternal() const = 0;

        virtual std::unique_ptr<ITextureBackend> CreateTexture(const ImageData& data) = 0;
        virtual std::unique_ptr<ISpriteBatchBackend> CreateSpriteBatch() = 0;

        // ---- 3D pipeline ----

        /**
         * @brief Clears color and depth buffers in a single call.
         *
         * @param r,g,b,a    Clear color in range 0..1.
         * @param depth      Depth value to clear with (0..1).
         */
        virtual void ClearColorAndDepth(float r, float g, float b, float a, float depth) = 0;

        /**
         * @brief Enables or disables depth testing.
         *
         * @note Status: PARTIAL. Only the EasyGL backend honors this; other
         *       backends throw on first 3D usage.
         */
        virtual void SetDepthTestEnabled(bool enabled) = 0;

        /**
         * @brief Creates a backend-specific vertex buffer for
         *        `VertexPositionColor` data.
         */
        virtual std::unique_ptr<IVertexBufferBackend> CreateVertexBuffer(int vertex_capacity) = 0;

        /**
         * @brief Creates a backend-specific 16-bit index buffer.
         */
        virtual std::unique_ptr<IIndexBufferBackend> CreateIndexBuffer16(int index_capacity) = 0;

        /**
         * @brief Draws colored primitives from `vb` using the supplied transform.
         *
         * The backend internally applies a basic colored-vertex shader
         * (equivalent to `BasicEffect` with `VertexColorEnabled = true`).
         *
         * @param vb            Vertex buffer to read from.
         * @param world,view,projection Per-draw transform matrices (XNA
         *                              row-major). The combined matrix
         *                              uploaded to the GPU is
         *                              `projection * view * world`.
         * @param primitive     Primitive topology.
         * @param primitiveCount Number of primitives (NOT vertices).
         */
        virtual void DrawColoredPrimitives(const IVertexBufferBackend& vb,
                                           const Matrix& world,
                                           const Matrix& view,
                                           const Matrix& projection,
                                           PrimitiveType primitive,
                                           int primitiveCount) = 0;

        /**
         * @brief Indexed counterpart of `DrawColoredPrimitives`.
         */
        virtual void DrawIndexedColoredPrimitives(const IVertexBufferBackend& vb,
                                                  const IIndexBufferBackend& ib,
                                                  const Matrix& world,
                                                  const Matrix& view,
                                                  const Matrix& projection,
                                                  PrimitiveType primitive,
                                                  int primitiveCount) = 0;
    };

    /**
     * @brief Presentation/scaling policy used when the virtual (game-logic)
     *        resolution differs from the physical surface size.
     *
     * Matches XNA/Windows Phone semantics:
     * - Letterbox            – scale = min(surfW/virtW, surfH/virtH); adds bars.
     * - Overscan             – scale = max(surfW/virtW, surfH/virtH); crops edges.
     * - Stretch              – stretches to fill without preserving aspect ratio.
     * - NativeBackBuffer     – no scaling; game draws at its requested size.
     * - FixedHeightDynamicWidth – keeps the game's preferred height as the
     *                            logical height and computes logical width from
     *                            the actual surface aspect ratio:
     *                              logicalW = round(outputW * preferredH / outputH)
     *                            Then applies LETTERBOX so the computed canvas
     *                            fills the surface perfectly (no bars, no crop).
     *                            This matches XNA/Windows Phone behaviour where
     *                            height=480 is fixed and wider devices simply
     *                            show more horizontal content.
     */
    enum class CnaPresentationMode
    {
        Letterbox = 0,
        Overscan = 1,
        Stretch = 2,
        NativeBackBuffer = 3,
        FixedHeightDynamicWidth = 4
    };

    /**
     * @brief Arguments for creating a graphics backend.
     * Currently minimal, but allows for easier extension.
     */
    struct GraphicsBackendCreateArgs
    {
        // TODO: SDL dependency should be abstracted later
        SDL_Window* window = nullptr;
        /// Virtual (game-logic) resolution the backend should present at.
        /// SDL_SetRenderLogicalPresentation will be set to this size so that
        /// the game always draws in its own coordinate space and the backend
        /// scales to the real surface automatically.
        /// 0 means "unset"; the backend should ignore logical presentation.
        int virtualWidth = 0;
        int virtualHeight = 0;
        /// Presentation/scaling policy. Default is FixedHeightDynamicWidth:
        /// keeps preferred height fixed and derives logical width from the
        /// actual surface aspect ratio, matching XNA/Windows Phone behaviour.
        CnaPresentationMode presentationMode = CnaPresentationMode::FixedHeightDynamicWidth;
    };

    // Factory function to be implemented by each backend
    // INTERNAL API - SDL dependency should be abstracted later
    std::unique_ptr<IGraphicsBackend> CreateGraphicsBackend(const GraphicsBackendCreateArgs& args);
}
