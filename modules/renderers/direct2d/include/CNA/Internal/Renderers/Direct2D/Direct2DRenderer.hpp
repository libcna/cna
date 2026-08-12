// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"

#include <d2d1_1.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <wrl/client.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

namespace CNA::Internal::Renderers::Direct2D
{
    class Direct2DRenderer;

    /**
     * @brief The exact Direct2D primitive/image composite selected from CNA BlendState.
     *
     * Primitive blend represents source-over and copy composition; DrawImage adds the exact
     * Porter-Duff modes below. Direct2D still has no general source/destination-factor blend-
     * equation interface. Keeping this mapping device-free makes the contract unit-testable.
     */
    enum class Direct2DBlendMode
    {
        /** @brief Premultiplied source-over composition. */
        SourceOver,
        /** @brief Bounded source-copy composition. */
        Copy,
        /** @brief Destination-over Porter-Duff composition. */
        DestinationOver,
        /** @brief Source-in Porter-Duff composition. */
        SourceIn,
        /** @brief Destination-in Porter-Duff composition. */
        DestinationIn,
        /** @brief Source-out Porter-Duff composition. */
        SourceOut,
        /** @brief Destination-out Porter-Duff composition. */
        DestinationOut,
        /** @brief Source-atop Porter-Duff composition. */
        SourceAtop,
        /** @brief Destination-atop Porter-Duff composition. */
        DestinationAtop,
        /** @brief Exclusive-or Porter-Duff composition. */
        Xor
    };

    /**
     * @brief Maps an exact CNA blend-factor tuple to a native Direct2D composition mode.
     *
     * @param colorSrcBlend Source color blend-factor ordinal.
     * @param alphaSrcBlend Source alpha blend-factor ordinal.
     * @param colorDstBlend Destination color blend-factor ordinal.
     * @param alphaDstBlend Destination alpha blend-factor ordinal.
     * @param colorBlendFunc Color blend-function ordinal.
     * @param alphaBlendFunc Alpha blend-function ordinal.
     * @return The exact Direct2D composition mode.
     * @throws std::runtime_error If Direct2D cannot express the requested tuple exactly.
     */
    Direct2DBlendMode BlendStateToDirect2DBlendMode(
        int colorSrcBlend, int alphaSrcBlend, int colorDstBlend, int alphaDstBlend,
        int colorBlendFunc, int alphaBlendFunc);

    /**
     * @brief Composites one premultiplied RGBA8 source pixel onto a destination pixel.
     *
     * The result comes from the Porter-Duff coefficient algebra alone --
     * `out = sourceFactor * source + destinationFactor * destination`, with the two factors chosen
     * by the operator's own definition -- and never consults CNA's Direct2D composite-mode mapping.
     * A test comparing a rendered pixel against this value is therefore an independent oracle
     * instead of the renderer checked against itself.
     *
     * @param blendMode Composition to evaluate.
     * @param source Premultiplied source pixel as R, G, B, A bytes.
     * @param destination Premultiplied destination pixel as R, G, B, A bytes.
     * @return The premultiplied composited pixel as R, G, B, A bytes.
     */
    [[nodiscard]] std::array<std::uint8_t, 4> CompositePorterDuffReference(
        Direct2DBlendMode blendMode, const std::array<std::uint8_t, 4>& source,
        const std::array<std::uint8_t, 4>& destination);

    /**
     * @brief Classifies a Direct2D, D3D11, or DXGI device-loss result.
     *
     * The recognized set is shared by EndDraw, presentation, and effect probes so every native
     * entry point makes the same recovery decision.
     *
     * @param hr Native result code to classify.
     * @return True for a result that requires complete device recreation.
     */
    [[nodiscard]] bool IsDeviceLossHResult(HRESULT hr);

    /**
     * @brief Returns Direct2D's truthful answer for one graphics capability.
     *
     * @param capability Capability to query.
     * @return True only for the native 2D anisotropic sampler mode.
     */
    [[nodiscard]] bool SupportsDirect2DCapability(CNA::GraphicsCapability capability) noexcept;

    /**
     * @brief Computes a bounded tightly packed RGBA8 byte count with checked arithmetic.
     *
     * @param width Image width in pixels.
     * @param height Image height in pixels.
     * @return The exact width * height * 4 byte count within D3D11's texture ceiling.
     */
    [[nodiscard]] std::size_t CheckedRgbaByteCount(int width, int height);

    /**
     * @brief Copies pitched RGBA8 rows into tightly packed native BGRA8 storage.
     *
     * @param rgba Source pixels.
     * @param stride Source row pitch in bytes.
     * @param width Image width in pixels.
     * @param height Image height in pixels.
     * @return Tightly packed BGRA8 bytes.
     */
    [[nodiscard]] std::vector<std::uint8_t> CopyRgbaToTightBgra(
        const std::uint8_t* rgba, int stride, int width, int height);

