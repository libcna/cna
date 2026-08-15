// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "CNA/Internal/Renderers/Common/PlatformGlRendererState.hpp"
#include "CNA/Internal/Renderers/Common/PlatformRendererSurfaceState.hpp"
#include "CNA/CNAHelper.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "Common/interface/RefCntAutoPtr.hpp"
#include "Graphics/GraphicsEngine/interface/DeviceContext.h"
#include "Graphics/GraphicsEngine/interface/EngineFactory.h"
#include "Graphics/GraphicsEngine/interface/GraphicsTypes.h"
#include "Graphics/GraphicsEngine/interface/Query.h"
#include "Graphics/GraphicsEngine/interface/RenderDevice.h"
#include "Graphics/GraphicsEngine/interface/SwapChain.h"

#include "CNA/GraphicsCapability.hpp"
#include "CNA/Internal/Renderers/Diligent/DiligentDeviceSelection.hpp"
#include "CNA/Internal/Graphics/VertexDeclarationFidelity.hpp"

namespace CNA::Internal::Renderers::Diligent
{
    /// Short alias for the third-party Diligent Engine namespace. Without it, every unqualified
    /// `Diligent::X` inside this namespace would resolve to this namespace itself and fail to
    /// find the type.
    namespace Dg = ::Diligent;

    // DiligentDeviceType, GetDeviceTypeName(), ParseDeviceTypeOverride() and
    // GetDeviceTypePreferenceOrder() live in DiligentDeviceSelection.hpp (included above) -- they
    // need no DiligentCore type, so GraphicsDevice.cpp's own window-flag selection can include
    // just that header without needing this file's DiligentCore include path (DILIGENT-57).

    /**
     * @brief CNAEXT. Converts XNA's `RasterizerState.DepthBias` (a small float, "r" units) into the
     * raw `Int32` units `Dg::RasterizerStateDesc::DepthBias` stores, exactly as
     * `DiligentRenderer::ApplyRasterizerState()` uses it -- exposed as a free function
     * (`DILIGENT-3`'s own established reason for doing this: testable with no GPU present).
     *
     * @p depthBias is scaled by 1000 and rounded to the nearest integer, then clamped to the `Int32`
     * range rather than truncated -- `PipelineKey::depthBias` used to instead mask this into a
     * single signed byte, which silently wrapped sign at magnitudes just past 0.127/-0.128
     * (`DILIGENT-64`).
     *
     * @param depthBias XNA-style depth bias value.
     * @return The raw Diligent `DepthBias` unit, clamped to `[INT32_MIN, INT32_MAX]`.
     */
    CNAEXT [[nodiscard]] std::int32_t ComputeDiligentDepthBiasRawUnits(float depthBias);

    /**
     * @brief CNAEXT. Decides `SupportsCapability()`'s answer from already-queried device facts,
     * exposed as a free function so the decision logic itself is testable with no GPU present
     * (`DILIGENT-3`'s own established reason for doing this) -- `DiligentRenderer`'s own
     * `SupportsCapability()` is a thin wrapper that fetches @p features /
     * @p maxAnisotropy / @p multiSampleSupported from a live device and calls this.
     *
     * @param capability            Capability being queried.
     * @param features              `IRenderDevice::GetDeviceInfo().Features` of the live device.
     * @param maxAnisotropy         `IRenderDevice::GetAdapterInfo().Sampler.MaxAnisotropy`.
     * @param multiSampleSupported  Whether the device supports any sample count above 1 for the
     *                              formats CNA render targets/back buffers actually use.
     * @return True if @p capability is genuinely usable given these facts.
     */
    CNAEXT [[nodiscard]] bool EvaluateCapability(CNA::GraphicsCapability capability,
                                                const Dg::DeviceFeatures& features,
                                                Dg::Uint8 maxAnisotropy, bool multiSampleSupported);

    class DiligentRenderer;

    /**
     * @brief Anything this renderer can bind to a sampler slot.
     *
     * Both plain textures and render targets are sampleable, but they reach `ITextureRenderer`
     * through different inheritance paths, so the draw paths need one type to ask for a shader
     * resource view. Without it, `SpriteBatch::Draw(renderTarget, ...)` -- ordinary XNA code --
     * would have to be refused purely because of how the C++ hierarchy is shaped.
     */
    class DiligentSampledTexture
    {
    public:
        virtual ~DiligentSampledTexture() = default;
        /** @brief Returns the shader resource view used when this resource is sampled. */
        [[nodiscard]] virtual Dg::ITextureView* GetShaderResourceView() const = 0;
    };

    /**
     * @brief A texture living in Diligent GPU memory, created from an `ImageData`.
     */
    class DiligentTextureRenderer final : public ITextureRenderer, public DiligentSampledTexture
    {
    public:
        /**
         * @brief Creates the GPU texture and uploads level 0 (plus any further supplied levels).
         *
         * @param owner Renderer that owns the render device; must outlive this texture.
         * @param data  Source pixels, RGBA8, top row first.
         */
        DiligentTextureRenderer(DiligentRenderer& owner, const ImageData& data);

        /** @brief Releases the GPU texture. */
        ~DiligentTextureRenderer() override;

        /** @brief Returns the texture width in pixels. */
        [[nodiscard]] int GetWidth() const override { return width_; }
        /** @brief Returns the texture height in pixels. */
        [[nodiscard]] int GetHeight() const override { return height_; }

        /**
         * @brief Replaces the whole level-0 image.
         *
         * @param rgba   Source pixels, RGBA8.
         * @param stride Source row pitch in bytes.
         */
        void UpdatePixels(const std::uint8_t* rgba, int stride) override;

        /**
         * @brief Replaces one mip level in full.
         *
         * @param level  Mip level to write.
         * @param rgba   Source pixels, RGBA8, tightly packed.
         * @param levelW Width of that level in pixels.
         * @param levelH Height of that level in pixels.
         */
        void UpdatePixelsLevel(int level, const std::uint8_t* rgba, int levelW, int levelH) override;

        /**
         * @brief Reads a sub-rectangle of a mip level back to the CPU through a staging texture.
         *
         * @param level      Mip level to read.
         * @param x          Left edge of the region, in pixels.
         * @param y          Top edge of the region, in pixels.
         * @param w          Width of the region, in pixels.
         * @param h          Height of the region, in pixels.
         * @param data       Destination for tightly packed RGBA8 rows, top row first.
         * @param dataLength Size of @p data in bytes; at least w * h * 4.
         * @return True if the whole region was written; false if nothing was read back.
         */
        [[nodiscard]] bool GetData(int level, int x, int y, int w, int h,
                                   void* data, int dataLength) const override;

        /** @brief CNAEXT. Returns the shader resource view used when this texture is sampled. */
        CNAEXT [[nodiscard]] Dg::ITextureView* GetShaderResourceView() const override { return srv_; }

    private:
        DiligentRenderer& owner_;
        Dg::RefCntAutoPtr<Dg::ITexture> texture_;
        Dg::ITextureView* srv_ = nullptr;
        int width_ = 0;
        int height_ = 0;
        int mipLevels_ = 1;
    };

    /**
     * @brief A cube map living in Diligent GPU memory.
     *
     * The six faces are the array slices of one `RESOURCE_DIM_TEX_CUBE` texture, in XNA's own face
     * order (+X, -X, +Y, -Y, +Z, -Z), which is also Diligent's.
     */
    class DiligentTextureCubeRenderer final : public ITextureCubeRenderer,
                                             public DiligentSampledTexture
    {
    public:
        /**
         * @brief Creates an empty cube map; faces are filled by `SetData`.
         *
         * @param owner         Renderer that owns the render device; must outlive this texture.
         * @param size          Edge length of each face, in texels.
         * @param mipMap        Whether to allocate a full mip chain.
         * @param surfaceFormat Raw XNA `SurfaceFormat` ordinal. Accepted and not honoured: this
         *                      renderer stores every texture as RGBA8, matching what every other
         *                      CNA renderer does with this parameter.
         */
        DiligentTextureCubeRenderer(DiligentRenderer& owner, int size, bool mipMap,
                                   int surfaceFormat);

        /** @brief Releases the GPU texture. */
        ~DiligentTextureCubeRenderer() override;

        /**
         * @brief Uploads raw RGBA8 pixels into a sub-rectangle of one cube face.
         *
         * @param face       Cube face index (0=+X, 1=-X, 2=+Y, 3=-Y, 4=+Z, 5=-Z).
         * @param level      Mip level to write.
         * @param x          Left edge of the region, in texels.
         * @param y          Top edge of the region, in texels.
         * @param w          Width of the region, in texels.
         * @param h          Height of the region, in texels.
         * @param data       Source pixels, tightly packed RGBA8 rows, top row first.
         * @param dataLength Size of @p data in bytes; at least w * h * 4.
         * @return True if the whole region was stored; false if nothing was stored.
         */
        [[nodiscard]] bool SetData(int face, int level, int x, int y, int w, int h,
                                   const void* data, int dataLength) override;

        /**
         * @brief Reads raw RGBA8 pixels back from a sub-rectangle of one cube face.
         *
         * @param face       Cube face index (0=+X, 1=-X, 2=+Y, 3=-Y, 4=+Z, 5=-Z).
         * @param level      Mip level to read.
         * @param x          Left edge of the region, in texels.
         * @param y          Top edge of the region, in texels.
         * @param w          Width of the region, in texels.
         * @param h          Height of the region, in texels.
         * @param data       Destination for tightly packed RGBA8 rows, top row first.
         * @param dataLength Size of @p data in bytes; exactly w * h * 4.
         * @return True if the whole region was written; false if nothing was read back.
         */
        [[nodiscard]] bool GetData(int face, int level, int x, int y, int w, int h,
                                   void* data, int dataLength) const override;

        /** @brief CNAEXT. Returns the shader resource view used when this cube map is sampled. */
        CNAEXT [[nodiscard]] Dg::ITextureView* GetShaderResourceView() const override { return srv_; }

    private:
        DiligentRenderer& owner_;
        Dg::RefCntAutoPtr<Dg::ITexture> texture_;
        Dg::ITextureView* srv_ = nullptr;
        int size_ = 0;
        int mipLevels_ = 1;
    };

