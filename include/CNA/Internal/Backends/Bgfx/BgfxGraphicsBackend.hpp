#pragma once

#include "../Common/IGraphicsBackend.hpp"
#include <bgfx/bgfx.h>
#include <SDL3/SDL.h>
#include <cstdint>

namespace CNA::Internal::Backends::Bgfx
{
    namespace Detail
    {
        bgfx::RendererType::Enum GetDefaultRendererType();
        bgfx::RendererType::Enum ParseRendererTypeOverride(const char* value);
        bgfx::RendererType::Enum ResolveRendererType(const char* value);
    }

    class BgfxGraphicsBackend;

    /// bgfx-backed occlusion query.
    class BgfxOcclusionQueryBackend : public IOcclusionQueryBackend
    {
    public:
        bgfx::OcclusionQueryHandle handle = BGFX_INVALID_HANDLE;

        BgfxOcclusionQueryBackend();
        ~BgfxOcclusionQueryBackend() override;

        void Begin() override {}
        void End()   override {}
        [[nodiscard]] bool IsComplete() const override;
        [[nodiscard]] int  PixelCount() const override;
    };

    class BgfxTextureBackend : public ITextureBackend
    {
    public:
        bgfx::TextureHandle textureHandle = BGFX_INVALID_HANDLE;
        int width = 0;
        int height = 0;

        explicit BgfxTextureBackend(const ImageData& data);
        ~BgfxTextureBackend() override;
        int GetWidth() const override { return width; }
        int GetHeight() const override { return height; }
        SDL_Texture* GetNativeTexture() const override { return nullptr; }
    };

    /// bgfx-backed cube map texture.
    class BgfxTextureCubeBackend : public ITextureCubeBackend
    {
    public:
        bgfx::TextureHandle handle = BGFX_INVALID_HANDLE;
        int size_ = 0;

        BgfxTextureCubeBackend(int size, bool mipMap, int surfaceFormat);
        ~BgfxTextureCubeBackend() override;

        void SetData(int face, int level, int x, int y, int w, int h,
                     const void* data, int dataLength) override;
    };

    /// bgfx-backed 3D (volume) texture.
    class BgfxTexture3DBackend : public ITexture3DBackend
    {
    public:
        bgfx::TextureHandle handle = BGFX_INVALID_HANDLE;

        BgfxTexture3DBackend(int w, int h, int depth, bool mipMap, int surfaceFormat);
        ~BgfxTexture3DBackend() override;

        void SetData(int level, int x, int y, int z,
                     int w, int h, int depth,
                     const void* data, int dataLength) override;
    };

    /// bgfx-backed 2D render target (bgfx framebuffer with color + depth textures).
    class BgfxRenderTargetBackend : public IRenderTargetBackend
    {
    public:
        bgfx::FrameBufferHandle fbo = BGFX_INVALID_HANDLE;
        bgfx::TextureHandle colorTex = BGFX_INVALID_HANDLE;
        int width = 0;
        int height = 0;

        BgfxRenderTargetBackend(int w, int h, bool hasDepth);
        ~BgfxRenderTargetBackend() override;

        int GetWidth()  const override { return width; }
        int GetHeight() const override { return height; }
        SDL_Texture* GetNativeTexture() const override { return nullptr; }
        void UpdatePixels(const uint8_t* rgba, int stride) override {}
        void BindGL() const override {}

        void BindAsRenderTarget()   override;
        void UnbindAsRenderTarget() override;
    };

    /// bgfx-backed cube map render target.
    class BgfxRenderTargetCubeBackend : public IRenderTargetCubeBackend
    {
    public:
        bgfx::FrameBufferHandle fbo = BGFX_INVALID_HANDLE;
        bgfx::TextureHandle cubeTex = BGFX_INVALID_HANDLE;
        int size_ = 0;

        BgfxRenderTargetCubeBackend(int size);
        ~BgfxRenderTargetCubeBackend() override;

        [[nodiscard]] int GetSize() const override { return size_; }
        void BindAsRenderTargetFace(int face) override;
        void UnbindAsRenderTarget() override;
        [[nodiscard]] unsigned int GetGLHandle() const override { return 0; }
    };

    class BgfxSpriteBatchBackend : public ISpriteBatchBackend
    {
    public:
        BgfxGraphicsBackend& graphicsBackend;
        bool begun = false;

        explicit BgfxSpriteBatchBackend(BgfxGraphicsBackend& graphicsBackend);
        ~BgfxSpriteBatchBackend() override = default;
        void Begin() override;
        void End() override;
        void Draw(const ITextureBackend& texture, float x, float y) override;
        void Draw(const ITextureBackend& texture,
                  const Rectangle& destinationRectangle,
                  const Rectangle& sourceRectangle,
                  const Color& color) override;
        void Draw(const ITextureBackend& texture,
                  const Rectangle& destinationRectangle,
                  const Rectangle& sourceRectangle,
                  const Color& color,
                  float rotation,
                  const Vector2& origin,
                  SpriteEffects effects,
                  float layerDepth) override;
    };

    class BgfxGraphicsBackend : public IGraphicsBackend
    {
    public:
        SDL_Window* window = nullptr;
        bgfx::ProgramHandle spriteProgram = BGFX_INVALID_HANDLE;
        bgfx::UniformHandle textureSampler = BGFX_INVALID_HANDLE;
        bgfx::ViewId spriteViewId = 0;
        bgfx::ViewId currentViewId_ = 0;  ///< Active view (0=backbuffer, 1=RT0, etc.)
        uint16_t cachedWidth = 0;
        uint16_t cachedHeight = 0;
        uint32_t clearRgba = 0x000000ff;
        bool initialized = false;