    /**
     * @brief Copies pitched native BGRA8 rows into tightly packed RGBA8 storage.
     *
     * @param bgra Source pixels.
     * @param stride Source row pitch in bytes.
     * @param width Image width in pixels.
     * @param height Image height in pixels.
     * @return Tightly packed RGBA8 bytes.
     */
    [[nodiscard]] std::vector<std::uint8_t> CopyBgraToTightRgba(
        const std::uint8_t* bgra, int stride, int width, int height);

    /**
     * @brief Selects the preferred authored mip level for a complete 2D transform.
     *
     * @param sourceWidth Source rectangle width.
     * @param sourceHeight Source rectangle height.
     * @param destinationWidth Destination rectangle width.
     * @param destinationHeight Destination rectangle height.
     * @param rotation Sprite rotation in radians.
     * @param batchTransform SpriteBatch transform.
     * @param presentationScaleX Horizontal presentation scale.
     * @param presentationScaleY Vertical presentation scale.
     * @param minifying Optional output set when the transform minifies either axis.
     * @return The nonnegative preferred level, or `INT_MAX` for a singular transform.
     */
    [[nodiscard]] int PreferredMipLevelForTransform(
        int sourceWidth, int sourceHeight, int destinationWidth, int destinationHeight,
        float rotation, const Matrix& batchTransform, float presentationScaleX,
        float presentationScaleY, bool* minifying = nullptr);

    /**
     * @brief Computes the continuous mip LOD for a complete 2D transform.
     *
     * @param sourceWidth Source rectangle width.
     * @param sourceHeight Source rectangle height.
     * @param destinationWidth Destination rectangle width.
     * @param destinationHeight Destination rectangle height.
     * @param rotation Sprite rotation in radians.
     * @param batchTransform SpriteBatch transform.
     * @param presentationScaleX Horizontal presentation scale.
     * @param presentationScaleY Vertical presentation scale.
     * @param minifying Optional output set when the transform minifies either axis.
     * @return Zero for magnification, a positive fractional LOD for minification, or positive
     * infinity for a singular transform.
     */
    [[nodiscard]] double FractionalMipLevelForTransform(
        int sourceWidth, int sourceHeight, int destinationWidth, int destinationHeight,
        float rotation, const Matrix& batchTransform, float presentationScaleX,
        float presentationScaleY, bool* minifying = nullptr);

    /**
     * @brief Maps a base-level source rectangle into one authored mip level.
     *
     * @param sourceRectangle Rectangle expressed in base-level texels.
     * @param baseWidth Base-level width.
     * @param baseHeight Base-level height.
     * @param mipWidth Selected mip-level width.
     * @param mipHeight Selected mip-level height.
     * @return The corresponding rectangle expressed in selected-level texels.
     */
    Rectangle MapSourceRectangleToMip(
        const Rectangle& sourceRectangle, int baseWidth, int baseHeight,
        int mipWidth, int mipHeight);

    /**
     * @brief Complete identity of one CPU-materialized SpriteBatch bitmap.
     *
     * Every input `MakeSpritePixels`/`MakeMipBlendedSpritePixels` reads is a member here, so two
     * draws with equal keys necessarily produce byte-identical pixels and the renderer may reuse
     * the bitmap it already uploaded for the first of them. A member missing from this struct
     * would be a correctness bug, not merely a cache miss, which is why the comparison is
     * defaulted over all of them rather than hand-written.
     */
    struct SpriteBitmapCacheKey
    {
        /** @brief Owning texture renderer identity. */
        const void* source = nullptr;
        /** @brief Upload counter of that texture, so any SetData invalidates its entries. */
        std::uint64_t contentVersion = 0;
        /** @brief Device generation the bitmap belongs to. */
        std::uint64_t deviceGeneration = 0;
        /** @brief Selected authored mip level. */
        int mipLevel = 0;
        /** @brief Upper mip level of an active mip-linear blend, or -1 when none is active. */
        int upperMipLevel = -1;
        /** @brief Fractional weight of the upper mip level. */
        float mipBlendFraction = 0.0f;
        /** @brief Source rectangle left edge in selected-level texels. */
        int sourceX = 0;
        /** @brief Source rectangle top edge in selected-level texels. */
        int sourceY = 0;
        /** @brief Source rectangle width in selected-level texels. */
        int sourceWidth = 0;
        /** @brief Source rectangle height in selected-level texels. */
        int sourceHeight = 0;
        /** @brief Packed RGBA modulation color. */
        std::uint32_t color = 0;
        /** @brief SpriteEffects flip bits. */
        int spriteEffects = 0;
        /** @brief Horizontal TextureAddressMode ordinal. */
        int addressU = 0;
        /** @brief Vertical TextureAddressMode ordinal. */
        int addressV = 0;
        /** @brief Whether the source channels are straight rather than premultiplied. */
        bool nonPremultipliedSource = false;

        /** @brief Compares every field that can change the produced pixels. */
        [[nodiscard]] bool operator==(const SpriteBitmapCacheKey&) const = default;
    };

