// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/CNAHelper.hpp"
#include "../Common/IGraphicsBackend.hpp"

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

struct SDL_Window;

namespace CNA::Internal::Backends::Sokol
{
    class SokolGraphicsBackend;

    /**
     * @brief Identifies the native API sokol_gfx was compiled to dispatch onto. NOXNA.
     *
     * Mirrors the CNA_SOKOL_API CMake option (cmake/BackendSelection.cmake) so a caller can read
     * back which API this build actually resolved to without including sokol_gfx.h itself.
     */
    enum class SokolApiEXT : std::uint8_t
    {
        /** @brief Desktop OpenGL 4.1 core (SOKOL_GLCORE). */
        GLCore,
        /** @brief OpenGL ES 3 / WebGL 2 (SOKOL_GLES3). */
        GLES3,
        /** @brief Direct3D 11 (SOKOL_D3D11). */
        D3D11,
        /** @brief Metal (SOKOL_METAL). */
        Metal,
        /** @brief WebGPU (SOKOL_WGPU). */
        WebGPU
    };

    /**
     * @brief Backend handle for a 2D texture, backed by a sokol_gfx image plus its texture view.
     *
     * sokol_gfx has no partial-image upload and permits at most one sg_update_image() per image
     * per frame, which a caller writing several mip levels in one frame violates immediately. This
     * class therefore keeps a CPU shadow of every mip level and recreates the whole image as an
     * immutable one whenever any level changes -- creation with initial data carries no per-frame
     * restriction, the same reasoning SokolVertexBufferBackend::SetData uses.
     */
    class SokolTextureBackend : public ITextureBackend
    {
    public:
        /**
         * @brief Creates a sokol_gfx image from decoded RGBA8 pixels and a matching texture view.
         *
         * @param data Source image; data.mipLevels mip levels are allocated, level 0 is uploaded
         *             from data.pixels and every further level starts out transparent black until
         *             UpdatePixelsLevel() supplies it.
         */
        explicit SokolTextureBackend(const ImageData& data);

        /** @brief Destroys the sokol_gfx view and image owned by this texture. */
        ~SokolTextureBackend() override;

        SokolTextureBackend(const SokolTextureBackend&) = delete;
        SokolTextureBackend& operator=(const SokolTextureBackend&) = delete;

        /**
         * @brief Returns the width of mip level 0 in pixels.
         * @return Texture width.
         */
        [[nodiscard]] int GetWidth() const override;

        /**
         * @brief Returns the height of mip level 0 in pixels.
         * @return Texture height.
         */
        [[nodiscard]] int GetHeight() const override;

        /**
         * @brief Returns null; this backend renders through sokol_gfx, not SDL_Renderer.
         * @return Always nullptr.
         */
        [[nodiscard]] SDL_Texture* GetNativeTexture() const override;

        /**
         * @brief Replaces mip level 0 in place.
         *
         * @param rgba   Source pixels, RGBA8.
         * @param stride Row pitch in bytes; rows are repacked when it exceeds width * 4.
         */
        void UpdatePixels(const uint8_t* rgba, int stride) override;

        /**
         * @brief Replaces a single mip level in place and re-uploads the whole image.
         *
         * @param level  Mip level to write.
         * @param rgba   Source pixels, RGBA8, tightly packed.
         * @param levelW Width of that mip level in pixels.
         * @param levelH Height of that mip level in pixels.
         */
        void UpdatePixelsLevel(int level, const uint8_t* rgba, int levelW, int levelH) override;

        /**
         * @brief Reads back raw RGBA8 pixels from the CPU shadow of the given mip level.
         *
         * Satisfies the ITextureBackend::GetData contract from the shadow rather than the GPU:
         * this backend has no render targets yet, so every texture's content came from a
         * SetData() this class already recorded.
         *
         * @param level      Mip level to read.
         * @param x          Left edge of the requested region, in pixels.
         * @param y          Top edge of the requested region, in pixels.
         * @param w          Width of the requested region, in pixels.
         * @param h          Height of the requested region, in pixels.
         * @param data       Destination for tightly packed RGBA8 rows, top row first.
         * @param dataLength Size of @p data in bytes.
         * @return True if the whole region was written; false if the region or level is unknown.
         */
        [[nodiscard]] bool GetData(int level, int x, int y, int w, int h,
                                   void* data, int dataLength) const override;

        /**
         * @brief Returns the raw sokol_gfx image handle id backing this texture. NOXNA.
         * @return sg_image id, or 0 when creation failed.
         */
        NOXNA [[nodiscard]] std::uint32_t GetImageIdEXT() const { return imageId_; }

        /**
         * @brief Returns the raw sokol_gfx texture-view handle id used to bind this texture. NOXNA.
         * @return sg_view id, or 0 when creation failed.
         */
        NOXNA [[nodiscard]] std::uint32_t GetViewIdEXT() const { return viewId_; }

    private:
        void RecreateImage();
        void DestroyImage();

        int width_ = 0;
        int height_ = 0;
        int mipLevels_ = 1;
        std::uint32_t imageId_ = 0;
        std::uint32_t viewId_ = 0;
        /// One tightly packed RGBA8 buffer per mip level, kept because sokol_gfx can only replace
        /// an image in full.
        std::vector<std::vector<std::uint8_t>> levels_;
    };

    /**
     * @brief Backend handle for a 2D off-screen render target.
     *
     * plan_sokol.md SOKOL-25: a real colour attachment (plus an optional combined depth-stencil
     * attachment) that a pass can render into, and a separate texture view of the same colour
     * image so a later pass can sample it -- sokol_gfx requires a distinct sg_view per use even
     * when both reference the same sg_image (see the "offscreen rendering" section of
     * sokol_gfx.h's own doc comment).
     *
     * plan_sokol.md SOKOL-26: when @p multiSampleCount is greater than 1 (after clamping to the
     * driver's `GL_MAX_SAMPLES`), this class follows sokol_gfx.h's own documented MSAA offscreen
     * workflow -- a multisample-only colour image (`usage.color_attachment`, `sample_count > 1`)
     * that a pass renders into, plus a *separate* single-sample resolve image
     * (`usage.resolve_attachment`) that sokol_gfx automatically resolves into at `sg_end_pass()`
     * once the pass names both attachment views. `colorImageId_`/`colorTextureViewId_` always name
     * the single-sample image (the resolve target when MSAA is active, the only image otherwise),
     * so sampling this target as a texture is unaffected by whether MSAA is on. A multisampled
     * depth-stencil image is allocated alongside the multisample colour image when both MSAA and a
     * depth-stencil attachment are requested -- it is never resolved (nothing here reads a render
     * target's depth back), matching every other backend's MSAA depth handling.
     *
     * Scope for this task: a single, non-mipmapped target. `RenderTargetCube` MSAA and MRT are not
     * implemented yet (plan_sokol.md SOKOL-26's remaining items) and `CreateRenderTarget2D` refuses
     * a mipmapped request explicitly rather than silently downgrading it.
     * `GetData()` (reading a rendered target back to the CPU) is not implemented either -- it
     * would need either sokol's opaque per-backend image handle exposed for a raw GL readback (the
     * approach ReadBackbuffer() already uses for the window's own framebuffer) or an injected,
     * self-managed GL texture; neither is done here, so it inherits ITextureBackend::GetData's
     * `return false` default, which the shared layer turns into a clean
     * `System::NotSupportedException` rather than fabricated pixels.
     */
    class SokolRenderTargetBackend : public IRenderTargetBackend
    {
    public:
        /**
         * @brief Creates the colour (and optional depth-stencil) attachment images and views.
         * @param width       Target width in pixels.
         * @param height      Target height in pixels.
         * @param hasDepthStencil True to also allocate a combined depth-stencil attachment.
         * @param multiSampleCount Requested MSAA sample count; 0 or 1 means no MSAA. Clamped to the
         *                         driver's `GL_MAX_SAMPLES` -- see GetMultiSampleCount() for the
         *                         real, applied value.
         */
        SokolRenderTargetBackend(int width, int height, bool hasDepthStencil, int multiSampleCount);

        /** @brief Destroys every sokol_gfx view and image owned by this target. */
        ~SokolRenderTargetBackend() override;

        SokolRenderTargetBackend(const SokolRenderTargetBackend&) = delete;
        SokolRenderTargetBackend& operator=(const SokolRenderTargetBackend&) = delete;

        /**
         * @brief Returns the target's width in pixels.
         * @return Target width.
         */
        [[nodiscard]] int GetWidth() const override;

        /**
         * @brief Returns the target's height in pixels.
         * @return Target height.
         */
        [[nodiscard]] int GetHeight() const override;

        /**
         * @brief Returns null; this backend renders through sokol_gfx, not SDL_Renderer.
         * @return Always nullptr.
         */
        [[nodiscard]] SDL_Texture* GetNativeTexture() const override;

        /** @brief Bind is driven entirely by SokolGraphicsBackend's own bound-target tracking;
         *         this override exists only to satisfy the pure-virtual interface. */
        void BindAsRenderTarget() override;

        /** @brief Unbind is driven entirely by SokolGraphicsBackend's own bound-target tracking;
         *         this override exists only to satisfy the pure-virtual interface. */
        void UnbindAsRenderTarget() override;

