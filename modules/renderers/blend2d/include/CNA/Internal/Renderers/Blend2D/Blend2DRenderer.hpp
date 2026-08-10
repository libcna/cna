#pragma once

#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "CNA/Internal/Renderers/Blend2D/Blend2DSurface.hpp"

#include <blend2d/blend2d.h>
#include <SDL3/SDL.h>

#include <cstdint>
#include <memory>

namespace CNA::Internal::Renderers::Blend2D
{
    /// Vertex/index buffer handles: Blend2D has no 3D vertex pipeline at all (it is a 2D-only
    /// vector rasterizer), so these keep only the bookkeeping IVertexBufferRenderer/
    /// IIndexBufferRenderer require -- the same deliberate no-backing-storage shape as the Stub
    /// renderer's handles (StubRenderer.hpp). Never reached by a real draw: Blend2DRenderer's
    /// DrawColoredPrimitives/DrawIndexedColoredPrimitives reject before touching them.
    class Blend2DVertexBufferRenderer final : public IVertexBufferRenderer
    {
    public:
        explicit Blend2DVertexBufferRenderer(int vertexCapacity) : vertexCount_(vertexCapacity) {}

        void SetData(const void* /*data*/, int vertex_count, std::size_t /*stride_in_bytes*/) override
        {
            vertexCount_ = vertex_count;
        }

        /// Discarded deliberately, the explicit decision IVertexBufferRenderer requires: this
        /// renderer keeps no vertex storage and binds no native layout, so there is nothing a
        /// declaration could describe unfaithfully (same reasoning as StubRenderer.hpp).
        void SetVertexDeclaration(const VertexDeclaration& /*vertexDeclaration*/) override {}

        [[nodiscard]] int GetVertexCount() const override { return vertexCount_; }

    private:
        int vertexCount_;
    };

    class Blend2DIndexBufferRenderer final : public IIndexBufferRenderer
    {
    public:
        explicit Blend2DIndexBufferRenderer(int indexCapacity) : indexCount_(indexCapacity) {}

        void SetData16(const void* /*data*/, int index_count) override
        {
            indexCount_ = index_count;
            isThirtyTwoBit_ = false;
        }

        void SetData32(const void* /*data*/, int index_count) override
        {
            indexCount_ = index_count;
            isThirtyTwoBit_ = true;
        }

        [[nodiscard]] int GetIndexCount() const override { return indexCount_; }
        [[nodiscard]] bool IsThirtyTwoBit() const override { return isThirtyTwoBit_; }

    private:
        int indexCount_;
        bool isThirtyTwoBit_ = false;
    };

    /**
     * @brief Internal mixin exposing the concrete BLImage backing a texture-shaped renderer
     * handle (plain texture or bindable render target).
     *
     * ISpriteBatchRenderer::Draw() receives only the abstract ITextureRenderer&, and dynamic_cast
     * to this interface is the safe way to reach the real image for blit_image() -- both concrete
     * classes below multiply-inherit it, so the cast can only fail if a *different* renderer's
     * texture handle reached this renderer, which is a caller bug worth a loud std::bad_cast
     * rather than a silent no-op (the same UB concern SdlRenderer.cpp's Draw() overloads document
     * for their own sibling-class RenderTarget/Texture handles, resolved here through a real
     * virtual interface instead of relying on GetNativeTexture() alone).
     */
    class Blend2DNativeImageSourceEXT
    {
    public:
        virtual ~Blend2DNativeImageSourceEXT() = default;
        [[nodiscard]] virtual const BLImage& NativeImageEXT() const = 0;
    };

    class Blend2DRenderer;

    /// Plain Texture2D handle: an owned premultiplied BLImage plus straight-RGBA reads/writes
    /// through Blend2DPixelConvert.hpp (Blend2D's own storage is premultiplied and channel-
    /// swapped -- see that header -- so every transfer converts, never a raw byte copy).
    class Blend2DTextureRenderer final : public ITextureRenderer, public Blend2DNativeImageSourceEXT
    {
    public:
        Blend2DTextureRenderer(int width, int height, const std::uint8_t* rgba);

