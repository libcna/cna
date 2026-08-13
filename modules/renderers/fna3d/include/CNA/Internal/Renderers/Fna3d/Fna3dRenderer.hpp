// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "CNA/Internal/Renderers/Fna3d/Fna3dApi.hpp"
#include "CNA/Internal/Renderers/Fna3d/Fna3dPresentation.hpp"
#include "CNA/Internal/Renderers/Fna3d/Fna3dStockEffects.hpp"

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

struct SDL_Window;

namespace CNA::Internal::Renderers::Fna3d
{
    class Fna3dRenderer;

    /**
     * @brief Shared lifetime/identity token for one FNA3D device.
     *
     * Resource wrappers may outlive the renderer that created them. Keeping a raw
     * `FNA3D_Device*` in every wrapper made their destructors and methods enter a destroyed
     * native device in that ordering. The renderer invalidates this token before the native
     * device disappears; wrappers then reject use and make destruction a no-op. Token identity
     * also distinguishes two simultaneously live FNA3D devices, which a type-only dynamic_cast
     * cannot do.
     */
    struct Fna3dDeviceState
    {
        /** @brief Live native device, or null after renderer teardown. */
        FNA3D_Device* device = nullptr;
        /** @brief Live owning renderer, or null once its teardown begins. */
        Fna3dRenderer* renderer = nullptr;

        /** @brief Returns the live device or raises a deterministic post-device-use error. */
        [[nodiscard]] FNA3D_Device* RequireDevice(const char* operation) const;
        /** @brief Returns the live renderer or raises a deterministic post-device-use error. */
        [[nodiscard]] Fna3dRenderer& RequireRenderer(const char* operation) const;
    };

    /**
     * @brief CNAEXT. Anything this renderer owns that can be bound to a sampler slot.
     *
     * A plain texture and a render target are unrelated types in the shared renderer interface
     * (`ITextureRenderer` vs `IRenderTargetRenderer`, `ITextureCubeRenderer` vs
     * `IRenderTargetCubeRenderer`), yet both are simply an `FNA3D_Texture` as far as
     * `FNA3D_VerifySampler` is concerned -- a rendered target sampled by a later draw is the whole
     * point of a render target. This mix-in is what lets the sprite path and the effect texture
     * binding accept either without either special-casing render targets or, worse, static_casting
     * one to the other.
     */
    class Fna3dSampledTexture
    {
    public:
        virtual ~Fna3dSampledTexture() = default;

        /** @brief The FNA3D texture object to bind to a sampler slot. */
        [[nodiscard]] virtual FNA3D_Texture* GetFna3dTextureEXT() const = 0;
        /** @brief Device identity/liveness token that owns the texture. */
        [[nodiscard]] virtual const std::shared_ptr<Fna3dDeviceState>&
        GetFna3dDeviceStateEXT() const = 0;
    };

    /**
     * @brief Renderer handle for an `FNA3D_Texture` used as a sampled 2D texture.
     *
     * Owns the FNA3D object and releases it through the device that created it. FNA3D allocates
     * mip storage but leaves its contents undefined until written, so the set of levels this
     * texture can honestly answer `GetData` for is tracked explicitly rather than assumed from
     * the allocated level count.
     */
    class Fna3dTextureRenderer : public ITextureRenderer, public Fna3dSampledTexture
    {
    public:
        /**
         * @brief Wraps an already-created FNA3D texture.
         *
         * @param deviceState  Shared owner identity and liveness state for @p texture.
         * @param texture      The FNA3D texture object.
         * @param width        Level-0 width in texels.
         * @param height       Level-0 height in texels.
         * @param levelCount   Allocated mip level count.
         * @param surfaceFormat Raw XNA `SurfaceFormat` ordinal the texture was created with.
         * @param compressedReadback Whether the running driver reads block-compressed textures
         *                     back; only consulted for a block-compressed @p surfaceFormat.
         */
        Fna3dTextureRenderer(std::shared_ptr<Fna3dDeviceState> deviceState,
                             FNA3D_Texture* texture, int width, int height, int levelCount,
                             int surfaceFormat, bool compressedReadback = true);
        ~Fna3dTextureRenderer() override;

        Fna3dTextureRenderer(const Fna3dTextureRenderer&) = delete;
        Fna3dTextureRenderer& operator=(const Fna3dTextureRenderer&) = delete;

        /** @brief Level-0 width in texels. */
        [[nodiscard]] int GetWidth() const override { return width_; }
        /** @brief Level-0 height in texels. */
        [[nodiscard]] int GetHeight() const override { return height_; }
        /** @brief Always null: FNA3D owns no SDL_Texture. */
        [[nodiscard]] SDL_Texture* GetNativeTexture() const override { return nullptr; }

        /**
         * @brief Replaces the whole of level 0 with tightly packed RGBA8 rows.
         * @param rgba   Source pixels, top row first.
         * @param stride Row pitch in bytes; rows are repacked when it exceeds `width * 4`.
         */
        void UpdatePixels(const std::uint8_t* rgba, int stride) override;

        /**
         * @brief Replaces the whole of one mip level.
         * @param level  Mip level to write.
         * @param rgba   Source pixels, top row first, tightly packed.
         * @param levelW Width of that level in texels.
         * @param levelH Height of that level in texels.
         */
        void UpdatePixelsLevel(int level, const std::uint8_t* rgba, int levelW,
                               int levelH) override;

        /**
         * @brief Whether this texture holds bytes the caller actually wrote for @p level.
         * @param level Mip level being queried.
         * @return True only for a level a `SetData` call has populated.
         */
        [[nodiscard]] bool HasDefinedMipLevel(int level) const noexcept override;

        /**
         * @brief Reads a sub-rectangle of a mip level back into @p data as RGBA8.
         * @param level      Mip level to read.
         * @param x          Left edge of the region, in texels.
         * @param y          Top edge of the region, in texels.
         * @param w          Region width in texels.
         * @param h          Region height in texels.
         * @param data       Destination for tightly packed RGBA8 rows, top row first.
         * @param dataLength Size of @p data in bytes.
         * @return True when the whole region was read back; false when this texture cannot serve it.
         */
        [[nodiscard]] bool GetData(int level, int x, int y, int w, int h, void* data,
                                   int dataLength) const override;

        /** @brief CNAEXT. The underlying FNA3D texture, for binding by the renderer. */
        [[nodiscard]] FNA3D_Texture* GetFna3dTextureEXT() const override
        {
            return deviceState_ != nullptr && deviceState_->device != nullptr ? texture_ : nullptr;
        }
        [[nodiscard]] const std::shared_ptr<Fna3dDeviceState>&
        GetFna3dDeviceStateEXT() const override { return deviceState_; }
        /** @brief CNAEXT. Raw XNA `SurfaceFormat` ordinal this texture was created with. */
        [[nodiscard]] int GetSurfaceFormatEXT() const { return surfaceFormat_; }
        /** @brief CNAEXT. Allocated mip level count. */
        [[nodiscard]] int GetLevelCountEXT() const { return levelCount_; }

    protected:
        /** @brief Records that @p level now holds caller-written bytes. */
        void MarkLevelDefinedEXT(int level);

        /** @brief Shared device identity and liveness guard. */
        std::shared_ptr<Fna3dDeviceState> deviceState_;
        /** @brief The FNA3D texture object. */
        FNA3D_Texture* texture_ = nullptr;
        /** @brief Level-0 width in texels. */
        int width_ = 0;
        /** @brief Level-0 height in texels. */
        int height_ = 0;
        /** @brief Allocated mip level count. */
        int levelCount_ = 1;
        /** @brief Raw XNA `SurfaceFormat` ordinal. */
        int surfaceFormat_ = 0;
        /** @brief Whether the running driver reads block-compressed textures back. */
        bool compressedReadback_ = true;
        /** @brief Bit i set once level i has been written by the caller. */
        std::uint32_t definedLevels_ = 0;
    };