        // Stored graphics state applied per-draw in bgfx
        uint64_t blendFlags_  = BGFX_STATE_BLEND_ALPHA;
        uint64_t depthFlags_  = BGFX_STATE_DEPTH_TEST_LESS | BGFX_STATE_WRITE_Z;
        uint64_t cullFlags_   = BGFX_STATE_CULL_CCW;
        // Sampler flags per slot (slots 0–15)
        static constexpr int kMaxSamplerSlots = 16;
        uint32_t samplerFlags_[kMaxSamplerSlots] = {};
        // Blend color for BGFX_STATE_BLEND_FACTOR
        float blendFactorR_ = 1.f, blendFactorG_ = 1.f, blendFactorB_ = 1.f, blendFactorA_ = 1.f;
        // Scissor rect (0,0,0,0 = disabled)
        uint16_t scissorX_ = 0, scissorY_ = 0, scissorW_ = 0, scissorH_ = 0;
        // Temporary MRT framebuffer (created on SetRenderTargets with count > 1)
        bgfx::FrameBufferHandle mrtFbo_ = BGFX_INVALID_HANDLE;

        explicit BgfxGraphicsBackend(SDL_Window* window);
        ~BgfxGraphicsBackend() override;
        void Clear(float r, float g, float b, float a) override;
        void Present() override;
        void GetViewportSize(int& width, int& height) override;

        void SetVirtualResolution(int width, int height) override
        {
        } // no-op: Bgfx uses physical viewport
        void SetPresentationMode(int /*mode*/) override
        {
        } // no-op: Bgfx has no logical presentation
        SDL_Window* GetWindowInternal() const override { return window; }
        SDL_Renderer* GetRendererInternal() const override { return nullptr; }

        std::unique_ptr<ITextureBackend> CreateTexture(const ImageData& data) override;
        std::unique_ptr<ISpriteBatchBackend> CreateSpriteBatch() override;
        std::unique_ptr<IOcclusionQueryBackend> CreateOcclusionQuery() override;
        std::unique_ptr<ITexture3DBackend> CreateTexture3D(int w, int h, int depth, bool mipMap, int surfaceFormat) override;
        std::unique_ptr<ITextureCubeBackend> CreateTextureCube(int size, bool mipMap, int surfaceFormat) override;
        std::unique_ptr<IRenderTargetBackend> CreateRenderTarget2D(int w, int h, bool hasDepth) override;
        void SetRenderTarget2D(IRenderTargetBackend* rt) override;
        void SetRenderTargets(IRenderTargetBackend* const* rts, int count) override;
        std::unique_ptr<IRenderTargetCubeBackend> CreateRenderTargetCube(int size) override;

        // Graphics state (stored; applied per-draw in SubmitSprite and future 3D draws)
        void ApplyBlendState(int colorSrcBlend, int alphaSrcBlend,
                             int colorDstBlend, int alphaDstBlend,
                             int colorBlendFunc, int alphaBlendFunc) override;
        void ApplyDepthStencilState(bool depthEnable, bool depthWriteEnable, int depthFunc,
                                    bool stencilEnable, int stencilFunc,
                                    int stencilPass, int stencilFail, int stencilDepthFail,
                                    int stencilMask, int stencilWriteMask, int referenceStencil,
                                    bool twoSidedStencilMode,
                                    int ccwStencilFunc, int ccwStencilPass,
                                    int ccwStencilFail, int ccwStencilDepthFail) override;
        void ApplyRasterizerState(int cullMode, int fillMode,
                                  bool scissorTestEnable) override;
        void SetScissorRect(int x, int y, int w, int h) override;
        void ApplySamplerState(int slot, int filter, int addressU, int addressV,
                               int maxAnisotropy) override;
        void SetBlendFactor(float r, float g, float b, float a) override;

        // 3D pipeline: NOT supported by the bgfx backend yet.
        // @note Status: STUB. Every entry point throws std::runtime_error.
        void ClearColorAndDepth(float r, float g, float b, float a, float depth) override;
        void SetDepthTestEnabled(bool enabled) override;
        void SetBlendEnabled(bool enabled) override;
        void SetDepthWriteEnabled(bool enabled) override;
        std::unique_ptr<IVertexBufferBackend> CreateVertexBuffer(int vertex_capacity) override;
        std::unique_ptr<IIndexBufferBackend> CreateIndexBuffer16(int index_capacity) override;
        void DrawColoredPrimitives(const IVertexBufferBackend& vb,
                                   const Matrix& world, const Matrix& view, const Matrix& projection,
                                   PrimitiveType primitive, int primitiveCount) override;
        void DrawIndexedColoredPrimitives(const IVertexBufferBackend& vb,
                                          const IIndexBufferBackend& ib,
                                          const Matrix& world, const Matrix& view, const Matrix& projection,
                                          PrimitiveType primitive, int primitiveCount) override;

        void SubmitSprite(const BgfxTextureBackend& texture,
                          const Rectangle& destinationRectangle,
                          const Rectangle& sourceRectangle,
                          const Color& color,
                          float rotation,
                          const Vector2& origin,
                          SpriteEffects effects,
                          float layerDepth);

    private:
        void EnsureViewState();
    };
}