        [[nodiscard]] int GetWidth() const override { return width_; }
        [[nodiscard]] int GetHeight() const override { return height_; }
        [[nodiscard]] SDL_Texture* GetNativeTexture() const override { return nullptr; }
        void UpdatePixels(const std::uint8_t* rgba, int stride) override;
        [[nodiscard]] bool GetData(int level, int x, int y, int w, int h, void* data,
                                   int dataLength) const override;
        [[nodiscard]] const BLImage& NativeImageEXT() const override { return image_; }

    private:
        int width_;
        int height_;
        BLImage image_;
    };

    /// RenderTarget2D handle: owns its own Blend2DSurface (context-bound BLImage) so it can be
    /// drawn into directly, then sampled as an ordinary texture once unbound -- the established
    /// "render, unbind, sample" render-target contract this codebase's other renderers use.
    class Blend2DRenderTargetRenderer final : public IRenderTargetRenderer,
                                               public Blend2DNativeImageSourceEXT
    {
    public:
        Blend2DRenderTargetRenderer(Blend2DRenderer& owner, int width, int height,
                                    bool preserveContents);
        ~Blend2DRenderTargetRenderer() override;

        [[nodiscard]] int GetWidth() const override { return surface_.Width(); }
        [[nodiscard]] int GetHeight() const override { return surface_.Height(); }
        [[nodiscard]] SDL_Texture* GetNativeTexture() const override { return nullptr; }
        [[nodiscard]] bool GetData(int level, int x, int y, int w, int h, void* data,
                                   int dataLength) const override;
        [[nodiscard]] const BLImage& NativeImageEXT() const override { return surface_.Image(); }

        /// Activates this target on the owning renderer -- mirrors SdlRenderTargetRenderer's own
        /// captured-SDL_Renderer* binding shape, just against Blend2DRenderer's single tracked
        /// "active surface" pointer instead of a native single-target API call.
        void BindAsRenderTarget() override;
        /// Restores the backbuffer, but only if this target is still the active one (a stale
        /// unbind after a different target was already bound must not clobber it).
        void UnbindAsRenderTarget() override;

        /// Blend2D's 2D raster targets never allocate a real depth/stencil plane, regardless of
        /// the requested DepthFormat -- the same truthful override SDL_Renderer's own 2D-only
        /// render targets use (IGraphicsRenderer.hpp's HasRealDepthBuffer doc comment, Task 708).
        [[nodiscard]] bool HasRealDepthBuffer(bool /*depthFormatWasRequested*/) const override
        {
            return false;
        }

        [[nodiscard]] Blend2DSurface& Surface() noexcept { return surface_; }
        [[nodiscard]] bool PreservesContents() const noexcept { return preserveContents_; }

    private:
        Blend2DRenderer& owner_;
        Blend2DSurface surface_;
        bool preserveContents_;
    };

    /// Real Blend2D-backed 2D sprite batch: every Draw() overload composes a BLContext transform
    /// (translate to destination, rotate, flip via negative scale, offset by the scaled origin)
    /// and blits the source sub-rectangle through blit_image() -- genuine per-draw rasterization,
    /// not a recorded or no-op placeholder. A non-white tint builds a temporary premultiplied
    /// sub-image (BuildTintedSubImageEXT) since Blend2D's stock image blit has no per-draw colour
    /// modulation of its own.
    class Blend2DSpriteBatchRenderer final : public ISpriteBatchRenderer
    {
    public:
        explicit Blend2DSpriteBatchRenderer(Blend2DRenderer& renderer) : renderer_(renderer) {}

        void Begin() override;
        void End() override;

        void Draw(const ITextureRenderer& texture, float x, float y) override;
        void Draw(const ITextureRenderer& texture, const Rectangle& destinationRectangle,
                  const Rectangle& sourceRectangle, const Color& color) override;
        void Draw(const ITextureRenderer& texture, const Rectangle& destinationRectangle,
                  const Rectangle& sourceRectangle, const Color& color, float rotation,
                  const Vector2& origin, SpriteEffects effects, float layerDepth) override;