    /**
     * @brief Renderer handle for a `RenderTarget2D`.
     *
     * FNA3D models a render target as a destination texture plus, when multisampled, a separate
     * colour renderbuffer that must be resolved into it; the depth/stencil renderbuffer is a third
     * object handed to `FNA3D_SetRenderTargets`. All three are owned here so a target's lifetime
     * is one object from CNA's side.
     */
    class Fna3dRenderTargetRenderer : public IRenderTargetRenderer,
                                      public Fna3dSampledTexture
    {
    public:
        /**
         * @brief Creates the target's texture and, as needed, its colour/depth renderbuffers.
         *
         * @param deviceState         Shared owner identity and liveness state.
         * @param width               Target width in pixels.
         * @param height              Target height in pixels.
         * @param depthFormat         Raw XNA `DepthFormat` ordinal.
         * @param preserveContents    Whether previously rendered colour must survive a re-bind.
         * @param mipMap              Whether a full mip chain is allocated and generated.
         * @param multiSampleCount    Requested MSAA sample count, clamped to the device maximum.
         * @param surfaceFormat       Raw XNA `SurfaceFormat` ordinal.
         */
        Fna3dRenderTargetRenderer(std::shared_ptr<Fna3dDeviceState> deviceState, int width,
                                  int height, int depthFormat, bool preserveContents, bool mipMap,
                                  int multiSampleCount, int surfaceFormat);
        ~Fna3dRenderTargetRenderer() override;

        Fna3dRenderTargetRenderer(const Fna3dRenderTargetRenderer&) = delete;
        Fna3dRenderTargetRenderer& operator=(const Fna3dRenderTargetRenderer&) = delete;

        /** @brief Target width in pixels. */
        [[nodiscard]] int GetWidth() const override { return width_; }
        /** @brief Target height in pixels. */
        [[nodiscard]] int GetHeight() const override { return height_; }
        /** @brief Always null: FNA3D owns no SDL_Texture. */
        [[nodiscard]] SDL_Texture* GetNativeTexture() const override { return nullptr; }

        /** @brief Binds this target as the sole draw destination. */
        void BindAsRenderTarget() override;
        /** @brief Restores the back buffer, resolving this target first. */
        void UnbindAsRenderTarget() override;

        /** @brief Device-clamped MSAA sample count actually allocated (0 when none). */
        [[nodiscard]] int GetMultiSampleCount() const override { return multiSampleCount_; }

        /**
         * @brief Depth/stencil format actually backing this target.
         * @param requestedDepthStencilFormat Requested `DepthFormat` ordinal.
         * @return The applied ordinal, which is the request -- FNA3D honours it exactly.
         */
        [[nodiscard]] int GetAppliedDepthStencilFormatEXT(
            int requestedDepthStencilFormat) const override;

        /**
         * @brief Whether real depth storage was allocated for this target.
         * @param depthFormatWasRequested Whether a non-None DepthFormat was requested.
         * @return True only when a depth/stencil renderbuffer exists.
         */
        [[nodiscard]] bool HasRealDepthBuffer(bool depthFormatWasRequested) const override;

        /**
         * @brief Whether a real stencil plane was allocated (Depth24Stencil8 only).
         * @param stencilFormatWasRequested Whether Depth24Stencil8 was requested.
         * @return True only when the allocated depth format carries stencil.
         */
        [[nodiscard]] bool HasRealStencilBuffer(bool stencilFormatWasRequested) const override;

        /**
         * @brief Reads a sub-rectangle of a rendered mip level back as RGBA8.
         * @param level      Mip level to read.
         * @param x          Left edge of the region, in pixels.
         * @param y          Top edge of the region, in pixels.
         * @param w          Region width in pixels.
         * @param h          Region height in pixels.
         * @param data       Destination for tightly packed RGBA8 rows, top row first.
         * @param dataLength Size of @p data in bytes.
         * @return True when the whole region was read back.
         */
        [[nodiscard]] bool GetData(int level, int x, int y, int w, int h, void* data,
                                   int dataLength) const override;

        /** @brief CNAEXT. The destination texture this target resolves into. */
        [[nodiscard]] FNA3D_Texture* GetFna3dTextureEXT() const override
        {
            return deviceState_ != nullptr && deviceState_->device != nullptr ? texture_ : nullptr;
        }
        [[nodiscard]] const std::shared_ptr<Fna3dDeviceState>&
        GetFna3dDeviceStateEXT() const override { return deviceState_; }
        /** @brief CNAEXT. The depth/stencil renderbuffer, or null when DepthFormat::None. */
        [[nodiscard]] FNA3D_Renderbuffer* GetDepthBufferEXT() const
        {
            return deviceState_ != nullptr && deviceState_->device != nullptr ? depthBuffer_
                                                                              : nullptr;
        }
        /** @brief CNAEXT. Raw XNA `DepthFormat` ordinal actually allocated. */
        [[nodiscard]] int GetDepthFormatEXT() const { return depthFormat_; }
        /** @brief CNAEXT. Whether previously rendered colour must survive a re-bind. */
        [[nodiscard]] bool GetPreserveContentsEXT() const { return preserveContents_; }
        /** @brief CNAEXT. Fills the FNA3D binding describing this target. */
        void FillBindingEXT(FNA3D_RenderTargetBinding& binding) const;

    private:
        std::shared_ptr<Fna3dDeviceState> deviceState_;
        FNA3D_Texture* texture_ = nullptr;
        FNA3D_Renderbuffer* colorBuffer_ = nullptr;
        FNA3D_Renderbuffer* depthBuffer_ = nullptr;
        int width_ = 0;
        int height_ = 0;
        int levelCount_ = 1;
        int depthFormat_ = 0;
        int multiSampleCount_ = 0;
        int surfaceFormat_ = 0;
        bool preserveContents_ = false;
    };

    /**
     * @brief Renderer handle for a sampled cube map.
     */
    class Fna3dTextureCubeRenderer : public ITextureCubeRenderer,
                                     public Fna3dSampledTexture
    {
    public:
        /**
         * @brief Wraps an already-created FNA3D cube texture.
         *
         * @param deviceState   Shared owner identity and liveness state for the texture.
         * @param texture       The FNA3D texture object.
         * @param size          Edge length of one face in texels.
         * @param levelCount    Allocated mip level count.
         * @param surfaceFormat Raw XNA `SurfaceFormat` ordinal.
         */
        Fna3dTextureCubeRenderer(std::shared_ptr<Fna3dDeviceState> deviceState,
                                 FNA3D_Texture* texture, int size, int levelCount,
                                 int surfaceFormat);
        ~Fna3dTextureCubeRenderer() override;

        Fna3dTextureCubeRenderer(const Fna3dTextureCubeRenderer&) = delete;
        Fna3dTextureCubeRenderer& operator=(const Fna3dTextureCubeRenderer&) = delete;

        /**
         * @brief Uploads RGBA8 pixels into a sub-rectangle of one cube face.
         * @param face       Cube face index (0=+X … 5=-Z).
         * @param level      Mip level to write.
         * @param x          Left edge of the region, in texels.
         * @param y          Top edge of the region, in texels.
         * @param w          Region width in texels.
         * @param h          Region height in texels.
         * @param data       Source pixels, tightly packed RGBA8, top row first.
         * @param dataLength Size of @p data in bytes.
         * @return True when the whole region was stored.
         */
        [[nodiscard]] bool SetData(int face, int level, int x, int y, int w, int h,
                                   const void* data, int dataLength) override;

        /**
         * @brief Reads RGBA8 pixels back from a sub-rectangle of one cube face.
         * @param face       Cube face index (0=+X … 5=-Z).
         * @param level      Mip level to read.
         * @param x          Left edge of the region, in texels.
         * @param y          Top edge of the region, in texels.
         * @param w          Region width in texels.
         * @param h          Region height in texels.
         * @param data       Destination for tightly packed RGBA8 rows.
         * @param dataLength Size of @p data in bytes.
         * @return True when the whole region was read back.
         */
        [[nodiscard]] bool GetData(int face, int level, int x, int y, int w, int h, void* data,
                                   int dataLength) const override;

        /** @brief Edge length of one face in texels. */
        [[nodiscard]] int GetSizeEXT() const noexcept override { return size_; }
        /** @brief CNAEXT. The underlying FNA3D texture, for binding by the renderer. */
        [[nodiscard]] FNA3D_Texture* GetFna3dTextureEXT() const override
        {
            return deviceState_ != nullptr && deviceState_->device != nullptr ? texture_ : nullptr;
        }
        [[nodiscard]] const std::shared_ptr<Fna3dDeviceState>&
        GetFna3dDeviceStateEXT() const override { return deviceState_; }

    protected:
        /** @brief Shared device identity and liveness guard. */
        std::shared_ptr<Fna3dDeviceState> deviceState_;
        /** @brief The FNA3D texture object. */
        FNA3D_Texture* texture_ = nullptr;
        /** @brief Edge length of one face in texels. */
        int size_ = 0;
        /** @brief Allocated mip level count. */
        int levelCount_ = 1;
        /** @brief Raw XNA `SurfaceFormat` ordinal. */
        int surfaceFormat_ = 0;
    };