        /**
         * @brief Returns whether this target actually allocated a depth-stencil attachment.
         * @param depthFormatWasRequested Unused: this backend allocates one iff it was asked to.
         * @return True if a depth-stencil attachment exists.
         */
        [[nodiscard]] bool HasRealDepthBuffer(bool depthFormatWasRequested) const override;

        /**
         * @brief Returns the real, driver-clamped MSAA sample count this target was created with.
         * @return Sample count greater than 1 when MSAA is active; 0 when it is not.
         */
        [[nodiscard]] int GetMultiSampleCount() const override { return multiSampleCount_; }

        /**
         * @brief Returns the raw sokol_gfx colour image handle id -- the single-sample image that
         * is sampled as a texture (the MSAA resolve target when MSAA is active). NOXNA.
         * @return sg_image id, or 0 when creation failed.
         */
        NOXNA [[nodiscard]] std::uint32_t GetColorImageIdEXT() const { return colorImageId_; }

        /**
         * @brief Returns the raw sokol_gfx colour-attachment view handle id, used when this
         * target is the active render target. This names the multisample image's attachment view
         * when MSAA is active, and the single-sample image's otherwise. NOXNA.
         * @return sg_view id, or 0 when creation failed.
         */
        NOXNA [[nodiscard]] std::uint32_t GetColorAttachmentViewIdEXT() const { return colorAttachmentViewId_; }

        /**
         * @brief Returns the raw sokol_gfx resolve-attachment view handle id, wired into a pass's
         * `attachments.resolves[0]` so sokol_gfx resolves the MSAA colour image into the
         * single-sample image at `sg_end_pass()`. NOXNA.
         * @return sg_view id, or 0 when this target is not multisampled.
         */
        NOXNA [[nodiscard]] std::uint32_t GetResolveAttachmentViewIdEXT() const { return resolveAttachmentViewId_; }

        /**
         * @brief Returns the raw sokol_gfx texture-view handle id, used to sample this target as
         * an ordinary texture in a later pass. NOXNA.
         * @return sg_view id, or 0 when creation failed.
         */
        NOXNA [[nodiscard]] std::uint32_t GetColorTextureViewIdEXT() const { return colorTextureViewId_; }

        /**
         * @brief Returns the raw sokol_gfx depth-stencil-attachment view handle id. NOXNA.
         * @return sg_view id, or 0 when this target has no depth-stencil attachment.
         */
        NOXNA [[nodiscard]] std::uint32_t GetDepthStencilAttachmentViewIdEXT() const { return depthStencilAttachmentViewId_; }

    private:
        int width_ = 0;
        int height_ = 0;
        int multiSampleCount_ = 0;
        std::uint32_t colorImageId_ = 0;
        std::uint32_t colorAttachmentViewId_ = 0;
        /// Only allocated when multiSampleCount_ > 0: the multisample-only image colorAttachmentViewId_
        /// actually attaches to. colorImageId_ is the separate single-sample resolve image in that case.
        std::uint32_t msaaColorImageId_ = 0;
        std::uint32_t resolveAttachmentViewId_ = 0;
        std::uint32_t colorTextureViewId_ = 0;
        std::uint32_t depthStencilImageId_ = 0;
        std::uint32_t depthStencilAttachmentViewId_ = 0;
    };

    /**
     * @brief Backend handle for a RenderTargetCube: a real sokol_gfx CUBE image plus one
     * colour-attachment view per face and a single shared depth-stencil attachment.
     *
     * plan_sokol.md SOKOL-26. The depth-stencil buffer is genuinely ONE resource shared by all six
     * faces (matching FNA3D's own cube render-target convention -- see
     * rendertarget_depthstencil_usage_test.cpp's U2 check), not per-face: it is a plain 2D
     * depth-stencil image, reused as the pass's depth attachment regardless of which face is
     * currently the colour attachment.
     *
     * Unlike `SokolRenderTargetBackend`, this class never attempts MSAA: sokol_gfx's own validation
     * layer hard-rejects a CUBE image with `sample_count > 1`
     * (`VALIDATE_IMAGEDESC_ATTACHMENT_MSAA_CUBE_IMAGE`), so a multisampled `RenderTargetCube` is a
     * permanent sokol_gfx API boundary, not a "not implemented yet" gap -- `multiSampleCount` is
     * always silently clamped to 1 (`GetMultiSampleCount()` always answers 0), the same declared
     * boundary `WebGPUGraphicsBackend`/`D3D9RenderTargetCubeBackend` report for their own reasons.
     */
    class SokolRenderTargetCubeBackend : public IRenderTargetCubeBackend
    {
    public:
        /**
         * @brief Creates the six colour-attachment views (and optional shared depth-stencil
         * attachment) for a cube render target.
         * @param size            Edge length of each face in pixels.
         * @param hasDepthStencil True to also allocate a shared depth-stencil attachment.
         */
        SokolRenderTargetCubeBackend(int size, bool hasDepthStencil);

        /** @brief Destroys every sokol_gfx view and image owned by this target. */
        ~SokolRenderTargetCubeBackend() override;

        SokolRenderTargetCubeBackend(const SokolRenderTargetCubeBackend&) = delete;
        SokolRenderTargetCubeBackend& operator=(const SokolRenderTargetCubeBackend&) = delete;

        /**
         * @brief Returns the edge length of each face in pixels.
         * @return Face edge length.
         */
        [[nodiscard]] int GetSize() const override;

        /** @brief Bind is driven entirely by SokolGraphicsBackend's own bound-target tracking;
         *         this override exists only to satisfy the pure-virtual interface. */
        void BindAsRenderTargetFace(int face) override;

        /** @brief Unbind is driven entirely by SokolGraphicsBackend's own bound-target tracking;
         *         this override exists only to satisfy the pure-virtual interface. */
        void UnbindAsRenderTarget() override;

        /**
         * @brief Returns whether this target actually allocated a depth-stencil attachment.
         * @param depthFormatWasRequested Unused: this backend allocates one iff it was asked to.
         * @return True if a depth-stencil attachment exists.
         */
        [[nodiscard]] bool HasRealDepthBuffer(bool depthFormatWasRequested) const override;

        /**
         * @brief Returns the raw sokol_gfx cube image handle id. NOXNA.
         * @return sg_image id, or 0 when creation failed.
         */
        NOXNA [[nodiscard]] std::uint32_t GetImageIdEXT() const { return imageId_; }

        /**
         * @brief Returns the raw sokol_gfx colour-attachment view handle id for one face. NOXNA.
         * @param face Cube face index (0=+X, 1=-X, 2=+Y, 3=-Y, 4=+Z, 5=-Z).
         * @return sg_view id, or 0 when creation failed or @p face is out of range.
         */
        NOXNA [[nodiscard]] std::uint32_t GetColorAttachmentViewIdEXT(int face) const
        {
            return (face >= 0 && face < 6) ? colorAttachmentViewIds_[static_cast<std::size_t>(face)] : 0;
        }

        /**
         * @brief Returns the raw sokol_gfx texture-view handle id, used to sample the whole cube.
         * NOXNA.
         * @return sg_view id, or 0 when creation failed.
         */
        NOXNA [[nodiscard]] std::uint32_t GetTextureViewIdEXT() const { return textureViewId_; }

        /**
         * @brief Returns the raw sokol_gfx depth-stencil-attachment view handle id, shared by every
         * face. NOXNA.
         * @return sg_view id, or 0 when this target has no depth-stencil attachment.
         */
        NOXNA [[nodiscard]] std::uint32_t GetDepthStencilAttachmentViewIdEXT() const
        {
            return depthStencilAttachmentViewId_;
        }

    private:
        int size_ = 0;
        std::uint32_t imageId_ = 0;
        std::array<std::uint32_t, 6> colorAttachmentViewIds_{};
        std::uint32_t textureViewId_ = 0;
        std::uint32_t depthStencilImageId_ = 0;
        std::uint32_t depthStencilAttachmentViewId_ = 0;
    };

    /**
     * @brief Backend handle for a cube texture: pure CPU-side RGBA8 storage, no sokol_gfx image.
     *
     * plan_sokol.md SOKOL-27. Nothing on this backend samples a cube texture yet -- dual-texture,
     * environment-mapped, skinned and PBR 3D draws all throw `NotYetImplemented` in
     * `DrawColored3D`, and there is no cube shader variant -- so allocating a real `sg_image` here
     * would be a GPU resource with no consumer. `Texture2D`/`RenderTarget2D` create real images
     * because SpriteBatch and the 3D paths genuinely sample them; this class stores exactly the six
     * faces' pixels `SetData()`/`GetData()` need and nothing more, mirroring the Software backend's
     * own `SoftwareTextureCubeBackend`.
     */
    class SokolTextureCubeBackend : public ITextureCubeBackend
    {
    public:
        /**
         * @brief Allocates zeroed RGBA8 storage for all six faces of every mip level.
         * @param size   Edge length of one cube face at mip 0, in texels.
         * @param mipMap Allocate the full mip chain down to 1x1 as well as level 0.
         */
        SokolTextureCubeBackend(int size, bool mipMap);