    private:
        void DrawQuad(const ITextureRenderer& texture, double destX, double destY, double destW,
                      double destH, int srcX, int srcY, int srcW, int srcH, const Color& color,
                      double rotation, double originX, double originY, bool flipH, bool flipV);

        Blend2DRenderer& renderer_;
        bool begun_ = false;
    };

    /**
     * @brief CNAEXT. The Blend2D graphics renderer -- a genuine 2D-only `IGraphicsRenderer`
     * implementation backed by the Blend2D vector rasterizer (https://github.com/blend2d/blend2d).
     *
     * Every draw is real Blend2D CPU rasterization into an owned BLImage backbuffer (or a bound
     * RenderTarget2D's own BLImage); presentation uploads the completed frame to an SDL streaming
     * texture -- the same "CPU raster + SDL presentation" shape already established by the SKIA
     * renderer (docs/skia-renderer.md). SDL never executes a Blend2D draw command, it only
     * displays the finished image. The 3D pipeline (vertex/index buffers, DrawColoredPrimitives,
     * depth/stencil) has no Blend2D equivalent and is truthfully refused via
     * Ensure3DSupported/HandleUnsupported3DCall.
     */
    class Blend2DRenderer final : public IGraphicsRenderer
    {
    public:
        Blend2DRenderer(SDL_Window* window, int virtualWidth, int virtualHeight,
                        CnaPresentationMode presentationMode, int swapInterval);
        ~Blend2DRenderer() override;

        void Clear(float r, float g, float b, float a) override;
        void Present() override;
        void GetViewportSize(int& width, int& height) override;
        void SetVirtualResolution(int width, int height) override;
        void SetPresentationMode(int mode) override;
        void SetSwapInterval(int interval) override;

        [[nodiscard]] SDL_Window* GetWindowInternal() const override { return window_; }
        [[nodiscard]] SDL_Renderer* GetRendererInternal() const override { return presentRenderer_; }

        std::unique_ptr<ITextureRenderer> CreateTexture(const ImageData& data) override;
        std::unique_ptr<ISpriteBatchRenderer> CreateSpriteBatch() override;
        std::unique_ptr<IRenderTargetRenderer> CreateRenderTarget2D(
            int w, int h, int depthFormat, bool preserveContents = false, bool mipMap = false,
            int multiSampleCount = 0) override;
        /// Blend2D supports exactly one active render target at a time -- the shared
        /// IGraphicsRenderer::SetRenderTargets default would otherwise silently bind only rts[0]
        /// and ignore the rest (same Task 709 reasoning SdlRenderer.cpp documents). Cube-face
        /// targets are rejected: Blend2D has no cube-map concept at all.
        void SetRenderTargets(const RenderTargetBindingDescriptor* renderTargets, int count) override;
        /// GraphicsDevice::SetRenderTarget(RenderTarget2D*) -- the single-target legacy entry
        /// point -- calls this directly rather than SetRenderTargets(); the shared
        /// IGraphicsRenderer default is a no-op, which would silently leave the backbuffer active
        /// while the caller believes a target is bound. Delegates to the same
        /// BindAsRenderTarget()/activeRenderTarget_ mechanism SetRenderTargets() uses.
        void SetRenderTarget2D(IRenderTargetRenderer* rt) override;

        void ReadBackbuffer(int x, int y, int w, int h, std::uint8_t* pixels) override;

        void ClearColorAndDepth(float r, float g, float b, float a, float /*depth*/) override
        {
            Clear(r, g, b, a);
        }
        void ClearDepth(float /*depth*/) override {}
        void ClearStencil(int /*stencil*/) override {}
        void ClearDepthAndStencil(float /*depth*/, int /*stencil*/) override {}
        void ClearColorAndStencil(float r, float g, float b, float a, int /*stencil*/) override
        {
            Clear(r, g, b, a);
        }
        void ClearColorDepthAndStencil(float r, float g, float b, float a, float /*depth*/,
                                       int /*stencil*/) override
        {
            Clear(r, g, b, a);
        }