    /**
     * @brief Renderer handle for a cube-map render target.
     */
    class Fna3dRenderTargetCubeRenderer : public IRenderTargetCubeRenderer,
                                          public Fna3dSampledTexture
    {
    public:
        /**
         * @brief Creates the cube target's texture and, as needed, its renderbuffers.
         *
         * @param deviceState      Shared owner identity and liveness state.
         * @param size             Edge length of one face in pixels.
         * @param depthFormat      Raw XNA `DepthFormat` ordinal.
         * @param preserveContents Whether previously rendered colour must survive a re-bind.
         * @param mipMap           Whether a full mip chain is allocated and generated.
         * @param multiSampleCount Requested MSAA sample count, clamped to the device maximum.
         */
        Fna3dRenderTargetCubeRenderer(std::shared_ptr<Fna3dDeviceState> deviceState, int size,
                                      int depthFormat, bool preserveContents, bool mipMap,
                                      int multiSampleCount);
        ~Fna3dRenderTargetCubeRenderer() override;

        Fna3dRenderTargetCubeRenderer(const Fna3dRenderTargetCubeRenderer&) = delete;
        Fna3dRenderTargetCubeRenderer& operator=(const Fna3dRenderTargetCubeRenderer&) = delete;

        /** @brief Edge length of one face in pixels. */
        [[nodiscard]] int GetSize() const override { return size_; }
        /** @brief Edge length of one face in pixels. */
        [[nodiscard]] int GetSizeEXT() const noexcept override { return size_; }

        /**
         * @brief Binds one cube face as the sole draw destination.
         * @param face Cube face index (0=+X … 5=-Z).
         */
        void BindAsRenderTargetFace(int face) override;
        /** @brief Restores the back buffer, resolving the bound face first. */
        void UnbindAsRenderTarget() override;

        /** @brief Device-clamped MSAA sample count actually allocated (0 when none). */
        [[nodiscard]] int GetMultiSampleCount() const override { return multiSampleCount_; }

        /**
         * @brief Whether real depth storage was allocated for this target.
         * @param depthFormatWasRequested Whether a non-None DepthFormat was requested.
         * @return True only when a depth/stencil renderbuffer exists.
         */
        [[nodiscard]] bool HasRealDepthBuffer(bool depthFormatWasRequested) const override;

        /**
         * @brief Reads RGBA8 pixels back from a rendered cube face.
         * @param face       Cube face index (0=+X … 5=-Z).
         * @param level      Mip level to read.
         * @param x          Left edge of the region, in pixels.
         * @param y          Top edge of the region, in pixels.
         * @param w          Region width in pixels.
         * @param h          Region height in pixels.
         * @param data       Destination for tightly packed RGBA8 rows.
         * @param dataLength Size of @p data in bytes.
         * @return True when the whole region was read back.
         */
        [[nodiscard]] bool GetData(int face, int level, int x, int y, int w, int h, void* data,
                                   int dataLength) const override;

        /** @brief CNAEXT. The destination cube texture this target resolves into. */
        [[nodiscard]] FNA3D_Texture* GetFna3dTextureEXT() const override
        {
            return deviceState_ != nullptr && deviceState_->device != nullptr ? texture_ : nullptr;
        }
        [[nodiscard]] const std::shared_ptr<Fna3dDeviceState>&
        GetFna3dDeviceStateEXT() const override { return deviceState_; }
        /** @brief CNAEXT. The depth/stencil renderbuffer, or null when DepthFormat::None. */
        [[nodiscard]] FNA3D_Renderbuffer* GetDepthBufferEXT() const
        {
            return deviceState_ != nullptr && deviceState_->device != nullptr ? depthBuffer_
                                                                              : nullptr;
        }
        /** @brief CNAEXT. Raw XNA `DepthFormat` ordinal actually allocated. */
        [[nodiscard]] int GetDepthFormatEXT() const { return depthFormat_; }
        /** @brief CNAEXT. Whether previously rendered colour must survive a re-bind. */
        [[nodiscard]] bool GetPreserveContentsEXT() const { return preserveContents_; }
        /**
         * @brief CNAEXT. Fills the FNA3D binding describing one face of this target.
         * @param binding Binding to populate.
         * @param face    Cube face index (0=+X … 5=-Z).
         */
        void FillBindingEXT(FNA3D_RenderTargetBinding& binding, int face) const;

    private:
        std::shared_ptr<Fna3dDeviceState> deviceState_;
        FNA3D_Texture* texture_ = nullptr;
        FNA3D_Renderbuffer* colorBuffer_ = nullptr;
        FNA3D_Renderbuffer* depthBuffer_ = nullptr;
        int size_ = 0;
        int levelCount_ = 1;
        int depthFormat_ = 0;
        int multiSampleCount_ = 0;
        int boundFace_ = 0;
        bool preserveContents_ = false;
    };

    /**
     * @brief Renderer handle for a sampled volume texture.
     */
    class Fna3dTexture3DRenderer : public ITexture3DRenderer, public Fna3dSampledTexture
    {
    public:
        /**
         * @brief Wraps an already-created FNA3D volume texture.
         *
         * @param deviceState   Shared owner identity and liveness state for the texture.
         * @param texture       The FNA3D texture object.
         * @param width         Width in voxels.
         * @param height        Height in voxels.
         * @param depth         Depth in voxels.
         * @param levelCount    Allocated mip level count.
         * @param surfaceFormat Raw XNA `SurfaceFormat` ordinal.
         * @param keepUploadMirror Whether to mirror uploads on the CPU so `GetData` can be served
         *                         when the selected FNA3D driver cannot read a volume texture back.
         */
        Fna3dTexture3DRenderer(std::shared_ptr<Fna3dDeviceState> deviceState,
                               FNA3D_Texture* texture, int width, int height, int depth,
                               int levelCount, int surfaceFormat, bool keepUploadMirror);
        ~Fna3dTexture3DRenderer() override;

        Fna3dTexture3DRenderer(const Fna3dTexture3DRenderer&) = delete;
        Fna3dTexture3DRenderer& operator=(const Fna3dTexture3DRenderer&) = delete;

        /**
         * @brief Uploads tightly packed voxels in the texture's declared surface format.
         * @param level      Mip level to write.
         * @param x          Left edge of the box, in voxels.
         * @param y          Top edge of the box, in voxels.
         * @param z          Front edge of the box, in voxels.
         * @param w          Box width in voxels.
         * @param h          Box height in voxels.
         * @param depth      Box depth in voxels.
         * @param data       Source voxels, tightly packed in the declared surface format.
         * @param dataLength Size of @p data in bytes.
         * @return True when the whole box was stored.
         */
        [[nodiscard]] bool SetData(int level, int x, int y, int z, int w, int h, int depth,
                                   const void* data, int dataLength) override;

        /**
         * @brief Reads tightly packed voxels in the texture's declared surface format.
         * @param level      Mip level to read.
         * @param x          Left edge of the box, in voxels.
         * @param y          Top edge of the box, in voxels.
         * @param z          Front edge of the box, in voxels.
         * @param w          Box width in voxels.
         * @param h          Box height in voxels.
         * @param depth      Box depth in voxels.
         * @param data       Destination for the tightly packed declared-format box.
         * @param dataLength Size of @p data in bytes.
         * @return True when the whole box was read back.
         */
        [[nodiscard]] bool GetData(int level, int x, int y, int z, int w, int h, int depth,
                                   void* data, int dataLength) const override;

        /**
         * @brief Reports this volume's dimensions.
         * @param width  Filled with the width in voxels.
         * @param height Filled with the height in voxels.
         * @param depth  Filled with the depth in voxels.
         */
        void GetDimensionsEXT(int& width, int& height, int& depth) const noexcept override;

        /** @brief CNAEXT. The underlying FNA3D texture, for binding by the renderer. */
        [[nodiscard]] FNA3D_Texture* GetFna3dTextureEXT() const override
        {
            return deviceState_ != nullptr && deviceState_->device != nullptr ? texture_ : nullptr;
        }
        [[nodiscard]] const std::shared_ptr<Fna3dDeviceState>&
        GetFna3dDeviceStateEXT() const override { return deviceState_; }

    private:
        std::shared_ptr<Fna3dDeviceState> deviceState_;
        FNA3D_Texture* texture_ = nullptr;
        int width_ = 0;
        int height_ = 0;
        int depth_ = 0;
        int levelCount_ = 1;
        int surfaceFormat_ = 0;
        // FNA3D's OpenGL driver implements no GetTextureData3D at all (it logs
        // "GetTextureData3D is unsupported!" and writes nothing), while its SDL_GPU and Direct3D
        // 11 drivers do. A volume texture in XNA has no GPU write path -- its content can only
        // come from SetData -- so mirroring the uploads is exact only if every requested voxel is
        // known to have been supplied. `uploadMirrorDefined_` tracks that coverage and makes a
        // read touching an uninitialised voxel fail rather than return resize-created zeroes.
        bool keepUploadMirror_ = false;
        std::vector<std::vector<std::uint8_t>> uploadMirror_;
        std::vector<std::vector<std::uint8_t>> uploadMirrorDefined_;
    };