        /**
         * @brief Copies one face's RGBA8 sub-rectangle into this backend's CPU storage.
         *
         * @param face       Cube face index (0=+X, 1=-X, 2=+Y, 3=-Y, 4=+Z, 5=-Z).
         * @param level      Mip level to write.
         * @param x          Left edge of the requested region, in texels.
         * @param y          Top edge of the requested region, in texels.
         * @param w          Width of the requested region, in texels.
         * @param h          Height of the requested region, in texels.
         * @param data       Source pixels, tightly packed RGBA8 rows, top row first.
         * @param dataLength Size of @p data in bytes.
         * @return True if the whole region was stored; false for an out-of-range level or face, an
         *         out-of-bounds rectangle, or a source buffer too small for the region.
         */
        [[nodiscard]] bool SetData(int face, int level, int x, int y, int w, int h,
                                   const void* data, int dataLength) override;

        /**
         * @brief Reads one face's stored RGBA8 sub-rectangle back from this backend's CPU shadow.
         *
         * @param face       Cube face index (0=+X, 1=-X, 2=+Y, 3=-Y, 4=+Z, 5=-Z).
         * @param level      Mip level to read.
         * @param x          Left edge of the requested region, in texels.
         * @param y          Top edge of the requested region, in texels.
         * @param w          Width of the requested region, in texels.
         * @param h          Height of the requested region, in texels.
         * @param data       Destination for tightly packed RGBA8 rows, top row first.
         * @param dataLength Size of @p data in bytes.
         * @return True if the whole region was written; false for an out-of-range level or face, or
         *         an out-of-bounds rectangle.
         */
        [[nodiscard]] bool GetData(int face, int level, int x, int y, int w, int h,
                                   void* data, int dataLength) const override;

        /**
         * @brief Returns the edge length of mip level 0 in texels. NOXNA.
         * @return Cube face edge length.
         */
        NOXNA [[nodiscard]] int GetSizeEXT() const { return size_; }

    private:
        /// Face edge length at @p level, never below 1 -- mirrors TextureCube.cpp's own mip
        /// dimension helper.
        [[nodiscard]] int LevelDim(int level) const;

        int size_ = 0;
        int levelCount_ = 1;
        /// levels_[level][face] -- one tightly packed RGBA8 buffer per face per allocated mip level.
        std::vector<std::array<std::vector<std::uint8_t>, 6>> levels_;
    };

    /**
     * @brief Backend handle for a volume (3D) texture: pure CPU-side RGBA8 storage, no sokol_gfx
     * image.
     *
     * plan_sokol.md SOKOL-27. Same rationale as `SokolTextureCubeBackend`: nothing on this backend
     * samples a volume texture (there is no 3D-sampler shader variant), so a real `sg_image` would
     * be a GPU resource with no consumer. Mirrors Software's own choice to leave `Texture3D`
     * unimplemented entirely -- this backend goes one step further and at least stores real pixels.
     */
    class SokolTexture3DBackend : public ITexture3DBackend
    {
    public:
        /**
         * @brief Allocates zeroed RGBA8 storage for every mip level.
         *
         * @param width  Level-0 width in texels.
         * @param height Level-0 height in texels.
         * @param depth  Level-0 depth in texels.
         * @param mipMap Allocate the full mip chain down to 1x1x1 as well as level 0. Matches
         *               Texture3D.cpp's own `CalculateMipLevels(width, height)` -- depth does not
         *               participate in the level COUNT, even though it still halves per level like
         *               width/height do (real volume-texture mip convention).
         */
        SokolTexture3DBackend(int width, int height, int depth, bool mipMap);

        /**
         * @brief Copies an RGBA8 sub-volume into this backend's CPU storage.
         *
         * @param level      Mip level to write.
         * @param x          Left edge of the requested box, in voxels.
         * @param y          Top edge of the requested box, in voxels.
         * @param z          Front edge of the requested box, in voxels.
         * @param w          Width of the requested box, in voxels.
         * @param h          Height of the requested box, in voxels.
         * @param depth      Depth of the requested box, in voxels.
         * @param data       Source voxels, tightly packed RGBA8, slice by slice front to back.
         * @param dataLength Size of @p data in bytes.
         * @return True if the whole box was stored; false for an out-of-range level, an
         *         out-of-bounds box, or a source buffer too small for the region.
         */
        [[nodiscard]] bool SetData(int level, int x, int y, int z, int w, int h, int depth,
                                   const void* data, int dataLength) override;

        /**
         * @brief Reads an RGBA8 sub-volume back from this backend's CPU shadow.
         *
         * @param level      Mip level to read.
         * @param x          Left edge of the requested box, in voxels.
         * @param y          Top edge of the requested box, in voxels.
         * @param z          Front edge of the requested box, in voxels.
         * @param w          Width of the requested box, in voxels.
         * @param h          Height of the requested box, in voxels.
         * @param depth      Depth of the requested box, in voxels.
         * @param data       Destination for tightly packed RGBA8 voxels, slice by slice front to
         *                   back.
         * @param dataLength Size of @p data in bytes.
         * @return True if the whole box was written; false for an out-of-range level or an
         *         out-of-bounds box.
         */
        [[nodiscard]] bool GetData(int level, int x, int y, int z, int w, int h, int depth,
                                   void* data, int dataLength) const override;

    private:
        [[nodiscard]] int LevelWidth(int level) const;
        [[nodiscard]] int LevelHeight(int level) const;
        [[nodiscard]] int LevelDepth(int level) const;

        int width_ = 0;
        int height_ = 0;
        int depth_ = 0;
        int levelCount_ = 1;
        /// levels_[level] -- one tightly packed RGBA8 buffer per allocated mip level, voxels
        /// ordered slice by slice (front to back), each slice row-major with the top row first.
        std::vector<std::vector<std::uint8_t>> levels_;
    };

    /**
     * @brief Backend handle for a vertex buffer.
     *
     * Each SetData() recreates the underlying immutable sokol_gfx buffer. sokol_gfx allows only
     * one sg_update_buffer() per buffer per frame, whereas immutable creation is unrestricted, so
     * recreation is what makes repeated uploads within a frame legal here.
     */
    class SokolVertexBufferBackend : public IVertexBufferBackend
    {
    public:
        /**
         * @brief Creates an empty vertex buffer handle with the given capacity hint.
         * @param vertexCapacity Number of vertices the owning VertexBuffer was created for.
         */
        explicit SokolVertexBufferBackend(int vertexCapacity);

        /** @brief Destroys the sokol_gfx buffer owned by this vertex buffer. */
        ~SokolVertexBufferBackend() override;

        SokolVertexBufferBackend(const SokolVertexBufferBackend&) = delete;
        SokolVertexBufferBackend& operator=(const SokolVertexBufferBackend&) = delete;

        /**
         * @brief Uploads vertex data, replacing the whole buffer.
         *
         * @param data          Packed vertex data.
         * @param vertexCount   Number of vertices.
         * @param strideInBytes Size of one vertex in bytes.
         */
        void SetData(const void* data, int vertexCount, std::size_t strideInBytes) override;

        /**
         * @brief Records the caller's full vertex declaration.
         *
         * The colored-3D draw path keys its pipeline on this declaration's real stride and element
         * offsets, so a genuinely custom layout is bound correctly rather than being matched
         * against a fixed set of recognised byte strides.
         *
         * @param vertexDeclaration Full declaration, including stride and elements.
         */
        void SetVertexDeclaration(const VertexDeclaration& vertexDeclaration) override;

        /**
         * @brief Returns the number of vertices uploaded by the last SetData().
         * @return Vertex count, or 0 if nothing has been uploaded.
         */
        [[nodiscard]] int GetVertexCount() const override;

        /**
         * @brief Returns the byte stride of the last uploaded vertex data. NOXNA.
         * @return Stride in bytes, or 0 if nothing has been uploaded.
         */
        NOXNA [[nodiscard]] std::size_t GetStrideEXT() const { return stride_; }

        /**
         * @brief Returns the vertex declaration recorded by SetVertexDeclaration(). NOXNA.
         * @return The declaration, or null when the owning VertexBuffer supplied none.
         */
        NOXNA [[nodiscard]] const VertexDeclaration* GetDeclarationEXT() const
        {
            return hasDeclaration_ ? &declaration_ : nullptr;
        }

        /**
         * @brief Returns the raw sokol_gfx buffer handle id. NOXNA.
         * @return sg_buffer id, or 0 when no data has been uploaded.
         */
        NOXNA [[nodiscard]] std::uint32_t GetBufferIdEXT() const { return bufferId_; }

    private:
        int capacity_ = 0;
        int vertexCount_ = 0;
        std::size_t stride_ = 0;
        std::uint32_t bufferId_ = 0;
        bool hasDeclaration_ = false;
        VertexDeclaration declaration_;
    };

    /**
     * @brief Backend handle for a 16- or 32-bit index buffer.
     *
     * Recreates its immutable sokol_gfx buffer on every upload, for the same per-frame update
     * restriction described on SokolVertexBufferBackend.
     */
    class SokolIndexBufferBackend : public IIndexBufferBackend
    {
    public:
        /**
         * @brief Creates an empty index buffer handle with the given capacity hint.
         * @param indexCapacity  Number of indices the owning IndexBuffer was created for.
         * @param thirtyTwoBit   True when the owning IndexBuffer uses 32-bit indices.
         */
        SokolIndexBufferBackend(int indexCapacity, bool thirtyTwoBit);