    /**
     * @brief Complete identity of one decorated SpriteBatch brush and its effect graph.
     *
     * Two draws with equal keys need byte-identical Direct2D objects, so the renderer may issue
     * the second one with the brush it already built -- without writing a single property, which
     * is what makes the reuse safe even though Direct2D may read a brush's state late.
     *
     * The destination position is deliberately absent: it lives in the device context's transform,
     * not in the brush, so many sprites drawn from one atlas cell at different positions share a
     * single entry.
     */
    struct SpriteBrushCacheKey
    {
        /** @brief Identity of the image the brush samples. */
        const void* image = nullptr;
        /** @brief Device generation the objects belong to. */
        std::uint64_t deviceGeneration = 0;
        /** @brief Packed RGBA modulation color. */
        std::uint32_t color = 0;
        /** @brief Whether a ColorMatrix tint stage is part of the graph. */
        bool tinted = false;
        /** @brief Whether the tint stage uses straight-alpha matrix semantics. */
        bool straightAlphaTint = false;
        /** @brief Whether a Premultiply stage is part of the graph. */
        bool premultiplyStage = false;
        /** @brief Brush source rectangle left edge. */
        int sourceX = 0;
        /** @brief Brush source rectangle top edge. */
        int sourceY = 0;
        /** @brief Brush source rectangle width. */
        int sourceWidth = 0;
        /** @brief Brush source rectangle height. */
        int sourceHeight = 0;
        /** @brief Horizontal extend mode ordinal. */
        int extendU = 0;
        /** @brief Vertical extend mode ordinal. */
        int extendV = 0;
        /** @brief Brush interpolation mode ordinal. */
        int interpolationMode = 0;
        /** @brief SpriteEffects flip bits baked into the brush transform. */
        int spriteEffects = 0;
        /** @brief Horizontal sprite origin baked into the brush transform. */
        float originX = 0.0f;
        /** @brief Vertical sprite origin baked into the brush transform. */
        float originY = 0.0f;

        /** @brief Compares every field baked into the cached objects. */
        [[nodiscard]] bool operator==(const SpriteBrushCacheKey&) const = default;
    };

    /** A device-dependent Direct2D bitmap plus the source RGBA8 shadow required for CPU-side tint,
     *  flip, wrap/mirror, and non-premultiplied SpriteBatch variants. */
    class Direct2DTextureRenderer final : public ITextureRenderer
    {
    public:
        /**
         * @brief Creates a device-owned bitmap and recoverable RGBA shadow.
         *
         * @param owner Owning Direct2D graphics renderer.
         * @param data Initial level-zero image data.
         */
        Direct2DTextureRenderer(Direct2DRenderer& owner, const ImageData& data);
        /** @brief Releases the native bitmap and unregisters its recovery record. */
        ~Direct2DTextureRenderer() override;

        [[nodiscard]] int GetWidth() const override { return width_; }
        [[nodiscard]] int GetHeight() const override { return height_; }

        void UpdatePixels(const uint8_t* rgba, int stride) override;
        void UpdatePixelsLevel(int level, const uint8_t* rgba, int levelW, int levelH) override;
        [[nodiscard]] bool HasDefinedMipLevel(int level) const noexcept override;
        [[nodiscard]] bool GetData(int level, int x, int y, int w, int h,
                                   void* data, int dataLength) const override;

        /**
         * @brief Returns the validated level-zero native bitmap.
         *
         * @return Non-owning bitmap pointer valid for the current device generation.
         */
        [[nodiscard]] ID2D1Bitmap1* Bitmap() const;
        /**
         * @brief Returns one validated authored mip bitmap.
         *
         * @param level Mip level to retrieve.
         * @return Non-owning bitmap pointer valid for the current device generation.
         */
        [[nodiscard]] ID2D1Bitmap1* BitmapForLevel(int level) const;
        /**
         * @brief Selects the greatest initialized mip not exceeding a preferred level.
         *
         * An incomplete authored chain falls back toward level zero rather than sampling
         * uninitialized storage.
         *
         * @param preferredLevel Preferred mip level.
         * @return An initialized mip-level index.
         */
        [[nodiscard]] int SelectAvailableMipLevel(int preferredLevel) const;
        /**
         * @brief Returns the RGBA shadow for one initialized authored mip.
         *
         * @param level Mip level to retrieve.
         * @return RGBA bytes for the complete selected level.
         */
        [[nodiscard]] const std::vector<uint8_t>& RgbaPixelsForLevel(int level) const;
        /**
         * @brief Reports whether an authored mip has actually been uploaded.
         *
         * @param level Mip level to query.
         * @return True for initialized storage; level zero is initialized at construction.
         */
        [[nodiscard]] bool IsMipLevelInitialized(int level) const;
        /**
         * @brief Returns the highest valid mip-level index.
         *
         * @return Zero for a non-mipmapped texture, otherwise the last allocated level index.
         */
        [[nodiscard]] int MaxMipLevel() const { return static_cast<int>(mipBitmaps_.size()); }
        /**
         * @brief Returns the upload counter used to invalidate cached derived pixels.
         *
         * Every successful level upload and every device-recovery recreation advances it, so a
         * cache keyed on this value can never serve pixels derived from replaced content.
         *
         * @return Monotonically increasing content version.
         */
        [[nodiscard]] std::uint64_t ContentVersion() const noexcept { return contentVersion_; }

