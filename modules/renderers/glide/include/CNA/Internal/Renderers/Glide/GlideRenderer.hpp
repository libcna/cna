#pragma once

#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"

#include <memory>

namespace CNA::Internal::Renderers::Glide
{
    class GlideTextureRenderer;
    class GlideSpriteBatchRenderer;

    /**
     * @brief Native 3dfx Glide 3.x renderer, intended for an emulated `glide3x.dll` runtime.
     *
     * CNA loads Glide dynamically from `CNA_GLIDE3X_DLL` or the executable's DLL search path. It
     * never falls back to another graphics API: every Clear, texture upload, sprite draw and
     * present in this renderer is a Glide call.  Besides 2D-over-Glide (`SpriteBatch` quads
     * submitted as two `grDrawTriangle` calls), the native fixed-function 3D path supports
     * clipped fixed-function triangle lists and strips with a hardware Z buffer.
     *
     * Historical Glide constraints are preserved intentionally: textures are converted to ARGB4444,
     * padded to a power of two and split into hardware-sized tiles when needed. The display context
     * is selected from a Glide-reported double-buffered mode; the game virtual resolution must fit
     * inside it. Render targets, programmable effects, volume/cube textures, stencil and GPU skinning
     * remain intentionally unsupported.
     */
    class GlideRenderer final : public IGraphicsRenderer
    {
    public:
        explicit GlideRenderer(const GraphicsRendererCreateArgs& args);
        ~GlideRenderer() override;

        GlideRenderer(const GlideRenderer&) = delete;
        GlideRenderer& operator=(const GlideRenderer&) = delete;

        void Clear(float r, float g, float b, float a) override;
        void Present() override;
        void GetViewportSize(int& width, int& height) override;
        void ReadBackbuffer(int x, int y, int w, int h, uint8_t* pixels) override;
        void SetVirtualResolution(int width, int height) override;
        void SetPresentationMode(int mode) override;
        void SetSwapInterval(int interval) override;
        void SetViewport(int x, int y, int w, int h, float minDepth, float maxDepth) override;

        std::unique_ptr<ITextureRenderer> CreateTexture(const ImageData& data) override;
        std::unique_ptr<ISpriteBatchRenderer> CreateSpriteBatch() override;
        std::unique_ptr<IRenderTargetRenderer> CreateRenderTarget2D(int w, int h, int depthFormat,
                                                                    bool preserveContents, bool mipMap,
                                                                    int multiSampleCount) override;
        void SetRenderTarget2D(IRenderTargetRenderer* rt) override;
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
        std::unique_ptr<IVertexBufferRenderer> CreateVertexBuffer(int vertexCapacity) override;
        std::unique_ptr<IIndexBufferRenderer> CreateIndexBuffer16(int indexCapacity) override;
        std::unique_ptr<IIndexBufferRenderer> CreateIndexBuffer32(int indexCapacity) override;
        std::unique_ptr<IOcclusionQueryRenderer> CreateOcclusionQuery() override;
        void DrawColoredPrimitives(const IVertexBufferRenderer& vb,
                                   const Matrix& world, const Matrix& view, const Matrix& projection,
                                   PrimitiveType primitive, int primitiveCount) override;
        void DrawIndexedColoredPrimitives(const IVertexBufferRenderer& vb,
                                          const IIndexBufferRenderer& ib,
                                          const Matrix& world, const Matrix& view, const Matrix& projection,
                                          PrimitiveType primitive, int primitiveCount) override;
        void DrawPrimitivesEx(const IVertexBufferRenderer& vb,
                              const Matrix& world, const Matrix& view, const Matrix& projection,
                              PrimitiveType primitive, int primitiveCount,
                              const GpuDrawParams& params) override;
        void DrawIndexedPrimitivesEx(const IVertexBufferRenderer& vb,
                                     const IIndexBufferRenderer& ib,
                                     const Matrix& world, const Matrix& view, const Matrix& projection,
                                     PrimitiveType primitive, int primitiveCount,
                                     const GpuDrawParams& params) override;

    private:
        struct Impl;
        std::shared_ptr<Impl> impl_;

        void DrawSprite(const ITextureRenderer& texture, const Rectangle& destinationRectangle,
                        const Rectangle& sourceRectangle, const Color& color, float rotation,
                        const Vector2& origin, SpriteEffects effects, const Matrix& transform,
                        int textureFilter, int addressU, int addressV);
        void DrawPrimitiveRange(const IVertexBufferRenderer& vb,
                                const Matrix& world, const Matrix& view, const Matrix& projection,
                                PrimitiveType primitive, int primitiveCount, int vertexStart,
                                const GpuDrawParams& params);
        void DrawIndexedPrimitiveRange(const IVertexBufferRenderer& vb,
                                       const IIndexBufferRenderer& ib,
                                       const Matrix& world, const Matrix& view, const Matrix& projection,
                                       PrimitiveType primitive, int primitiveCount,
                                       int startIndex, int baseVertex, const GpuDrawParams& params);

        friend class GlideTextureRenderer;
        friend class GlideSpriteBatchRenderer;
    };
} // namespace CNA::Internal::Renderers::Glide