        /** @brief Destroys the sokol_gfx buffer owned by this index buffer. */
        ~SokolIndexBufferBackend() override;

        SokolIndexBufferBackend(const SokolIndexBufferBackend&) = delete;
        SokolIndexBufferBackend& operator=(const SokolIndexBufferBackend&) = delete;

        /**
         * @brief Uploads 16-bit index data, replacing the whole buffer.
         * @param data       Packed 16-bit indices.
         * @param indexCount Number of indices.
         */
        void SetData16(const void* data, int indexCount) override;

        /**
         * @brief Uploads 32-bit index data, replacing the whole buffer.
         * @param data       Packed 32-bit indices.
         * @param indexCount Number of indices.
         */
        void SetData32(const void* data, int indexCount) override;

        /**
         * @brief Returns the number of indices uploaded by the last SetData16()/SetData32().
         * @return Index count, or 0 if nothing has been uploaded.
         */
        [[nodiscard]] int GetIndexCount() const override;

        /**
         * @brief Returns whether this buffer holds 32-bit indices.
         * @return True for 32-bit indices, false for 16-bit.
         */
        [[nodiscard]] bool IsThirtyTwoBit() const override;

        /**
         * @brief Returns the raw sokol_gfx buffer handle id. NOXNA.
         * @return sg_buffer id, or 0 when no data has been uploaded.
         */
        NOXNA [[nodiscard]] std::uint32_t GetBufferIdEXT() const { return bufferId_; }

    private:
        void Upload(const void* data, int indexCount, std::size_t indexSize);

        int capacity_ = 0;
        int indexCount_ = 0;
        bool thirtyTwoBit_ = false;
        std::uint32_t bufferId_ = 0;
    };

    /**
     * @brief SpriteBatch implementation drawing through a single streamed sokol_gfx vertex buffer.
     *
     * Quads are accumulated between Begin() and End() and flushed whenever the source texture
     * changes. Each flush appends its vertices to a per-frame streaming buffer (sg_append_buffer)
     * and draws them against a pre-built, immutable quad index buffer, so an arbitrary number of
     * flushes per frame is legal despite sokol_gfx's one-update-per-buffer-per-frame rule.
     */
    class SokolSpriteBatchBackend : public ISpriteBatchBackend
    {
    public:
        /** @brief One sprite-batch vertex: position, texture coordinate, and premultiplied tint. */
        struct Vertex
        {
            /** @brief X position, in XNA top-left-origin pixel space. */
            float x;
            /** @brief Y position, in XNA top-left-origin pixel space. */
            float y;
            /** @brief U texture coordinate. */
            float u;
            /** @brief V texture coordinate. */
            float v;
            /** @brief Red tint, 0..1. */
            float r;
            /** @brief Green tint, 0..1. */
            float g;
            /** @brief Blue tint, 0..1. */
            float b;
            /** @brief Alpha tint, 0..1. */
            float a;
        };

        /**
         * @brief Creates a sprite batch bound to the given backend.
         * @param backend Owning graphics backend; supplies the shared pipeline/sampler caches.
         */
        explicit SokolSpriteBatchBackend(SokolGraphicsBackend& backend);

        /** @brief Destroys this sprite batch. Shared GPU resources stay owned by the backend. */
        ~SokolSpriteBatchBackend() override;

        SokolSpriteBatchBackend(const SokolSpriteBatchBackend&) = delete;
        SokolSpriteBatchBackend& operator=(const SokolSpriteBatchBackend&) = delete;

        /** @brief Begins a batch; Draw() is only legal between Begin() and End(). */
        void Begin() override;

        /** @brief Ends the batch, flushing any sprites still pending. */
        void End() override;

        /**
         * @brief Sets the transform applied on top of the 2D orthographic projection.
         * @param m Transform matrix, as passed to SpriteBatch::Begin.
         */
        void SetTransformMatrix(const Matrix& m) override;

        /**
         * @brief Sets the texture filter applied to every Draw() in this batch.
         * @param textureFilter Raw Microsoft::Xna::Framework::Graphics::TextureFilter value.
         */
        void SetSamplerFilter(int textureFilter) override;

        /**
         * @brief Sets the texture address mode applied to every Draw() in this batch.
         * @param addressU Raw TextureAddressMode value for U.
         * @param addressV Raw TextureAddressMode value for V.
         */
        void SetSamplerAddressMode(int addressU, int addressV) override;

        /**
         * @brief Draws a whole texture at the given position, untinted.
         * @param texture Source texture.
         * @param x       Destination left edge, in pixels.
         * @param y       Destination top edge, in pixels.
         */
        void Draw(const ITextureBackend& texture, float x, float y) override;

        /**
         * @brief Draws a source region of a texture into a destination rectangle.
         * @param texture              Source texture.
         * @param destinationRectangle Destination rectangle, in pixels.
         * @param sourceRectangle      Source region, in texels.
         * @param color                Tint colour.
         */
        void Draw(const ITextureBackend& texture,
                  const Rectangle& destinationRectangle,
                  const Rectangle& sourceRectangle,
                  const Color& color) override;

        /**
         * @brief Draws a source region with rotation, origin, flipping, and layer depth.
         * @param texture              Source texture.
         * @param destinationRectangle Destination rectangle, in pixels.
         * @param sourceRectangle      Source region, in texels.
         * @param color                Tint colour.
         * @param rotation             Rotation in radians, about @p origin.
         * @param origin               Rotation/scaling origin, in source-texture pixels.
         * @param effects              Horizontal/vertical flip flags.
         * @param layerDepth           Sort depth; ordering is resolved by the shared SpriteBatch.
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
        void FlushBatch();

        SokolGraphicsBackend& backend_;
        bool begun_ = false;
        std::vector<Vertex> pendingVertices_;
        const ITextureBackend* currentTexture_ = nullptr;
        Matrix transform_ = Matrix::getIdentityProperty();
        int pendingFilter_ = 0;   // TextureFilter::Linear
        int pendingAddressU_ = 1; // TextureAddressMode::Clamp
        int pendingAddressV_ = 1; // TextureAddressMode::Clamp
    };

    /**
     * @brief Backend handle for an occlusion query, implemented with a raw GL query object.
     *
     * plan_sokol.md SOKOL-29. sokol_gfx exposes no query API of its own, but on this backend's
     * target platform (SOKOL_GLCORE) sokol renders through an ordinary GL context this class can
     * issue raw `glBeginQuery`/`glEndQuery` calls against directly -- a query records whatever the
     * GL context rasterizes between them regardless of which layer (sokol_gfx or this class) issued
     * the draw calls. Restricted to the GL APIs the same way `ReadBackbuffer` is:
     * `SokolGraphicsBackend::CreateOcclusionQuery` returns null on any other `CNA_SOKOL_API`.
     */
    class SokolOcclusionQueryBackend : public IOcclusionQueryBackend
    {
    public:
        /** @brief Allocates the underlying GL query object. */
        SokolOcclusionQueryBackend();

        /** @brief Destroys the GL query object. */
        ~SokolOcclusionQueryBackend() override;

        SokolOcclusionQueryBackend(const SokolOcclusionQueryBackend&) = delete;
        SokolOcclusionQueryBackend& operator=(const SokolOcclusionQueryBackend&) = delete;

        /**
         * @brief Starts recording samples that pass the depth/stencil test for subsequent draws.
         *
         * FNA's own OcclusionQuery.Begin()/End() are pure one-line forwards to FNA3D with no call-
         * sequence validation at all (plan_sokol.md SOKOL-29's own audit), so a repeated Begin()
         * with no intervening End() must not throw here either -- but raw GL, unlike FNA3D's
         * drivers, raises GL_INVALID_OPERATION for exactly that (double Begin, or End with no
         * active Begin), and an unconsumed GL error left pending trips sokol_gfx's own internal
         * `glGetError()==0` assertions the next time IT touches GL, at a completely unrelated call
         * site. `active_` tracks the real GL query state so this class only ever issues a
         * glBeginQuery/glEndQuery pair GL itself considers legal, silently absorbing everything
         * else -- matching FNA's "no exception" contract without corrupting sokol's error state.
         */
        void Begin() override;

        /** @brief Stops recording; the result becomes available some time after this call. See
         *         Begin()'s own comment for why an unmatched End() is silently absorbed. */
        void End() override;

        /**
         * @brief Returns whether the GPU has finished processing this query.
         * @return True once `PixelCount()` can be read without stalling the pipeline.
         */
        [[nodiscard]] bool IsComplete() const override;

        /**
         * @brief Returns the number of samples that passed the depth/stencil test.
         * @return The sample count once complete; 0 while still pending (matching the codebase's
         *         other GL query backend, EasyGL's).
         */
        [[nodiscard]] int PixelCount() const override;