    private:
        friend class Direct2DRenderer;
        void RecreateBitmap();

        Direct2DRenderer* owner_ = nullptr;
        Microsoft::WRL::ComPtr<ID2D1Bitmap1> bitmap_;
        std::vector<uint8_t> rgbaPixels_;
        std::vector<Microsoft::WRL::ComPtr<ID2D1Bitmap1>> mipBitmaps_;
        std::vector<std::vector<uint8_t>> mipRgbaPixels_;
        int width_ = 0;
        int height_ = 0;
        std::uint64_t deviceGeneration_ = 0;
        std::uint64_t contentVersion_ = 1;
    };

    /** A GPU-resident Direct2D target bitmap.  It can be sampled after it is unbound. */
    class Direct2DRenderTargetRenderer final : public IRenderTargetRenderer
    {
    public:
        /**
         * @brief Creates a non-mipmapped GPU-resident Direct2D render target.
         *
         * @param owner Owning Direct2D graphics renderer.
         * @param width Width in physical pixels.
         * @param height Height in physical pixels.
         * @param mipMap Must be false; mipmapped targets are rejected.
         */
        Direct2DRenderTargetRenderer(Direct2DRenderer& owner, int width, int height, bool mipMap);
        /** @brief Releases the native target and unregisters its recovery record. */
        ~Direct2DRenderTargetRenderer() override;

        [[nodiscard]] int GetWidth() const override { return width_; }
        [[nodiscard]] int GetHeight() const override { return height_; }

        void UpdatePixels(const uint8_t* rgba, int stride) override;
        void UpdatePixelsLevel(int level, const uint8_t* rgba, int levelW, int levelH) override;
        [[nodiscard]] bool HasDefinedMipLevel(int level) const noexcept override;
        [[nodiscard]] bool GetData(int level, int x, int y, int w, int h,
                                   void* data, int dataLength) const override;

        void BindAsRenderTarget() override;
        void UnbindAsRenderTarget() override;
        [[nodiscard]] bool HasRealDepthBuffer(bool /*depthFormatWasRequested*/) const override
        {
            return false;
        }
        [[nodiscard]] bool HasRealStencilBuffer(bool /*stencilFormatWasRequested*/) const override
        {
            return false;
        }
        [[nodiscard]] int GetAppliedDepthStencilFormatEXT(int /*requestedDepthStencilFormat*/) const override
        {
            return 0;
        }

        /**
         * @brief Returns the validated level-zero native target bitmap.
         *
         * @return Non-owning bitmap pointer valid for the current device generation.
         */
        [[nodiscard]] ID2D1Bitmap1* Bitmap() const;
        /**
         * @brief Returns the validated target bitmap for level zero.
         *
         * @param level Must be zero because mipmapped render targets are unsupported.
         * @return Non-owning level-zero bitmap pointer.
         */
        [[nodiscard]] ID2D1Bitmap1* BitmapForLevel(int level) const;

    private:
        friend class Direct2DRenderer;
        void RecreateBitmap();

        Direct2DRenderer* owner_ = nullptr;
        Microsoft::WRL::ComPtr<ID2D1Bitmap1> bitmap_;
        int width_ = 0;
        int height_ = 0;
        std::uint64_t deviceGeneration_ = 0;
    };

    /** @brief Direct2D implementation of CNA's renderer-facing SpriteBatch command sink. */
    class Direct2DSpriteBatchRenderer final : public ISpriteBatchRenderer
    {
    public:
        /**
         * @brief Creates a SpriteBatch command sink owned by one Direct2D renderer.
         *
         * @param owner Owning Direct2D graphics renderer.
         */
        explicit Direct2DSpriteBatchRenderer(Direct2DRenderer& owner) : owner_(&owner) {}

        void Begin() override;
        void End() override;
        void SetTransformMatrix(const Matrix& matrix) override { transform_ = matrix; }
        void SetCustomEffect(Effect* effect) override;
        void SetSamplerFilter(int textureFilter) override;
        void SetSamplerAddressMode(int addressU, int addressV) override;
        void Draw(const ITextureRenderer& texture, float x, float y) override;
        void Draw(const ITextureRenderer& texture, const Rectangle& destinationRectangle,
                  const Rectangle& sourceRectangle, const Color& color) override;
        void Draw(const ITextureRenderer& texture, const Rectangle& destinationRectangle,
                  const Rectangle& sourceRectangle, const Color& color, float rotation,
                  const Vector2& origin, SpriteEffects effects, float layerDepth) override;