    /**
     * @brief A volume texture living in Diligent GPU memory.
     */
    class DiligentTexture3DRenderer final : public ITexture3DRenderer
    {
    public:
        /**
         * @brief Creates an empty volume texture; voxels are filled by `SetData`.
         *
         * @param owner         Renderer that owns the render device; must outlive this texture.
         * @param width         Width in voxels.
         * @param height        Height in voxels.
         * @param depth         Depth in voxels.
         * @param mipMap        Whether to allocate a full mip chain.
         * @param surfaceFormat Raw XNA `SurfaceFormat` ordinal; accepted and not honoured, see
         *                      `DiligentTextureCubeRenderer`'s constructor.
         */
        DiligentTexture3DRenderer(DiligentRenderer& owner, int width, int height, int depth,
                                 bool mipMap, int surfaceFormat);

        /** @brief Releases the GPU texture. */
        ~DiligentTexture3DRenderer() override;

        /**
         * @brief Uploads raw RGBA8 voxels into a sub-volume of one mip level.
         *
         * @param level      Mip level to write.
         * @param x          Left edge of the box, in voxels.
         * @param y          Top edge of the box, in voxels.
         * @param z          Front edge of the box, in voxels.
         * @param w          Width of the box, in voxels.
         * @param h          Height of the box, in voxels.
         * @param depth      Depth of the box, in voxels.
         * @param data       Source voxels, tightly packed RGBA8, slice by slice, front to back.
         * @param dataLength Size of @p data in bytes; at least w * h * depth * 4.
         * @return True if the whole box was stored; false if nothing was stored.
         */
        [[nodiscard]] bool SetData(int level, int x, int y, int z, int w, int h, int depth,
                                   const void* data, int dataLength) override;

        /**
         * @brief Reads raw RGBA8 voxels back from a sub-volume of one mip level.
         *
         * @param level      Mip level to read.
         * @param x          Left edge of the box, in voxels.
         * @param y          Top edge of the box, in voxels.
         * @param z          Front edge of the box, in voxels.
         * @param w          Width of the box, in voxels.
         * @param h          Height of the box, in voxels.
         * @param depth      Depth of the box, in voxels.
         * @param data       Destination for the tightly packed RGBA8 box.
         * @param dataLength Size of @p data in bytes; exactly w * h * depth * 4.
         * @return True if the whole box was written; false if nothing was read back.
         */
        [[nodiscard]] bool GetData(int level, int x, int y, int z, int w, int h, int depth,
                                   void* data, int dataLength) const override;

        /** @brief CNAEXT. Returns the shader resource view used when this volume is sampled. */
        CNAEXT [[nodiscard]] Dg::ITextureView* GetShaderResourceView() const { return srv_; }

    private:
        DiligentRenderer& owner_;
        Dg::RefCntAutoPtr<Dg::ITexture> texture_;
        Dg::ITextureView* srv_ = nullptr;
        int width_ = 0;
        int height_ = 0;
        int depth_ = 0;
        int mipLevels_ = 1;
    };

    /**
     * @brief An off-screen 2D render target: a colour texture that can be both drawn into and
     * sampled, plus an optional depth-stencil buffer.
     */
    class DiligentRenderTargetRenderer final : public IRenderTargetRenderer,
                                              public DiligentSampledTexture
    {
    public:
        /**
         * @brief Creates the colour texture and, when requested, its depth-stencil buffer.
         *
         * @param owner            Renderer that owns the render device; must outlive this target.
         * @param width            Target width in pixels.
         * @param height           Target height in pixels.
         * @param depthFormat      Raw XNA `DepthFormat` ordinal; `None` (0) allocates no
         *                         depth-stencil buffer at all, any other value allocates a
         *                         combined depth24-stencil8 one.
         * @param preserveContents Whether the target's previous colour must survive a bind cycle.
         *                         Accepted and always honoured: Diligent's immediate context binds
         *                         without a load operation, so contents persist either way, which
         *                         satisfies both `PreserveContents` and `DiscardContents`.
         * @param mipMap           Whether to allocate a mip chain, regenerated when the target is
         *                         unbound.
         * @param multiSampleCount Requested MSAA sample count; clamped to what the colour and
         *                         depth-stencil formats both support (`DILIGENT-25`), see
         *                         `DiligentRenderer::ClampSampleCount()`. A clamped result
         *                         above 1 allocates a real multisampled colour (and depth-stencil)
         *                         texture plus a single-sampled resolve texture that
         *                         `UnbindAsRenderTarget()`/`GetData()` resolve into before either
         *                         samples or reads this target.
         */
        DiligentRenderTargetRenderer(DiligentRenderer& owner, int width, int height,
                                    int depthFormat, bool preserveContents, bool mipMap,
                                    int multiSampleCount);

        /** @brief Releases the GPU textures. */
        ~DiligentRenderTargetRenderer() override;

        /** @brief Returns the target width in pixels. */
        [[nodiscard]] int GetWidth() const override { return width_; }
        /** @brief Returns the target height in pixels. */
        [[nodiscard]] int GetHeight() const override { return height_; }

        /**
         * @brief Replaces the whole level-0 image of the target's colour texture.
         *
         * @param rgba   Source pixels, RGBA8.
         * @param stride Source row pitch in bytes.
         */
        void UpdatePixels(const std::uint8_t* rgba, int stride) override;

        /**
         * @brief Reads rendered pixels back to the CPU.
         *
         * When this target is multisampled, resolves the multisampled colour texture into the
         * single-sampled resolve texture first, so a read always sees fully resolved samples.
         *
         * @param level      Mip level to read.
         * @param x          Left edge of the region, in pixels.
         * @param y          Top edge of the region, in pixels.
         * @param w          Width of the region, in pixels.
         * @param h          Height of the region, in pixels.
         * @param data       Destination for tightly packed RGBA8 rows, top row first.
         * @param dataLength Size of @p data in bytes; at least w * h * 4.
         * @return True if the whole region was written; false if nothing was read back.
         */
        [[nodiscard]] bool GetData(int level, int x, int y, int w, int h,
                                   void* data, int dataLength) const override;

        /** @brief Makes subsequent draws render into this target. */
        void BindAsRenderTarget() override;

        /**
         * @brief Restores the back buffer, resolves a multisampled target and regenerates this
         * target's mip chain if it has one.
         */
        void UnbindAsRenderTarget() override;

        /** @brief Returns the real applied MSAA sample count, or 0 when this target is single-sampled. */
        [[nodiscard]] int GetMultiSampleCount() const override
        {
            return appliedMultiSampleCount_ > 1 ? appliedMultiSampleCount_ : 0;
        }

        /** @brief CNAEXT. Returns the Diligent-native sample count (always >= 1, unlike `GetMultiSampleCount()`). */
        CNAEXT [[nodiscard]] int GetDiligentSampleCount() const { return appliedMultiSampleCount_; }

        /**
         * @brief Reports whether this target really has depth-stencil storage.
         *
         * @param depthFormatWasRequested Whether a non-`None` `DepthFormat` was requested.
         * @return True only when a depth-stencil buffer was actually allocated.
         */
        [[nodiscard]] bool HasRealDepthBuffer(bool depthFormatWasRequested) const override;

        /** @brief CNAEXT. Returns the view drawn into when this target is bound. */
        CNAEXT [[nodiscard]] Dg::ITextureView* GetRenderTargetView() const { return rtv_; }
        /** @brief CNAEXT. Returns the depth-stencil view, or nullptr when none was allocated. */
        CNAEXT [[nodiscard]] Dg::ITextureView* GetDepthStencilView() const { return dsv_; }
        /** @brief CNAEXT. Returns the shader resource view used when this target is sampled. */
        CNAEXT [[nodiscard]] Dg::ITextureView* GetShaderResourceView() const override { return srv_; }
        /** @brief CNAEXT. Returns the colour texture's pixel format. */
        CNAEXT [[nodiscard]] Dg::TEXTURE_FORMAT GetColorFormat() const;
        /** @brief CNAEXT. Returns the depth-stencil format, or `TEX_FORMAT_UNKNOWN` when there is none. */
        CNAEXT [[nodiscard]] Dg::TEXTURE_FORMAT GetDepthStencilFormat() const;

    private:
        DiligentRenderer& owner_;
        Dg::RefCntAutoPtr<Dg::ITexture> colorTexture_;
        Dg::RefCntAutoPtr<Dg::ITexture> depthTexture_;
        /// Single-sampled resolve target, allocated only when `appliedMultiSampleCount_ > 1`. `srv_`
        /// and mip regeneration both read from this texture, never from the multisampled
        /// `colorTexture_` directly -- Diligent's built-in shaders here sample a plain `Texture2D`,
        /// not a `Texture2DMS`.
        Dg::RefCntAutoPtr<Dg::ITexture> resolveTexture_;
        Dg::ITextureView* rtv_ = nullptr;
        Dg::ITextureView* dsv_ = nullptr;
        Dg::ITextureView* srv_ = nullptr;
        int width_ = 0;
        int height_ = 0;
        int mipLevels_ = 1;
        int appliedMultiSampleCount_ = 1;
        bool preserveContents_ = false;
    };

