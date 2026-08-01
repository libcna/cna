// SPDX-License-Identifier: MS-PL
#pragma once

#include "../Common/IGraphicsBackend.hpp"

#include <d2d1_1.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <wrl/client.h>

#include <cstdint>
#include <memory>
#include <vector>

namespace CNA::Internal::Backends::Direct2D
{
    class Direct2DGraphicsBackend;

    /**
     * @brief The Direct2D primitive blend selected from CNA's standard BlendState presets.
     *
     * Direct2D's primitive blend API can represent source-over, copy, and additive composition,
     * but it has no general source/destination-factor blend-equation interface.  Keeping this
     * mapping independent of a live device makes the supported contract testable without a window.
     */
    enum class Direct2DBlendMode
    {
        SourceOver,
        Copy,
        Add
    };

    Direct2DBlendMode BlendStateToDirect2DBlendMode(int colorSrcBlend, int alphaSrcBlend,
                                                    int colorDstBlend, int alphaDstBlend,
                                                    int colorBlendFunc, int alphaBlendFunc);

    /** A device-dependent Direct2D bitmap plus the source RGBA8 shadow required for CPU-side tint,
     *  flip, wrap/mirror, and non-premultiplied SpriteBatch variants. */
    class Direct2DTextureBackend final : public ITextureBackend
    {
    public:
        Direct2DTextureBackend(Direct2DGraphicsBackend& owner, const ImageData& data);

        [[nodiscard]] int GetWidth() const override { return width_; }
        [[nodiscard]] int GetHeight() const override { return height_; }
        [[nodiscard]] SDL_Texture* GetNativeTexture() const override { return nullptr; }
        void UpdatePixels(const uint8_t* rgba, int stride) override;
        void UpdatePixelsLevel(int level, const uint8_t* rgba, int levelW, int levelH) override;

        [[nodiscard]] ID2D1Bitmap1* Bitmap() const { return bitmap_.Get(); }
        [[nodiscard]] const std::vector<uint8_t>& RgbaPixels() const { return rgbaPixels_; }

    private:
        void RecreateBitmap();

        Direct2DGraphicsBackend* owner_ = nullptr;
        Microsoft::WRL::ComPtr<ID2D1Bitmap1> bitmap_;
        std::vector<uint8_t> rgbaPixels_;
        int width_ = 0;
        int height_ = 0;
    };

    /** A GPU-resident Direct2D target bitmap.  It can be sampled after it is unbound. */
    class Direct2DRenderTargetBackend final : public IRenderTargetBackend
    {
    public:
        Direct2DRenderTargetBackend(Direct2DGraphicsBackend& owner, int width, int height);
        ~Direct2DRenderTargetBackend() override;

        [[nodiscard]] int GetWidth() const override { return width_; }
        [[nodiscard]] int GetHeight() const override { return height_; }
        [[nodiscard]] SDL_Texture* GetNativeTexture() const override { return nullptr; }
        void UpdatePixels(const uint8_t* rgba, int stride) override;
        [[nodiscard]] bool GetData(int level, int x, int y, int w, int h,
                                   void* data, int dataLength) const override;

        void BindAsRenderTarget() override;
        void UnbindAsRenderTarget() override;
        [[nodiscard]] bool HasRealDepthBuffer(bool /*depthFormatWasRequested*/) const override
        {
            return false;
        }

        [[nodiscard]] ID2D1Bitmap1* Bitmap() const { return bitmap_.Get(); }

    private:
        Direct2DGraphicsBackend* owner_ = nullptr;
        Microsoft::WRL::ComPtr<ID2D1Bitmap1> bitmap_;
        int width_ = 0;
        int height_ = 0;
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
        bool linearFilter_ = true;
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
                                CnaPresentationMode presentationMode, int swapInterval);
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
        void SetScissorRect(int x, int y, int width, int height) override;
        void ApplyBlendState(int colorSrcBlend, int alphaSrcBlend, int colorDstBlend,
                             int alphaDstBlend, int colorBlendFunc, int alphaBlendFunc,
                             const BlendWriteState& writeState) override;

        [[nodiscard]] bool SupportsDepthStencil() const override { return false; }
        [[nodiscard]] bool SupportsCapability(CNA::GraphicsCapability /*capability*/) const override
        {
            return false;
        }
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
                        bool linearFilter, int addressU, int addressV);

    private:
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
        void CreateBackBufferTarget();
        void EnsureMainTargetSize();
        void EndDrawing(const char* operation);
        [[nodiscard]] PresentationTransform GetPresentationTransform() const;
        [[nodiscard]] D2D1_MATRIX_3X2_F PresentationMatrix() const;
        void ApplyScissorClip();
        void ClearScissorClip();
        [[nodiscard]] std::vector<uint8_t> MakeSpritePixels(const Direct2DTextureBackend& texture,
                                                             const Rectangle& sourceRectangle,
                                                             const Color& color, SpriteEffects effects,
                                                             int addressU, int addressV) const;
        [[nodiscard]] static D2D1_MATRIX_3X2_F ToD2DMatrix(const Matrix& matrix);
        [[nodiscard]] static D2D1_MATRIX_3X2_F Multiply(const D2D1_MATRIX_3X2_F& left,
                                                         const D2D1_MATRIX_3X2_F& right);

        SDL_Window* window_ = nullptr;
        int virtualWidth_ = 0;
        int virtualHeight_ = 0;
        CnaPresentationMode presentationMode_ = CnaPresentationMode::FixedHeightDynamicWidth;
        int swapInterval_ = 1;
        Direct2DRenderTargetBackend* activeRenderTarget_ = nullptr;
        bool drawing_ = false;
        bool scissorActive_ = false;
        bool scissorPushed_ = false;
        Rectangle scissorRect_{};
        Direct2DBlendMode blendMode_ = Direct2DBlendMode::SourceOver;
        bool nonPremultipliedSource_ = false;

        Microsoft::WRL::ComPtr<ID3D11Device> d3dDevice_;
        Microsoft::WRL::ComPtr<ID3D11DeviceContext> d3dContext_;
        Microsoft::WRL::ComPtr<IDXGISwapChain1> swapChain_;
        Microsoft::WRL::ComPtr<ID3D11Texture2D> backBufferTexture_;
        Microsoft::WRL::ComPtr<ID2D1Factory1> d2dFactory_;
        Microsoft::WRL::ComPtr<ID2D1DeviceContext> d2dContext_;
        Microsoft::WRL::ComPtr<ID2D1Bitmap1> backBufferTarget_;
        /// Keeps CPU-generated SpriteBatch bitmaps alive until Direct2D finishes the frame.
        std::vector<Microsoft::WRL::ComPtr<ID2D1Bitmap1>> transientBitmaps_;
    };
}
