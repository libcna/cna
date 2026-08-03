// SPDX-License-Identifier: MS-PL
#pragma once

#include "../Common/IGraphicsBackend.hpp"

#include <d2d1_1.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <wrl/client.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

namespace CNA::Internal::Backends::Direct2D
{
    class Direct2DGraphicsBackend;

    /**
     * @brief The exact Direct2D primitive/image composite selected from CNA BlendState.
     *
     * Primitive blend represents source-over, copy and additive composition; DrawImage adds the
     * exact Porter-Duff modes below. Direct2D still has no general source/destination-factor
     * blend-equation interface. Keeping this mapping device-free makes the contract unit-testable.
     */
    enum class Direct2DBlendMode
    {
        SourceOver,
        Copy,
        Add,
        DestinationOver,
        SourceIn,
        DestinationIn,
        SourceOut,
        DestinationOut,
        SourceAtop,
        DestinationAtop,
        Xor
    };

    Direct2DBlendMode BlendStateToDirect2DBlendMode(int colorSrcBlend, int alphaSrcBlend,
                                                    int colorDstBlend, int alphaDstBlend,
                                                    int colorBlendFunc, int alphaBlendFunc);

    /** Pure helpers kept public to make Direct2D's manually managed mip policy unit-testable. */
    int PreferredMipLevelForTransform(int sourceWidth, int sourceHeight,
                                      int destinationWidth, int destinationHeight,
                                      float rotation, const Matrix& batchTransform,
                                      float presentationScaleX, float presentationScaleY,
                                      bool* minifying = nullptr);
    Rectangle MapSourceRectangleToMip(const Rectangle& sourceRectangle,
                                      int baseWidth, int baseHeight,
                                      int mipWidth, int mipHeight);

    /** A device-dependent Direct2D bitmap plus the source RGBA8 shadow required for CPU-side tint,
     *  flip, wrap/mirror, and non-premultiplied SpriteBatch variants. */
    class Direct2DTextureBackend final : public ITextureBackend
    {
    public:
        Direct2DTextureBackend(Direct2DGraphicsBackend& owner, const ImageData& data);
        ~Direct2DTextureBackend() override;

        [[nodiscard]] int GetWidth() const override { return width_; }
        [[nodiscard]] int GetHeight() const override { return height_; }
        [[nodiscard]] SDL_Texture* GetNativeTexture() const override { return nullptr; }
        void UpdatePixels(const uint8_t* rgba, int stride) override;
        void UpdatePixelsLevel(int level, const uint8_t* rgba, int levelW, int levelH) override;

        [[nodiscard]] ID2D1Bitmap1* Bitmap() const;
        [[nodiscard]] ID2D1Bitmap1* BitmapForLevel(int level) const;
        /// Returns the greatest initialized mip level not exceeding @p preferredLevel. A texture
        /// with no uploaded lower levels deliberately falls back to level zero instead of
        /// sampling black/uninitialized storage during minification.
        [[nodiscard]] int SelectAvailableMipLevel(int preferredLevel) const;
        [[nodiscard]] const std::vector<uint8_t>& RgbaPixelsForLevel(int level) const;

    private:
        friend class Direct2DGraphicsBackend;
        void RecreateBitmap();

        Direct2DGraphicsBackend* owner_ = nullptr;
        Microsoft::WRL::ComPtr<ID2D1Bitmap1> bitmap_;
        std::vector<uint8_t> rgbaPixels_;
        std::vector<Microsoft::WRL::ComPtr<ID2D1Bitmap1>> mipBitmaps_;
        std::vector<std::vector<uint8_t>> mipRgbaPixels_;
        int width_ = 0;
        int height_ = 0;
        std::uint64_t deviceGeneration_ = 0;
    };

    /** A GPU-resident Direct2D target bitmap.  It can be sampled after it is unbound. */
    class Direct2DRenderTargetBackend final : public IRenderTargetBackend
    {
    public:
        Direct2DRenderTargetBackend(Direct2DGraphicsBackend& owner, int width, int height, bool mipMap);
        ~Direct2DRenderTargetBackend() override;

        [[nodiscard]] int GetWidth() const override { return width_; }
        [[nodiscard]] int GetHeight() const override { return height_; }
        [[nodiscard]] SDL_Texture* GetNativeTexture() const override { return nullptr; }
        void UpdatePixels(const uint8_t* rgba, int stride) override;
        void UpdatePixelsLevel(int level, const uint8_t* rgba, int levelW, int levelH) override;
        [[nodiscard]] bool GetData(int level, int x, int y, int w, int h,
                                   void* data, int dataLength) const override;

        void BindAsRenderTarget() override;
        void UnbindAsRenderTarget() override;
        [[nodiscard]] bool HasRealDepthBuffer(bool /*depthFormatWasRequested*/) const override
        {
            return false;
        }

