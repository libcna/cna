// SPDX-License-Identifier: MS-PL
#pragma once

#include "../Common/IGraphicsBackend.hpp"
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
#include "Graphics/GraphicsEngine/interface/RenderDevice.h"
#include "Graphics/GraphicsEngine/interface/SwapChain.h"

namespace CNA::Internal::Backends::Diligent
{
    /// Short alias for the third-party Diligent Engine namespace. Without it, every unqualified
    /// `Diligent::X` inside this namespace would resolve to this namespace itself and fail to
    /// find the type.
    namespace Dg = ::Diligent;

    /**
     * @brief NOXNA. The Diligent Engine device type (native graphics API) a backend instance runs on.
     *
     * Diligent is itself an abstraction over several native APIs, so unlike every other CNA
     * backend the choice is made at runtime rather than by the `CNA_GRAPHICS_BACKEND` CMake
     * option. Which values are actually available in a given build is decided by the
     * `CNA_DILIGENT_HAS_*` compile definitions set by `cna_link_diligent()`.
     */
    enum class DiligentDeviceType
    {
        /** @brief Direct3D 12 (Windows only). */
        D3D12,
        /** @brief Vulkan. */
        Vulkan,
        /** @brief Direct3D 11 (Windows only). */
        D3D11,
        /** @brief OpenGL / OpenGL ES. */
        OpenGL,
    };

    /**
     * @brief NOXNA. Returns the stable, human-readable name of a device type ("Vulkan", "OpenGL", …).
     *
     * @param type Device type to name.
     * @return Name of the device type; never empty.
     */
    NOXNA [[nodiscard]] const char* GetDeviceTypeName(DiligentDeviceType type);

    /**
     * @brief NOXNA. Parses the `CNA_DILIGENT_DEVICE` environment variable value.
     *
     * Accepted values are case-insensitive: `d3d12`/`direct3d12`, `vulkan`/`vk`, `d3d11`/
     * `direct3d11`, `opengl`/`gl`, and `auto` (meaning "use the built-in preference order").
     *
     * @param value Raw environment variable text.
     * @return The requested device type, or an empty optional for `auto`.
     * @throws std::runtime_error If @p value names no known device type.
     */
    NOXNA [[nodiscard]] std::vector<DiligentDeviceType> ParseDeviceTypeOverride(const std::string& value);

    /**
     * @brief NOXNA. Returns the device types this build can attempt, in preference order.
     *
     * The order is D3D12 → Vulkan → D3D11 → OpenGL, filtered down to the engines DiligentCore
     * actually built for this platform. Device creation walks the list and uses the first entry
     * that succeeds, so an entry being present means "worth attempting", not "known to work".
     *
     * @return Preference-ordered device types; empty only if no engine was built at all.
     */
    NOXNA [[nodiscard]] std::vector<DiligentDeviceType> GetDeviceTypePreferenceOrder();

    class DiligentGraphicsBackend;

    /**
     * @brief Anything this backend can bind to a sampler slot.
     *
     * Both plain textures and render targets are sampleable, but they reach `ITextureBackend`
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
    class DiligentTextureBackend final : public ITextureBackend, public DiligentSampledTexture
    {
    public:
        /**
         * @brief Creates the GPU texture and uploads level 0 (plus any further supplied levels).
         *
         * @param owner Backend that owns the render device; must outlive this texture.
         * @param data  Source pixels, RGBA8, top row first.
         */
        DiligentTextureBackend(DiligentGraphicsBackend& owner, const ImageData& data);

        /** @brief Releases the GPU texture. */
        ~DiligentTextureBackend() override;

        /** @brief Returns the texture width in pixels. */
        [[nodiscard]] int GetWidth() const override { return width_; }
        /** @brief Returns the texture height in pixels. */
        [[nodiscard]] int GetHeight() const override { return height_; }

        /**
         * @brief Returns nothing: this backend has no SDL_Texture behind its textures.
         * @return Always nullptr.
         */
        [[nodiscard]] SDL_Texture* GetNativeTexture() const override { return nullptr; }

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

        /** @brief NOXNA. Returns the shader resource view used when this texture is sampled. */
        NOXNA [[nodiscard]] Dg::ITextureView* GetShaderResourceView() const override { return srv_; }