    private:
        Direct2DRenderer* owner_ = nullptr;
        Matrix transform_ = Matrix::getIdentityProperty();
        bool begun_ = false;
        int textureFilter_ = 0; // TextureFilter::Linear
        int addressU_ = 1; // TextureAddressMode::Clamp
        int addressV_ = 1;
    };

    /**
     * @brief Windows-only, hardware-accelerated Direct2D 1.1 renderer.
     *
     * The renderer owns a BGRA-capable D3D11 device and a DXGI flip-model swap chain solely as
     * Direct2D's presentation surface.  All application 2D work is issued through
     * ID2D1DeviceContext; CNA does not route SpriteBatch through SDL_Renderer.  Direct2D is a 2D
     * API, so all 3D construction/draw/clear calls fail explicitly and SupportsCapability() is
     * false for CNA's 3D capabilities.
     */
    class Direct2DRenderer final : public IGraphicsRenderer
    {
    public:
        /**
         * @brief Creates the Direct2D device, pixel-space logical target, and flip-model swap chain.
         *
         * @param window SDL3 window whose Win32 HWND receives presentation.
         * @param virtualWidth Requested logical width, or zero with `virtualHeight` for automatic sizing.
         * @param virtualHeight Requested logical height, or zero with `virtualWidth` for automatic sizing.
         * @param presentationMode Initial CNA presentation mode.
         * @param swapInterval Presentation interval from zero through two.
         * @param contextRecoveryEnabled Whether newly created 2D resources register for recovery.
         * @param deviceEventCallback Device lost/reset lifecycle callback.
         * @param backBufferFormat Must identify `SurfaceFormat::Color`.
         * @param depthStencilFormat Must identify `DepthFormat::None`.
         * @param multiSampleCount Must request a non-multisampled surface.
         */
        Direct2DRenderer(SDL_Window* window, int virtualWidth, int virtualHeight,
                                CnaPresentationMode presentationMode, int swapInterval,
                                bool contextRecoveryEnabled = true,
                                std::function<void(RendererDeviceEvent)> deviceEventCallback = nullptr,
                                int backBufferFormat = 0, int depthStencilFormat = 0,
                                int multiSampleCount = 0);
        /** @brief Releases Direct2D, DXGI, and D3D11 presentation resources without throwing. */
        ~Direct2DRenderer() override;

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

        std::unique_ptr<ITextureRenderer> CreateTexture(const ImageData& data) override;
        std::unique_ptr<ISpriteBatchRenderer> CreateSpriteBatch() override;
        std::unique_ptr<IRenderTargetRenderer> CreateRenderTarget2D(int width, int height,
                                                                    int depthFormat,
                                                                    bool preserveContents = false,
                                                                    bool mipMap = false,
                                                                    int multiSampleCount = 0) override;
        std::unique_ptr<IRenderTargetRenderer> CreateRenderTarget2DEXT(
            int width, int height, int depthFormat, bool preserveContents,
            bool mipMap, int multiSampleCount, int surfaceFormat) override;
        void SetRenderTarget2D(IRenderTargetRenderer* renderTarget) override;
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
        [[nodiscard]] bool SupportsDepthBuffer() const override { return false; }
        [[nodiscard]] bool SupportsStencilBuffer() const override { return false; }
        [[nodiscard]] bool SupportsCapability(CNA::GraphicsCapability capability) const override;
        void Ensure3DSupported(const char* operation) const override;
        [[nodiscard]] int ApplyMultiSampleCount(int requestedMultiSampleCount) override;
        [[nodiscard]] int GetAppliedBackBufferFormatEXT(int /*requestedFormat*/) const override { return 0; }
        [[nodiscard]] int GetAppliedDepthStencilFormatEXT(int /*requestedFormat*/) const override { return 0; }
        void UpdatePresentationFormatEXT(int backBufferFormat, int depthStencilFormat,
                                         bool isFullScreen) override;
        /**
         * @brief Returns the active Direct2D device's maximum bitmap dimension.
         *
         * @return `ID2D1DeviceContext::GetMaximumBitmapSize()` for the live device.
         */
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
        std::unique_ptr<ITexture3DRenderer> CreateTexture3D(
            int width, int height, int depth, bool mipMap, int surfaceFormat) override;
        std::unique_ptr<ITextureCubeRenderer> CreateTextureCube(
            int size, bool mipMap, int surfaceFormat) override;
        std::unique_ptr<IRenderTargetCubeRenderer> CreateRenderTargetCube(
            int size, int depthFormat, bool preserveContents = false,
            bool mipMap = false, int multiSampleCount = 0) override;
        std::unique_ptr<IOcclusionQueryRenderer> CreateOcclusionQuery() override;
        void DrawColoredPrimitives(const IVertexBufferRenderer& vb, const Matrix& world,
                                   const Matrix& view, const Matrix& projection,
                                   PrimitiveType primitive, int primitiveCount) override;
        void DrawIndexedColoredPrimitives(const IVertexBufferRenderer& vb, const IIndexBufferRenderer& ib,
                                          const Matrix& world, const Matrix& view,
                                          const Matrix& projection, PrimitiveType primitive,
                                          int primitiveCount) override;