        [[nodiscard]] ID2D1Bitmap1* Bitmap() const;
        [[nodiscard]] ID2D1Bitmap1* BitmapForLevel(int level) const;
        [[nodiscard]] int SelectAvailableMipLevel(int preferredLevel) const;

    private:
        friend class Direct2DGraphicsBackend;
        void RecreateBitmap();
        void MarkMipLevelsDirty();
        void EnsureMipLevelsCurrent();

        Direct2DGraphicsBackend* owner_ = nullptr;
        Microsoft::WRL::ComPtr<ID2D1Bitmap1> bitmap_;
        std::vector<Microsoft::WRL::ComPtr<ID2D1Bitmap1>> mipBitmaps_;
        int width_ = 0;
        int height_ = 0;
        bool mipMap_ = false;
        bool mipLevelsDirty_ = false;
        std::uint64_t deviceGeneration_ = 0;
    };

    class Direct2DSpriteBatchBackend final : public ISpriteBatchBackend
    {
    public:
        explicit Direct2DSpriteBatchBackend(Direct2DGraphicsBackend& owner) : owner_(&owner) {}

        void Begin() override;
        void End() override;
        void SetTransformMatrix(const Matrix& matrix) override { transform_ = matrix; }
        void SetCustomEffect(Effect* effect) override;
        void SetSamplerFilter(int textureFilter) override;
        void SetSamplerAddressMode(int addressU, int addressV) override;
        void Draw(const ITextureBackend& texture, float x, float y) override;
        void Draw(const ITextureBackend& texture, const Rectangle& destinationRectangle,
                  const Rectangle& sourceRectangle, const Color& color) override;
        void Draw(const ITextureBackend& texture, const Rectangle& destinationRectangle,
                  const Rectangle& sourceRectangle, const Color& color, float rotation,
                  const Vector2& origin, SpriteEffects effects, float layerDepth) override;

    private:
        Direct2DGraphicsBackend* owner_ = nullptr;
        Matrix transform_ = Matrix::getIdentityProperty();
        bool begun_ = false;
        int textureFilter_ = 0; // TextureFilter::Linear
        int addressU_ = 1; // TextureAddressMode::Clamp
        int addressV_ = 1;
    };

    /**
     * @brief Windows-only, hardware-accelerated Direct2D 1.1 backend.
     *
     * The backend owns a BGRA-capable D3D11 device and a DXGI flip-model swap chain solely as
     * Direct2D's presentation surface.  All application 2D work is issued through
     * ID2D1DeviceContext; CNA does not route SpriteBatch through SDL_Renderer.  Direct2D is a 2D
     * API, so all 3D construction/draw/clear calls fail explicitly and SupportsCapability() is
     * false for CNA's 3D capabilities.
     */
    class Direct2DGraphicsBackend final : public IGraphicsBackend
    {
    public:
        Direct2DGraphicsBackend(SDL_Window* window, int virtualWidth, int virtualHeight,
                                CnaPresentationMode presentationMode, int swapInterval,
                                bool contextRecoveryEnabled = true);
        ~Direct2DGraphicsBackend() override;

        void Clear(float r, float g, float b, float a) override;
        void Present() override;
        void GetViewportSize(int& width, int& height) override;
        void SetVirtualResolution(int width, int height) override;
        void SetPresentationMode(int mode) override;
        void SetSwapInterval(int interval) override;
        bool TransformWindowToLogical(float windowX, float windowY,
                                      float& logicalX, float& logicalY) const override;
        bool TransformLogicalToWindow(float logicalX, float logicalY,
                                      float& windowX, float& windowY) const override;
        void ReadBackbuffer(int x, int y, int w, int h, uint8_t* pixels) override;

        [[nodiscard]] SDL_Window* GetWindowInternal() const override { return window_; }
        [[nodiscard]] SDL_Renderer* GetRendererInternal() const override { return nullptr; }