    private:
        std::uint32_t queryId_ = 0;
        /// Whether a glBeginQuery for this object is currently outstanding (no matching
        /// glEndQuery yet) -- see Begin()'s own comment for why this exists.
        bool active_ = false;
        /// Whether at least one Begin()/End() cycle has completed. A fresh query object has no
        /// GL_QUERY_RESULT_AVAILABLE state at all -- GL raises GL_INVALID_OPERATION for
        /// glGetQueryObject* on a query that was never started OR is still active, and real XNA
        /// code (and this file's own IsComplete()/PixelCount() callers) legitimately asks before
        /// the first End() -- so IsComplete()/PixelCount() only ever query GL once this is true.
        bool hasResult_ = false;
    };

    /**
     * @brief CNA graphics backend implemented on sokol_gfx (https://github.com/floooh/sokol).
     *
     * CNA keeps ownership of the SDL window and the game loop; this class creates only the GPU
     * context (SDL_GL_CreateContext for the GL APIs) and drives sokol_gfx inside it, so sokol_app
     * is deliberately not used.
     *
     * Scope: this is the backend's 2D baseline -- clear/present, Texture2D, vertex/index buffers,
     * and a real SpriteBatch. The 3D draw path, render targets, cube/volume textures, custom
     * effects and occlusion queries are not implemented and fail loudly rather than silently
     * no-opping. See plan_sokol.md and docs/sokol-backend.md for the current capability boundary.
     */
    class SokolGraphicsBackend : public IGraphicsBackend
    {
    public:
        /**
         * @brief Creates the GPU context for @p window and initialises sokol_gfx in it.
         *
         * @param args Backend creation arguments; window, virtual resolution, presentation mode,
         *             multisample count and swap interval are honoured.
         */
        explicit SokolGraphicsBackend(const GraphicsBackendCreateArgs& args);

        /** @brief Shuts sokol_gfx down and destroys the GPU context. */
        ~SokolGraphicsBackend() override;

        SokolGraphicsBackend(const SokolGraphicsBackend&) = delete;
        SokolGraphicsBackend& operator=(const SokolGraphicsBackend&) = delete;

        /**
         * @brief Clears the colour buffer.
         * @param r,g,b,a Clear colour, 0..1.
         */
        void Clear(float r, float g, float b, float a) override;

        /**
         * @brief Clears colour and depth.
         * @param r,g,b,a Clear colour, 0..1.
         * @param depth   Depth value to clear with, 0..1.
         */
        void ClearColorAndDepth(float r, float g, float b, float a, float depth) override;

        /**
         * @brief Clears the depth buffer only.
         * @param depth Depth value to clear with, 0..1.
         */
        void ClearDepth(float depth) override;

        /**
         * @brief Clears the stencil buffer only.
         * @param stencil Stencil value to clear with.
         */
        void ClearStencil(int stencil) override;

        /**
         * @brief Clears depth and stencil.
         * @param depth   Depth value to clear with, 0..1.
         * @param stencil Stencil value to clear with.
         */
        void ClearDepthAndStencil(float depth, int stencil) override;

        /**
         * @brief Clears colour and stencil.
         * @param r,g,b,a Clear colour, 0..1.
         * @param stencil Stencil value to clear with.
         */
        void ClearColorAndStencil(float r, float g, float b, float a, int stencil) override;

        /**
         * @brief Clears colour, depth and stencil.
         * @param r,g,b,a Clear colour, 0..1.
         * @param depth   Depth value to clear with, 0..1.
         * @param stencil Stencil value to clear with.
         */
        void ClearColorDepthAndStencil(float r, float g, float b, float a,
                                       float depth, int stencil) override;

        /** @brief Ends the frame's render pass, commits it, and swaps the window's buffers. */
        void Present() override;

        /**
         * @brief Returns the logical (virtual) viewport size games draw in.
         * @param width  Receives the logical width in pixels.
         * @param height Receives the logical height in pixels.
         */
        void GetViewportSize(int& width, int& height) override;

        /**
         * @brief Updates the logical presentation size at runtime.
         * @param width  New logical width in pixels.
         * @param height New logical height in pixels.
         */
        void SetVirtualResolution(int width, int height) override;

        /**
         * @brief Updates the presentation/scaling policy at runtime.
         * @param mode Raw CnaPresentationMode value.
         */
        void SetPresentationMode(int mode) override;

        /**
         * @brief Updates the swap interval at runtime.
         * @param interval 0 = immediate, 1 = VSync, 2 = half refresh rate.
         */
        void SetSwapInterval(int interval) override;

        /**
         * @brief Converts a point from physical window to logical game coordinates.
         * @param windowX Window-space X.
         * @param windowY Window-space Y.
         * @param logX    Receives the logical X.
         * @param logY    Receives the logical Y.
         * @return True when a virtual resolution is configured, false otherwise.
         */
        bool TransformWindowToLogical(float windowX, float windowY,
                                      float& logX, float& logY) const override;

        /**
         * @brief Converts a point from logical game to physical window coordinates.
         * @param logX    Logical X.
         * @param logY    Logical Y.
         * @param windowX Receives the window-space X.
         * @param windowY Receives the window-space Y.
         * @return True when a virtual resolution is configured, false otherwise.
         */
        bool TransformLogicalToWindow(float logX, float logY,
                                      float& windowX, float& windowY) const override;

        /**
         * @brief Returns the SDL window this backend renders into.
         * @return The window; never null for a successfully constructed backend.
         */
        [[nodiscard]] SDL_Window* GetWindowInternal() const override;

        /**
         * @brief Returns null; this backend does not use SDL_Renderer.
         * @return Always nullptr.
         */
        [[nodiscard]] SDL_Renderer* GetRendererInternal() const override;

        /**
         * @brief Creates a sokol_gfx-backed 2D texture.
         * @param data Decoded RGBA8 source image.
         * @return The new texture backend.
         */
        std::unique_ptr<ITextureBackend> CreateTexture(const ImageData& data) override;

        /**
         * @brief Creates a CPU-storage-only cube texture (plan_sokol.md SOKOL-27).
         *
         * No real GPU resource is allocated: nothing on this backend samples a cube texture yet
         * (dual-texture/environment-map/skinned/PBR 3D draws all throw), so `SokolTextureCubeBackend`
         * only stores the six faces' pixels `SetData()`/`GetData()` need.
         *
         * @param size         Edge length of one cube face at mip 0, in texels.
         * @param mipMap       Allocate the full mip chain down to 1x1 as well as level 0.
         * @param surfaceFormat Unused -- this backend always stores RGBA8, matching CreateTexture.
         * @return The new cube texture backend.
         */
        std::unique_ptr<ITextureCubeBackend> CreateTextureCube(
            int size, bool mipMap, int surfaceFormat) override;

        /**
         * @brief Creates a CPU-storage-only volume texture (plan_sokol.md SOKOL-27).
         *
         * No real GPU resource is allocated: nothing on this backend samples a volume texture yet
         * (there is no 3D-sampler shader variant), so `SokolTexture3DBackend` only stores the
         * voxels `SetData()`/`GetData()` need.
         *
         * @param w            Level-0 width in texels.
         * @param h            Level-0 height in texels.
         * @param depth        Level-0 depth in texels.
         * @param mipMap       Allocate the full mip chain down to 1x1x1 as well as level 0.
         * @param surfaceFormat Unused -- this backend always stores RGBA8, matching CreateTexture.
         * @return The new volume texture backend.
         */
        std::unique_ptr<ITexture3DBackend> CreateTexture3D(
            int w, int h, int depth, bool mipMap, int surfaceFormat) override;

        /**
         * @brief Creates a raw-GL occlusion query (plan_sokol.md SOKOL-29).
         * @return The new occlusion query backend, or null on a non-GL `CNA_SOKOL_API`.
         */
        std::unique_ptr<IOcclusionQueryBackend> CreateOcclusionQuery() override;

        /**
         * @brief Creates a sokol_gfx-backed SpriteBatch.
         * @return The new sprite batch backend.
         */
        std::unique_ptr<ISpriteBatchBackend> CreateSpriteBatch() override;

        /**
         * @brief Creates a sokol_gfx-backed vertex buffer.
         * @param vertexCapacity Number of vertices to size the buffer for.
         * @return The new vertex buffer backend.
         */
        std::unique_ptr<IVertexBufferBackend> CreateVertexBuffer(int vertexCapacity) override;

        /**
         * @brief Creates a sokol_gfx-backed 16-bit index buffer.
         * @param indexCapacity Number of indices to size the buffer for.
         * @return The new index buffer backend.
         */
        std::unique_ptr<IIndexBufferBackend> CreateIndexBuffer16(int indexCapacity) override;

        /**
         * @brief Creates a sokol_gfx-backed 32-bit index buffer.
         * @param indexCapacity Number of indices to size the buffer for.
         * @return The new index buffer backend.
         */
        std::unique_ptr<IIndexBufferBackend> CreateIndexBuffer32(int indexCapacity) override;

        /**
         * @brief Reads rendered back-buffer pixels back into @p pixels as RGBA8.
         * @param x      Left edge of the region, in game coordinates.
         * @param y      Top edge of the region, in game coordinates.
         * @param w      Region width in pixels.
         * @param h      Region height in pixels.
         * @param pixels Destination for w * h * 4 bytes, top row first.
         */
        void ReadBackbuffer(int x, int y, int w, int h, uint8_t* pixels) override;