        /**
         * @brief Creates a native premultiplied-alpha bitmap from tightly described RGBA pixels.
         *
         * Every CNA bitmap uses `DXGI_FORMAT_B8G8R8A8_UNORM` with `D2D1_ALPHA_MODE_PREMULTIPLIED`.
         * A straight-alpha source is converted to premultiplied before it reaches Direct2D, either
         * by the Premultiply effect or by the CPU fallback, so there is no alpha-ignoring bitmap
         * kind for application content.
         *
         * @param rgba Source RGBA bytes.
         * @param width Width in pixels.
         * @param height Height in pixels.
         * @return A caller-owned native bitmap pointer.
         */
        [[nodiscard]] ID2D1Bitmap1* CreateBitmapFromRgba(const uint8_t* rgba, int width,
                                                          int height) const;
        /**
         * @brief Replaces an existing bitmap's pixels in place instead of allocating a new one.
         *
         * The caller must already have committed every outstanding command that reads the bitmap,
         * because this writes through the existing COM object rather than swapping in a new one.
         *
         * @param bitmap Bitmap to overwrite.
         * @param rgba Tightly packed replacement RGBA bytes.
         * @param width Replacement width in pixels.
         * @param height Replacement height in pixels.
         * @return True on success; false when this runtime cannot upload in place and the caller
         * must recreate the bitmap instead.
         */
        [[nodiscard]] bool TryUploadBitmapRgba(ID2D1Bitmap1& bitmap, const uint8_t* rgba,
                                               int width, int height) const;
        /**
         * @brief Binds a validated Direct2D render target, or the logical backbuffer for null.
         *
         * @param renderTarget Target to bind, or null to return to the logical backbuffer.
         */
        void BindRenderTarget(Direct2DRenderTargetRenderer* renderTarget);
        /**
         * @brief Unbinds a target if it is currently active.
         *
         * @param renderTarget Target whose native lifetime is ending.
         */
        void ReleaseRenderTarget(Direct2DRenderTargetRenderer* renderTarget);
        /** @brief Begins a Direct2D command batch on the current validated application target. */
        void EnsureDrawing();
        /**
         * @brief Draws one validated 2D sprite through Direct2D 1.1.
         *
         * @param texture Device-owned ordinary texture or render target source.
         * @param destinationRectangle Destination rectangle in SpriteBatch coordinates.
         * @param sourceRectangle Source rectangle in base-level texels.
         * @param color Independent RGBA modulation color.
         * @param rotation Rotation in radians.
         * @param origin Source-space rotation and scale origin.
         * @param effects Supported horizontal and vertical flip flags.
         * @param batchTransform SpriteBatch transform matrix.
         * @param textureFilter TextureFilter ordinal.
         * @param addressU Horizontal TextureAddressMode ordinal.
         * @param addressV Vertical TextureAddressMode ordinal.
         */
        void DrawSprite(const ITextureRenderer& texture, const Rectangle& destinationRectangle,
                        const Rectangle& sourceRectangle, const Color& color, float rotation,
                        const Vector2& origin, SpriteEffects effects, const Matrix& batchTransform,
                        int textureFilter, int addressU, int addressV);