    /**
     * @brief A cube-map render target: one `RESOURCE_DIM_TEX_CUBE` colour texture with a
     * per-face render-target view, plus an optional shared depth-stencil buffer.
     *
     * Only one face can be the active draw target at a time, matching every other CNA renderer's
     * `IRenderTargetCubeRenderer` contract — the depth-stencil buffer is therefore a single shared
     * allocation rather than six, since only one face is ever being rendered into at once.
     */
    class DiligentRenderTargetCubeRenderer final : public IRenderTargetCubeRenderer,
                                                  public DiligentSampledTexture
    {
    public:
        /**
         * @brief Creates the six-face colour texture and, when requested, a shared depth buffer.
         *
         * @param owner            Renderer that owns the render device; must outlive this target.
         * @param size             Edge length of each face, in texels.
         * @param depthFormat      Raw XNA `DepthFormat` ordinal; `None` (0) allocates no
         *                         depth-stencil buffer at all.
         * @param preserveContents Accepted and always honoured, see
         *                         `DiligentRenderTargetRenderer`'s identical parameter.
         * @param mipMap           Whether to allocate a mip chain, regenerated when a face is
         *                         unbound.
         */
        DiligentRenderTargetCubeRenderer(DiligentRenderer& owner, int size, int depthFormat,
                                        bool preserveContents, bool mipMap);

        /** @brief Releases the GPU textures. */
        ~DiligentRenderTargetCubeRenderer() override;

        /** @brief Returns the edge length of each face, in texels. */
        [[nodiscard]] int GetSize() const override { return size_; }

        /**
         * @brief Makes subsequent draws render into face @p face.
         * @param face Cube face index (0=+X, 1=-X, 2=+Y, 3=-Y, 4=+Z, 5=-Z).
         */
        void BindAsRenderTargetFace(int face) override;

        /** @brief Restores the back buffer and regenerates the mip chain if this target has one. */
        void UnbindAsRenderTarget() override;

        /** @brief Returns 0: this renderer allocates no multisampled targets yet (`DILIGENT-25`). */
        [[nodiscard]] int GetMultiSampleCount() const override { return 0; }

        /**
         * @brief Reports whether this target really has depth-stencil storage.
         *
         * @param depthFormatWasRequested Whether a non-`None` `DepthFormat` was requested.
         * @return True only when a depth-stencil buffer was actually allocated.
         */
        [[nodiscard]] bool HasRealDepthBuffer(bool depthFormatWasRequested) const override;

        /**
         * @brief Reads rendered pixels back from one face.
         *
         * @param face       Cube face index (0=+X, 1=-X, 2=+Y, 3=-Y, 4=+Z, 5=-Z).
         * @param level      Mip level to read.
         * @param x          Left edge of the region, in pixels.
         * @param y          Top edge of the region, in pixels.
         * @param w          Width of the region, in pixels.
         * @param h          Height of the region, in pixels.
         * @param data       Destination for tightly packed RGBA8 rows, top row first.
         * @param dataLength Size of @p data in bytes; at least w * h * 4.
         * @return True if the whole region was written; false if nothing was read back.
         */
        [[nodiscard]] bool GetData(int face, int level, int x, int y, int w, int h,
                                   void* data, int dataLength) const override;

        /** @brief CNAEXT. Returns the render-target view for face @p face (0..5). */
        CNAEXT [[nodiscard]] Dg::ITextureView* GetFaceRenderTargetView(int face) const
        {
            return faceRtv_[face].RawPtr();
        }
        /** @brief CNAEXT. Returns the depth-stencil view, or nullptr when none was allocated. */
        CNAEXT [[nodiscard]] Dg::ITextureView* GetDepthStencilView() const { return dsv_; }
        /** @brief CNAEXT. Returns the shader resource view used when this cube map is sampled. */
        CNAEXT [[nodiscard]] Dg::ITextureView* GetShaderResourceView() const override { return srv_; }
        /** @brief CNAEXT. Returns the colour texture's pixel format. */
        CNAEXT [[nodiscard]] Dg::TEXTURE_FORMAT GetColorFormat() const;
        /** @brief CNAEXT. Returns the depth-stencil format, or `TEX_FORMAT_UNKNOWN` when there is none. */
        CNAEXT [[nodiscard]] Dg::TEXTURE_FORMAT GetDepthStencilFormat() const;

    private:
        DiligentRenderer& owner_;
        Dg::RefCntAutoPtr<Dg::ITexture> colorTexture_;
        Dg::RefCntAutoPtr<Dg::ITexture> depthTexture_;
        Dg::RefCntAutoPtr<Dg::ITextureView> faceRtv_[6];
        Dg::ITextureView* dsv_ = nullptr;
        Dg::ITextureView* srv_ = nullptr;
        int size_ = 0;
        int mipLevels_ = 1;
        bool preserveContents_ = false;
    };

    /**
     * @brief A Diligent vertex buffer, re-created whenever a larger upload arrives.
     */
    class DiligentVertexBufferRenderer final : public IVertexBufferRenderer
    {
    public:
        /**
         * @brief Creates the handle; GPU storage is allocated on the first upload.
         *
         * @param owner          Renderer that owns the render device; must outlive this buffer.
         * @param vertexCapacity Number of vertices the caller intends to store.
         */
        DiligentVertexBufferRenderer(DiligentRenderer& owner, int vertexCapacity);

        /** @brief Releases the GPU buffer. */
        ~DiligentVertexBufferRenderer() override;

        /**
         * @brief Uploads vertex data, growing the GPU buffer when needed.
         *
         * @param data          Packed vertex data.
         * @param vertexCount   Number of vertices.
         * @param strideInBytes Size of one vertex in bytes.
         */
        void SetData(const void* data, int vertexCount, std::size_t strideInBytes) override;

        /**
         * @brief Uploads vertex data with an explicit streaming hint (`SetDataOptions::Discard`
         * maps the buffer with `MAP_FLAG_DISCARD`, `NoOverwrite` with `MAP_FLAG_NO_OVERWRITE`;
         * `None` behaves like `Discard`, matching this renderer's D3D11 sibling).
         *
         * @param data          Packed vertex data.
         * @param vertexCount   Number of vertices.
         * @param strideInBytes Size of one vertex in bytes.
         * @param options       Streaming hint.
         */
        void SetDataWithOptions(const void* data, int vertexCount, std::size_t strideInBytes,
                                SetDataOptions options) override;

        /**
         * @brief Records the caller's full vertex declaration for input-layout selection.
         *
         * @param vertexDeclaration Declaration, including stride and elements in declaration order.
         */
        void SetVertexDeclaration(const VertexDeclaration& vertexDeclaration) override;

        /** @brief Returns the number of vertices currently stored. */
        [[nodiscard]] int GetVertexCount() const override { return vertexCount_; }

        /** @brief CNAEXT. Returns the size of one vertex in bytes, or 0 before the first upload. */
        CNAEXT [[nodiscard]] std::size_t GetStride() const { return stride_; }
        /** @brief CNAEXT. Returns the underlying Diligent buffer, or nullptr before the first upload. */
        CNAEXT [[nodiscard]] Dg::IBuffer* GetBuffer() const { return buffer_; }

        /**
         * @brief CNAEXT. REMED-GFX-DECL-GUARD: the declaration this buffer last had propagated.
         *
         * Empty until `SetVertexDeclaration` runs, which is exactly the case the draw-time guard
         * treats as "nothing was declared, so there is nothing to contradict".
         *
         * @return The remembered elements and stride.
         */
        CNAEXT [[nodiscard]] const CNA::Internal::Graphics::DeclaredVertexLayout&
        GetDeclarationEXT() const { return declaration_; }

    private:
        DiligentRenderer& owner_;
        Dg::RefCntAutoPtr<Dg::IBuffer> buffer_;
        int vertexCount_ = 0;
        int capacity_ = 0;
        std::size_t stride_ = 0;
        std::size_t allocatedBytes_ = 0;
        CNA::Internal::Graphics::DeclaredVertexLayout declaration_;
    };

    /**
     * @brief A Diligent index buffer holding either 16- or 32-bit indices.
     */
    class DiligentIndexBufferRenderer final : public IIndexBufferRenderer
    {
    public:
        /**
         * @brief Creates the handle; GPU storage is allocated on the first upload.
         *
         * @param owner         Renderer that owns the render device; must outlive this buffer.
         * @param indexCapacity Number of indices the caller intends to store.
         * @param thirtyTwoBit  True for 32-bit indices, false for 16-bit.
         */
        DiligentIndexBufferRenderer(DiligentRenderer& owner, int indexCapacity, bool thirtyTwoBit);

        /** @brief Releases the GPU buffer. */
        ~DiligentIndexBufferRenderer() override;

        /**
         * @brief Uploads 16-bit indices.
         *
         * @param data       Packed 16-bit index data.
         * @param indexCount Number of indices.
         */
        void SetData16(const void* data, int indexCount) override;

        /**
         * @brief Uploads 32-bit indices.
         *
         * @param data       Packed 32-bit index data.
         * @param indexCount Number of indices.
         */
        void SetData32(const void* data, int indexCount) override;

        /**
         * @brief Uploads 16-bit indices with an explicit streaming hint. See
         * `DiligentVertexBufferRenderer::SetDataWithOptions()` for the flag mapping.
         *
         * @param data       Packed 16-bit index data.
         * @param indexCount Number of indices.
         * @param options    Streaming hint.
         */
        void SetData16WithOptions(const void* data, int indexCount, SetDataOptions options) override;

        /**
         * @brief Uploads 32-bit indices with an explicit streaming hint. See
         * `DiligentVertexBufferRenderer::SetDataWithOptions()` for the flag mapping.
         *
         * @param data       Packed 32-bit index data.
         * @param indexCount Number of indices.
         * @param options    Streaming hint.
         */
        void SetData32WithOptions(const void* data, int indexCount, SetDataOptions options) override;

        /** @brief Returns the number of indices currently stored. */
        [[nodiscard]] int GetIndexCount() const override { return indexCount_; }
        /** @brief Returns true when this buffer holds 32-bit indices. */
        [[nodiscard]] bool IsThirtyTwoBit() const override { return thirtyTwoBit_; }

