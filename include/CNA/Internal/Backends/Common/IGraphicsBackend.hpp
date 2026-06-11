#pragma once

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Graphics/Viewport.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteEffects.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include <cstddef>
#include <stdexcept>
#include <string>
#include <memory>
#include <unordered_map>
#include "CNA/Internal/Graphics/ImageData.hpp"

struct SDL_Window;
struct SDL_Renderer;
struct SDL_Texture;

namespace Microsoft::Xna::Framework::Graphics { class Effect; }

namespace CNA::Internal::Backends
{
    using Color = Microsoft::Xna::Framework::Color;
    using Rectangle = Microsoft::Xna::Framework::Rectangle;
    using Vector2 = Microsoft::Xna::Framework::Vector2;
    using SpriteEffects = Microsoft::Xna::Framework::Graphics::SpriteEffects;
    using PrimitiveType = Microsoft::Xna::Framework::Graphics::PrimitiveType;
    using Matrix = Microsoft::Xna::Framework::Matrix;
    using Effect = Microsoft::Xna::Framework::Graphics::Effect;
    using ImageData = CNA::Internal::Graphics::ImageData;

    /**
     * @brief Backend handle for a vertex buffer.
     *
     * Owned by `Microsoft::Xna::Framework::Graphics::VertexBuffer`. The
     * concrete type is backend-specific (e.g. an OpenGL VBO+VAO pair) and
     * intentionally hidden from the public XNA-like API.
     *
     * @note Status: IMPLEMENTED for VertexPositionColor (stride 16),
     *       VertexPositionTexture (20), VertexPositionColorTexture (24),
     *       and VertexPositionNormalTexture (32) on the EasyGL backend.
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
     */
    class IIndexBufferBackend
    {
    public:
        virtual ~IIndexBufferBackend() = default;
        virtual void SetData16(const void* data, int index_count) = 0;
        virtual void SetData32(const void* data, int index_count)
        {
            throw std::runtime_error("SetData32 not supported by this backend");
        }
        [[nodiscard]] virtual int  GetIndexCount()   const = 0;
        [[nodiscard]] virtual bool IsThirtyTwoBit()  const { return false; }
    };

    /**
     * @brief Backend handle for a GPU occlusion query.
     *
     * On OpenGL ES 3.0 (EasyGL), uses GL_ANY_SAMPLES_PASSED — so PixelCount()
     * returns 0 (no visible samples) or 1 (at least one visible sample), not an
     * exact pixel count. This matches FNA's behaviour on GLES3.
     */
    class IOcclusionQueryBackend
    {
    public:
        virtual ~IOcclusionQueryBackend() = default;
        virtual void Begin() = 0;
        virtual void End()   = 0;
        [[nodiscard]] virtual bool IsComplete() const = 0;
        [[nodiscard]] virtual int  PixelCount() const = 0;
    };

    class ITextureBackend
    {
    public:
        virtual ~ITextureBackend() = default;
        virtual int GetWidth() const = 0;
        virtual int GetHeight() const = 0;
        // TODO: SDL dependency should be abstracted later
        virtual SDL_Texture* GetNativeTexture() const = 0;
        /// Replaces full level-0 texture pixels in-place. stride = row bytes (width * 4 for RGBA).
        virtual void UpdatePixels(const uint8_t* rgba, int stride) {}
        /// Uploads a specific mip level. levelW/levelH are the dimensions at that level.
        virtual void UpdatePixelsLevel(int level, const uint8_t* rgba, int levelW, int levelH) {}
        /// Binds the underlying GL texture handle (no-op on non-GL backends).
        virtual void BindGL() const {}
        /// Shares a reference to the CPU pixel buffer owned by Texture2D::cpuPixels_.
        /// The backend stores this reference for OpenGL context-loss restoration instead
        /// of keeping its own duplicate copy of the pixel data.
        virtual void ShareCpuPixels(std::shared_ptr<std::vector<uint8_t>> /*pixels*/) {}
    };

    /// Backend handle for a 2D render target (off-screen FBO on EasyGL).
    class IRenderTargetBackend : public ITextureBackend
    {
    public:
        /// Bind the FBO so subsequent draws go to this render target.
        virtual void BindAsRenderTarget() = 0;
        /// Unbind and restore the default framebuffer (back buffer).
        virtual void UnbindAsRenderTarget() = 0;
    };