    private:
        friend class Direct2DTextureRenderer;
        friend class Direct2DRenderTargetRenderer;
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
        /// D2D-111: gives the transient collections their steady-state capacity once, so an
        /// ordinary frame reuses it (std::vector::clear keeps capacity) instead of growing.
        void ReserveTransientCapacity();
        /// D2D-112: total transient COM objects currently held until the next EndDraw.
        [[nodiscard]] std::size_t TransientResourceCount() const;
        /// D2D-112: commits the current chunk and starts a new one when an extremely long
        /// SpriteBatch would otherwise accumulate transients without bound. Deliberately does not
        /// re-run EnsureMainTargetSize, so the target cannot change inside one batch.
        void FlushTransientChunkIfOverBudget();
        void ReleaseDeviceResourcesNoThrow(bool reportLiveObjects = false);
        void ReportLiveDeviceObjectsNoThrow();
        void RecreateDeviceResourcesForRecovery();
        void RegisterTexture(Direct2DTextureRenderer* texture);
        void UnregisterTexture(Direct2DTextureRenderer* texture);
        void RegisterRenderTarget(Direct2DRenderTargetRenderer* renderTarget);
        void UnregisterRenderTarget(Direct2DRenderTargetRenderer* renderTarget);
        [[nodiscard]] bool IsRegisteredRenderTarget(const Direct2DRenderTargetRenderer* renderTarget) const;
        void EnsureResourceGeneration(std::uint64_t generation, const char* resourceKind) const;
        void CreateBackBufferTarget();
        [[nodiscard]] HWND GetWindowHandle() const;
        void CreateLogicalTarget();
        /// D2D-50/D2D-52: creates a logical-target-sized bitmap without touching logicalTarget_ or
        /// the device context's current target, so a resize can create-then-swap instead of
        /// destroy-then-create -- a failed CreateBitmap here leaves the previous logical target
        /// (and virtualWidth_/virtualHeight_/presentationMode_) intact and usable.
        [[nodiscard]] Microsoft::WRL::ComPtr<ID2D1Bitmap1> CreateLogicalTargetBitmap() const;
        void EnsureMainTargetSize();
        /// D2D-55: reports the window's current display scale (physical pixels per window unit) and
        /// records a change of it. The renderer needs no other reaction to a DPI or monitor change
        /// -- see the analysis in docs/direct2d-renderer.md -- but a scale change must be visible
        /// in the diagnostics of a run that claims presentation evidence.
        [[nodiscard]] float ObserveWindowDisplayScale();
        void EndDrawing(const char* operation);
        /// Copies a render target through a temporary Direct2D CPU-readable bitmap. This is a
        /// 2D-only path: CopyFromRenderTarget is the native route; CopyFromBitmap is a narrower
        /// fallback for a runtime that exposes the current target bitmap but not the former call.
        void ReadRenderTargetPixels(const Direct2DRenderTargetRenderer& renderTarget, int level, int x, int y,
                                    int width, int height, uint8_t* pixels);
        void ReadCurrentTargetPixels(int x, int y, int width, int height,
                                     const D2D1_PIXEL_FORMAT& pixelFormat, uint8_t* pixels);
        [[nodiscard]] PresentationTransform GetPresentationTransform() const;
        /// Computes the raw per-mode presentation transform, without D2D-53's minimum-size clamp.
        /// Only GetPresentationTransform() (which applies that clamp) should call this.
        [[nodiscard]] PresentationTransform ComputePresentationTransform() const;
        [[nodiscard]] D2D1_MATRIX_3X2_F PresentationMatrix() const;
        [[nodiscard]] D2D1_MATRIX_3X2_F ViewportMatrix() const;
        void ApplyOutputClips();
        void ClearOutputClips();
        [[nodiscard]] bool ProbeEffectSupport(REFCLSID effectId, const char* operation);
        [[nodiscard]] bool SupportsColorMatrixEffect();
        [[nodiscard]] bool SupportsPremultiplyEffect();
        void MakeSpritePixels(const Direct2DTextureRenderer& texture,
                              const Rectangle& sourceRectangle,
                              const Color& color, SpriteEffects effects,
                              int addressU, int addressV, int mipLevel,
                              std::vector<uint8_t>& pixels) const;
        /// D2D-74: same contract as MakeSpritePixels, but samples and linearly blends two mip
        /// levels (lowerSource/lowerLevel and upperSource/upperLevel) before tinting, for
        /// MipLinear-family TextureFilter values. Output is sized to lowerSource.
        void MakeMipBlendedSpritePixels(
            const Direct2DTextureRenderer& texture, const Rectangle& lowerSource,
            const Rectangle& upperSource, int lowerLevel, int upperLevel, float blendFraction,
            const Color& color, SpriteEffects effects, int addressU, int addressV,
            std::vector<uint8_t>& pixels) const;
        /**
         * @brief Returns the cached CPU-materialized bitmap for a key, or null on a miss.
         *
         * @param key Complete identity of the wanted pixels.
         * @return Non-owning bitmap pointer that stays alive at least until the next EndDraw.
         */
        [[nodiscard]] ID2D1Bitmap1* FindCachedSpriteBitmap(const SpriteBitmapCacheKey& key) const;
        /**
         * @brief Returns the cached decorated brush for a key, or null on a miss.
         *
         * @param key Complete identity of the wanted brush and effect graph.
         * @return Non-owning brush pointer that stays alive at least until the next EndDraw.
         */
        [[nodiscard]] ID2D1ImageBrush* FindCachedSpriteBrush(const SpriteBrushCacheKey& key) const;
        /// Stores a freshly built brush graph, evicting oldest-first within the entry budget.
        /// Evicted objects move into the transient collections for the same reason cached bitmaps
        /// do: a command recorded earlier in this chunk may still reference them.
        void StoreCachedSpriteBrush(const SpriteBrushCacheKey& key,
                                    const Microsoft::WRL::ComPtr<ID2D1Effect>& tintEffect,
                                    const Microsoft::WRL::ComPtr<ID2D1Image>& tintOutput,
                                    const Microsoft::WRL::ComPtr<ID2D1Effect>& premultiplyEffect,
                                    const Microsoft::WRL::ComPtr<ID2D1Image>& premultiplyOutput,
                                    const Microsoft::WRL::ComPtr<ID2D1ImageBrush>& brush);
        /// Stores a freshly uploaded CPU-materialized bitmap, evicting oldest-first within the
        /// documented entry/byte budget. Evicted bitmaps move into transientBitmaps_, so a command
        /// recorded earlier in this chunk keeps a live reference until EndDraw releases it.
        void StoreCachedSpriteBitmap(const SpriteBitmapCacheKey& key,
                                     const Microsoft::WRL::ComPtr<ID2D1Bitmap1>& bitmap,
                                     std::size_t byteCount);
        [[nodiscard]] static D2D1_MATRIX_3X2_F ToD2DMatrix(const Matrix& matrix);
        [[nodiscard]] static D2D1_MATRIX_3X2_F Multiply(const D2D1_MATRIX_3X2_F& left,
                                                         const D2D1_MATRIX_3X2_F& right);

