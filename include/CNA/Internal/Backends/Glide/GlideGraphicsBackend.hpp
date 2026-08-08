#pragma once

#include "../Common/IGraphicsBackend.hpp"

#include <memory>

namespace CNA::Internal::Backends::Glide
{
    class GlideTextureBackend;
    class GlideSpriteBatchBackend;

    /**
     * @brief Native 3dfx Glide 3.x backend, intended for an emulated `glide3x.dll` runtime.
     *
     * CNA loads Glide dynamically from `CNA_GLIDE3X_DLL` or the executable's DLL search path. It
     * never falls back to another graphics API: every Clear, texture upload, sprite draw and
     * present in this backend is a Glide call.  Besides 2D-over-Glide (`SpriteBatch` quads
     * submitted as two `grDrawTriangle` calls), the native fixed-function 3D path supports
     * clipped fixed-function triangle lists and strips with a hardware Z buffer.
     *
     * Historical Glide constraints are preserved intentionally: textures are converted to ARGB4444,
     * padded to a power of two and split into hardware-sized tiles when needed. The display context
     * is selected from a Glide-reported double-buffered mode; the game virtual resolution must fit
     * inside it. Render targets, programmable effects, volume/cube textures, stencil and GPU skinning
     * remain intentionally unsupported.
     */
    class GlideGraphicsBackend final : public IGraphicsBackend
    {
    public:
        explicit GlideGraphicsBackend(const GraphicsBackendCreateArgs& args);
        ~GlideGraphicsBackend() override;

        GlideGraphicsBackend(const GlideGraphicsBackend&) = delete;
        GlideGraphicsBackend& operator=(const GlideGraphicsBackend&) = delete;

        void Clear(float r, float g, float b, float a) override;
        void Present() override;
        void GetViewportSize(int& width, int& height) override;
        void ReadBackbuffer(int x, int y, int w, int h, uint8_t* pixels) override;
        void SetVirtualResolution(int width, int height) override;
        void SetPresentationMode(int mode) override;
        void SetSwapInterval(int interval) override;
        void SetViewport(int x, int y, int w, int h, float minDepth, float maxDepth) override;
        SDL_Window* GetWindowInternal() const override;
        SDL_Renderer* GetRendererInternal() const override { return nullptr; }

        std::unique_ptr<ITextureBackend> CreateTexture(const ImageData& data) override;
        std::unique_ptr<ISpriteBatchBackend> CreateSpriteBatch() override;
        std::unique_ptr<IRenderTargetBackend> CreateRenderTarget2D(int w, int h, int depthFormat,
                                                                    bool preserveContents, bool mipMap,
                                                                    int multiSampleCount) override;
        void SetRenderTarget2D(IRenderTargetBackend* rt) override;
        void SetRenderTargets(const RenderTargetBindingDescriptor* renderTargets, int count) override;
        void SetScissorRect(int x, int y, int w, int h) override;
        void ApplyBlendState(int colorSrcBlend, int alphaSrcBlend,
                             int colorDstBlend, int alphaDstBlend,
                             int colorBlendFunc, int alphaBlendFunc,
                             const BlendWriteState& writeState) override;
        void ApplyDepthStencilState(bool depthEnable, bool depthWriteEnable,
                                    int depthFunc,
                                    bool stencilEnable, int stencilFunc,
                                    int stencilPass, int stencilFail, int stencilDepthFail,
                                    int stencilMask, int stencilWriteMask, int referenceStencil,
                                    bool twoSidedStencilMode,
                                    int ccwStencilFunc, int ccwStencilPass,
                                    int ccwStencilFail, int ccwStencilDepthFail) override;
        void ApplyRasterizerState(int cullMode, int fillMode,
                                  bool scissorTestEnable,
                                  float depthBias = 0.0f,
                                  float slopeScaleDepthBias = 0.0f) override;
        void ApplySamplerState(int slot, int filter,
                               int addressU, int addressV,
                               int maxAnisotropy) override;
        void ApplySamplerMipState(int slot, int maxMipLevel, float lodBias) override;

        [[nodiscard]] bool SupportsDepthStencil() const override { return false; }
        [[nodiscard]] bool SupportsDepthBuffer() const override { return true; }
        [[nodiscard]] bool SupportsStencilBuffer() const override { return false; }
        [[nodiscard]] bool SupportsCapability(CNA::GraphicsCapability capability) const override;
        [[nodiscard]] int GetMaxTextureDimension() const override;

        void ClearColorAndDepth(float r, float g, float b, float a, float depth) override;
        void ClearDepth(float depth) override;
        void ClearStencil(int stencil) override;
        void ClearDepthAndStencil(float depth, int stencil) override;
        void ClearColorAndStencil(float r, float g, float b, float a, int stencil) override;
        void ClearColorDepthAndStencil(float r, float g, float b, float a, float depth, int stencil) override;
        void SetDepthTestEnabled(bool enabled) override;
        void SetBlendEnabled(bool enabled) override;
        void SetDepthWriteEnabled(bool enabled) override;
        std::unique_ptr<IVertexBufferBackend> CreateVertexBuffer(int vertexCapacity) override;
        std::unique_ptr<IIndexBufferBackend> CreateIndexBuffer16(int indexCapacity) override;
        std::unique_ptr<IIndexBufferBackend> CreateIndexBuffer32(int indexCapacity) override;
        std::unique_ptr<IOcclusionQueryBackend> CreateOcclusionQuery() override;
        void DrawColoredPrimitives(const IVertexBufferBackend& vb,
                                   const Matrix& world, const Matrix& view, const Matrix& projection,
                                   PrimitiveType primitive, int primitiveCount) override;
        void DrawIndexedColoredPrimitives(const IVertexBufferBackend& vb,
                                          const IIndexBufferBackend& ib,
                                          const Matrix& world, const Matrix& view, const Matrix& projection,
                                          PrimitiveType primitive, int primitiveCount) override;
        void DrawPrimitivesEx(const IVertexBufferBackend& vb,
                              const Matrix& world, const Matrix& view, const Matrix& projection,
                              PrimitiveType primitive, int primitiveCount,
                              const GpuDrawParams& params) override;
        void DrawIndexedPrimitivesEx(const IVertexBufferBackend& vb,
                                     const IIndexBufferBackend& ib,
                                     const Matrix& world, const Matrix& view, const Matrix& projection,
                                     PrimitiveType primitive, int primitiveCount,
                                     const GpuDrawParams& params) override;

    private:
        struct Impl;
        std::shared_ptr<Impl> impl_;

        void DrawSprite(const ITextureBackend& texture, const Rectangle& destinationRectangle,
                        const Rectangle& sourceRectangle, const Color& color, float rotation,
                        const Vector2& origin, SpriteEffects effects, const Matrix& transform,
                        int textureFilter, int addressU, int addressV);
        void DrawPrimitiveRange(const IVertexBufferBackend& vb,
                                const Matrix& world, const Matrix& view, const Matrix& projection,
                                PrimitiveType primitive, int primitiveCount, int vertexStart,
                                const GpuDrawParams& params);
        void DrawIndexedPrimitiveRange(const IVertexBufferBackend& vb,
                                       const IIndexBufferBackend& ib,
                                       const Matrix& world, const Matrix& view, const Matrix& projection,
                                       PrimitiveType primitive, int primitiveCount,
                                       int startIndex, int baseVertex, const GpuDrawParams& params);

        friend class GlideTextureBackend;
        friend class GlideSpriteBatchBackend;
    };
} // namespace CNA::Internal::Backends::Glide