        /** @brief CNAEXT. Returns the underlying Diligent buffer, or nullptr before the first upload. */
        CNAEXT [[nodiscard]] Dg::IBuffer* GetBuffer() const { return buffer_; }

    private:
        void Upload(const void* data, int indexCount, std::size_t elementSize,
                   SetDataOptions options = SetDataOptions::None);

        DiligentRenderer& owner_;
        Dg::RefCntAutoPtr<Dg::IBuffer> buffer_;
        int indexCount_ = 0;
        int capacity_ = 0;
        bool thirtyTwoBit_ = false;
        std::size_t allocatedBytes_ = 0;
    };

    /**
     * @brief Batching sprite renderer built on one dynamic vertex buffer and a shared
     * quad index buffer.
     *
     * Sprites accumulated between `Begin()` and `End()` are flushed whenever the bound texture,
     * sampler state or vertex capacity changes, and once more at `End()`.
     */
    class DiligentSpriteBatchRenderer final : public ISpriteBatchRenderer
    {
    public:
        /**
         * @brief Creates a sprite batch bound to @p owner.
         *
         * @param owner Renderer that owns the render device; must outlive this sprite batch.
         */
        explicit DiligentSpriteBatchRenderer(DiligentRenderer& owner);

        /** @brief Releases the batch's GPU buffers. */
        ~DiligentSpriteBatchRenderer() override;

        /** @brief Starts a batch; resets the accumulated sprite list. */
        void Begin() override;
        /** @brief Flushes any remaining sprites and ends the batch. */
        void End() override;

        /**
         * @brief Sets the transform applied on top of the 2D orthographic projection.
         * @param m Transform matrix, in XNA row-vector convention.
         */
        void SetTransformMatrix(const Matrix& m) override;

        /**
         * @brief Sets the texture filter used by subsequent draws.
         * @param textureFilter Raw XNA `TextureFilter` ordinal.
         */
        void SetSamplerFilter(int textureFilter) override;

        /**
         * @brief Sets the texture addressing modes used by subsequent draws.
         * @param addressU Raw XNA `TextureAddressMode` ordinal for U.
         * @param addressV Raw XNA `TextureAddressMode` ordinal for V.
         */
        void SetSamplerAddressMode(int addressU, int addressV) override;

        /**
         * @brief Draws a whole texture at a position, unscaled and untinted.
         *
         * @param texture Texture to draw.
         * @param x       Left edge in logical pixels.
         * @param y       Top edge in logical pixels.
         */
        void Draw(const ITextureRenderer& texture, float x, float y) override;

        /**
         * @brief Draws a source rectangle of a texture into a destination rectangle, tinted.
         *
         * @param texture              Texture to draw.
         * @param destinationRectangle Destination in logical pixels.
         * @param sourceRectangle      Source region in texels.
         * @param color                Tint colour.
         */
        void Draw(const ITextureRenderer& texture,
                  const Rectangle& destinationRectangle,
                  const Rectangle& sourceRectangle,
                  const Color& color) override;

        /**
         * @brief Full sprite draw with rotation, origin, flipping and layer depth.
         *
         * @param texture              Texture to draw.
         * @param destinationRectangle Destination in logical pixels.
         * @param sourceRectangle      Source region in texels.
         * @param color                Tint colour.
         * @param rotation             Rotation in radians, clockwise around @p origin.
         * @param origin               Rotation/scaling origin, in source texels.
         * @param effects              Horizontal/vertical flip flags.
         * @param layerDepth           Layer depth in [0,1]; written to the depth output.
         */
        void Draw(const ITextureRenderer& texture,
                  const Rectangle& destinationRectangle,
                  const Rectangle& sourceRectangle,
                  const Color& color,
                  float rotation,
                  const Vector2& origin,
                  SpriteEffects effects,
                  float layerDepth) override;

    private:
        /** @brief One sprite vertex: position, texture coordinate, RGBA tint. */
        struct SpriteVertex
        {
            float x, y, z;
            float u, v;
            float r, g, b, a;
        };

        void PushQuad(int textureWidthPixels, int textureHeightPixels,
                      const Rectangle& destinationRectangle,
                      const Rectangle& sourceRectangle,
                      const Color& color,
                      float rotation,
                      const Vector2& origin,
                      SpriteEffects effects,
                      float layerDepth);
        void Flush();
        void EnsureCapacity(std::size_t spriteCount);

        DiligentRenderer& owner_;
        Dg::RefCntAutoPtr<Dg::IBuffer> vertexBuffer_;
        Dg::RefCntAutoPtr<Dg::IBuffer> indexBuffer_;
        std::size_t bufferedSprites_ = 0;
        std::vector<SpriteVertex> vertices_;
        const DiligentSampledTexture* currentTexture_ = nullptr;
        std::pair<int, int> currentTextureSize_{0, 0};
        Matrix transform_;
        bool hasTransform_ = false;
        int filter_ = 0;
        int addressU_ = 1;
        int addressV_ = 1;
        bool inBatch_ = false;
    };

    /**
     * @brief Hardware occlusion query backed by Diligent's `IQuery`.
     *
     * Unlike Vulkan's CNA renderer (which defers all draws to a later command-buffer recording pass
     * and therefore has to track which deferred draws fall between `Begin()`/`End()`), this renderer
     * issues draws immediately against a single `IDeviceContext`, so `Begin()`/`End()` can wrap the
     * real `BeginQuery`/`EndQuery` calls directly.
     *
     * `QUERY_TYPE_OCCLUSION` (an exact visible-sample count) needs the device feature
     * `occlusionQueryPrecise`, which not every Vulkan implementation exposes — Mesa's `lavapipe`
     * software rasterizer, used for this renderer's own GPU tests, is one that does not. When the
     * exact query type is unavailable this falls back to `QUERY_TYPE_BINARY_OCCLUSION` and reports
     * only 0 (nothing visible) or 1 (something visible), matching the CNA convention already
     * documented on `OcclusionQuery::getPixelCountProperty()` for GLES3's identical limitation.
     */
    class DiligentOcclusionQueryRenderer final : public IOcclusionQueryRenderer
    {
    public:
        /**
         * @brief Creates a hardware occlusion query owned by @p owner.
         * @param owner Renderer that owns the render device and context; must outlive this query.
         * @throws std::runtime_error If neither exact nor binary occlusion queries are supported.
         */
        explicit DiligentOcclusionQueryRenderer(DiligentRenderer* owner);

        /** @brief Marks the start of the query; subsequent draws are counted until `End()`. */
        void Begin() override;
        /** @brief Marks the end of the query. */
        void End() override;
        /** @brief Returns whether the GPU has finished the query and a pixel count is available. */
        [[nodiscard]] bool IsComplete() const override;
        /**
         * @brief Returns the last retrieved visible-sample count, or 0 before one is available.
         *
         * On a device without `occlusionQueryPrecise` this is 0 or 1 rather than an exact count
         * (see the class comment).
         */
        [[nodiscard]] int PixelCount() const override;

    private:
        DiligentRenderer* owner_ = nullptr;
        Dg::RefCntAutoPtr<Dg::IQuery> query_;
        bool binaryOnly_ = false;
        // Diligent's IQuery asserts (DEV_CHECK) if GetData() is called outside its own Ended
        // state -- before the first End(), or again after a GetData() call already auto-invalidated
        // it back to Inactive. This mirrors that state machine so GetData() is only ever reached
        // when it is actually legal to call.
        mutable bool ended_ = false;
        mutable int pixelCount_ = 0;
    };

    /**
     * @brief CNA graphics renderer implemented on Diligent Engine.
     *
     * Unlike CNA's other renderers this one does not target a single native API: DiligentCore is a
     * portable abstraction over Direct3D 11/12, Vulkan and OpenGL, and the concrete device type is
     * selected at runtime (see `GetDeviceTypePreferenceOrder()` and the `CNA_DILIGENT_DEVICE`
     * environment variable). All shaders are authored once in HLSL and cross-compiled by Diligent
     * for whichever device was selected.
     *
     * The implemented surface is the 2D/3D baseline described in `plan_diligent.md`: swap chain
     * setup, the clear/present family, `Texture2D`, vertex/index buffers, `SpriteBatch`, the
     * untextured/textured/lit 3D draw paths, back-buffer readback, occlusion queries, MSAA (back
     * buffer and `RenderTarget2D`), and the blend/depth-stencil/rasterizer/sampler state family.
     * Everything outside it — custom `ShaderEffect` programs, instancing, `RenderTargetCube` MSAA,
     * and the `Pbr` effect variant — reports itself as unsupported rather than silently rendering
     * something else.
     */
    class DiligentRenderer final : public IGraphicsRenderer
    {
        friend class DiligentTextureRenderer;
        friend class DiligentTextureCubeRenderer;
        friend class DiligentTexture3DRenderer;
        friend class DiligentRenderTargetRenderer;
        friend class DiligentRenderTargetCubeRenderer;
        friend class DiligentVertexBufferRenderer;
        friend class DiligentIndexBufferRenderer;
        friend class DiligentSpriteBatchRenderer;
        friend class DiligentOcclusionQueryRenderer;

    public:
        /**
         * @brief Creates the render device, immediate context and swap chain for @p args.surface.
         *
         * @param args Renderer creation arguments; `surface.windowId` must identify a live window.
         * @throws std::runtime_error If no Diligent device type could be created for this window.
         */
        explicit DiligentRenderer(const GraphicsRendererCreateArgs& args);

        /** @brief Releases every Diligent object owned by this renderer. */
        ~DiligentRenderer() override;

        /**
         * @brief Clears the colour buffer.
         * @param r,g,b,a Clear colour components in 0..1.
         */
        void Clear(float r, float g, float b, float a) override;

        /** @brief Presents the swap chain, honouring the current swap interval. */
        void Present() override;

        /**
         * @brief Returns the logical (game-coordinate) presentation size.
         * @param width  Receives the logical width in pixels.
         * @param height Receives the logical height in pixels.
         */
        void GetViewportSize(int& width, int& height) override;