    private:
        DiligentGraphicsBackend& owner_;
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
    class DiligentTextureCubeBackend final : public ITextureCubeBackend,
                                             public DiligentSampledTexture
    {
    public:
        /**
         * @brief Creates an empty cube map; faces are filled by `SetData`.
         *
         * @param owner         Backend that owns the render device; must outlive this texture.
         * @param size          Edge length of each face, in texels.
         * @param mipMap        Whether to allocate a full mip chain.
         * @param surfaceFormat Raw XNA `SurfaceFormat` ordinal. Accepted and not honoured: this
         *                      backend stores every texture as RGBA8, matching what every other
         *                      CNA backend does with this parameter.
         */
        DiligentTextureCubeBackend(DiligentGraphicsBackend& owner, int size, bool mipMap,
                                   int surfaceFormat);

        /** @brief Releases the GPU texture. */
        ~DiligentTextureCubeBackend() override;

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

        /** @brief NOXNA. Returns the shader resource view used when this cube map is sampled. */
        NOXNA [[nodiscard]] Dg::ITextureView* GetShaderResourceView() const override { return srv_; }

    private:
        DiligentGraphicsBackend& owner_;
        Dg::RefCntAutoPtr<Dg::ITexture> texture_;
        Dg::ITextureView* srv_ = nullptr;
        int size_ = 0;
        int mipLevels_ = 1;
    };

    /**
     * @brief A volume texture living in Diligent GPU memory.
     */
    class DiligentTexture3DBackend final : public ITexture3DBackend
    {
    public:
        /**
         * @brief Creates an empty volume texture; voxels are filled by `SetData`.
         *
         * @param owner         Backend that owns the render device; must outlive this texture.
         * @param width         Width in voxels.
         * @param height        Height in voxels.
         * @param depth         Depth in voxels.
         * @param mipMap        Whether to allocate a full mip chain.
         * @param surfaceFormat Raw XNA `SurfaceFormat` ordinal; accepted and not honoured, see
         *                      `DiligentTextureCubeBackend`'s constructor.
         */
        DiligentTexture3DBackend(DiligentGraphicsBackend& owner, int width, int height, int depth,
                                 bool mipMap, int surfaceFormat);

        /** @brief Releases the GPU texture. */
        ~DiligentTexture3DBackend() override;

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

        /** @brief NOXNA. Returns the shader resource view used when this volume is sampled. */
        NOXNA [[nodiscard]] Dg::ITextureView* GetShaderResourceView() const { return srv_; }

    private:
        DiligentGraphicsBackend& owner_;
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
    class DiligentRenderTargetBackend final : public IRenderTargetBackend,
                                              public DiligentSampledTexture
    {
    public:
        /**
         * @brief Creates the colour texture and, when requested, its depth-stencil buffer.
         *
         * @param owner            Backend that owns the render device; must outlive this target.
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
         */
        DiligentRenderTargetBackend(DiligentGraphicsBackend& owner, int width, int height,
                                    int depthFormat, bool preserveContents, bool mipMap);

        /** @brief Releases the GPU textures. */
        ~DiligentRenderTargetBackend() override;

        /** @brief Returns the target width in pixels. */
        [[nodiscard]] int GetWidth() const override { return width_; }
        /** @brief Returns the target height in pixels. */
        [[nodiscard]] int GetHeight() const override { return height_; }

        /**
         * @brief Returns nothing: this backend has no SDL_Texture behind its render targets.
         * @return Always nullptr.
         */
        [[nodiscard]] SDL_Texture* GetNativeTexture() const override { return nullptr; }

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

        /** @brief Restores the back buffer and regenerates this target's mip chain if it has one. */
        void UnbindAsRenderTarget() override;

        /** @brief Returns 0: this backend allocates no multisampled targets yet (`DILIGENT-25`). */
        [[nodiscard]] int GetMultiSampleCount() const override { return 0; }

        /**
         * @brief Reports whether this target really has depth-stencil storage.
         *
         * @param depthFormatWasRequested Whether a non-`None` `DepthFormat` was requested.
         * @return True only when a depth-stencil buffer was actually allocated.
         */
        [[nodiscard]] bool HasRealDepthBuffer(bool depthFormatWasRequested) const override;

        /** @brief NOXNA. Returns the view drawn into when this target is bound. */
        NOXNA [[nodiscard]] Dg::ITextureView* GetRenderTargetView() const { return rtv_; }
        /** @brief NOXNA. Returns the depth-stencil view, or nullptr when none was allocated. */
        NOXNA [[nodiscard]] Dg::ITextureView* GetDepthStencilView() const { return dsv_; }
        /** @brief NOXNA. Returns the shader resource view used when this target is sampled. */
        NOXNA [[nodiscard]] Dg::ITextureView* GetShaderResourceView() const override { return srv_; }
        /** @brief NOXNA. Returns the colour texture's pixel format. */
        NOXNA [[nodiscard]] Dg::TEXTURE_FORMAT GetColorFormat() const;
        /** @brief NOXNA. Returns the depth-stencil format, or `TEX_FORMAT_UNKNOWN` when there is none. */
        NOXNA [[nodiscard]] Dg::TEXTURE_FORMAT GetDepthStencilFormat() const;

    private:
        DiligentGraphicsBackend& owner_;
        Dg::RefCntAutoPtr<Dg::ITexture> colorTexture_;
        Dg::RefCntAutoPtr<Dg::ITexture> depthTexture_;
        Dg::ITextureView* rtv_ = nullptr;
        Dg::ITextureView* dsv_ = nullptr;
        Dg::ITextureView* srv_ = nullptr;
        int width_ = 0;
        int height_ = 0;
        int mipLevels_ = 1;
        bool preserveContents_ = false;
    };