    /**
     * @brief Renderer handle for a vertex buffer.
     *
     * Remembers the caller's own `VertexDeclaration` when one is supplied, because FNA3D binds a
     * full declaration per stream rather than deriving a layout from a byte stride. The internal
     * routes that bind no public buffer (SpriteBatch, `DrawUser*`) supply none, and a
     * stride-keyed layout table stands in for them.
     */
    class Fna3dVertexBufferRenderer : public IVertexBufferRenderer
    {
    public:
        /**
         * @brief Allocates a dynamic FNA3D vertex buffer.
         *
         * @param deviceState    Shared owner identity and liveness state for the buffer.
         * @param vertexCapacity Number of vertices the buffer must hold.
         * @param maxStride      Largest vertex stride the buffer must accommodate.
         */
        Fna3dVertexBufferRenderer(std::shared_ptr<Fna3dDeviceState> deviceState,
                                  int vertexCapacity, int maxStride);
        ~Fna3dVertexBufferRenderer() override;

        Fna3dVertexBufferRenderer(const Fna3dVertexBufferRenderer&) = delete;
        Fna3dVertexBufferRenderer& operator=(const Fna3dVertexBufferRenderer&) = delete;

        /**
         * @brief Uploads vertex data, growing the GPU allocation when needed.
         * @param data            Packed vertex data.
         * @param vertex_count    Number of vertices.
         * @param stride_in_bytes Size of one vertex in bytes.
         */
        void SetData(const void* data, int vertex_count, std::size_t stride_in_bytes) override;

        /**
         * @brief Uploads vertex data with an explicit streaming hint.
         * @param data            Packed vertex data.
         * @param vertex_count    Number of vertices.
         * @param stride_in_bytes Size of one vertex in bytes.
         * @param options         Streaming hint forwarded to FNA3D verbatim.
         */
        void SetDataWithOptions(const void* data, int vertex_count, std::size_t stride_in_bytes,
                                SetDataOptions options) override;

        /**
         * @brief Remembers the caller's declaration so FNA3D can bind the real layout.
         * @param vertexDeclaration Full declaration, including stride and elements.
         */
        void SetVertexDeclaration(const VertexDeclaration& vertexDeclaration) override;

        /** @brief Number of vertices most recently uploaded. */
        [[nodiscard]] int GetVertexCount() const override { return vertexCount_; }

        /** @brief CNAEXT. The underlying FNA3D buffer. */
        [[nodiscard]] FNA3D_Buffer* GetFna3dBufferEXT() const
        {
            return deviceState_ != nullptr && deviceState_->device != nullptr ? buffer_ : nullptr;
        }
        /** @brief CNAEXT. Device identity/liveness token that owns this buffer. */
        [[nodiscard]] const std::shared_ptr<Fna3dDeviceState>&
        GetFna3dDeviceStateEXT() const { return deviceState_; }
        /** @brief CNAEXT. Stride of the most recent upload, in bytes. */
        [[nodiscard]] int GetStrideEXT() const { return stride_; }
        /** @brief CNAEXT. Whether a caller-supplied declaration is available. */
        [[nodiscard]] bool HasDeclarationEXT() const { return hasDeclaration_; }
        /** @brief CNAEXT. The caller's declaration elements, in declaration order. */
        [[nodiscard]] const std::vector<FNA3D_VertexElement>& GetDeclarationElementsEXT() const
        {
            return declarationElements_;
        }
        /** @brief CNAEXT. Stride declared by the caller's `VertexDeclaration`, in bytes. */
        [[nodiscard]] int GetDeclarationStrideEXT() const { return declarationStride_; }

    private:
        void EnsureCapacity(int byteCount);

        std::shared_ptr<Fna3dDeviceState> deviceState_;
        FNA3D_Buffer* buffer_ = nullptr;
        int capacityBytes_ = 0;
        int vertexCount_ = 0;
        int stride_ = 0;
        bool hasDeclaration_ = false;
        int declarationStride_ = 0;
        std::vector<FNA3D_VertexElement> declarationElements_;
    };

    /**
     * @brief Renderer handle for a 16- or 32-bit index buffer.
     */
    class Fna3dIndexBufferRenderer : public IIndexBufferRenderer
    {
    public:
        /**
         * @brief Allocates a dynamic FNA3D index buffer.
         * @param deviceState   Shared owner identity and liveness state for the buffer.
         * @param indexCapacity Number of indices the buffer must hold.
         * @param thirtyTwoBit  Whether indices are 32-bit.
         */
        Fna3dIndexBufferRenderer(std::shared_ptr<Fna3dDeviceState> deviceState,
                                 int indexCapacity, bool thirtyTwoBit);
        ~Fna3dIndexBufferRenderer() override;

        Fna3dIndexBufferRenderer(const Fna3dIndexBufferRenderer&) = delete;
        Fna3dIndexBufferRenderer& operator=(const Fna3dIndexBufferRenderer&) = delete;

        /**
         * @brief Uploads 16-bit index data.
         * @param data        Packed 16-bit indices.
         * @param index_count Number of indices.
         */
        void SetData16(const void* data, int index_count) override;

        /**
         * @brief Uploads 32-bit index data.
         * @param data        Packed 32-bit indices.
         * @param index_count Number of indices.
         */
        void SetData32(const void* data, int index_count) override;

        /**
         * @brief Uploads 16-bit index data with a streaming hint.
         * @param data        Packed 16-bit indices.
         * @param index_count Number of indices.
         * @param options     Streaming hint forwarded to FNA3D verbatim.
         */
        void SetData16WithOptions(const void* data, int index_count,
                                  SetDataOptions options) override;

        /**
         * @brief Uploads 32-bit index data with a streaming hint.
         * @param data        Packed 32-bit indices.
         * @param index_count Number of indices.
         * @param options     Streaming hint forwarded to FNA3D verbatim.
         */
        void SetData32WithOptions(const void* data, int index_count,
                                  SetDataOptions options) override;

        /** @brief Number of indices most recently uploaded. */
        [[nodiscard]] int GetIndexCount() const override { return indexCount_; }
        /** @brief Whether this buffer holds 32-bit indices. */
        [[nodiscard]] bool IsThirtyTwoBit() const override { return thirtyTwoBit_; }

        /** @brief CNAEXT. The underlying FNA3D buffer. */
        [[nodiscard]] FNA3D_Buffer* GetFna3dBufferEXT() const
        {
            return deviceState_ != nullptr && deviceState_->device != nullptr ? buffer_ : nullptr;
        }
        /** @brief CNAEXT. Device identity/liveness token that owns this buffer. */
        [[nodiscard]] const std::shared_ptr<Fna3dDeviceState>&
        GetFna3dDeviceStateEXT() const { return deviceState_; }
        /** @brief CNAEXT. The FNA3D index element size matching this buffer. */
        [[nodiscard]] FNA3D_IndexElementSize GetElementSizeEXT() const
        {
            return thirtyTwoBit_ ? FNA3D_INDEXELEMENTSIZE_32BIT : FNA3D_INDEXELEMENTSIZE_16BIT;
        }

    private:
        void Upload(const void* data, int indexCount, int elementBytes, bool thirtyTwoBit,
                    FNA3D_SetDataOptions options);
        void EnsureCapacity(int byteCount);

        std::shared_ptr<Fna3dDeviceState> deviceState_;
        FNA3D_Buffer* buffer_ = nullptr;
        int capacityBytes_ = 0;
        int indexCount_ = 0;
        bool thirtyTwoBit_ = false;
    };

    /**
     * @brief Renderer handle for a hardware occlusion query.
     */
    class Fna3dOcclusionQueryRenderer : public IOcclusionQueryRenderer
    {
    public:
        /**
         * @brief Creates an FNA3D query object.
         * @param deviceState Shared owner identity and liveness state for the query.
         */
        explicit Fna3dOcclusionQueryRenderer(std::shared_ptr<Fna3dDeviceState> deviceState);
        ~Fna3dOcclusionQueryRenderer() override;

        Fna3dOcclusionQueryRenderer(const Fna3dOcclusionQueryRenderer&) = delete;
        Fna3dOcclusionQueryRenderer& operator=(const Fna3dOcclusionQueryRenderer&) = delete;

        /** @brief Starts counting samples that pass the depth/stencil test. */
        void Begin() override;
        /** @brief Stops counting. */
        void End() override;
        /** @brief Whether the result is available without stalling. */
        [[nodiscard]] bool IsComplete() const override;
        /** @brief The number of samples counted between Begin() and End(). */
        [[nodiscard]] int PixelCount() const override;

    private:
        std::shared_ptr<Fna3dDeviceState> deviceState_;
        FNA3D_Query* query_ = nullptr;
    };