    class ISpriteBatchBackend
    {
    public:
        virtual ~ISpriteBatchBackend() = default;
        virtual void Begin() = 0;
        virtual void End() = 0;
        /// Sets the transform matrix applied on top of the 2D ortho projection.
        /// Must be called before the first Draw of each Begin/End block.
        virtual void SetTransformMatrix(const Matrix& m) {}
        /// Sets a custom Effect to use for sprite rendering instead of the built-in sprite shader.
        /// Pass nullptr to restore the built-in shader. Must be called before Begin().
        virtual void SetCustomEffect(Effect* effect) {}
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

    /**
     * @brief Per-draw effect parameters forwarded from the XNA effect layer
     *        to the graphics backend.
     *
     * Populated by GraphicsDevice from the currently bound BasicEffect before
     * each draw call so the backend can select and configure the appropriate
     * GLSL shader variant.
     */
    struct GpuDrawParams
    {
        const ITextureBackend* texture0       = nullptr;      ///< Texture unit 0, or null
        float diffuseColor[4]  = {1,1,1,1};                  ///< RGBA 0..1
        float ambientColor[3]  = {0,0,0};                     ///< RGB 0..1
        float light0Dir[3]     = {0,-1,0};                    ///< World-space, pre-normalized
        float light0Diffuse[3] = {1,1,1};                     ///< RGB 0..1
        float worldColMajor[16] = {                           ///< Column-major world matrix
            1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
        bool textureEnabled      = false;
        bool vertexColorEnabled  = true;
        bool lightingEnabled     = false;
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
        /// Converts a point from physical window coordinates to logical (virtual)
        /// game coordinates. Returns true on success. Default: no-op (returns false).
        virtual bool TransformWindowToLogical(float windowX, float windowY,
                                              float& logX, float& logY) const { return false; }
        // TODO: SDL dependency should be abstracted later
        virtual SDL_Window* GetWindowInternal() const = 0;
        virtual SDL_Renderer* GetRendererInternal() const = 0;

        virtual std::unique_ptr<ITextureBackend> CreateTexture(const ImageData& data) = 0;
        virtual std::unique_ptr<ISpriteBatchBackend> CreateSpriteBatch() = 0;

        /// Reads the rendered backbuffer pixels for the given region into @p pixels (RGBA8).
        /// @p x, @p y are top-left in game coordinates; @p pixels must hold w*h*4 bytes.
        /// Default implementation throws — override in backends that support readback.
        virtual void ReadBackbuffer(int x, int y, int w, int h, uint8_t* pixels)
        {
            throw std::runtime_error("ReadBackbuffer: not implemented in this backend");
        }

        /// Creates a backend occlusion query object. Returns nullptr on
        /// backends that do not support hardware occlusion queries.
        virtual std::unique_ptr<IOcclusionQueryBackend> CreateOcclusionQuery() { return nullptr; }

        /// Creates an off-screen FBO-backed render target. Returns nullptr on
        /// backends that do not support render targets.
        virtual std::unique_ptr<IRenderTargetBackend> CreateRenderTarget2D(int w, int h, bool hasDepth) { return nullptr; }

        /// Activates the given render target (binds its FBO). Pass nullptr to
        /// restore the default back buffer.
        virtual void SetRenderTarget2D(IRenderTargetBackend* rt) {}

        // ---- Graphics state ----

        /// Applies a BlendState to the backend. Default: no-op.
        virtual void ApplyBlendState(int colorSrcBlend, int alphaSrcBlend,
                                     int colorDstBlend, int alphaDstBlend,
                                     int colorBlendFunc, int alphaBlendFunc) {}

        /// Applies a DepthStencilState to the backend. Default: no-op.
        virtual void ApplyDepthStencilState(bool depthEnable, bool depthWriteEnable,
                                            int depthFunc) {}

        /// Applies a RasterizerState to the backend. Default: no-op.
        virtual void ApplyRasterizerState(int cullMode, int fillMode,
                                          bool scissorTestEnable) {}

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
        virtual void SetBlendEnabled(bool enabled) = 0;
        virtual void SetDepthWriteEnabled(bool enabled) = 0;

        /**
         * @brief Creates a backend-specific vertex buffer for
         *        `VertexPositionColor` data.
         */
        virtual std::unique_ptr<IVertexBufferBackend> CreateVertexBuffer(int vertex_capacity) = 0;

        /**
         * @brief Creates a backend-specific 16-bit index buffer.
         */
        virtual std::unique_ptr<IIndexBufferBackend> CreateIndexBuffer16(int index_capacity) = 0;
        /// Creates a 32-bit index buffer. Default delegates to CreateIndexBuffer16 for
        /// backends that do not yet support 32-bit indices.
        virtual std::unique_ptr<IIndexBufferBackend> CreateIndexBuffer32(int index_capacity)
        {
            return CreateIndexBuffer16(index_capacity);
        }

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

        /**
         * @brief Effect-aware draw — selects the shader variant based on
         *        vertex layout (derived from stride) and @p params.
         *
         * Default implementation falls back to DrawColoredPrimitives so
         * backends that have not yet implemented this path still work.
         */
        virtual void DrawPrimitivesEx(const IVertexBufferBackend& vb,
                                      const Matrix& world,
                                      const Matrix& view,
                                      const Matrix& projection,
                                      PrimitiveType primitive,
                                      int primitiveCount,
                                      const GpuDrawParams& params)
        {
            DrawColoredPrimitives(vb, world, view, projection, primitive, primitiveCount);
        }

        /**
         * @brief Indexed counterpart of `DrawPrimitivesEx`.
         */
        virtual void DrawIndexedPrimitivesEx(const IVertexBufferBackend& vb,
                                             const IIndexBufferBackend& ib,
                                             const Matrix& world,
                                             const Matrix& view,
                                             const Matrix& projection,
                                             PrimitiveType primitive,
                                             int primitiveCount,
                                             const GpuDrawParams& params)
        {
            DrawIndexedColoredPrimitives(vb, ib, world, view, projection, primitive, primitiveCount);
        }

        /// Instanced indexed draw — default throws on backends that don't support it.
        virtual void DrawInstancedPrimitivesEx(const IVertexBufferBackend& vb,
                                               const IIndexBufferBackend& ib,
                                               const Matrix& world,
                                               const Matrix& view,
                                               const Matrix& projection,
                                               PrimitiveType primitive,
                                               int primitiveCount,
                                               int instanceCount,
                                               const GpuDrawParams& params)
        {
            (void)vb; (void)ib; (void)world; (void)view; (void)projection;
            (void)primitive; (void)primitiveCount; (void)instanceCount; (void)params;
            throw std::runtime_error(
                "DrawInstancedPrimitives is not supported on this graphics backend.");
        }

        // ---- Debug / testing ----

        /// Simulates an OpenGL context loss.
        /// On Web (Emscripten): triggers WEBGL_lose_context.loseContext().
        /// On desktop: destroys the SDL GL context and immediately recreates it,
        /// forcing all GPU resources to be re-initialised.
        virtual void DebugSimulateContextLoss() {}

        /// Simulates an OpenGL context restore after a previous DebugSimulateContextLoss().
        /// On Web: triggers WEBGL_lose_context.restoreContext().
        /// On desktop: equivalent to DebugSimulateContextLoss() (destroy + recreate).
        virtual void DebugRestoreContext() {}

        // ---- Window → backend registry ----
        // Backends that implement TransformWindowToLogical register themselves here
        // so that SdlInputBridge can map physical mouse coordinates to logical ones
        // even for backends that have no SDL_Renderer (e.g. EasyGL).

        static void RegisterForWindow(SDL_Window* window, IGraphicsBackend* backend)
        {
            windowRegistry()[window] = backend;
        }
        static void UnregisterForWindow(SDL_Window* window)
        {
            windowRegistry().erase(window);
        }
        static IGraphicsBackend* GetForWindow(SDL_Window* window)
        {
            auto& reg = windowRegistry();
            auto it = reg.find(window);
            return it != reg.end() ? it->second : nullptr;
        }

    private:
        static std::unordered_map<SDL_Window*, IGraphicsBackend*>& windowRegistry()
        {
            static std::unordered_map<SDL_Window*, IGraphicsBackend*> reg;
            return reg;
        }
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
        /// When false, the EasyGL backend will not keep CPU-side copies of
        /// texture pixels or vertex/index data and will not register resources
        /// with the ResourceRegistry. This eliminates the per-texture CPU
        /// shadow copy overhead at the cost of making GL context-loss recovery
        /// impossible. Safe on desktop where context loss never occurs.
        bool contextRecoveryEnabled = true;
    };

    // Factory function to be implemented by each backend
    // INTERNAL API - SDL dependency should be abstracted later
    std::unique_ptr<IGraphicsBackend> CreateGraphicsBackend(const GraphicsBackendCreateArgs& args);
}