    /**
     * @brief A cube-map render target: one `RESOURCE_DIM_TEX_CUBE` colour texture with a
     * per-face render-target view, plus an optional shared depth-stencil buffer.
     *
     * Only one face can be the active draw target at a time, matching every other CNA backend's
     * `IRenderTargetCubeBackend` contract — the depth-stencil buffer is therefore a single shared
     * allocation rather than six, since only one face is ever being rendered into at once.
     */
    class DiligentRenderTargetCubeBackend final : public IRenderTargetCubeBackend,
                                                  public DiligentSampledTexture
    {
    public:
        /**
         * @brief Creates the six-face colour texture and, when requested, a shared depth buffer.
         *
         * @param owner            Backend that owns the render device; must outlive this target.
         * @param size             Edge length of each face, in texels.
         * @param depthFormat      Raw XNA `DepthFormat` ordinal; `None` (0) allocates no
         *                         depth-stencil buffer at all.
         * @param preserveContents Accepted and always honoured, see
         *                         `DiligentRenderTargetBackend`'s identical parameter.
         * @param mipMap           Whether to allocate a mip chain, regenerated when a face is
         *                         unbound.
         */
        DiligentRenderTargetCubeBackend(DiligentGraphicsBackend& owner, int size, int depthFormat,
                                        bool preserveContents, bool mipMap);

        /** @brief Releases the GPU textures. */
        ~DiligentRenderTargetCubeBackend() override;

        /** @brief Returns the edge length of each face, in texels. */
        [[nodiscard]] int GetSize() const override { return size_; }

        /**
         * @brief Makes subsequent draws render into face @p face.
         * @param face Cube face index (0=+X, 1=-X, 2=+Y, 3=-Y, 4=+Z, 5=-Z).
         */
        void BindAsRenderTargetFace(int face) override;

        /** @brief Restores the back buffer and regenerates the mip chain if this target has one. */
        void UnbindAsRenderTarget() override;

        /** @brief Returns 0: this backend allocates no multisampled targets yet (`DILIGENT-25`). */
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

        /** @brief NOXNA. Returns the render-target view for face @p face (0..5). */
        NOXNA [[nodiscard]] Dg::ITextureView* GetFaceRenderTargetView(int face) const
        {
            return faceRtv_[face].RawPtr();
        }
        /** @brief NOXNA. Returns the depth-stencil view, or nullptr when none was allocated. */
        NOXNA [[nodiscard]] Dg::ITextureView* GetDepthStencilView() const { return dsv_; }
        /** @brief NOXNA. Returns the shader resource view used when this cube map is sampled. */
        NOXNA [[nodiscard]] Dg::ITextureView* GetShaderResourceView() const override { return srv_; }
        /** @brief NOXNA. Returns the colour texture's pixel format. */
        NOXNA [[nodiscard]] Dg::TEXTURE_FORMAT GetColorFormat() const;
        /** @brief NOXNA. Returns the depth-stencil format, or `TEX_FORMAT_UNKNOWN` when there is none. */
        NOXNA [[nodiscard]] Dg::TEXTURE_FORMAT GetDepthStencilFormat() const;

    private:
        DiligentGraphicsBackend& owner_;
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
    class DiligentVertexBufferBackend final : public IVertexBufferBackend
    {
    public:
        /**
         * @brief Creates the handle; GPU storage is allocated on the first upload.
         *
         * @param owner          Backend that owns the render device; must outlive this buffer.
         * @param vertexCapacity Number of vertices the caller intends to store.
         */
        DiligentVertexBufferBackend(DiligentGraphicsBackend& owner, int vertexCapacity);

        /** @brief Releases the GPU buffer. */
        ~DiligentVertexBufferBackend() override;

        /**
         * @brief Uploads vertex data, growing the GPU buffer when needed.
         *
         * @param data          Packed vertex data.
         * @param vertexCount   Number of vertices.
         * @param strideInBytes Size of one vertex in bytes.
         */
        void SetData(const void* data, int vertexCount, std::size_t strideInBytes) override;

        /**
         * @brief Records the caller's full vertex declaration for input-layout selection.
         *
         * @param vertexDeclaration Declaration, including stride and elements in declaration order.
         */
        void SetVertexDeclaration(const VertexDeclaration& vertexDeclaration) override;

        /** @brief Returns the number of vertices currently stored. */
        [[nodiscard]] int GetVertexCount() const override { return vertexCount_; }

        /** @brief NOXNA. Returns the size of one vertex in bytes, or 0 before the first upload. */
        NOXNA [[nodiscard]] std::size_t GetStride() const { return stride_; }
        /** @brief NOXNA. Returns the underlying Diligent buffer, or nullptr before the first upload. */
        NOXNA [[nodiscard]] Dg::IBuffer* GetBuffer() const { return buffer_; }

    private:
        DiligentGraphicsBackend& owner_;
        Dg::RefCntAutoPtr<Dg::IBuffer> buffer_;
        int vertexCount_ = 0;
        int capacity_ = 0;
        std::size_t stride_ = 0;
        std::size_t allocatedBytes_ = 0;
    };

    /**
     * @brief A Diligent index buffer holding either 16- or 32-bit indices.
     */
    class DiligentIndexBufferBackend final : public IIndexBufferBackend
    {
    public:
        /**
         * @brief Creates the handle; GPU storage is allocated on the first upload.
         *
         * @param owner         Backend that owns the render device; must outlive this buffer.
         * @param indexCapacity Number of indices the caller intends to store.
         * @param thirtyTwoBit  True for 32-bit indices, false for 16-bit.
         */
        DiligentIndexBufferBackend(DiligentGraphicsBackend& owner, int indexCapacity, bool thirtyTwoBit);

        /** @brief Releases the GPU buffer. */
        ~DiligentIndexBufferBackend() override;

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

        /** @brief Returns the number of indices currently stored. */
        [[nodiscard]] int GetIndexCount() const override { return indexCount_; }
        /** @brief Returns true when this buffer holds 32-bit indices. */
        [[nodiscard]] bool IsThirtyTwoBit() const override { return thirtyTwoBit_; }

        /** @brief NOXNA. Returns the underlying Diligent buffer, or nullptr before the first upload. */
        NOXNA [[nodiscard]] Dg::IBuffer* GetBuffer() const { return buffer_; }

    private:
        void Upload(const void* data, int indexCount, std::size_t elementSize);

        DiligentGraphicsBackend& owner_;
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
    class DiligentSpriteBatchBackend final : public ISpriteBatchBackend
    {
    public:
        /**
         * @brief Creates a sprite batch bound to @p owner.
         *
         * @param owner Backend that owns the render device; must outlive this sprite batch.
         */
        explicit DiligentSpriteBatchBackend(DiligentGraphicsBackend& owner);

        /** @brief Releases the batch's GPU buffers. */
        ~DiligentSpriteBatchBackend() override;

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
        void Draw(const ITextureBackend& texture, float x, float y) override;

        /**
         * @brief Draws a source rectangle of a texture into a destination rectangle, tinted.
         *
         * @param texture              Texture to draw.
         * @param destinationRectangle Destination in logical pixels.
         * @param sourceRectangle      Source region in texels.
         * @param color                Tint colour.
         */
        void Draw(const ITextureBackend& texture,
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
        void Draw(const ITextureBackend& texture,
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

        DiligentGraphicsBackend& owner_;
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
     * @brief CNA graphics backend implemented on Diligent Engine.
     *
     * Unlike CNA's other backends this one does not target a single native API: DiligentCore is a
     * portable abstraction over Direct3D 11/12, Vulkan and OpenGL, and the concrete device type is
     * selected at runtime (see `GetDeviceTypePreferenceOrder()` and the `CNA_DILIGENT_DEVICE`
     * environment variable). All shaders are authored once in HLSL and cross-compiled by Diligent
     * for whichever device was selected.
     *
     * The implemented surface is the 2D/3D baseline described in `plan_diligent.md`: swap chain
     * setup, the clear/present family, `Texture2D`, vertex/index buffers, `SpriteBatch`, the
     * untextured/textured/lit 3D draw paths, back-buffer readback, and the blend/depth-stencil/
     * rasterizer/sampler state family. Everything outside it — render targets, cube and volume
     * textures, occlusion queries, custom `ShaderEffect` programs, instancing, and the
     * `DualTexture`/`EnvironmentMap`/`Skinned`/`Pbr` effect variants — reports itself as
     * unsupported rather than silently rendering something else.
     */
    class DiligentGraphicsBackend final : public IGraphicsBackend
    {
        friend class DiligentTextureBackend;
        friend class DiligentTextureCubeBackend;
        friend class DiligentTexture3DBackend;
        friend class DiligentRenderTargetBackend;
        friend class DiligentRenderTargetCubeBackend;
        friend class DiligentVertexBufferBackend;
        friend class DiligentIndexBufferBackend;
        friend class DiligentSpriteBatchBackend;

    public:
        /**
         * @brief Creates the render device, immediate context and swap chain for @p args.window.
         *
         * @param args Backend creation arguments; `window` must be a live SDL window.
         * @throws std::runtime_error If no Diligent device type could be created for this window.
         */
        explicit DiligentGraphicsBackend(const GraphicsBackendCreateArgs& args);

        /** @brief Releases every Diligent object owned by this backend. */
        ~DiligentGraphicsBackend() override;

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

        /** @brief Returns the back buffer's multisample count; always 1 (MSAA is a later phase). */
        [[nodiscard]] int GetMultiSampleCount() const override { return 1; }

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

        /** @brief Returns the SDL window this backend renders into. */
        [[nodiscard]] SDL_Window* GetWindowInternal() const override { return window_; }
        /** @brief Returns nothing: this backend never creates an SDL_Renderer. */
        [[nodiscard]] SDL_Renderer* GetRendererInternal() const override { return nullptr; }

        /**
         * @brief Creates a GPU texture from CPU pixels.
         * @param data Source image, RGBA8.
         * @return The new texture backend.
         */
        std::unique_ptr<ITextureBackend> CreateTexture(const ImageData& data) override;

        /** @brief Creates a sprite batch bound to this backend. */
        std::unique_ptr<ISpriteBatchBackend> CreateSpriteBatch() override;

        /**
         * @brief Creates an empty cube map.
         *
         * @param size          Edge length of each face, in texels.
         * @param mipMap        Whether to allocate a full mip chain.
         * @param surfaceFormat Raw XNA `SurfaceFormat` ordinal; accepted and not honoured.
         * @return The new cube texture backend.
         */
        std::unique_ptr<ITextureCubeBackend> CreateTextureCube(int size, bool mipMap,
                                                               int surfaceFormat) override;

        /**
         * @brief Creates an empty volume texture.
         *
         * @param w             Width in voxels.
         * @param h             Height in voxels.
         * @param depth         Depth in voxels.
         * @param mipMap        Whether to allocate a full mip chain.
         * @param surfaceFormat Raw XNA `SurfaceFormat` ordinal; accepted and not honoured.
         * @return The new volume texture backend.
         */
        std::unique_ptr<ITexture3DBackend> CreateTexture3D(int w, int h, int depth, bool mipMap,
                                                           int surfaceFormat) override;

        /**
         * @brief Creates an off-screen 2D render target.
         *
         * @param w                Target width in pixels.
         * @param h                Target height in pixels.
         * @param depthFormat      Raw XNA `DepthFormat` ordinal.
         * @param preserveContents Whether previous colour must survive a bind cycle.
         * @param mipMap           Whether to allocate a mip chain.
         * @param multiSampleCount Requested MSAA sample count; clamped to 1 (none) here, and
         *                         `IRenderTargetBackend::GetMultiSampleCount()` reports the real
         *                         applied value of 0 rather than the request (`DILIGENT-25`).
         * @return The new render target backend.
         */
        std::unique_ptr<IRenderTargetBackend> CreateRenderTarget2D(int w, int h, int depthFormat,
                                                                   bool preserveContents,
                                                                   bool mipMap,
                                                                   int multiSampleCount) override;

        /**
         * @brief Makes subsequent draws render into @p rt, or into the back buffer when null.
         * @param rt Render target to bind, or nullptr for the back buffer.
         */
        void SetRenderTarget2D(IRenderTargetBackend* rt) override;

        /**
         * @brief Creates a cube-map render target.
         *
         * @param size             Edge length of each face, in texels.
         * @param depthFormat      Raw XNA `DepthFormat` ordinal.
         * @param preserveContents Whether previous colour must survive a bind cycle.
         * @param mipMap           Whether to allocate a mip chain.
         * @param multiSampleCount Requested MSAA sample count; clamped to 1 here, see
         *                         `CreateRenderTarget2D`'s identical note.
         * @return The new cube render target backend.
         */
        std::unique_ptr<IRenderTargetCubeBackend> CreateRenderTargetCube(
            int size, int depthFormat, bool preserveContents, bool mipMap,
            int multiSampleCount) override;

        /**
         * @brief Makes subsequent draws render into one face of a cube render target.
         * @param rt   Cube render target to bind, or nullptr for the back buffer.
         * @param face Cube face index (0=+X, 1=-X, 2=+Y, 3=-Y, 4=+Z, 5=-Z).
         */
        void SetRenderTargetCubeFace(IRenderTargetCubeBackend* rt, int face) override;

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
         * @brief Reports whether a capability is genuinely available on this backend.
         * @param capability Capability to query.
         * @return True when supported.
         */
        [[nodiscard]] bool SupportsCapability(CNA::GraphicsCapability capability) const override;

        /** @brief Returns the device's real maximum 2D texture dimension. */
        [[nodiscard]] int GetMaxTextureDimension() const override { return maxTextureDimension_; }

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
         *                       this backend renders to a single target, and `MultiSampleMask` has
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
         * @return The new vertex buffer backend.
         */
        std::unique_ptr<IVertexBufferBackend> CreateVertexBuffer(int vertex_capacity) override;

        /**
         * @brief Creates a 16-bit index buffer.
         * @param index_capacity Number of indices the caller intends to store.
         * @return The new index buffer backend.
         */
        std::unique_ptr<IIndexBufferBackend> CreateIndexBuffer16(int index_capacity) override;

        /**
         * @brief Creates a 32-bit index buffer.
         * @param index_capacity Number of indices the caller intends to store.
         * @return The new index buffer backend.
         */
        std::unique_ptr<IIndexBufferBackend> CreateIndexBuffer32(int index_capacity) override;

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
        void DrawColoredPrimitives(const IVertexBufferBackend& vb,
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
        void DrawIndexedColoredPrimitives(const IVertexBufferBackend& vb, const IIndexBufferBackend& ib,
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
         * @throws std::runtime_error If @p params request an effect feature this backend does not
         *                            implement yet.
         */
        void DrawPrimitivesEx(const IVertexBufferBackend& vb,
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
         * @throws std::runtime_error If @p params request an effect feature this backend does not
         *                            implement yet.
         */
        void DrawIndexedPrimitivesEx(const IVertexBufferBackend& vb, const IIndexBufferBackend& ib,
                                     const Matrix& world, const Matrix& view, const Matrix& projection,
                                     PrimitiveType primitive, int primitiveCount,
                                     const GpuDrawParams& params) override;

        /** @brief NOXNA. Returns the device type this backend actually created. */
        NOXNA [[nodiscard]] DiligentDeviceType GetDeviceType() const { return deviceType_; }

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
        };

        /** @brief Constant buffer contents shared by every built-in shader. */
        struct ShaderConstants
        {
            float worldViewProj[16];
            float world[16];
            float diffuseColor[4];
            float emissiveAmbient[4];
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

        void CreateDeviceAndSwapChain(const GraphicsBackendCreateArgs& args);
        bool TryCreateDevice(DiligentDeviceType type, int multiSampleCount);
        void CreateConstantBuffer();
        void CreateFallbackTexture();
        void UploadBoneTransforms(const GpuDrawParams& params);
        void SyncSwapChainSize();
        void EnsureRenderTargetsBound();
        void ApplyViewportAndScissor();
        [[nodiscard]] LogicalViewport ComputeLogicalViewport() const;
        [[nodiscard]] CachedPipeline& GetOrCreatePipeline(const PipelineKey& key);
        [[nodiscard]] PipelineKey MakePipelineKey(ShaderVariant variant, PrimitiveType primitive) const;
        [[nodiscard]] Dg::ISampler* GetOrCreateSampler(int filter, int addressU, int addressV,
                                                       int maxAnisotropy);
        void UploadConstants(const ShaderConstants& constants);
        [[nodiscard]] bool ReadTextureRegion(Dg::ITexture* texture, Dg::Uint32 mipLevel,
                                             Dg::Uint32 arraySlice, int x, int y, int z,
                                             int w, int h, int depth,
                                             void* data, int dataLength);
        void DrawInternal(const IVertexBufferBackend& vb, const IIndexBufferBackend* ib,
                          const Matrix& world, const Matrix& view, const Matrix& projection,
                          PrimitiveType primitive, int primitiveCount,
                          const GpuDrawParams* params);
        void DrawSpriteQuads(Dg::IBuffer* vertexBuffer, Dg::IBuffer* indexBuffer,
                             std::size_t spriteCount, const DiligentSampledTexture& texture,
                             const Matrix* transform, int filter, int addressU, int addressV);
        [[nodiscard]] Dg::ITextureView* GetBackBufferTextureView() const;
        [[nodiscard]] Dg::ITextureView* GetCurrentRenderTargetView() const;
        [[nodiscard]] DiligentRenderTargetBackend* PrimaryRenderTarget() const;
        [[nodiscard]] Dg::ITextureView* GetCurrentDepthStencilView() const;
        [[nodiscard]] Dg::TEXTURE_FORMAT CurrentColorFormat() const;
        [[nodiscard]] Dg::TEXTURE_FORMAT CurrentDepthStencilFormat() const;

        SDL_Window* window_ = nullptr;
        DiligentDeviceType deviceType_ = DiligentDeviceType::Vulkan;
        Dg::RefCntAutoPtr<Dg::IRenderDevice> device_;
        Dg::RefCntAutoPtr<Dg::IDeviceContext> context_;
        Dg::RefCntAutoPtr<Dg::ISwapChain> swapChain_;
        Dg::RefCntAutoPtr<Dg::IEngineFactory> engineFactory_;
        Dg::RefCntAutoPtr<Dg::IBuffer> constantBuffer_;
        /// SkinnedEffect's 72-matrix bone palette. Its own buffer: at 4.5 KB it would dominate the
        /// per-draw constant block every non-skinned draw uploads too.
        Dg::RefCntAutoPtr<Dg::IBuffer> boneBuffer_;
        /// 1x1 opaque white, bound whenever a textured vertex layout is drawn with texturing
        /// switched off. The shader ignores it (`g_Flags.x` is 0), but a pipeline that declares a
        /// texture variable still has to have one bound.
        Dg::RefCntAutoPtr<Dg::ITexture> fallbackTexture_;
        Dg::ITextureView* fallbackTextureView_ = nullptr;

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
        int samplerFilter_ = 0;
        int samplerAddressU_ = 1;
        int samplerAddressV_ = 1;
        int samplerMaxAnisotropy_ = 4;

        bool scissorEnabled_ = false;
        int scissorRect_[4] = {0, 0, 0, 0};
        bool customViewport_ = false;
        int viewportRect_[4] = {0, 0, 0, 0};
        float viewportDepth_[2] = {0.0f, 1.0f};

        bool renderTargetsBound_ = false;
        /// Every bound 2D render target, slot-aligned. Empty means the back buffer or a bound
        /// cube face (mutually exclusive with this: see currentCubeTarget_).
        std::vector<DiligentRenderTargetBackend*> currentRenderTargets_;
        /// The cube render target currently bound as the draw target, or nullptr. Mutually
        /// exclusive with a non-empty currentRenderTargets_ -- binding one clears the other.
        DiligentRenderTargetCubeBackend* currentCubeTarget_ = nullptr;
        int currentCubeFace_ = -1;
    };
}