    /**
     * @brief The 2D sprite path, drawn through XNA's own `SpriteEffect`.
     *
     * Sprites are accumulated into a CPU staging array as `VertexPositionColorTexture` quads and
     * flushed in runs that share a texture, exactly like XNA's own batching. Each flush uploads one
     * dynamic vertex buffer, binds the sprite effect with an orthographic `MatrixTransform`, and
     * issues one indexed triangle-list draw.
     */
    class Fna3dSpriteBatchRenderer : public ISpriteBatchRenderer
    {
    public:
        /**
         * @brief Binds this sprite batch to its renderer.
         * @param deviceState Shared owner identity and liveness state; routes live submissions.
         */
        explicit Fna3dSpriteBatchRenderer(std::shared_ptr<Fna3dDeviceState> deviceState);
        ~Fna3dSpriteBatchRenderer() override;

        Fna3dSpriteBatchRenderer(const Fna3dSpriteBatchRenderer&) = delete;
        Fna3dSpriteBatchRenderer& operator=(const Fna3dSpriteBatchRenderer&) = delete;

        /** @brief Starts a batch, clearing any queued sprites. */
        void Begin() override;
        /** @brief Flushes every queued sprite. */
        void End() override;

        /**
         * @brief Sets the transform applied on top of the 2D orthographic projection.
         * @param m Transform matrix.
         */
        void SetTransformMatrix(const Matrix& m) override;

        /**
         * @brief Sets the sampler filter used for subsequent draws.
         * @param textureFilter Raw XNA `TextureFilter` ordinal.
         */
        void SetSamplerFilter(int textureFilter) override;

        /**
         * @brief Sets the sampler address mode used for subsequent draws.
         * @param addressU Raw XNA `TextureAddressMode` ordinal for U.
         * @param addressV Raw XNA `TextureAddressMode` ordinal for V.
         */
        void SetSamplerAddressMode(int addressU, int addressV) override;

        /**
         * @brief Flushes per-draw while `SpriteSortMode::Immediate` is active.
         * @param immediate Whether the current batch is Immediate.
         */
        void SetImmediateMode(bool immediate) override;

        /**
         * @brief Queues a sprite at its natural size.
         * @param texture Source texture.
         * @param x       Destination left edge, in logical pixels.
         * @param y       Destination top edge, in logical pixels.
         */
        void Draw(const ITextureRenderer& texture, float x, float y) override;

        /**
         * @brief Queues a tinted sprite from a source rectangle.
         * @param texture              Source texture.
         * @param destinationRectangle Destination rectangle, in logical pixels.
         * @param sourceRectangle      Source rectangle, in texels.
         * @param color                Tint colour.
         */
        void Draw(const ITextureRenderer& texture, const Rectangle& destinationRectangle,
                  const Rectangle& sourceRectangle, const Color& color) override;

        /**
         * @brief Queues a tinted, rotated, flipped sprite from a source rectangle.
         * @param texture              Source texture.
         * @param destinationRectangle Destination rectangle, in logical pixels.
         * @param sourceRectangle      Source rectangle, in texels.
         * @param color                Tint colour.
         * @param rotation             Rotation in radians about @p origin.
         * @param origin               Rotation origin, in source-rectangle texels.
         * @param effects              Horizontal/vertical flip flags.
         * @param layerDepth           Depth in [0,1]; written to the vertex Z.
         */
        void Draw(const ITextureRenderer& texture, const Rectangle& destinationRectangle,
                  const Rectangle& sourceRectangle, const Color& color, float rotation,
                  const Vector2& origin, SpriteEffects effects, float layerDepth) override;

    private:
        struct SpriteVertex
        {
            float x, y, z;
            std::uint32_t color;
            float u, v;
        };

        void Flush();
        void Queue(const ITextureRenderer& texture, const Rectangle& destinationRectangle,
                   const Rectangle& sourceRectangle, const Color& color, float rotation,
                   const Vector2& origin, SpriteEffects effects, float layerDepth);

        std::shared_ptr<Fna3dDeviceState> deviceState_;
        std::vector<SpriteVertex> vertices_;
        const ITextureRenderer* batchTexture_ = nullptr;
        Matrix transform_;
        int samplerFilter_ = 0;
        int addressU_ = 1;
        int addressV_ = 1;
        bool immediate_ = false;
    };

    /**
     * @brief CNA's FNA3D graphics renderer.
     *
     * FNA3D is the XNA-shaped C graphics library FNA itself renders through: its device API is a
     * near one-to-one match for `IGraphicsRenderer`, and every enumeration it exposes is the XNA
     * 4.0 enumeration CNA already ports. Like LLGL, Diligent, sokol and bgfx it is a portable
     * middleware layer rather than one native API -- it selects SDL_GPU, Direct3D 11 or OpenGL at
     * runtime, overridable with the `FNA3D_FORCE_DRIVER` SDL hint.
     *
     * Shaders are the one place FNA3D constrains its consumer: `FNA3D_CreateEffect` accepting a
     * compiled Direct3D 9 Effect Framework binary is the library's *only* shader entry point, so
     * this renderer draws through the vendored XNA stock effects (see `effects/README.md`) and
     * reports `GraphicsCapability::CustomEffects` as false rather than pretending it can compile a
     * caller's GLSL/HLSL source string.
     */
    class Fna3dRenderer : public IGraphicsRenderer
    {
    public:
        /**
         * @brief Creates the FNA3D device on the SDL window CNA already owns.
         * @param args Renderer creation arguments from `GraphicsDevice`.
         * @throws std::runtime_error if no FNA3D driver could create a device.
         */
        explicit Fna3dRenderer(const GraphicsRendererCreateArgs& args);
        ~Fna3dRenderer() override;

        Fna3dRenderer(const Fna3dRenderer&) = delete;
        Fna3dRenderer& operator=(const Fna3dRenderer&) = delete;

        // ---- Presentation ----

        /**
         * @brief Clears the colour buffer.
         * @param r,g,b,a Clear colour in 0..1.
         */
        void Clear(float r, float g, float b, float a) override;
        /** @brief Presents the back buffer, scaled per the active presentation mode. */
        void Present() override;

        /**
         * @brief Reports the logical (virtual) size game code sees.
         * @param width  Filled with the logical width.
         * @param height Filled with the logical height.
         */
        void GetViewportSize(int& width, int& height) override;

        /**
         * @brief Reports the physical window rectangle the back buffer is presented into.
         * @param x      Filled with the left edge in window pixels.
         * @param y      Filled with the top edge in window pixels.
         * @param width  Filled with the width in window pixels.
         * @param height Filled with the height in window pixels.
         */
        void GetDefaultViewportRect(int& x, int& y, int& width, int& height) override;

        /**
         * @brief Resizes the back buffer to a new logical resolution.
         * @param width  New logical width.
         * @param height New logical height.
         */
        void SetVirtualResolution(int width, int height) override;

        /**
         * @brief Selects the presentation/scaling policy.
         * @param mode Raw `CnaPresentationMode` ordinal.
         */
        void SetPresentationMode(int mode) override;

        /**
         * @brief Updates the swap interval.
         * @param interval 0 = immediate, 1 = VSync, 2 = half rate.
         */
        void SetSwapInterval(int interval) override;

        /**
         * @brief Reconfigures the back buffer's MSAA sample count in place.
         * @param requestedMultiSampleCount Requested sample count.
         * @return The device-clamped sample count actually applied.
         */
        int ApplyMultiSampleCount(int requestedMultiSampleCount) override;

        /**
         * @brief Reconfigures the tracked back-buffer/depth formats and fullscreen state.
         * @param backBufferFormat   Raw `SurfaceFormat` ordinal.
         * @param depthStencilFormat Raw `DepthFormat` ordinal.
         * @param isFullScreen       Whether exclusive fullscreen was requested.
         */
        void UpdatePresentationFormatEXT(int backBufferFormat, int depthStencilFormat,
                                         bool isFullScreen) override;

        /**
         * @brief Reports the back-buffer format FNA3D actually created.
         * @param requestedFormat Requested `SurfaceFormat` ordinal.
         * @return The applied ordinal.
         */
        [[nodiscard]] int GetAppliedBackBufferFormatEXT(int requestedFormat) const override;

        /**
         * @brief Reports the depth/stencil format FNA3D actually created.
         * @param requestedFormat Requested `DepthFormat` ordinal.
         * @return The applied ordinal.
         */
        [[nodiscard]] int GetAppliedDepthStencilFormatEXT(int requestedFormat) const override;

        /** @brief The back buffer's device-clamped MSAA sample count (0 when none). */
        [[nodiscard]] int GetMultiSampleCount() const override;

        /**
         * @brief Maps a window-space point to logical game coordinates.
         * @param windowX Window-space X.
         * @param windowY Window-space Y.
         * @param logX    Filled with the logical X.
         * @param logY    Filled with the logical Y.
         * @return True on success.
         */
        [[nodiscard]] bool TransformWindowToLogical(float windowX, float windowY, float& logX,
                                                    float& logY) const override;