        /**
         * @brief Creates a single, non-multisampled, non-mipmapped off-screen render target.
         *
         * @param w                Target width in pixels.
         * @param h                Target height in pixels.
         * @param depthFormat      Raw DepthFormat ordinal; None (0) allocates no depth-stencil
         *                         attachment, any other value allocates a combined one.
         * @param preserveContents Unused: this backend always preserves a target's content across
         *                         binds (a real FBO naturally does), matching the EasyGL
         *                         backend's own documented simplification -- an explicit `Clear()`
         *                         is what actually discards content, on every backend.
         * @param mipMap           Must be false; a mipmapped render target is not implemented yet.
         * @param multiSampleCount Silently clamped to 1 (no MSAA) -- not implemented yet
         *                         (plan_sokol.md SOKOL-26); observable via IRenderTargetBackend::
         *                         GetMultiSampleCount(), matching every other backend's own
         *                         device-clamped-count convention.
         * @return The new render target backend.
         */
        std::unique_ptr<IRenderTargetBackend> CreateRenderTarget2D(
            int w, int h, int depthFormat, bool preserveContents, bool mipMap,
            int multiSampleCount) override;

        /**
         * @brief Creates a single, non-multisampled, non-mipmapped cube render target.
         *
         * @param size             Edge length of each face in pixels.
         * @param depthFormat      Raw DepthFormat ordinal; None (0) allocates no depth-stencil
         *                         attachment, any other value allocates a combined one, shared by
         *                         all six faces (plan_sokol.md SOKOL-26 -- see
         *                         SokolRenderTargetCubeBackend's own doc comment).
         * @param preserveContents Unused, for the same reason CreateRenderTarget2D's identically
         *                         named parameter is: a real sokol_gfx image naturally preserves
         *                         its content across binds.
         * @param mipMap           Must be false; a mipmapped cube render target is not implemented
         *                         yet.
         * @param multiSampleCount Silently clamped to 1 (no MSAA) -- not implemented yet, matching
         *                         CreateRenderTarget2D's own convention.
         * @return The new cube render target backend.
         */
        std::unique_ptr<IRenderTargetCubeBackend> CreateRenderTargetCube(
            int size, int depthFormat, bool preserveContents, bool mipMap,
            int multiSampleCount) override;

        /**
         * @brief Activates a single render target, or restores the back buffer.
         * @param rt The target to activate, or null to restore the back buffer.
         */
        void SetRenderTarget2D(IRenderTargetBackend* rt) override;

        /**
         * @brief Binds a render-target set. A single RenderTarget2D, a single RenderTargetCube
         * face, or the back buffer (null / count 0) is supported; MRT is not implemented yet.
         * @param renderTargets Ordered attachment descriptors, or null for the back buffer.
         * @param count         Number of attachments; 0 restores the back buffer.
         */
        void SetRenderTargets(const RenderTargetBindingDescriptor* renderTargets,
                              int count) override;

        /**
         * @brief Applies a BlendState to every subsequent draw.
         * @param colorSrcBlend  Raw Blend value for the colour source factor.
         * @param alphaSrcBlend  Raw Blend value for the alpha source factor.
         * @param colorDstBlend  Raw Blend value for the colour destination factor.
         * @param alphaDstBlend  Raw Blend value for the alpha destination factor.
         * @param colorBlendFunc Raw BlendFunction value for the colour equation.
         * @param alphaBlendFunc Raw BlendFunction value for the alpha equation.
         * @param writeState     Per-slot colour write masks and the coverage sample mask.
         */
        void ApplyBlendState(int colorSrcBlend, int alphaSrcBlend,
                             int colorDstBlend, int alphaDstBlend,
                             int colorBlendFunc, int alphaBlendFunc,
                             const BlendWriteState& writeState) override;

        /**
         * @brief Applies a DepthStencilState to every subsequent draw.
         * @param depthEnable         Whether depth testing is enabled.
         * @param depthWriteEnable    Whether depth writes are enabled.
         * @param depthFunc           Raw CompareFunction value for the depth test.
         * @param stencilEnable       Whether stencil testing is enabled.
         * @param stencilFunc         Raw CompareFunction value for the front-face stencil test.
         * @param stencilPass         Raw StencilOperation value for a passing front-face test.
         * @param stencilFail         Raw StencilOperation value for a failing front-face test.
         * @param stencilDepthFail    Raw StencilOperation value for a front-face depth failure.
         * @param stencilMask         Stencil read mask.
         * @param stencilWriteMask    Stencil write mask.
         * @param referenceStencil    Stencil reference value.
         * @param twoSidedStencilMode Whether back faces use their own stencil operations.
         * @param ccwStencilFunc      Raw CompareFunction value for the back-face stencil test.
         * @param ccwStencilPass      Raw StencilOperation value for a passing back-face test.
         * @param ccwStencilFail      Raw StencilOperation value for a failing back-face test.
         * @param ccwStencilDepthFail Raw StencilOperation value for a back-face depth failure.
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
         * @brief Applies a RasterizerState to every subsequent draw.
         * @param cullMode            Raw CullMode value.
         * @param fillMode            Raw FillMode value.
         * @param scissorTestEnable   Whether the scissor test is enabled.
         * @param depthBias           Constant depth bias.
         * @param slopeScaleDepthBias Slope-scaled depth bias.
         */
        void ApplyRasterizerState(int cullMode, int fillMode,
                                  bool scissorTestEnable,
                                  float depthBias,
                                  float slopeScaleDepthBias) override;

        /**
         * @brief Applies a SamplerState to a texture slot.
         * @param slot          Texture unit index.
         * @param filter        Raw TextureFilter value.
         * @param addressU      Raw TextureAddressMode value for U.
         * @param addressV      Raw TextureAddressMode value for V.
         * @param maxAnisotropy Maximum anisotropy, 1..16.
         */
        void ApplySamplerState(int slot, int filter, int addressU, int addressV,
                               int maxAnisotropy) override;

        /**
         * @brief Sets the constant blend colour used by the BlendFactor blend modes.
         * @param r,g,b,a Blend factor colour, 0..1.
         */
        void SetBlendFactor(float r, float g, float b, float a) override;

        /**
         * @brief Sets the stencil reference value used by subsequent draws.
         * @param value Stencil reference value.
         */
        void SetReferenceStencil(int value) override;

        /**
         * @brief Sets the scissor clip rectangle, in logical game coordinates.
         * @param x Left edge.
         * @param y Top edge.
         * @param w Width.
         * @param h Height.
         */
        void SetScissorRect(int x, int y, int w, int h) override;

        /**
         * @brief Sets the viewport rectangle and depth range, in logical game coordinates.
         * @param x        Left edge.
         * @param y        Top edge.
         * @param w        Width.
         * @param h        Height.
         * @param minDepth Near depth-range bound.
         * @param maxDepth Far depth-range bound.
         */
        void SetViewport(int x, int y, int w, int h, float minDepth, float maxDepth) override;

        /**
         * @brief Enables or disables depth testing for subsequent draws.
         * @param enabled Whether the depth test is enabled.
         */
        void SetDepthTestEnabled(bool enabled) override;

        /**
         * @brief Enables or disables blending for subsequent draws.
         * @param enabled Whether blending is enabled.
         */
        void SetBlendEnabled(bool enabled) override;

        /**
         * @brief Enables or disables depth writes for subsequent draws.
         * @param enabled Whether depth writes are enabled.
         */
        void SetDepthWriteEnabled(bool enabled) override;

        /**
         * @brief Draws vertex-coloured primitives with the built-in colored-3D program.
         * @param vb             Vertex buffer to read from.
         * @param world          World matrix.
         * @param view           View matrix.
         * @param projection     Projection matrix.
         * @param primitive      Primitive topology.
         * @param primitiveCount Number of primitives.
         */
        void DrawColoredPrimitives(const IVertexBufferBackend& vb,
                                   const Matrix& world,
                                   const Matrix& view,
                                   const Matrix& projection,
                                   PrimitiveType primitive,
                                   int primitiveCount) override;

        /**
         * @brief Indexed counterpart of DrawColoredPrimitives().
         * @param vb             Vertex buffer to read from.
         * @param ib             Index buffer to read from.
         * @param world          World matrix.
         * @param view           View matrix.
         * @param projection     Projection matrix.
         * @param primitive      Primitive topology.
         * @param primitiveCount Number of primitives.
         */
        void DrawIndexedColoredPrimitives(const IVertexBufferBackend& vb,
                                          const IIndexBufferBackend& ib,
                                          const Matrix& world,
                                          const Matrix& view,
                                          const Matrix& projection,
                                          PrimitiveType primitive,
                                          int primitiveCount) override;

        /**
         * @brief Effect-aware non-indexed draw.
         *
         * Only the untextured, unlit path is implemented; any effect requesting texturing,
         * lighting, dual texturing, environment mapping, skinning or PBR throws rather than
         * quietly rendering an unshaded approximation of it.
         *
         * @param vb             Vertex buffer to read from.
         * @param world          World matrix.
         * @param view           View matrix.
         * @param projection     Projection matrix.
         * @param primitive      Primitive topology.
         * @param primitiveCount Number of primitives.
         * @param params         Per-draw effect parameters.
         */
        void DrawPrimitivesEx(const IVertexBufferBackend& vb,
                              const Matrix& world,
                              const Matrix& view,
                              const Matrix& projection,
                              PrimitiveType primitive,
                              int primitiveCount,
                              const GpuDrawParams& params) override;