        void SetDepthTestEnabled(bool /*enabled*/) override {}
        void SetBlendEnabled(bool enabled) override { blendEnabled_ = enabled; }
        void SetDepthWriteEnabled(bool /*enabled*/) override {}

        std::unique_ptr<IVertexBufferRenderer> CreateVertexBuffer(int vertex_capacity) override;
        std::unique_ptr<IIndexBufferRenderer> CreateIndexBuffer16(int index_capacity) override;

        void SetScissorRect(int x, int y, int w, int h) override;
        void ApplyBlendState(int colorSrcBlend, int alphaSrcBlend, int colorDstBlend,
                             int alphaDstBlend, int colorBlendFunc, int alphaBlendFunc,
                             const BlendWriteState& writeState) override;

        [[nodiscard]] bool SupportsDepthStencil() const override { return false; }
        [[nodiscard]] bool SupportsCapability(CNA::GraphicsCapability capability) const override;

        // Ensure3DSupported() deliberately keeps the shared IGraphicsRenderer no-op default
        // (not overridden here): GraphicsDevice's own DrawUserPrimitives/DrawUserIndexedPrimitives
        // overloads call it BEFORE their own argument validation (GraphicsDevice.cpp), so an
        // eager override here would make an invalid-argument call report "unsupported" instead of
        // the XNA-faithful ArgumentOutOfRangeException every renderer's argument-guard tests
        // expect (same reasoning SdlRenderer.cpp's own lack of an override embodies). Rejection
        // instead happens where the real 3D work would start: DrawColoredPrimitives/
        // DrawIndexedColoredPrimitives below, exactly like every other 2D-only renderer in this
        // codebase.
        void DrawColoredPrimitives(const IVertexBufferRenderer& /*vb*/, const Matrix& /*world*/,
                                   const Matrix& /*view*/, const Matrix& /*projection*/,
                                   PrimitiveType /*primitive*/, int /*primitiveCount*/) override
        {
            HandleUnsupported3DCall("BLEND2D", "DrawColoredPrimitives");
        }
        void DrawIndexedColoredPrimitives(const IVertexBufferRenderer& /*vb*/,
                                          const IIndexBufferRenderer& /*ib*/, const Matrix& /*world*/,
                                          const Matrix& /*view*/, const Matrix& /*projection*/,
                                          PrimitiveType /*primitive*/, int /*primitiveCount*/) override
        {
            HandleUnsupported3DCall("BLEND2D", "DrawIndexedColoredPrimitives");
        }

        /// The currently active raster surface -- the bound RenderTarget2D, or the backbuffer
        /// when none is bound. Every Clear()/SpriteBatch draw/readback targets this surface,
        /// matching XNA's "operations apply to the currently set render target" contract.
        [[nodiscard]] Blend2DSurface& ActiveSurface() noexcept;
        [[nodiscard]] BLCompOp ActiveCompOp() const noexcept;

        /// CNAEXT. Internal hook used by Blend2DRenderTargetRenderer::BindAsRenderTarget/
        /// UnbindAsRenderTarget (see that class) -- not part of the public IGraphicsRenderer
        /// contract, SetRenderTargets() is.
        void SetActiveRenderTargetEXT(Blend2DRenderTargetRenderer* target) noexcept
        {
            activeRenderTarget_ = target;
        }
        [[nodiscard]] Blend2DRenderTargetRenderer* GetActiveRenderTargetEXT() const noexcept
        {
            return activeRenderTarget_;
        }

    private:
        void RecreatePresentationTexture();

        SDL_Window* window_;
        SDL_Renderer* presentRenderer_ = nullptr;
        SDL_Texture* presentTexture_ = nullptr;
        int virtualWidth_;
        int virtualHeight_;
        CnaPresentationMode presentationMode_;
        int swapInterval_;

        Blend2DSurface backbuffer_;
        Blend2DRenderTargetRenderer* activeRenderTarget_ = nullptr;

        bool blendEnabled_ = true;
        BLCompOp appliedCompOp_ = BL_COMP_OP_SRC_OVER;
    };
} // namespace CNA::Internal::Renderers::Blend2D