        /**
         * @brief Maps a logical game point back to window space.
         * @param logX    Logical X.
         * @param logY    Logical Y.
         * @param windowX Filled with the window-space X.
         * @param windowY Filled with the window-space Y.
         * @return True on success.
         */
        [[nodiscard]] bool TransformLogicalToWindow(float logX, float logY, float& windowX,
                                                    float& windowY) const override;

        /** @brief The SDL window FNA3D presents into. */
        [[nodiscard]] SDL_Window* GetWindowInternal() const override { return window_; }
        /** @brief Always null: FNA3D owns no SDL_Renderer. */
        [[nodiscard]] SDL_Renderer* GetRendererInternal() const override { return nullptr; }

        /**
         * @brief Reads the back buffer's rendered pixels for a region.
         * @param x      Left edge in logical pixels.
         * @param y      Top edge in logical pixels.
         * @param w      Region width.
         * @param h      Region height.
         * @param pixels Destination for w*h*4 RGBA8 bytes, top row first.
         */
        void ReadBackbuffer(int x, int y, int w, int h, std::uint8_t* pixels) override;

        // ---- Resources ----

        /**
         * @brief Creates a sampled 2D texture and uploads its level-0 pixels.
         * @param data Decoded image data.
         * @return The renderer texture handle.
         */
        std::unique_ptr<ITextureRenderer> CreateTexture(const ImageData& data) override;

        /** @brief Creates the 2D sprite path. */
        std::unique_ptr<ISpriteBatchRenderer> CreateSpriteBatch() override;

        /** @brief Creates a hardware occlusion query. */
        std::unique_ptr<IOcclusionQueryRenderer> CreateOcclusionQuery() override;

        /**
         * @brief Creates a volume texture.
         * @param w             Width in voxels.
         * @param h             Height in voxels.
         * @param depth         Depth in voxels.
         * @param mipMap        Whether a full mip chain is allocated.
         * @param surfaceFormat Raw `SurfaceFormat` ordinal.
         * @return The renderer handle.
         */
        std::unique_ptr<ITexture3DRenderer> CreateTexture3D(int w, int h, int depth, bool mipMap,
                                                            int surfaceFormat) override;

        /**
         * @brief Creates a cube map.
         * @param size          Edge length of one face in texels.
         * @param mipMap        Whether a full mip chain is allocated.
         * @param surfaceFormat Raw `SurfaceFormat` ordinal.
         * @return The renderer handle.
         */
        std::unique_ptr<ITextureCubeRenderer> CreateTextureCube(int size, bool mipMap,
                                                                int surfaceFormat) override;

        /**
         * @brief Creates a 2D render target with the default `Color` surface format.
         * @param w                Width in pixels.
         * @param h                Height in pixels.
         * @param depthFormat      Raw `DepthFormat` ordinal.
         * @param preserveContents Whether rendered colour survives a re-bind.
         * @param mipMap           Whether a full mip chain is allocated and generated.
         * @param multiSampleCount Requested MSAA sample count.
         * @return The renderer handle.
         */
        std::unique_ptr<IRenderTargetRenderer> CreateRenderTarget2D(
            int w, int h, int depthFormat, bool preserveContents, bool mipMap,
            int multiSampleCount) override;

        /**
         * @brief Creates a 2D render target with an explicit surface format.
         * @param w                Width in pixels.
         * @param h                Height in pixels.
         * @param depthFormat      Raw `DepthFormat` ordinal.
         * @param preserveContents Whether rendered colour survives a re-bind.
         * @param mipMap           Whether a full mip chain is allocated and generated.
         * @param multiSampleCount Requested MSAA sample count.
         * @param surfaceFormat    Raw `SurfaceFormat` ordinal.
         * @return The renderer handle.
         */
        std::unique_ptr<IRenderTargetRenderer> CreateRenderTarget2DEXT(
            int w, int h, int depthFormat, bool preserveContents, bool mipMap,
            int multiSampleCount, int surfaceFormat) override;

        /**
         * @brief Creates a cube-map render target.
         * @param size             Edge length of one face in pixels.
         * @param depthFormat      Raw `DepthFormat` ordinal.
         * @param preserveContents Whether rendered colour survives a re-bind.
         * @param mipMap           Whether a full mip chain is allocated and generated.
         * @param multiSampleCount Requested MSAA sample count.
         * @return The renderer handle.
         */
        std::unique_ptr<IRenderTargetCubeRenderer> CreateRenderTargetCube(
            int size, int depthFormat, bool preserveContents, bool mipMap,
            int multiSampleCount) override;

        /**
         * @brief Refuses a source-string shader program.
         *
         * FNA3D's only shader entry point takes a compiled Direct3D 9 Effect binary; there is no
         * call anywhere in `FNA3D.h` that compiles GLSL or HLSL source. Returning null is the
         * interface's own "this renderer does not support programmable shaders" answer and pairs
         * with `SupportsCapability(CustomEffects) == false`.
         *
         * @param vertSrc Ignored.
         * @param fragSrc Ignored.
         * @return Always null.
         */
        std::unique_ptr<IEffectRenderer> CreateEffectRenderer(const std::string& vertSrc,
                                                              const std::string& fragSrc) override;

        /**
         * @brief Binds a single 2D render target, or the back buffer when null.
         * @param rt Target to bind, or null.
         */
        void SetRenderTarget2D(IRenderTargetRenderer* rt) override;

        /**
         * @brief Binds a normalized ordered render-target set.
         * @param renderTargets Descriptors in slot order, or null.
         * @param count         Number of descriptors; 0 restores the back buffer.
         */
        void SetRenderTargets(const RenderTargetBindingDescriptor* renderTargets,
                              int count) override;

        // ---- Graphics state ----

        /**
         * @brief Applies a BlendState.
         * @param colorSrcBlend  Raw `Blend` ordinal for the colour source factor.
         * @param alphaSrcBlend  Raw `Blend` ordinal for the alpha source factor.
         * @param colorDstBlend  Raw `Blend` ordinal for the colour destination factor.
         * @param alphaDstBlend  Raw `Blend` ordinal for the alpha destination factor.
         * @param colorBlendFunc Raw `BlendFunction` ordinal for colour.
         * @param alphaBlendFunc Raw `BlendFunction` ordinal for alpha.
         * @param writeState     Per-MRT colour write masks and the coverage sample mask.
         */
        void ApplyBlendState(int colorSrcBlend, int alphaSrcBlend, int colorDstBlend,
                             int alphaDstBlend, int colorBlendFunc, int alphaBlendFunc,
                             const BlendWriteState& writeState) override;

        /**
         * @brief Applies a DepthStencilState.
         * @param depthEnable         Whether depth testing is enabled.
         * @param depthWriteEnable    Whether depth writes are enabled.
         * @param depthFunc           Raw `CompareFunction` ordinal for depth.
         * @param stencilEnable       Whether stencil testing is enabled.
         * @param stencilFunc         Raw `CompareFunction` ordinal for stencil.
         * @param stencilPass         Raw `StencilOperation` ordinal on pass.
         * @param stencilFail         Raw `StencilOperation` ordinal on fail.
         * @param stencilDepthFail    Raw `StencilOperation` ordinal on depth fail.
         * @param stencilMask         Stencil read mask.
         * @param stencilWriteMask    Stencil write mask.
         * @param referenceStencil    Stencil reference value.
         * @param twoSidedStencilMode Whether back faces use the CCW operations.
         * @param ccwStencilFunc      Raw `CompareFunction` ordinal for back faces.
         * @param ccwStencilPass      Raw `StencilOperation` ordinal on back-face pass.
         * @param ccwStencilFail      Raw `StencilOperation` ordinal on back-face fail.
         * @param ccwStencilDepthFail Raw `StencilOperation` ordinal on back-face depth fail.
         */
        void ApplyDepthStencilState(bool depthEnable, bool depthWriteEnable, int depthFunc,
                                    bool stencilEnable, int stencilFunc, int stencilPass,
                                    int stencilFail, int stencilDepthFail, int stencilMask,
                                    int stencilWriteMask, int referenceStencil,
                                    bool twoSidedStencilMode, int ccwStencilFunc,
                                    int ccwStencilPass, int ccwStencilFail,
                                    int ccwStencilDepthFail) override;

        /**
         * @brief Applies a RasterizerState.
         * @param cullMode            Raw `CullMode` ordinal.
         * @param fillMode            Raw `FillMode` ordinal.
         * @param scissorTestEnable   Whether the scissor test is enabled.
         * @param depthBias           Constant depth bias.
         * @param slopeScaleDepthBias Slope-scaled depth bias.
         */
        void ApplyRasterizerState(int cullMode, int fillMode, bool scissorTestEnable,
                                  float depthBias, float slopeScaleDepthBias) override;