        /** @brief Refreshes the platform surface snapshot and resizes the swap chain if needed. */
        void OnSurfaceChanged(const RendererSurfaceInfo& surface) override;

        /**
         * @brief Sets the logical resolution the game draws in.
         * @param width  Logical width in pixels.
         * @param height Logical height in pixels.
         */
        void SetVirtualResolution(int width, int height) override;

        /**
         * @brief Sets the presentation/scaling policy.
         * @param mode Raw `CnaPresentationMode` ordinal.
         * @throws std::out_of_range If @p mode names no presentation mode.
         */
        void SetPresentationMode(int mode) override;

        /**
         * @brief Sets the swap interval used by the next `Present()`.
         * @param interval 0 = immediate, 1 = VSync, 2 = half refresh rate.
         */
        void SetSwapInterval(int interval) override;

        /** @brief Returns the back buffer's real applied MSAA sample count, or 0 when single-sampled. */
        [[nodiscard]] int GetMultiSampleCount() const override;

        /**
         * @brief Reconfigures the back buffer's MSAA sample count in place.
         *
         * @param requestedMultiSampleCount Requested sample count; clamped to what the swap
         *                                  chain's colour and depth-stencil formats both support
         *                                  (`DILIGENT-25`).
         * @return The real applied sample count (0 = no MSAA), matching `GetMultiSampleCount()`.
         */
        int ApplyMultiSampleCount(int requestedMultiSampleCount) override;

        /**
         * @brief Converts physical window coordinates to logical game coordinates.
         *
         * @param windowX Physical X coordinate.
         * @param windowY Physical Y coordinate.
         * @param logX    Receives the logical X coordinate.
         * @param logY    Receives the logical Y coordinate.
         * @return True when the conversion was performed.
         */
        bool TransformWindowToLogical(float windowX, float windowY,
                                      float& logX, float& logY) const override;

        /**
         * @brief Converts logical game coordinates to physical window coordinates.
         *
         * @param logX    Logical X coordinate.
         * @param logY    Logical Y coordinate.
         * @param windowX Receives the physical X coordinate.
         * @param windowY Receives the physical Y coordinate.
         * @return True when the conversion was performed.
         */
        bool TransformLogicalToWindow(float logX, float logY,
                                      float& windowX, float& windowY) const override;

        /**
         * @brief Creates a GPU texture from CPU pixels.
         * @param data Source image, RGBA8.
         * @return The new texture renderer.
         */
        std::unique_ptr<ITextureRenderer> CreateTexture(const ImageData& data) override;

        /** @brief Creates a sprite batch bound to this renderer. */
        std::unique_ptr<ISpriteBatchRenderer> CreateSpriteBatch() override;

        /**
         * @brief Creates an empty cube map.
         *
         * @param size          Edge length of each face, in texels.
         * @param mipMap        Whether to allocate a full mip chain.
         * @param surfaceFormat Raw XNA `SurfaceFormat` ordinal; accepted and not honoured.
         * @return The new cube texture renderer.
         */
        std::unique_ptr<ITextureCubeRenderer> CreateTextureCube(int size, bool mipMap,
                                                               int surfaceFormat) override;

        /**
         * @brief Creates an empty volume texture.
         *
         * @param w             Width in voxels.
         * @param h             Height in voxels.
         * @param depth         Depth in voxels.
         * @param mipMap        Whether to allocate a full mip chain.
         * @param surfaceFormat Raw XNA `SurfaceFormat` ordinal; accepted and not honoured.
         * @return The new volume texture renderer.
         */
        std::unique_ptr<ITexture3DRenderer> CreateTexture3D(int w, int h, int depth, bool mipMap,
                                                           int surfaceFormat) override;

        /**
         * @brief Creates an off-screen 2D render target.
         *
         * @param w                Target width in pixels.
         * @param h                Target height in pixels.
         * @param depthFormat      Raw XNA `DepthFormat` ordinal.
         * @param preserveContents Whether previous colour must survive a bind cycle.
         * @param mipMap           Whether to allocate a mip chain.
         * @param multiSampleCount Requested MSAA sample count; clamped to what the colour and
         *                         depth-stencil formats both support, and
         *                         `IRenderTargetRenderer::GetMultiSampleCount()` reports that real
         *                         applied value rather than the raw request (`DILIGENT-25`).
         * @return The new render target renderer.
         */
        std::unique_ptr<IRenderTargetRenderer> CreateRenderTarget2D(int w, int h, int depthFormat,
                                                                   bool preserveContents,
                                                                   bool mipMap,
                                                                   int multiSampleCount) override;

        /**
         * @brief Makes subsequent draws render into @p rt, or into the back buffer when null.
         * @param rt Render target to bind, or nullptr for the back buffer.
         */
        void SetRenderTarget2D(IRenderTargetRenderer* rt) override;

        /**
         * @brief Creates a cube-map render target.
         *
         * @param size             Edge length of each face, in texels.
         * @param depthFormat      Raw XNA `DepthFormat` ordinal.
         * @param preserveContents Whether previous colour must survive a bind cycle.
         * @param mipMap           Whether to allocate a mip chain.
         * @param multiSampleCount Requested MSAA sample count; clamped to 1 here, see
         *                         `CreateRenderTarget2D`'s identical note.
         * @return The new cube render target renderer.
         */
        std::unique_ptr<IRenderTargetCubeRenderer> CreateRenderTargetCube(
            int size, int depthFormat, bool preserveContents, bool mipMap,
            int multiSampleCount) override;

        /**
         * @brief Makes subsequent draws render into one face of a cube render target.
         * @param rt   Cube render target to bind, or nullptr for the back buffer.
         * @param face Cube face index (0=+X, 1=-X, 2=+Y, 3=-Y, 4=+Z, 5=-Z).
         */
        void SetRenderTargetCubeFace(IRenderTargetCubeRenderer* rt, int face) override;

        /**
         * @brief Reads back a region of the rendered back buffer.
         *
         * @param x      Left edge in logical game coordinates.
         * @param y      Top edge in logical game coordinates.
         * @param w      Region width in pixels.
         * @param h      Region height in pixels.
         * @param pixels Destination for w * h * 4 bytes of RGBA8, top row first.
         */
        void ReadBackbuffer(int x, int y, int w, int h, std::uint8_t* pixels) override;

        /**
         * @brief Binds a render-target set.
         *
         * Only the back buffer is supported so far, so a non-empty set is refused rather than
         * silently redirected to the back buffer.
         *
         * @param renderTargets Normalized target descriptors, or nullptr for the back buffer.
         * @param count         Number of descriptors; 0 restores the back buffer.
         * @throws std::runtime_error If @p count is greater than 0.
         */
        void SetRenderTargets(const RenderTargetBindingDescriptor* renderTargets, int count) override;

        /**
         * @brief Reports whether a capability is genuinely available on this renderer.
         * @param capability Capability to query.
         * @return True when supported.
         */
        [[nodiscard]] bool SupportsCapability(CNA::GraphicsCapability capability) const override;

        /**
         * @brief Creates a hardware occlusion query.
         * @return A real `DiligentOcclusionQueryRenderer` backed by Diligent's `IQuery`.
         */
        std::unique_ptr<IOcclusionQueryRenderer> CreateOcclusionQuery() override;

        /** @brief Returns the device's real maximum 2D texture dimension. */
        [[nodiscard]] int GetMaxTextureDimension() const override { return maxTextureDimension_; }

        /**
         * @brief Inserts a named GPU debug label into the command stream via
         * `IDeviceContext::InsertDebugLabel()`. External debug tools (RenderDoc, PIX, Vulkan
         * validation layers with debug-utils enabled) may surface this; it has no rendering effect.
         *
         * @param marker Label text. A null or empty marker is a no-op.
         */
        void SetStringMarkerEXT(const char* marker) override;

        /**
         * @brief Applies an XNA `BlendState` to the pipeline state used by subsequent draws.
         *
         * @param colorSrcBlend  Raw XNA `Blend` ordinal for the colour source factor.
         * @param alphaSrcBlend  Raw XNA `Blend` ordinal for the alpha source factor.
         * @param colorDstBlend  Raw XNA `Blend` ordinal for the colour destination factor.
         * @param alphaDstBlend  Raw XNA `Blend` ordinal for the alpha destination factor.
         * @param colorBlendFunc Raw XNA `BlendFunction` ordinal for colour.
         * @param alphaBlendFunc Raw XNA `BlendFunction` ordinal for alpha.
         * @param writeState     Per-slot colour write masks and the coverage sample mask. The
         *                       slot-0 write mask is honoured; slots 1..3 have no effect because
         *                       this renderer renders to a single target, and `MultiSampleMask` has
         *                       no effect because the back buffer is single-sampled.
         */
        void ApplyBlendState(int colorSrcBlend, int alphaSrcBlend,
                             int colorDstBlend, int alphaDstBlend,
                             int colorBlendFunc, int alphaBlendFunc,
                             const BlendWriteState& writeState) override;