        /**
         * @brief Indexed counterpart of DrawPrimitivesEx(); same capability boundary.
         * @param vb             Vertex buffer to read from.
         * @param ib             Index buffer to read from.
         * @param world          World matrix.
         * @param view           View matrix.
         * @param projection     Projection matrix.
         * @param primitive      Primitive topology.
         * @param primitiveCount Number of primitives.
         * @param params         Per-draw effect parameters.
         */
        void DrawIndexedPrimitivesEx(const IVertexBufferBackend& vb,
                                     const IIndexBufferBackend& ib,
                                     const Matrix& world,
                                     const Matrix& view,
                                     const Matrix& projection,
                                     PrimitiveType primitive,
                                     int primitiveCount,
                                     const GpuDrawParams& params) override;

        /**
         * @brief Reports which features this backend's current baseline actually supports.
         * @param capability Feature to query.
         * @return True when the feature is implemented and usable.
         */
        [[nodiscard]] bool SupportsCapability(CNA::GraphicsCapability capability) const override;

        /**
         * @brief Returns the back buffer's device-clamped MSAA sample count.
         * @return Sample count; 1 when multisampling is off.
         */
        [[nodiscard]] int GetMultiSampleCount() const override;

        /**
         * @brief Returns the native API sokol_gfx was compiled to dispatch onto. NOXNA.
         * @return The resolved SokolApiEXT value for this build.
         */
        NOXNA [[nodiscard]] static SokolApiEXT GetApiEXT();

        /**
         * @brief Returns the maximum number of sprite quads one frame may draw. NOXNA.
         *
         * The sprite streaming buffer is allocated once at construction, so a frame that exceeds
         * this cap raises an error rather than silently dropping sprites.
         *
         * @return Per-frame sprite quad capacity.
         */
        NOXNA [[nodiscard]] static int GetMaxSpriteQuadsPerFrameEXT();

        /**
         * @brief Draws one flushed run of sprite quads. NOXNA, called by SokolSpriteBatchBackend.
         *
         * @param texture  Source texture for the run.
         * @param vertices Quad vertices, four per quad in top-left, top-right, bottom-right,
         *                 bottom-left order.
         * @param transform Transform applied on top of the orthographic projection.
         * @param filter   Raw TextureFilter value.
         * @param addressU Raw TextureAddressMode value for U.
         * @param addressV Raw TextureAddressMode value for V.
         */
        NOXNA void DrawSpriteRunEXT(const ITextureBackend& texture,
                                    const std::vector<SokolSpriteBatchBackend::Vertex>& vertices,
                                    const Matrix& transform,
                                    int filter, int addressU, int addressV);

        /**
         * @brief Returns the logical (virtual) presentation size. NOXNA.
         * @param width  Receives the logical width in pixels.
         * @param height Receives the logical height in pixels.
         */
        NOXNA void GetLogicalSizeEXT(int& width, int& height) const;

        /**
         * @brief Returns the physical window size in pixels. NOXNA.
         * @param width  Receives the physical width.
         * @param height Receives the physical height.
         */
        NOXNA void GetPhysicalSizeEXT(int& width, int& height) const;

    private:
        struct PipelineKey
        {
            int colorSrcBlend;
            int alphaSrcBlend;
            int colorDstBlend;
            int alphaDstBlend;
            int colorBlendFunc;
            int alphaBlendFunc;
            int colorWriteChannels;
            bool blendEnabled;
            bool depthTestEnabled;
            bool depthWriteEnabled;
            int depthFunc;
            /// See Pipeline3DKey's identical field for why this has to be part of the key.
            bool hasDepthAttachment;
            /// See Pipeline3DKey's identical field: sokol_gfx requires a pipeline's sample_count to
            /// match the pass it draws into, and a RenderTarget2D's MSAA count is independent of
            /// both the swapchain's and every other render target's.
            int sampleCount;

            bool operator==(const PipelineKey& other) const;
        };

        struct PipelineKeyHash
        {
            std::size_t operator()(const PipelineKey& key) const;
        };

        /**
         * @brief Which of the three 3D shader programs a Pipeline3DKey targets.
         *
         * Each kind has its own attribute set (see Pipeline3DKey), so the pipeline object -- and
         * therefore the cache key -- must name which shader it was built against, not just the
         * vertex layout.
         */
        enum class Shader3DKind
        {
            /** @brief colored3d.glsl -- vertex colour only, no texture, no lighting. */
            Colored,
            /** @brief textured3d.glsl -- texture + vertex colour, no lighting. */
            Textured,
            /** @brief lit3d.glsl -- ambient + up to 3 directional lights, always samples a texture
             *  (a real one, or the backend's 1x1 white fallback). */
            Lit
        };

        /**
         * @brief Identity of a 3D pipeline: which shader, the shared render state, and the exact
         * vertex layout and topology it was built for.
         *
         * The layout has to be part of the key because sokol_gfx bakes it into the pipeline
         * object, unlike the sprite path where every draw shares one fixed vertex format.
         */
        struct Pipeline3DKey
        {
            Shader3DKind kind;
            int colorSrcBlend;
            int alphaSrcBlend;
            int colorDstBlend;
            int alphaDstBlend;
            int colorBlendFunc;
            int alphaBlendFunc;
            int colorWriteChannels;
            bool blendEnabled;
            bool depthTestEnabled;
            bool depthWriteEnabled;
            int depthFunc;
            /// Whether the pass this pipeline is used in has a real depth-stencil attachment at
            /// all -- true for the swapchain always, and for a render target only when it was
            /// created with a depth format. sokol_gfx bakes the attachment's pixel format into the
            /// pipeline and rejects SG_PIXELFORMAT_DEPTH_STENCIL against a pass with none, so this
            /// has to be part of the key, not just depthTestEnabled/depthWriteEnabled.
            bool hasDepthAttachment;
            /// Whether the currently active pass has a real depth-stencil attachment -- see
            /// hasDepthAttachment's own comment. sokol_gfx bakes `desc.stencil` into the pipeline
            /// too, and rejects a stencil-enabled pipeline against a pass with no attachment at
            /// all, so stencil is only ever requested when this (and hasDepthAttachment, which is
            /// identical in practice -- both ask about the same one attachment) both hold.
            bool stencilEnabled;
            int stencilFunc;
            int stencilPass;
            int stencilFail;
            int stencilDepthFail;
            /// Front-face values when twoSidedStencilMode is false; CounterClockwiseStencil* below
            /// are only meaningful when it is true.
            int ccwStencilFunc;
            int ccwStencilPass;
            int ccwStencilFail;
            int ccwStencilDepthFail;
            bool twoSidedStencilMode;
            /// sokol_gfx's sg_stencil_state.read_mask/write_mask/ref are uint8_t and baked into the
            /// pipeline object (unlike most APIs' dynamic stencil reference), so all three are part
            /// of this key.
            int stencilMask;
            int stencilWriteMask;
            int referenceStencil;
            /// RasterizerState.DepthBias/SlopeScaleDepthBias -- also baked into sg_depth_state, so
            /// also part of the key. Compared/hashed as exact bit patterns: both are stored,
            /// never computed, values straight from RasterizerState, so the same RasterizerState
            /// always produces bit-identical floats here.
            float depthBias;
            float slopeScaleDepthBias;
            /// The active pass's real sample count (the bound RenderTarget2D's MSAA count if one is
            /// bound and multisampled, otherwise the swapchain's) -- sokol_gfx bakes sample_count
            /// into the pipeline object and rejects a mismatch against the pass it draws into
            /// (plan_sokol.md SOKOL-26).
            int sampleCount;
            int cullMode;
            int primitiveType;
            /// Raw sg_index_type: sokol_gfx bakes the index type into the pipeline, and rejects
            /// both an indexed pipeline used without an index buffer and a width mismatch, so
            /// non-indexed / 16-bit / 32-bit draws each need their own pipeline object.
            int indexType;
            int stride;
            int positionOffset;
            int positionFormat;
            /// Byte offset of the Color element, or -1 when the declaration has none.
            int colorOffset;
            int colorFormat;
            /// Byte offset of the TextureCoordinate element, or -1 when absent (Colored kind
            /// never reads this; Lit kind only needs it when a real texture is bound).
            int texCoordOffset;
            int texCoordFormat;
            /// Byte offset of the Normal element, or -1 when absent (only Lit kind reads this).
            int normalOffset;
            int normalFormat;

            bool operator==(const Pipeline3DKey& other) const;
        };

        struct Pipeline3DKeyHash
        {
            std::size_t operator()(const Pipeline3DKey& key) const;
        };

        struct SamplerKey
        {
            int filter;
            int addressU;
            int addressV;
            int maxAnisotropy;

            bool operator==(const SamplerKey& other) const;
        };

        struct SamplerKeyHash
        {
            std::size_t operator()(const SamplerKey& key) const;
        };