        /**
         * @brief Applies a SamplerState to one texture slot.
         * @param slot          Texture unit index.
         * @param filter        Raw `TextureFilter` ordinal.
         * @param addressU      Raw `TextureAddressMode` ordinal for U.
         * @param addressV      Raw `TextureAddressMode` ordinal for V.
         * @param maxAnisotropy Maximum anisotropy level.
         */
        void ApplySamplerState(int slot, int filter, int addressU, int addressV,
                               int maxAnisotropy) override;

        /**
         * @brief Applies the sampler controls not carried by filter/address mode.
         * @param slot        Texture unit index.
         * @param maxMipLevel Highest resolution mip level the sampler may use.
         * @param lodBias     Mip level-of-detail bias.
         */
        void ApplySamplerMipState(int slot, int maxMipLevel, float lodBias) override;

        /**
         * @brief Sets the constant blend colour used by the BlendFactor blend modes.
         * @param r,g,b,a Colour components in 0..1.
         */
        void SetBlendFactor(float r, float g, float b, float a) override;

        /**
         * @brief Sets the device's standalone stencil reference value.
         * @param value New reference value.
         */
        void SetReferenceStencil(int value) override;

        /**
         * @brief Sets the scissor clip rectangle.
         * @param x,y,w,h Rectangle in logical pixels.
         */
        void SetScissorRect(int x, int y, int w, int h) override;

        /**
         * @brief Sets the GPU viewport rectangle and depth range.
         * @param x,y,w,h  Rectangle in logical pixels.
         * @param minDepth Near depth-range bound.
         * @param maxDepth Far depth-range bound.
         */
        void SetViewport(int x, int y, int w, int h, float minDepth, float maxDepth) override;

        // ---- Clears ----

        /**
         * @brief Clears colour and depth.
         * @param r,g,b,a Clear colour in 0..1.
         * @param depth   Depth value.
         */
        void ClearColorAndDepth(float r, float g, float b, float a, float depth) override;

        /**
         * @brief Clears depth only.
         * @param depth Depth value.
         */
        void ClearDepth(float depth) override;

        /**
         * @brief Clears stencil only.
         * @param stencil Stencil value.
         */
        void ClearStencil(int stencil) override;

        /**
         * @brief Clears depth and stencil.
         * @param depth   Depth value.
         * @param stencil Stencil value.
         */
        void ClearDepthAndStencil(float depth, int stencil) override;

        /**
         * @brief Clears colour and stencil.
         * @param r,g,b,a Clear colour in 0..1.
         * @param stencil Stencil value.
         */
        void ClearColorAndStencil(float r, float g, float b, float a, int stencil) override;

        /**
         * @brief Clears colour, depth and stencil.
         * @param r,g,b,a Clear colour in 0..1.
         * @param depth   Depth value.
         * @param stencil Stencil value.
         */
        void ClearColorDepthAndStencil(float r, float g, float b, float a, float depth,
                                       int stencil) override;

        // ---- 3D pipeline ----

        /**
         * @brief Enables or disables depth testing, preserving the rest of the depth/stencil state.
         * @param enabled Whether depth testing is enabled.
         */
        void SetDepthTestEnabled(bool enabled) override;

        /**
         * @brief Enables or disables blending.
         * @param enabled Whether blending is enabled.
         */
        void SetBlendEnabled(bool enabled) override;

        /**
         * @brief Enables or disables depth writes.
         * @param enabled Whether depth writes are enabled.
         */
        void SetDepthWriteEnabled(bool enabled) override;

        /**
         * @brief Creates a vertex buffer.
         * @param vertex_capacity Number of vertices the buffer must hold.
         * @return The renderer handle.
         */
        std::unique_ptr<IVertexBufferRenderer> CreateVertexBuffer(int vertex_capacity) override;

        /**
         * @brief Creates a 16-bit index buffer.
         * @param index_capacity Number of indices the buffer must hold.
         * @return The renderer handle.
         */
        std::unique_ptr<IIndexBufferRenderer> CreateIndexBuffer16(int index_capacity) override;

        /**
         * @brief Creates a 32-bit index buffer.
         * @param index_capacity Number of indices the buffer must hold.
         * @return The renderer handle.
         */
        std::unique_ptr<IIndexBufferRenderer> CreateIndexBuffer32(int index_capacity) override;

        /**
         * @brief Draws vertex-coloured primitives through BasicEffect.
         * @param vb             Vertex buffer to read from.
         * @param world          World matrix.
         * @param view           View matrix.
         * @param projection     Projection matrix.
         * @param primitive      Primitive topology.
         * @param primitiveCount Number of primitives.
         */
        void DrawColoredPrimitives(const IVertexBufferRenderer& vb, const Matrix& world,
                                   const Matrix& view, const Matrix& projection,
                                   PrimitiveType primitive, int primitiveCount) override;

        /**
         * @brief Indexed counterpart of DrawColoredPrimitives.
         * @param vb             Vertex buffer to read from.
         * @param ib             Index buffer to read from.
         * @param world          World matrix.
         * @param view           View matrix.
         * @param projection     Projection matrix.
         * @param primitive      Primitive topology.
         * @param primitiveCount Number of primitives.
         */
        void DrawIndexedColoredPrimitives(const IVertexBufferRenderer& vb,
                                          const IIndexBufferRenderer& ib, const Matrix& world,
                                          const Matrix& view, const Matrix& projection,
                                          PrimitiveType primitive, int primitiveCount) override;

        /**
         * @brief Effect-aware non-indexed draw.
         * @param vb             Stream 0's vertex buffer.
         * @param world          World matrix.
         * @param view           View matrix.
         * @param projection     Projection matrix.
         * @param primitive      Primitive topology.
         * @param primitiveCount Number of primitives.
         * @param params         Flattened effect parameters and bound vertex streams.
         */
        void DrawPrimitivesEx(const IVertexBufferRenderer& vb, const Matrix& world,
                              const Matrix& view, const Matrix& projection,
                              PrimitiveType primitive, int primitiveCount,
                              const GpuDrawParams& params) override;

        /**
         * @brief Effect-aware indexed draw.
         * @param vb             Stream 0's vertex buffer.
         * @param ib             Index buffer.
         * @param world          World matrix.
         * @param view           View matrix.
         * @param projection     Projection matrix.
         * @param primitive      Primitive topology.
         * @param primitiveCount Number of primitives.
         * @param params         Flattened effect parameters and bound vertex streams.
         */
        void DrawIndexedPrimitivesEx(const IVertexBufferRenderer& vb,
                                     const IIndexBufferRenderer& ib, const Matrix& world,
                                     const Matrix& view, const Matrix& projection,
                                     PrimitiveType primitive, int primitiveCount,
                                     const GpuDrawParams& params) override;

        /**
         * @brief Effect-aware instanced indexed draw.
         * @param vb             Stream 0's vertex buffer.
         * @param ib             Index buffer.
         * @param world          World matrix.
         * @param view           View matrix.
         * @param projection     Projection matrix.
         * @param primitive      Primitive topology.
         * @param primitiveCount Number of primitives per instance.
         * @param instanceCount  Number of instances.
         * @param params         Flattened effect parameters and bound vertex streams.
         */
        void DrawInstancedPrimitivesEx(const IVertexBufferRenderer& vb,
                                       const IIndexBufferRenderer& ib, const Matrix& world,
                                       const Matrix& view, const Matrix& projection,
                                       PrimitiveType primitive, int primitiveCount,
                                       int instanceCount, const GpuDrawParams& params) override;

        /**
         * @brief Reports which CNA graphics capabilities this renderer genuinely has.
         * @param capability The capability being queried.
         * @return True only when FNA3D and the selected runtime driver actually provide it.
         */
        [[nodiscard]] bool SupportsCapability(CNA::GraphicsCapability capability) const override;

        /** @brief The largest single-axis texture dimension FNA3D guarantees. */
        [[nodiscard]] int GetMaxTextureDimension() const override;

        /**
         * @brief Inserts a named debug label into the FNA3D command stream.
         * @param marker Label text.
         */
        void SetStringMarkerEXT(const char* marker) override;

        // ---- CNAEXT: renderer-internal services used by its own resource classes ----

        /** @brief CNAEXT. The FNA3D device this renderer owns. */
        [[nodiscard]] FNA3D_Device* GetDeviceEXT() const { return device_; }

        /** @brief CNAEXT. Shared identity/liveness token for resources owned by this device. */
        [[nodiscard]] const std::shared_ptr<Fna3dDeviceState>& GetDeviceStateEXT() const
        {
            return deviceState_;
        }

        /** @brief CNAEXT. The presentation geometry currently in effect. */
        [[nodiscard]] const PresentationLayout& GetPresentationLayoutEXT() const
        {
            return layout_;
        }