        /**
         * @brief Applies an XNA `DepthStencilState` to the pipeline state used by subsequent draws.
         *
         * @param depthEnable         Whether depth testing is enabled.
         * @param depthWriteEnable    Whether depth writes are enabled.
         * @param depthFunc           Raw XNA `CompareFunction` ordinal for depth.
         * @param stencilEnable       Whether stencil testing is enabled.
         * @param stencilFunc         Raw XNA `CompareFunction` ordinal for the front face.
         * @param stencilPass         Raw XNA `StencilOperation` ordinal for the pass case.
         * @param stencilFail         Raw XNA `StencilOperation` ordinal for the stencil-fail case.
         * @param stencilDepthFail    Raw XNA `StencilOperation` ordinal for the depth-fail case.
         * @param stencilMask         Stencil read mask.
         * @param stencilWriteMask    Stencil write mask.
         * @param referenceStencil    Stencil reference value.
         * @param twoSidedStencilMode Whether back faces use their own stencil operations.
         * @param ccwStencilFunc      Raw XNA `CompareFunction` ordinal for the back face.
         * @param ccwStencilPass      Raw XNA `StencilOperation` ordinal for the back pass case.
         * @param ccwStencilFail      Raw XNA `StencilOperation` ordinal for the back fail case.
         * @param ccwStencilDepthFail Raw XNA `StencilOperation` ordinal for the back depth-fail case.
         */
        void ApplyDepthStencilState(bool depthEnable, bool depthWriteEnable,
                                    int depthFunc,
                                    bool stencilEnable, int stencilFunc,
                                    int stencilPass, int stencilFail, int stencilDepthFail,
                                    int stencilMask, int stencilWriteMask, int referenceStencil,
                                    bool twoSidedStencilMode,
                                    int ccwStencilFunc, int ccwStencilPass,
                                    int ccwStencilFail, int ccwStencilDepthFail) override;

        /**
         * @brief Applies an XNA `RasterizerState` to the pipeline state used by subsequent draws.
         *
         * @param cullMode            Raw XNA `CullMode` ordinal.
         * @param fillMode            Raw XNA `FillMode` ordinal.
         * @param scissorTestEnable   Whether the scissor test is enabled.
         * @param depthBias           Constant depth bias.
         * @param slopeScaleDepthBias Slope-scaled depth bias.
         */
        void ApplyRasterizerState(int cullMode, int fillMode,
                                  bool scissorTestEnable,
                                  float depthBias, float slopeScaleDepthBias) override;

        /**
         * @brief Applies an XNA `SamplerState` to a texture slot.
         *
         * @param slot          Texture slot index.
         * @param filter        Raw XNA `TextureFilter` ordinal.
         * @param addressU      Raw XNA `TextureAddressMode` ordinal for U.
         * @param addressV      Raw XNA `TextureAddressMode` ordinal for V.
         * @param maxAnisotropy Maximum anisotropy in 1..16.
         */
        void ApplySamplerState(int slot, int filter, int addressU, int addressV,
                               int maxAnisotropy) override;

        /**
         * @brief Sets the constant blend colour used by the `BlendFactor` blend factors.
         * @param r,g,b,a Blend factor components in 0..1.
         */
        void SetBlendFactor(float r, float g, float b, float a) override;

        /**
         * @brief Sets the stencil reference value standalone from a full depth-stencil state.
         * @param value New reference value.
         */
        void SetReferenceStencil(int value) override;

        /**
         * @brief Sets the scissor clip rectangle, in logical game coordinates.
         * @param x,y,w,h Scissor rectangle.
         */
        void SetScissorRect(int x, int y, int w, int h) override;

        /**
         * @brief Sets the viewport rectangle and depth range, in logical game coordinates.
         * @param x,y,w,h            Viewport rectangle.
         * @param minDepth,maxDepth  Depth range.
         */
        void SetViewport(int x, int y, int w, int h, float minDepth, float maxDepth) override;

        /**
         * @brief Clears the colour and depth buffers.
         * @param r,g,b,a Clear colour components in 0..1.
         * @param depth   Depth value to clear with.
         */
        void ClearColorAndDepth(float r, float g, float b, float a, float depth) override;

        /**
         * @brief Clears the depth buffer.
         * @param depth Depth value to clear with.
         */
        void ClearDepth(float depth) override;

        /**
         * @brief Clears the stencil buffer.
         * @param stencil Stencil value to clear with.
         */
        void ClearStencil(int stencil) override;

        /**
         * @brief Clears the depth and stencil buffers.
         * @param depth   Depth value to clear with.
         * @param stencil Stencil value to clear with.
         */
        void ClearDepthAndStencil(float depth, int stencil) override;

        /**
         * @brief Clears the colour and stencil buffers.
         * @param r,g,b,a Clear colour components in 0..1.
         * @param stencil Stencil value to clear with.
         */
        void ClearColorAndStencil(float r, float g, float b, float a, int stencil) override;

        /**
         * @brief Clears the colour, depth and stencil buffers.
         * @param r,g,b,a Clear colour components in 0..1.
         * @param depth   Depth value to clear with.
         * @param stencil Stencil value to clear with.
         */
        void ClearColorDepthAndStencil(float r, float g, float b, float a,
                                       float depth, int stencil) override;

        /**
         * @brief Enables or disables depth testing for subsequent draws.
         * @param enabled Whether the depth test is enabled.
         */
        void SetDepthTestEnabled(bool enabled) override;

        /**
         * @brief Enables or disables alpha blending for subsequent draws.
         * @param enabled Whether blending is enabled.
         */
        void SetBlendEnabled(bool enabled) override;

        /**
         * @brief Enables or disables depth writes for subsequent draws.
         * @param enabled Whether depth writes are enabled.
         */
        void SetDepthWriteEnabled(bool enabled) override;

        /**
         * @brief Creates a vertex buffer.
         * @param vertex_capacity Number of vertices the caller intends to store.
         * @return The new vertex buffer renderer.
         */
        std::unique_ptr<IVertexBufferRenderer> CreateVertexBuffer(int vertex_capacity) override;

        /**
         * @brief Creates a 16-bit index buffer.
         * @param index_capacity Number of indices the caller intends to store.
         * @return The new index buffer renderer.
         */
        std::unique_ptr<IIndexBufferRenderer> CreateIndexBuffer16(int index_capacity) override;

        /**
         * @brief Creates a 32-bit index buffer.
         * @param index_capacity Number of indices the caller intends to store.
         * @return The new index buffer renderer.
         */
        std::unique_ptr<IIndexBufferRenderer> CreateIndexBuffer32(int index_capacity) override;

        /**
         * @brief Draws primitives using the built-in vertex-colour shader.
         *
         * @param vb             Vertex buffer to read from.
         * @param world          World matrix.
         * @param view           View matrix.
         * @param projection     Projection matrix.
         * @param primitive      Primitive topology.
         * @param primitiveCount Number of primitives.
         */
        void DrawColoredPrimitives(const IVertexBufferRenderer& vb,
                                   const Matrix& world, const Matrix& view, const Matrix& projection,
                                   PrimitiveType primitive, int primitiveCount) override;

        /**
         * @brief Indexed counterpart of `DrawColoredPrimitives`.
         *
         * @param vb             Vertex buffer to read from.
         * @param ib             Index buffer to read from.
         * @param world          World matrix.
         * @param view           View matrix.
         * @param projection     Projection matrix.
         * @param primitive      Primitive topology.
         * @param primitiveCount Number of primitives.
         */
        void DrawIndexedColoredPrimitives(const IVertexBufferRenderer& vb, const IIndexBufferRenderer& ib,
                                          const Matrix& world, const Matrix& view, const Matrix& projection,
                                          PrimitiveType primitive, int primitiveCount) override;

        /**
         * @brief Effect-aware draw; selects the shader variant from the vertex stride and @p params.
         *
         * @param vb             Vertex buffer to read from.
         * @param world          World matrix.
         * @param view           View matrix.
         * @param projection     Projection matrix.
         * @param primitive      Primitive topology.
         * @param primitiveCount Number of primitives.
         * @param params         Per-draw effect parameters.
         * @throws std::runtime_error If @p params request an effect feature this renderer does not
         *                            implement yet.
         */
        void DrawPrimitivesEx(const IVertexBufferRenderer& vb,
                              const Matrix& world, const Matrix& view, const Matrix& projection,
                              PrimitiveType primitive, int primitiveCount,
                              const GpuDrawParams& params) override;

        /**
         * @brief Indexed counterpart of `DrawPrimitivesEx`.
         *
         * @param vb             Vertex buffer to read from.
         * @param ib             Index buffer to read from.
         * @param world          World matrix.
         * @param view           View matrix.
         * @param projection     Projection matrix.
         * @param primitive      Primitive topology.
         * @param primitiveCount Number of primitives.
         * @param params         Per-draw effect parameters.
         * @throws std::runtime_error If @p params request an effect feature this renderer does not
         *                            implement yet.
         */
        void DrawIndexedPrimitivesEx(const IVertexBufferRenderer& vb, const IIndexBufferRenderer& ib,
                                     const Matrix& world, const Matrix& view, const Matrix& projection,
                                     PrimitiveType primitive, int primitiveCount,
                                     const GpuDrawParams& params) override;

        /**
         * @brief Hardware-instanced indexed draw: `params.instanceVb` supplies one 4x4 world matrix
         * per instance (four consecutive `float4` rows, stride 64), bound at vertex input slot 1
         * with a per-instance step rate, alongside @p vb's per-vertex data at slot 0. Only `Position`
         * is read from @p vb; the pixel stage outputs `params.diffuseColor` flat, with no texture or
         * lighting, matching every other CNA renderer's own minimal hardware-instancing baseline.
         *
         * @param vb             Per-vertex buffer; only its `Position` is consumed.
         * @param ib             Index buffer shared by every instance.
         * @param world          Unused: instancing supplies a world matrix per instance instead.
         * @param view           View matrix.
         * @param projection     Projection matrix.
         * @param primitive      Primitive topology.
         * @param primitiveCount Number of primitives per instance.
         * @param instanceCount  Number of instances to draw.
         * @param params         Per-draw parameters; `instanceVb` must be non-null.
         * @throws std::runtime_error If `params.instanceVb` is null or foreign to this renderer.
         */
        void DrawInstancedPrimitivesEx(const IVertexBufferRenderer& vb, const IIndexBufferRenderer& ib,
                                       const Matrix& world, const Matrix& view, const Matrix& projection,
                                       PrimitiveType primitive, int primitiveCount, int instanceCount,
                                       const GpuDrawParams& params) override;

        /** @brief CNAEXT. Returns the device type this renderer actually created. */
        CNAEXT [[nodiscard]] DiligentDeviceType GetDeviceType() const { return deviceType_; }