        std::unique_ptr<ITextureBackend> CreateTexture(const ImageData& data) override;
        std::unique_ptr<ISpriteBatchBackend> CreateSpriteBatch() override;
        std::unique_ptr<IRenderTargetBackend> CreateRenderTarget2D(int width, int height,
                                                                    int depthFormat,
                                                                    bool preserveContents = false,
                                                                    bool mipMap = false,
                                                                    int multiSampleCount = 0) override;
        void SetRenderTarget2D(IRenderTargetBackend* renderTarget) override;
        void SetRenderTargets(const RenderTargetBindingDescriptor* renderTargets, int count) override;
        void SetContextRecoveryEnabled(bool enabled) override { contextRecoveryEnabled_ = enabled; }
        void DebugSimulateContextLoss() override;
        void DebugRestoreContext() override;
        void SetScissorRect(int x, int y, int width, int height) override;
        void SetViewport(int x, int y, int width, int height, float minDepth, float maxDepth) override;
        void ApplyDepthStencilState(bool depthEnable, bool depthWriteEnable, int depthFunc,
                                    bool stencilEnable, int stencilFunc,
                                    int stencilPass, int stencilFail, int stencilDepthFail,
                                    int stencilMask, int stencilWriteMask, int referenceStencil,
                                    bool twoSidedStencilMode,
                                    int ccwStencilFunc, int ccwStencilPass,
                                    int ccwStencilFail, int ccwStencilDepthFail) override;
        void ApplyRasterizerState(int cullMode, int fillMode, bool scissorTestEnable,
                                  float depthBias = 0.0f,
                                  float slopeScaleDepthBias = 0.0f) override;
        void ApplyBlendState(int colorSrcBlend, int alphaSrcBlend, int colorDstBlend,
                             int alphaDstBlend, int colorBlendFunc, int alphaBlendFunc,
                             const BlendWriteState& writeState) override;
        void SetBlendFactor(float r, float g, float b, float a) override;

        [[nodiscard]] bool SupportsDepthStencil() const override { return false; }
        [[nodiscard]] bool SupportsCapability(CNA::GraphicsCapability capability) const override;
        [[nodiscard]] int ApplyMultiSampleCount(int requestedMultiSampleCount) override;

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
        std::unique_ptr<IOcclusionQueryBackend> CreateOcclusionQuery() override;
        void DrawColoredPrimitives(const IVertexBufferBackend& vb, const Matrix& world,
                                   const Matrix& view, const Matrix& projection,
                                   PrimitiveType primitive, int primitiveCount) override;
        void DrawIndexedColoredPrimitives(const IVertexBufferBackend& vb, const IIndexBufferBackend& ib,
                                          const Matrix& world, const Matrix& view,
                                          const Matrix& projection, PrimitiveType primitive,
                                          int primitiveCount) override;

        // Called by the concrete texture, target, and SpriteBatch handles above.
        [[nodiscard]] ID2D1Bitmap1* CreateBitmapFromRgba(const uint8_t* rgba, int width, int height,
                                                          bool ignoreAlpha = false) const;
        void BindRenderTarget(Direct2DRenderTargetBackend* renderTarget);
        void ReleaseRenderTarget(Direct2DRenderTargetBackend* renderTarget);
        void EnsureDrawing();
        void DrawSprite(const ITextureBackend& texture, const Rectangle& destinationRectangle,
                        const Rectangle& sourceRectangle, const Color& color, float rotation,
                        const Vector2& origin, SpriteEffects effects, const Matrix& batchTransform,
                        int textureFilter, int addressU, int addressV);

    private:
        friend class Direct2DTextureBackend;
        friend class Direct2DRenderTargetBackend;
        struct PresentationTransform
        {
            float scaleX = 1.0f;
            float scaleY = 1.0f;
            float offsetX = 0.0f;
            float offsetY = 0.0f;
            int logicalWidth = 0;
            int logicalHeight = 0;
        };

        void CreateDeviceResources();
        void ReleaseDeviceResourcesNoThrow(bool reportLiveObjects = false);
        void ReportLiveDeviceObjectsNoThrow();
        void RecreateDeviceResourcesForRecovery();
        void RegisterTexture(Direct2DTextureBackend* texture);
        void UnregisterTexture(Direct2DTextureBackend* texture);
        void RegisterRenderTarget(Direct2DRenderTargetBackend* renderTarget);
        void UnregisterRenderTarget(Direct2DRenderTargetBackend* renderTarget);
        [[nodiscard]] bool IsRegisteredRenderTarget(const Direct2DRenderTargetBackend* renderTarget) const;
        void EnsureResourceGeneration(std::uint64_t generation, const char* resourceKind) const;
        void CreateBackBufferTarget();
        void CreateLogicalTarget();
        void EnsureMainTargetSize();
        void EndDrawing(const char* operation);
        /// Copies a render target through a temporary Direct2D CPU-readable bitmap. This is a
        /// 2D-only path: CopyFromRenderTarget is the native route; CopyFromBitmap is a narrower
        /// fallback for a runtime that exposes the current target bitmap but not the former call.
        void GenerateRenderTargetMipLevels(Direct2DRenderTargetBackend& renderTarget);
        void ReadRenderTargetPixels(const Direct2DRenderTargetBackend& renderTarget, int level, int x, int y,
                                    int width, int height, uint8_t* pixels);
        void ReadCurrentTargetPixels(int x, int y, int width, int height,
                                     const D2D1_PIXEL_FORMAT& pixelFormat, uint8_t* pixels);
        [[nodiscard]] PresentationTransform GetPresentationTransform() const;
        [[nodiscard]] D2D1_MATRIX_3X2_F PresentationMatrix() const;
        [[nodiscard]] D2D1_MATRIX_3X2_F ViewportMatrix() const;
        void ApplyOutputClips();
        void ClearOutputClips();
        [[nodiscard]] bool SupportsColorMatrixEffect();
        [[nodiscard]] bool SupportsPremultiplyEffect();
        void MarkActiveRenderTargetMipLevelsDirty();
        [[nodiscard]] std::vector<uint8_t> MakeSpritePixels(const Direct2DTextureBackend& texture,
                                                             const Rectangle& sourceRectangle,
                                                             const Color& color, SpriteEffects effects,
                                                             int addressU, int addressV,
                                                             int mipLevel) const;
        [[nodiscard]] static D2D1_MATRIX_3X2_F ToD2DMatrix(const Matrix& matrix);
        [[nodiscard]] static D2D1_MATRIX_3X2_F Multiply(const D2D1_MATRIX_3X2_F& left,
                                                         const D2D1_MATRIX_3X2_F& right);