        /**
         * @brief CNAEXT. Draws one queued sprite run through the stock SpriteEffect.
         * @param texture     FNA3D texture bound to sampler 0.
         * @param vertices    Interleaved position/colour/texcoord vertices, 24 bytes each.
         * @param vertexCount Number of vertices; must be a multiple of four.
         * @param transform   Transform applied on top of the orthographic projection.
         * @param filter      Raw `TextureFilter` ordinal.
         * @param addressU    Raw `TextureAddressMode` ordinal for U.
         * @param addressV    Raw `TextureAddressMode` ordinal for V.
         */
        void DrawSpriteRunEXT(FNA3D_Texture* texture, const void* vertices, int vertexCount,
                              const Matrix& transform, int filter, int addressU, int addressV);

        /**
         * @brief CNAEXT. Re-applies the currently bound render-target set to FNA3D.
         *
         * Used by the render-target classes after a resolve, and by the device-state paths that
         * must restore the draw destination without going through `SetRenderTargets` again.
         */
        void RebindCurrentTargetsEXT();

        /** @brief CNAEXT. Restores the back buffer, resolving any bound targets first. */
        void UnbindTargetsEXT();

        /**
         * @brief CNAEXT. Refuses a texture surface format the running FNA3D driver cannot store.
         *
         * FNA3D reports compressed-format support per family (`FNA3D_SupportsDXT1`,
         * `FNA3D_SupportsS3TC`, `FNA3D_SupportsBC7`). Creating a texture the driver has no format
         * for would leave a resource that samples as garbage, so the request is refused by name
         * instead.
         *
         * @param surfaceFormat Raw XNA `SurfaceFormat` ordinal.
         * @param what          Resource kind named in the exception, e.g. "Texture2D".
         * @throws std::runtime_error when the driver does not support that format.
         */
        void RequireTextureFormatSupportedEXT(int surfaceFormat, const char* what) const;

        /**
         * @brief CNAEXT. Refuses an sRGB render-target format on a driver without sRGB targets.
         *
         * @param surfaceFormat Raw XNA `SurfaceFormat` ordinal.
         * @throws std::runtime_error when an sRGB target was asked of a driver that has none.
         */
        void RequireRenderTargetFormatSupportedEXT(int surfaceFormat) const;

        /** @brief CNAEXT. Fragment-shader texture slots the running driver actually exposes. */
        [[nodiscard]] int GetMaxTextureSlotsEXT() const { return maxTextureSlots_; }

        /** @brief CNAEXT. Vertex-shader texture slots the running driver actually exposes. */
        [[nodiscard]] int GetMaxVertexTextureSlotsEXT() const { return maxVertexTextureSlots_; }

        /**
         * @brief CNAEXT. Whether the running driver can store this block-compressed format.
         * @param surfaceFormat Raw XNA `SurfaceFormat` ordinal.
         * @return True when the format is uncompressed or its compressed family is supported.
         */
        [[nodiscard]] bool SupportsTextureFormatEXT(int surfaceFormat) const;

        /**
         * @brief CNAEXT. Whether the running driver reads block-compressed textures back.
         *
         * Measured once at device creation, because FNA3D's drivers disagree (its OpenGL and
         * Direct3D 11 drivers refuse compressed `GetTextureData2D` outright, its SDL_GPU driver
         * forwards it) and expose no query. `GetData` on a compressed texture reports that it read
         * nothing when this is false, rather than returning a buffer the driver never wrote.
         *
         * @return True when a compressed readback genuinely returns the stored blocks.
         */
        [[nodiscard]] bool SupportsCompressedReadbackEXT() const
        {
            return compressedReadbackSupported_;
        }

        /**
         * @brief CNAEXT. Whether this driver returned an exact probe result for volume readback.
         *
         * When false, each `Fna3dTexture3DRenderer` uses its checked upload mirror instead of
         * asking the native driver to write an uninitialised output buffer.
         */
        [[nodiscard]] bool SupportsTexture3DReadbackEXT() const
        {
            return texture3DReadbackSupported_;
        }

        /**
         * @brief CNAEXT. Selects and applies the stock effect a draw's parameters describe.
         * @param world      World matrix.
         * @param view       View matrix.
         * @param projection Projection matrix.
         * @param params     Flattened effect parameters.
         */
        void ApplyStockEffectEXT(const Matrix& world, const Matrix& view, const Matrix& projection,
                                 const GpuDrawParams& params);

        /**
         * @brief CNAEXT. Binds every declared vertex stream for the draw about to be submitted.
         * @param vb         Stream 0's vertex buffer.
         * @param params     Flattened effect parameters carrying the bound streams.
         * @param baseVertex Base-vertex term FNA3D applies to every per-vertex stream.
         */
        void ApplyVertexBindingsEXT(const IVertexBufferRenderer& vb, const GpuDrawParams& params,
                                    int baseVertex);

        /**
         * @brief CNAEXT. Applies the effect and the vertex bindings, in that order.
         * @param vb         Stream 0's vertex buffer.
         * @param world      World matrix.
         * @param view       View matrix.
         * @param projection Projection matrix.
         * @param params     Flattened effect parameters.
         * @param baseVertex Base-vertex term FNA3D applies to every per-vertex stream.
         */
        void PrepareDrawEXT(const IVertexBufferRenderer& vb, const Matrix& world,
                            const Matrix& view, const Matrix& projection,
                            const GpuDrawParams& params, int baseVertex);

        /**
         * @brief CNAEXT. Binds a texture and its sampler to one slot.
         * @param slot    Sampler slot index.
         * @param texture Texture to bind, or null to unbind.
         */
        void BindEffectTextureEXT(int slot, const ITextureRenderer* texture);

        /**
         * @brief CNAEXT. Writes the shared lighting/material parameters of a lit stock effect.
         * @param effect       Effect being configured.
         * @param world        World matrix.
         * @param view         View matrix.
         * @param params       Flattened effect parameters.
         * @param withSpecular Whether this effect declares the specular parameters.
         */
        void ApplyLightingParamsEXT(Fna3dStockEffect& effect, const Matrix& world,
                                    const Matrix& view, const GpuDrawParams& params,
                                    bool withSpecular);

    private:
        struct BoundTarget
        {
            FNA3D_RenderTargetBinding binding{};
            bool needsResolve = false;
        };

        void ApplyCurrentBlendState();
        void ApplyCurrentDepthStencilState();
        void ClearInternal(FNA3D_ClearOptions options, const FNA3D_Vec4* color, float depth,
                           int stencil);
        void ProbeTexture3DReadbackSupport();
        void ProbeCompressedReadbackSupport();
        void QueryDriverLimits();
        void ResetBackbuffer();
        void RecomputeLayout();
        [[nodiscard]] int ClampMultiSampleCount(int requested, int format) const;

        SDL_Window* window_ = nullptr;
        std::shared_ptr<Fna3dDeviceState> deviceState_ = std::make_shared<Fna3dDeviceState>();
        FNA3D_Device* device_ = nullptr;
        FNA3D_PresentationParameters presentation_{};
        PresentationLayout layout_{};
        CnaPresentationMode presentationMode_ = CnaPresentationMode::FixedHeightDynamicWidth;

        FNA3D_BlendState blendState_{};
        FNA3D_DepthStencilState depthStencilState_{};
        FNA3D_RasterizerState rasterizerState_{};
        std::array<FNA3D_SamplerState, 16> samplerStates_{};

        std::array<Fna3dStockEffect, static_cast<std::size_t>(StockEffectKind::Count)> effects_{};

        std::vector<BoundTarget> boundTargets_;
        FNA3D_Renderbuffer* boundDepthBuffer_ = nullptr;
        FNA3D_DepthFormat boundDepthFormat_ = FNA3D_DEPTHFORMAT_NONE;
        bool boundPreserveContents_ = false;

        std::unique_ptr<Fna3dVertexBufferRenderer> spriteVertexBuffer_;
        std::unique_ptr<Fna3dIndexBufferRenderer> spriteIndexBuffer_;
        int spriteIndexQuadCapacity_ = 0;

        std::vector<std::uint8_t> readbackScratch_;

        /// Measured once at device creation: whether the selected FNA3D driver implements
        /// GetTextureData3D at all. See Fna3dTexture3DRenderer's own mirror.
        bool texture3DReadbackSupported_ = true;

        /// Measured once at device creation: whether the selected FNA3D driver implements
        /// GetTextureData2D for block-compressed formats. Unlike the volume case there is no
        /// mirror behind it -- a false here makes GetData answer "read nothing".
        bool compressedReadbackSupported_ = false;

        /// Driver-reported format and slot limits, queried once at device creation. Every one of
        /// them exists so a request the driver cannot serve is refused by name rather than
        /// accepted into a resource that would sample as garbage.
        bool supportsDxt1_ = false;
        bool supportsS3tc_ = false;
        bool supportsBc7_ = false;
        bool supportsSrgbRenderTargets_ = false;
        int maxTextureSlots_ = 0;
        int maxVertexTextureSlots_ = 0;
    };
}