    private:
        /** @brief Built-in shader programs, selected by vertex stride and draw parameters. */
        enum class ShaderVariant
        {
            Sprite,             ///< 2D sprite quads (position, UV, tint)
            Colored3D,          ///< stride 16: position + packed colour
            Textured3D,         ///< stride 20: position + UV
            ColoredTextured3D,  ///< stride 24: position + packed colour + UV
            LitTextured3D,      ///< stride 32: position + normal + UV, directional lighting
            DualTexture3D,      ///< stride 20: position + UV, two modulated texture layers
            DualTextureColored3D, ///< stride 24: position + packed colour + UV, two layers
            EnvironmentMap3D,   ///< stride 32: lit surface plus a cube-map reflection
            Skinned3D,          ///< stride 52: lit surface skinned by a 72-bone palette
            Instanced3D,        ///< position-only vertex + a per-instance world matrix, flat colour
            LitTexturedVertexLit3D, ///< stride 32: LitTextured3D's PreferPerPixelLighting==false sibling
            SkinnedVertexLit3D,     ///< stride 52: Skinned3D's PreferPerPixelLighting==false sibling
            Pbr3D,                  ///< stride 48: PbrEffect's glTF metallic-roughness BRDF
            SkinnedPbr3D,           ///< stride 68: Pbr3D combined with Skinned3D's bone palette
        };

        /** @brief Everything that distinguishes one Diligent pipeline state object from another. */
        struct PipelineKey
        {
            ShaderVariant variant = ShaderVariant::Sprite;
            std::uint32_t topology = 0;
            std::uint32_t blend = 0;
            std::uint32_t blendFuncs = 0;
            std::uint32_t writeMask = 0;
            std::uint32_t depth = 0;
            std::uint32_t stencilFront = 0;
            std::uint32_t stencilBack = 0;
            std::uint32_t stencilMasks = 0;
            std::uint32_t raster = 0;
            /// Colour format of slot 0 in the low half, depth-stencil format in the high half. A
            /// pipeline state is only valid for the formats it was created against, and a render
            /// target's formats are not necessarily the swap chain's -- the surface may hand out
            /// BGRA while every CNA render target is RGBA.
            std::uint32_t targetFormats = 0;
            /// Formats of MRT slots 1..3, one byte each (every Diligent texture format ordinal
            /// fits), plus the bound target count in the top byte.
            std::uint32_t extraTargetFormats = 0;
            /// The currently bound target's real (Diligent-native, always >= 1) MSAA sample count.
            /// A pipeline's `SmplDesc.Count` must match the sample count of whatever it draws into,
            /// or Vulkan rejects it as incompatible with the render pass -- the same class of bug
            /// `targetFormats` was added to fix (`DILIGENT-24`'s note on this struct).
            std::uint32_t sampleCount = 1;
            /// Whether `RasterizerDesc.ScissorEnable` is baked into this pipeline (1) or not (0).
            /// Diligent pipelines are immutable, so this has to be part of the cache key like every
            /// other `RasterizerDesc`/`BlendDesc`/`DepthStencilDesc` field -- omitting it let a
            /// pipeline created under one `ScissorTestEnable` value get reused, unchanged, after a
            /// later `ApplyRasterizerState()` call toggled it (`DILIGENT-58`).
            std::uint32_t scissorEnable = 0;
            /// Raw Diligent `DepthBias` units (`ComputeDiligentDepthBiasRawUnits()`). Previously
            /// packed into a single signed byte inside `raster`, which silently wrapped sign past
            /// +-0.127/-0.128; stored losslessly as its own `Int32` field instead (`DILIGENT-64`).
            std::int32_t depthBias = 0;
            /// `Dg::RasterizerStateDesc::SlopeScaledDepthBias` is itself a `Float32`, so this is
            /// stored exactly rather than quantized to 1/16 steps the way the old byte packing did.
            float slopeScaledDepthBias = 0.0f;
            /// `BlendState.MultiSampleMask` (`BlendWriteState::multiSampleMask`), carried straight
            /// into `Dg::GraphicsPipelineDesc::SampleMask`. Previously silently discarded entirely
            /// (`DILIGENT-60`).
            std::uint32_t sampleMask = 0xFFFFFFFFu;
            /// `ShaderVariant::Instanced3D` only: the real per-vertex (slot 0) stream stride, in
            /// bytes, of the `IVertexBufferRenderer` actually bound for this draw. Zero for every
            /// other variant. Used as the PSO's slot-0 `LayoutElement::Stride` instead of a
            /// hardcoded `16` -- a caller's real stride is part of what makes one cached pipeline
            /// safe to reuse for another draw; a different stride needs its own pipeline, not a
            /// silently-misfetching reuse of this one (`DILIGENT-65`).
            std::uint32_t instancedVertexStride = 0;
            /// `ShaderVariant::Instanced3D` only: the real per-instance (slot 1) stream stride, in
            /// bytes. Zero for every other variant. Used as the PSO's slot-1 `LayoutElement::Stride`
            /// instead of letting `LAYOUT_ELEMENT_AUTO_STRIDE` derive it purely from the four float4
            /// rows this shader declares (64) -- a padded or otherwise larger real instance stream
            /// would silently misfetch every instance past the first under that assumption
            /// (`DILIGENT-65`).
            std::uint32_t instancedInstanceStride = 0;
            /// `ShaderVariant::Instanced3D` only: the bound per-instance stream's own
            /// `VertexBufferBinding.InstanceFrequency`, carried into the slot-1 layout elements'
            /// `LayoutElement::InstanceDataStepRate`. Zero for every other variant. REMED-GFX-202:
            /// the frequency is a property of the binding, so two draws that differ only in it
            /// genuinely need two pipelines -- a step rate of 2 must advance the matrix once per
            /// two instances, and reusing a rate-1 pipeline would silently draw the wrong ones.
            std::uint32_t instancedStepRate = 0;

            bool operator==(const PipelineKey& other) const noexcept;
        };

        /** @brief Hashes a `PipelineKey` for the pipeline cache. */
        struct PipelineKeyHash
        {
            std::size_t operator()(const PipelineKey& key) const noexcept;
        };

        /** @brief A cached pipeline state and its shader resource binding. */
        struct CachedPipeline
        {
            Dg::RefCntAutoPtr<Dg::IPipelineState> pipeline;
            Dg::RefCntAutoPtr<Dg::IShaderResourceBinding> binding;
            Dg::IShaderResourceVariable* textureVariable = nullptr;
            Dg::IShaderResourceVariable* texture2Variable = nullptr;
            Dg::IShaderResourceVariable* envMapVariable = nullptr;
            Dg::IShaderResourceVariable* normalMapVariable = nullptr;
            Dg::IShaderResourceVariable* metallicRoughnessVariable = nullptr;
            Dg::IShaderResourceVariable* emissiveMapVariable = nullptr;
            Dg::IShaderResourceVariable* occlusionMapVariable = nullptr;
        };

        /** @brief Constant buffer contents shared by every built-in shader. */
        struct ShaderConstants
        {
            float worldViewProj[16];
            float world[16];
            float diffuseColor[4];
            /// Raw `AmbientLightColor`, folded into the per-light diffuse sum in the shader (so it
            /// gets multiplied by `DiffuseColor` exactly once, together with the directional
            /// lights) -- never combined with `emissive` on the CPU side (`DILIGENT-59`).
            float ambient[4];
            /// Raw `EmissiveColor` (BasicEffect/SkinnedEffect: `EmissiveColor*Alpha`;
            /// EnvironmentMapEffect: FNA's own pre-baked `EmissiveColor + AmbientLightColor*
            /// DiffuseColor`, see `EnvironmentMapEffect::FillGpuDrawParams()`) -- added to the lit
            /// result AFTER the ambient/light sum is multiplied by `DiffuseColor`, never multiplied
            /// by it again in the shader (`DILIGENT-59`).
            float emissive[4];
            float eyePositionSpecularPower[4];
            float specularColor[4];
            float lightDir[3][4];
            float lightDiffuse[3][4];
            float lightSpecular[3][4];
            float flags[4];
            float alphaTest[4];
            float fogVector[4];
            float fogColor[4];
            float envMapParams[4];
            float envMapSpecular[4];
        };

        /** @brief The physical viewport rectangle a logical (virtual) canvas maps onto. */
        struct LogicalViewport
        {
            float x = 0.0f;
            float y = 0.0f;
            float width = 0.0f;
            float height = 0.0f;
            float logicalWidth = 0.0f;
            float logicalHeight = 0.0f;
        };