        SDL_Window* window_ = nullptr;
        int virtualWidth_ = 0;
        int virtualHeight_ = 0;
        CnaPresentationMode presentationMode_ = CnaPresentationMode::FixedHeightDynamicWidth;
        int swapInterval_ = 1;
        bool contextRecoveryEnabled_ = true;
        std::function<void(RendererDeviceEvent)> deviceEventCallback_;
        std::uint64_t deviceGeneration_ = 1;
        Direct2DRenderTargetRenderer* activeRenderTarget_ = nullptr;
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
        bool initialDeviceDefaultDepthStatePending_ = true;
        Direct2DBlendMode blendMode_ = Direct2DBlendMode::SourceOver;
        bool nonPremultipliedSource_ = false;
        std::optional<Direct2DBlendMode> pendingBlendMode_;
        bool pendingNonPremultipliedSource_ = false;
        float observedDisplayScale_ = 0.0f;
        bool diagnosticsEnabled_ = false;
        bool debugLayerEnabled_ = false;
        bool usingWarp_ = false;
        std::uint64_t endDrawCount_ = 0;
        std::uint64_t transientResourceReleaseCount_ = 0;
        std::size_t transientResourceHighWater_ = 0;
        /// D2D-112: approximate bytes held by transient CPU-fallback bitmaps in the current chunk,
        /// and how often the chunk budget forced an intermediate commit.
        std::size_t transientByteEstimate_ = 0;
        std::uint64_t transientChunkFlushCount_ = 0;

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
        /// D2D-106/D2D-111: reused RGBA->BGRA staging for in-place bitmap uploads, so a repeated
        /// SetData of the same size performs no allocation after the first one.
        mutable std::vector<uint8_t> uploadScratch_;
        /// D2D-109: reused working buffer for CPU-materialized sprite pixels. It is consumed
        /// synchronously by the bitmap upload, so one buffer serves every sprite.
        mutable std::vector<uint8_t> spritePixelScratch_;
        struct SpriteBitmapCacheEntry
        {
            SpriteBitmapCacheKey key;
            Microsoft::WRL::ComPtr<ID2D1Bitmap1> bitmap;
            std::size_t byteCount = 0;
        };
        /// D2D-109: bitmaps for repeated identical decorated draws, bounded by entry count and
        /// bytes. Cleared with the rest of the device domain, so no entry outlives its device.
        std::vector<SpriteBitmapCacheEntry> spriteBitmapCache_;
        std::size_t spriteBitmapCacheBytes_ = 0;
        std::uint64_t spriteBitmapCacheHits_ = 0;
        std::uint64_t spriteBitmapCacheMisses_ = 0;
        struct SpriteBrushCacheEntry
        {
            SpriteBrushCacheKey key;
            Microsoft::WRL::ComPtr<ID2D1Effect> tintEffect;
            Microsoft::WRL::ComPtr<ID2D1Image> tintOutput;
            Microsoft::WRL::ComPtr<ID2D1Effect> premultiplyEffect;
            Microsoft::WRL::ComPtr<ID2D1Image> premultiplyOutput;
            Microsoft::WRL::ComPtr<ID2D1ImageBrush> brush;
        };
        /// D2D-107: decorated brush graphs for repeated identical configurations. A cached brush
        /// keeps its own reference to the image it samples, so its input can never be freed and
        /// its address reused while the entry lives.
        std::vector<SpriteBrushCacheEntry> spriteBrushCache_;
        std::uint64_t spriteBrushCacheHits_ = 0;
        std::uint64_t spriteBrushCacheMisses_ = 0;
        // Wine and Proton's Direct2D expose image brushes but may omit the built-in effects.
        // Cache capability per device generation so ordinary textures can use the GPU effect path
        // on native Direct2D while retaining a correct CPU fallback on those runtimes.
        std::optional<bool> colorMatrixEffectSupported_;
        std::optional<bool> premultiplyEffectSupported_;
        // Non-owning; only resources constructed while recovery is enabled register here.
        std::vector<Direct2DTextureRenderer*> recoverableTextures_;
        std::vector<Direct2DRenderTargetRenderer*> recoverableRenderTargets_;
    };
}