        void CreateGpuContext(SDL_Window* window, int multiSampleCount);
        void SetupSokol();
        void CreateSpriteResources();
        void BeginPassIfNeeded();
        void EndPassIfActive();
        void QueueClear(bool color, float r, float g, float b, float a,
                        bool depth, float depthValue,
                        bool stencil, int stencilValue);
        void DrawColored3D(const IVertexBufferBackend& vb,
                           const IIndexBufferBackend* ib,
                           const Matrix& world,
                           const Matrix& view,
                           const Matrix& projection,
                           PrimitiveType primitive,
                           int primitiveCount,
                           const GpuDrawParams& params);
        [[nodiscard]] std::uint32_t Get3DPipeline(const Pipeline3DKey& key);
        [[nodiscard]] SokolTextureBackend& GetDefaultWhiteTexture();
        [[nodiscard]] std::uint32_t GetSpritePipeline();
        [[nodiscard]] std::uint32_t GetSampler(int filter, int addressU, int addressV,
                                               int maxAnisotropy);
        void ApplyPendingViewportAndScissor();
        /// Shared by SetRenderTarget2D and the single-RenderTarget2D case of SetRenderTargets --
        /// both public GraphicsDevice entry points (the singular SetRenderTarget(RenderTarget2D*)
        /// convenience and the vector-based SetRenderTargets) reach the backend through genuinely
        /// different IGraphicsBackend virtuals, so there is no single call site to put this in.
        void BindSingleRenderTarget2D(SokolRenderTargetBackend* rt);
        /// Cube-face counterpart of BindSingleRenderTarget2D -- mutually exclusive with it, so each
        /// clears the other's tracking field.
        void BindRenderTargetCubeFace(SokolRenderTargetCubeBackend* rt, int face);
        /// Returns the size (in pixels) of whatever is currently the draw/clear target: the bound
        /// render target when one is active, otherwise the window's physical size. RT pixel space
        /// has no logical/physical distinction (no letterboxing), unlike the back buffer.
        void GetCurrentTargetSizeEXT(int& width, int& height) const;
        /// Whether the currently active pass (swapchain, RenderTarget2D or RenderTargetCube face)
        /// has a real depth-stencil attachment. Shared by BeginPassIfNeeded, GetSpritePipeline and
        /// DrawColored3D's Pipeline3DKey construction -- sokol_gfx bakes the attachment's pixel
        /// format into every pipeline, so all three need the identical answer.
        [[nodiscard]] bool CurrentPassHasDepthStencilAttachmentEXT() const;
        /// Returns the real sample count of whatever is currently the draw target: a bound
        /// RenderTarget2D's own (device-clamped) MultiSampleCount when it is multisampled, 1 when
        /// it is bound but not, and the swapchain's sampleCount_ otherwise. A RenderTargetCube face
        /// is never multisampled -- sokol_gfx's own validation layer hard-rejects a CUBE image with
        /// sample_count > 1 -- so it always answers 1. Shared by GetSpritePipeline and
        /// DrawColored3D's Pipeline3DKey construction, mirroring
        /// CurrentPassHasDepthStencilAttachmentEXT's rationale.
        [[nodiscard]] int CurrentPassSampleCountEXT() const;

        SDL_Window* window_ = nullptr;
        void* glContext_ = nullptr;
        int virtualWidth_ = 0;
        int virtualHeight_ = 0;
        CnaPresentationMode presentationMode_ = CnaPresentationMode::FixedHeightDynamicWidth;
        int sampleCount_ = 1;
        int swapInterval_ = 1;

        bool passActive_ = false;
        /// The active off-screen render target, or null for the back buffer. Not owned -- the
        /// public RenderTarget2D (via its backend unique_ptr) owns the lifetime; a target that
        /// outlives its binding is unbound the same way GraphicsDevice::Dispose() unbinds anything
        /// else, by the caller issuing SetRenderTarget(nullptr)/SetRenderTargets({}) first.
        SokolRenderTargetBackend* currentRenderTarget_ = nullptr;
        /// The active cube-face render target, or null. Mutually exclusive with
        /// currentRenderTarget_ -- BindSingleRenderTarget2D and BindRenderTargetCubeFace each
        /// clear the other. Not owned, same lifetime contract as currentRenderTarget_.
        SokolRenderTargetCubeBackend* currentRenderTargetCube_ = nullptr;
        /// Which face of currentRenderTargetCube_ is the active colour attachment. Meaningless
        /// while currentRenderTargetCube_ is null.
        int currentRenderTargetCubeFace_ = 0;
        /// Pending pass action for the next BeginPassIfNeeded(), reset to "load" after each use so
        /// only an explicit Clear* call ever discards existing content.
        bool pendingClearColor_ = false;
        float pendingClear_[4] = {0.0f, 0.0f, 0.0f, 1.0f};
        bool pendingClearDepth_ = false;
        float pendingDepth_ = 1.0f;
        bool pendingClearStencil_ = false;
        int pendingStencil_ = 0;

        int blendColorSrc_ = 0;   // Blend::One
        int blendAlphaSrc_ = 0;   // Blend::One
        int blendColorDst_ = 1;   // Blend::Zero
        int blendAlphaDst_ = 1;   // Blend::Zero
        int blendColorFunc_ = 0;  // BlendFunction::Add
        int blendAlphaFunc_ = 0;  // BlendFunction::Add
        int colorWriteChannels_ = 15;
        bool blendEnabled_ = false;
        float blendFactor_[4] = {1.0f, 1.0f, 1.0f, 1.0f};

        bool depthTestEnabled_ = false;
        bool depthWriteEnabled_ = true;
        int depthFunc_ = 3;       // CompareFunction::LessEqual
        int referenceStencil_ = 0;

        bool stencilEnabled_ = false;
        int stencilFunc_ = 0;         // CompareFunction::Always
        int stencilPass_ = 0;         // StencilOperation::Keep
        int stencilFail_ = 0;         // StencilOperation::Keep
        int stencilDepthFail_ = 0;    // StencilOperation::Keep
        int ccwStencilFunc_ = 0;      // CompareFunction::Always
        int ccwStencilPass_ = 0;      // StencilOperation::Keep
        int ccwStencilFail_ = 0;      // StencilOperation::Keep
        int ccwStencilDepthFail_ = 0; // StencilOperation::Keep
        bool twoSidedStencilMode_ = false;
        int stencilMask_ = 0x7FFFFFFF;      // DepthStencilState.Default; truncated to 0xFF on use
        int stencilWriteMask_ = 0x7FFFFFFF; // ditto

        bool scissorEnabled_ = false;
        int scissorRect_[4] = {0, 0, 0, 0};
        bool viewportSet_ = false;
        int viewportRect_[4] = {0, 0, 0, 0};
        /// Viewport.MinDepth/MaxDepth (plan_sokol.md SOKOL-37). Applied via a raw glDepthRangef call
        /// in ApplyPendingViewportAndScissor -- see SetViewport's own doc comment for why sokol_gfx
        /// itself has no equivalent call.
        float viewportMinDepth_ = 0.0f;
        float viewportMaxDepth_ = 1.0f;

        int cullMode_ = 0;         // CullMode::None
        int fillMode_ = 0;         // FillMode::Solid
        /// RasterizerState.DepthBias/SlopeScaleDepthBias, mapped straight onto sg_depth_state's
        /// own bias/bias_slope_scale -- the same values EasyGL/Vulkan pass to
        /// glPolygonOffset(slopeScaleDepthBias, depthBias), matching FNA's own OpenGL driver.
        float depthBias_ = 0.0f;
        float slopeScaleDepthBias_ = 0.0f;

        /// Per-slot SamplerState, mirroring GraphicsDevice.SamplerStates -- read by the textured/
        /// lit 3D draw paths for texture unit 0. Defaults match XNA's own SamplerState default
        /// (equivalent to LinearWrap, MaxAnisotropy 4). SpriteBatch does not read this array: it
        /// receives its filter/address values directly from SpriteBatch.Begin's own SamplerState.
        struct SamplerSlotState
        {
            int filter = 0;         // TextureFilter::Linear
            int addressU = 0;       // TextureAddressMode::Wrap
            int addressV = 0;       // TextureAddressMode::Wrap
            int maxAnisotropy = 4;
        };
        static constexpr int kMaxSamplerSlots = 16;
        SamplerSlotState samplerSlots_[kMaxSamplerSlots];

        std::uint32_t spriteShaderId_ = 0;
        std::uint32_t spriteVertexBufferId_ = 0;
        std::uint32_t spriteIndexBufferId_ = 0;
        std::uint32_t colored3dShaderId_ = 0;
        std::uint32_t textured3dShaderId_ = 0;
        std::uint32_t lit3dShaderId_ = 0;
        /// Lazily created 1x1 opaque-white texture, bound by the Lit shader whenever
        /// GpuDrawParams::textureEnabled is false -- lets one shader serve both "textured and lit"
        /// and "vertex-coloured and lit" (the multiply is then a no-op), the same convention the
        /// EasyGL backend's own default-white-texture fallback uses.
        std::unique_ptr<SokolTextureBackend> defaultWhiteTexture_;
        std::unordered_map<PipelineKey, std::uint32_t, PipelineKeyHash> pipelineCache_;
        std::unordered_map<Pipeline3DKey, std::uint32_t, Pipeline3DKeyHash> pipeline3dCache_;
        std::unordered_map<SamplerKey, std::uint32_t, SamplerKeyHash> samplerCache_;
    };
}