        void CreateDeviceAndSwapChain(const GraphicsRendererCreateArgs& args);
        bool TryCreateDevice(DiligentDeviceType type, int multiSampleCount);
        void CreateConstantBuffer();
        void CreateFallbackTexture();
        void CreateFallbackFlatNormalTexture();
        void UploadBoneTransforms(const GpuDrawParams& params);
        void UploadPbrConstants(const GpuDrawParams& params);
        void SyncSwapChainSize();
        void EnsureRenderTargetsBound();
        void ApplyViewportAndScissor();
        [[nodiscard]] LogicalViewport ComputeLogicalViewport() const;
        [[nodiscard]] CachedPipeline& GetOrCreatePipeline(const PipelineKey& key);
        [[nodiscard]] PipelineKey MakePipelineKey(ShaderVariant variant, PrimitiveType primitive,
                                                  std::uint32_t instancedVertexStride = 0,
                                                  std::uint32_t instancedInstanceStride = 0,
                                                  std::uint32_t instancedStepRate = 0) const;
        [[nodiscard]] Dg::ISampler* GetOrCreateSampler(int filter, int addressU, int addressV,
                                                       int maxAnisotropy);
        void UploadConstants(const ShaderConstants& constants);
        [[nodiscard]] bool ReadTextureRegion(Dg::ITexture* texture, Dg::Uint32 mipLevel,
                                             Dg::Uint32 arraySlice, int x, int y, int z,
                                             int w, int h, int depth,
                                             void* data, int dataLength);
        void DrawInternal(const IVertexBufferRenderer& vb, const IIndexBufferRenderer* ib,
                          const Matrix& world, const Matrix& view, const Matrix& projection,
                          PrimitiveType primitive, int primitiveCount,
                          const GpuDrawParams* params);
        void DrawSpriteQuads(Dg::IBuffer* vertexBuffer, Dg::IBuffer* indexBuffer,
                             std::size_t spriteCount, const DiligentSampledTexture& texture,
                             const Matrix* transform, int filter, int addressU, int addressV);
        [[nodiscard]] Dg::ITextureView* GetBackBufferTextureView() const;
        [[nodiscard]] Dg::ITextureView* GetCurrentRenderTargetView() const;
        [[nodiscard]] DiligentRenderTargetRenderer* PrimaryRenderTarget() const;
        [[nodiscard]] Dg::ITextureView* GetCurrentDepthStencilView() const;
        [[nodiscard]] Dg::TEXTURE_FORMAT CurrentColorFormat() const;
        [[nodiscard]] Dg::TEXTURE_FORMAT CurrentDepthStencilFormat() const;
        /// CNAEXT. Picks the largest sample count <= @p requested that @p colorFormat (and
        /// @p depthFormat, when it is not `TEX_FORMAT_UNKNOWN`) reports support for via
        /// `GetTextureFormatInfoExt()`; 1 (no MSAA) if @p requested is <= 1 or the device supports
        /// nothing higher. Shared by the back buffer (`ApplyMultiSampleCount()`/the constructor,
        /// called with the swap chain's own granted formats) and every `DiligentRenderTargetRenderer`
        /// (called with its own `RGBA8_UNORM` colour format and, only when depth was requested, its
        /// own `D24_UNORM_S8_UINT` depth format -- never the swap chain's, which may differ and
        /// caused a real OpenGL `RenderTarget2D` MSAA-resolve failure before `DILIGENT-62`).
        [[nodiscard]] int ClampSampleCount(int requested, Dg::TEXTURE_FORMAT colorFormat,
                                           Dg::TEXTURE_FORMAT depthFormat = Dg::TEX_FORMAT_UNKNOWN) const;
        /// CNAEXT. The sample count a pipeline drawing into whatever is currently bound must be
        /// created with: 1 for a cube-target face (no cube MSAA yet), a render target's own applied
        /// count, or the back buffer's `sampleCount_`.
        [[nodiscard]] int CurrentSampleCount() const;
        /// CNAEXT. (Re)allocates `msaaBackBufferColor_`/`msaaBackBufferDepth_` to the current physical
        /// size when `sampleCount_ > 1`, or releases them when it is not. Called after the sample
        /// count changes (`ApplyMultiSampleCount()`) and after every swap chain resize
        /// (`SyncSwapChainSize()`), since both invalidate a texture sized to the old dimensions.
        void RecreateMsaaBackBufferTargets();
        /// CNAEXT. Resolves `msaaBackBufferColor_` into the swap chain's current back buffer texture
        /// when `sampleCount_ > 1`; a no-op otherwise. Shared by `Present()` (so the presented image
        /// is resolved) and `ReadBackbuffer()` (so a mid-frame readback is too).
        void ResolveMsaaBackBufferIfNeeded();
        /// CNAEXT. Resolves mip level 0, array slice 0 of a multisampled @p src into a single-sampled
        /// @p dst of the same dimensions and format. Shared by `ResolveMsaaBackBufferIfNeeded()` and
        /// every `DiligentRenderTargetRenderer` with a `resolveTexture_`.
        void ResolveTextureSubresource(Dg::ITexture* src, Dg::ITexture* dst);

        PlatformRendererSurfaceState surface_;
        CNA::Platform::IPlatformGlContext* platformGlContextService_ = nullptr;
        /// Only set when deviceType_ == OpenGL. Diligent's own GLContext (GLContextLinux.cpp)
        /// attaches to whatever GL context is already current on this thread via glXGetCurrentContext()
        /// -- it does not create one itself, unlike Vulkan/D3D where DiligentCore owns the whole
        /// device/context/swap-chain lifecycle. The platform context must be current before
        /// CreateDeviceAndSwapChainGL(), and outlives every Diligent object that can issue GL calls.
        std::unique_ptr<PlatformGlContextOwner> glContext_;
        DiligentDeviceType deviceType_ = DiligentDeviceType::Vulkan;
        Dg::RefCntAutoPtr<Dg::IRenderDevice> device_;
        Dg::RefCntAutoPtr<Dg::IDeviceContext> context_;
        Dg::RefCntAutoPtr<Dg::ISwapChain> swapChain_;
        Dg::RefCntAutoPtr<Dg::IEngineFactory> engineFactory_;
        /// Diligent-native back buffer MSAA sample count; always >= 1, 1 meaning no MSAA. The swap
        /// chain itself is never multisampled (no native graphics API allows that) -- when this is
        /// > 1, `msaaBackBufferColor_`/`msaaBackBufferDepth_` are the real draw target and every
        /// `Present()`/`ReadBackbuffer()` resolves them into the swap chain's actual back buffer
        /// first, exactly the same offscreen-then-resolve shape every other CNA renderer's back
        /// buffer MSAA uses.
        int sampleCount_ = 1;
        Dg::RefCntAutoPtr<Dg::ITexture> msaaBackBufferColor_;
        Dg::RefCntAutoPtr<Dg::ITexture> msaaBackBufferDepth_;
        Dg::ITextureView* msaaBackBufferColorView_ = nullptr;
        Dg::ITextureView* msaaBackBufferDepthView_ = nullptr;
        Dg::RefCntAutoPtr<Dg::IBuffer> constantBuffer_;
        /// SkinnedEffect's 72-matrix bone palette. Its own buffer: at 4.5 KB it would dominate the
        /// per-draw constant block every non-skinned draw uploads too.
        Dg::RefCntAutoPtr<Dg::IBuffer> boneBuffer_;
        /// 1x1 opaque white, bound whenever a textured vertex layout is drawn with texturing
        /// switched off. The shader ignores it (`g_Flags.x` is 0), but a pipeline that declares a
        /// texture variable still has to have one bound.
        Dg::RefCntAutoPtr<Dg::ITexture> fallbackTexture_;
        Dg::ITextureView* fallbackTextureView_ = nullptr;
        /// PbrEffect: 1x1 (128,128,255) -- decodes (via `rgb*2-1`) to tangent-space (0,0,1), the
        /// geometric normal unperturbed. Bound whenever `PbrEffect.NormalMap` is unset; the white
        /// `fallbackTextureView_` above is reused as-is for the other three optional PBR maps
        /// (metallic-roughness/emissive/occlusion), since 1.0 is already each one's own
        /// map-absent identity (factor*1.0, tint*1.0, unoccluded).
        Dg::RefCntAutoPtr<Dg::ITexture> flatNormalTexture_;
        Dg::ITextureView* flatNormalTextureView_ = nullptr;
        /// PbrEffect's ambient/metallic/emissive/roughness factors, separate from `constantBuffer_`
        /// because PBR's own formula (ambient scales albedo*occlusion, emissive is added
        /// standalone, both meaningfully distinct from every other stock effect's diffuse-lighting
        /// model) doesn't fit that shared block's `g_Ambient`/`g_Emissive` pair at all, not just a
        /// packing convenience.
        Dg::RefCntAutoPtr<Dg::IBuffer> pbrBuffer_;

        std::unordered_map<PipelineKey, CachedPipeline, PipelineKeyHash> pipelines_;
        std::unordered_map<std::uint64_t, Dg::RefCntAutoPtr<Dg::ISampler>> samplers_;

        int physicalWidth_ = 0;
        int physicalHeight_ = 0;
        int virtualWidth_ = 0;
        int virtualHeight_ = 0;
        int swapInterval_ = 1;
        int maxTextureDimension_ = 16384;
        CnaPresentationMode presentationMode_ = CnaPresentationMode::FixedHeightDynamicWidth;

        PipelineKey state_;
        int referenceStencil_ = 0;
        float blendFactor_[4] = {1.0f, 1.0f, 1.0f, 1.0f};

        /// DILIGENT-48: one independent entry per XNA `GraphicsDevice.SamplerStates` slot, matching
        /// `SamplerStateCollection::MaxSamplers`. Every named texture-binding site (`g_Texture` at
        /// slot 0, `g_Texture2`/`g_EnvMap` at slot 1, the PBR maps at slots 1-4) looks up its own
        /// slot here instead of sharing one set of values -- previously `ApplySamplerState()`
        /// discarded every call whose `slot` was not 0, so `SamplerStates[1]` and above were
        /// silently aliased to whatever `SamplerStates[0]` happened to be.
        struct SamplerSlotState
        {
            int filter = 0;
            int addressU = 1;
            int addressV = 1;
            int maxAnisotropy = 4;
        };
        SamplerSlotState samplerSlots_[16];

        bool scissorEnabled_ = false;
        int scissorRect_[4] = {0, 0, 0, 0};
        bool customViewport_ = false;
        int viewportRect_[4] = {0, 0, 0, 0};
        float viewportDepth_[2] = {0.0f, 1.0f};

        bool renderTargetsBound_ = false;
        /// Every bound 2D render target, slot-aligned. Empty means the back buffer or a bound
        /// cube face (mutually exclusive with this: see currentCubeTarget_).
        std::vector<DiligentRenderTargetRenderer*> currentRenderTargets_;
        /// The cube render target currently bound as the draw target, or nullptr. Mutually
        /// exclusive with a non-empty currentRenderTargets_ -- binding one clears the other.
        DiligentRenderTargetCubeRenderer* currentCubeTarget_ = nullptr;
        int currentCubeFace_ = -1;
    };
}