        SDL_Window* window_ = nullptr;
        int virtualWidth_ = 0;
        int virtualHeight_ = 0;
        CnaPresentationMode presentationMode_ = CnaPresentationMode::FixedHeightDynamicWidth;
        int swapInterval_ = 1;
        bool contextRecoveryEnabled_ = true;
        std::uint64_t deviceGeneration_ = 1;
        Direct2DRenderTargetBackend* activeRenderTarget_ = nullptr;
        bool drawing_ = false;
        bool viewportSet_ = false;
        int viewportX_ = 0;
        int viewportY_ = 0;
        int viewportWidth_ = 0;
        int viewportHeight_ = 0;
        bool scissorTestEnabled_ = false;
        bool scissorActive_ = false;
        bool viewportPushed_ = false;
        bool scissorPushed_ = false;
        Rectangle scissorRect_{};
        Direct2DBlendMode blendMode_ = Direct2DBlendMode::SourceOver;
        bool nonPremultipliedSource_ = false;
        // GraphicsDevice always forwards BlendState.BlendFactor immediately after ApplyBlendState,
        // even when the accepted Direct2D preset cannot consume a constant blend factor. Consume
        // that bookkeeping write once; later standalone non-white BlendFactor requests fail.
        bool pendingBlendStateFactorWrite_ = false;
        bool diagnosticsEnabled_ = false;
        bool debugLayerEnabled_ = false;
        bool usingWarp_ = false;
        std::uint64_t endDrawCount_ = 0;
        std::uint64_t transientResourceReleaseCount_ = 0;
        std::size_t transientResourceHighWater_ = 0;

        Microsoft::WRL::ComPtr<ID3D11Device> d3dDevice_;
        Microsoft::WRL::ComPtr<ID3D11DeviceContext> d3dContext_;
        Microsoft::WRL::ComPtr<IDXGISwapChain1> swapChain_;
        Microsoft::WRL::ComPtr<ID3D11Texture2D> backBufferTexture_;
        Microsoft::WRL::ComPtr<ID2D1Factory1> d2dFactory_;
        Microsoft::WRL::ComPtr<ID2D1DeviceContext> d2dContext_;
        Microsoft::WRL::ComPtr<ID2D1Bitmap1> backBufferTarget_;
        /// Logical 2D framebuffer. Presentation scales this bitmap into the physical swap chain,
        /// while GetBackBufferData reads it byte-for-byte before presentation filtering.
        Microsoft::WRL::ComPtr<ID2D1Bitmap1> logicalTarget_;
        /// Keeps transient SpriteBatch resources alive until Direct2D finishes the frame.  The
        /// bitmap collection is for ordinary textures; effects/image brushes let rendered targets
        /// stay GPU resident while they are tinted, flipped, or tiled.
        std::vector<Microsoft::WRL::ComPtr<ID2D1Bitmap1>> transientBitmaps_;
        std::vector<Microsoft::WRL::ComPtr<ID2D1Effect>> transientEffects_;
        std::vector<Microsoft::WRL::ComPtr<ID2D1Image>> transientImages_;
        std::vector<Microsoft::WRL::ComPtr<ID2D1ImageBrush>> transientImageBrushes_;
        // Wine and Proton's Direct2D expose image brushes but may omit the built-in effects.
        // Cache capability per device generation so ordinary textures can use the GPU effect path
        // on native Direct2D while retaining a correct CPU fallback on those runtimes.
        std::optional<bool> colorMatrixEffectSupported_;
        std::optional<bool> premultiplyEffectSupported_;
        // Non-owning; only resources constructed while recovery is enabled register here.
        std::vector<Direct2DTextureBackend*> recoverableTextures_;
        std::vector<Direct2DRenderTargetBackend*> recoverableRenderTargets_;
    };
}
