// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/CNAHelper.hpp"
#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "System/NotSupportedException.hpp"

#include <array>
#include <cstdint>
#include <cstddef>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

struct SDL_Window;

namespace CNA::Internal::Renderers::Sokol
{
    class SokolRenderer;

    /**
     * @brief Identifies the native API sokol_gfx was compiled to dispatch onto. CNAEXT.
     *
     * Mirrors the CNA_SOKOL_API CMake option (cmake/RendererSelection.cmake) so a caller can read
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
     * @brief Renderer handle for a 2D texture, backed by a sokol_gfx image plus its texture view.
     *
     * sokol_gfx has no partial-image upload and permits at most one sg_update_image() per image
     * per frame, which a caller writing several mip levels in one frame violates immediately. This
     * class therefore keeps a CPU shadow of every mip level and recreates the whole image as an
     * immutable one whenever any level changes -- creation with initial data carries no per-frame
     * restriction, the same reasoning SokolVertexBufferRenderer::SetData uses.
     */
    class SokolTextureRenderer : public ITextureRenderer
    {
    public:
        /**
         * @brief Creates a sokol_gfx image from decoded RGBA8 pixels and a matching texture view.
         *
         * @param data Source image; data.mipLevels mip levels are allocated, level 0 is uploaded
         *             from data.pixels and every further level starts out transparent black until
         *             UpdatePixelsLevel() supplies it.
         */
        explicit SokolTextureRenderer(const ImageData& data);

        /** @brief Destroys the sokol_gfx view and image owned by this texture. */
        ~SokolTextureRenderer() override;

        SokolTextureRenderer(const SokolTextureRenderer&) = delete;
        SokolTextureRenderer& operator=(const SokolTextureRenderer&) = delete;

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
         * Satisfies the ITextureRenderer::GetData contract from the shadow rather than the GPU:
         * this renderer has no render targets yet, so every texture's content came from a
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
         * @brief Returns the raw sokol_gfx image handle id backing this texture. CNAEXT.
         * @return sg_image id, or 0 when creation failed.
         */
        CNAEXT [[nodiscard]] std::uint32_t GetImageIdEXT() const { return imageId_; }

        /**
         * @brief Returns the raw sokol_gfx texture-view handle id used to bind this texture. CNAEXT.
         * @return sg_view id, or 0 when creation failed.
         */
        CNAEXT [[nodiscard]] std::uint32_t GetViewIdEXT() const { return viewId_; }

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
     * @brief Renderer handle for a 2D off-screen render target.
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
     * target's depth back), matching every other renderer's MSAA depth handling.
     *
     * plan_sokol.md SOKOL-39: when @p mipMap is true, `colorImageId_` (the single-sample resolve/
     * only colour image) is allocated with a full mip chain (`sg_image_desc.num_mipmaps` -- GL
     * storage for every level, via sokol_gfx's own `glTexStorage2D`-based allocation, exists
     * immediately even though only level 0 is ever rendered into: there is no public XNA API to
     * bind a specific RenderTarget2D mip level as the active target). `RegenerateMipmapsIfNeededEXT()`
     * calls a raw `glGenerateMipmap` on unbind, mirroring `EasyGLRenderTargetRenderer`'s own
     * "auto-generate on unbind" contract (matching real D3D9 XNA's `D3DUSAGE_AUTOGENMIPMAP`
     * semantics) -- the same GL-escape-hatch discipline `ReadColorImagePixelsViaGL`/
     * `SokolOcclusionQueryRenderer` already use.
     *
     * `GetData()` reads back any of the allocated levels (not just 0) via the same throwaway-GL-FBO
     * approach, attaching the requested mip level instead of always level 0.
     */
    class SokolRenderTargetRenderer : public IRenderTargetRenderer
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
         * @param mipMap      True to allocate the full mip chain (regenerated on unbind).
         */
        SokolRenderTargetRenderer(int width, int height, bool hasDepthStencil, int multiSampleCount,
                                 bool mipMap);

        /** @brief Destroys every sokol_gfx view and image owned by this target. */
        ~SokolRenderTargetRenderer() override;

        SokolRenderTargetRenderer(const SokolRenderTargetRenderer&) = delete;
        SokolRenderTargetRenderer& operator=(const SokolRenderTargetRenderer&) = delete;

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


        /** @brief Bind is driven entirely by SokolRenderer's own bound-target tracking;
         *         this override exists only to satisfy the pure-virtual interface. */
        void BindAsRenderTarget() override;

        /** @brief Unbind is driven entirely by SokolRenderer's own bound-target tracking;
         *         this override exists only to satisfy the pure-virtual interface. */
        void UnbindAsRenderTarget() override;

        /**
         * @brief Returns whether this target actually allocated a depth-stencil attachment.
         * @param depthFormatWasRequested Unused: this renderer allocates one iff it was asked to.
         * @return True if a depth-stencil attachment exists.
         */
        [[nodiscard]] bool HasRealDepthBuffer(bool depthFormatWasRequested) const override;

        /**
         * @brief Reads back a rectangle of this target's colour content via a throwaway GL FBO.
         *
         * plan_sokol.md SOKOL-38/39. GL-only. @p level may be any allocated mip level (0 when
         * `mipMap=false`, 0..LevelCount-1 otherwise).
         *
         * @param level      Requested mip level; out of range returns false.
         * @param x          Left edge of the requested region, in pixels (that level's own space).
         * @param y          Top edge of the requested region, in pixels.
         * @param w          Width of the requested region, in pixels.
         * @param h          Height of the requested region, in pixels.
         * @param data       Destination for tightly packed RGBA8 rows, top row first.
         * @param dataLength Size of @p data in bytes.
         * @return True if the whole region was written; false on a non-GL `CNA_SOKOL_API`, an
         *         out-of-range request, or an out-of-range level.
         */
        [[nodiscard]] bool GetData(int level, int x, int y, int w, int h,
                                   void* data, int dataLength) const override;

        /**
         * @brief Returns the real, driver-clamped MSAA sample count this target was created with.
         * @return Sample count greater than 1 when MSAA is active; 0 when it is not.
         */
        [[nodiscard]] int GetMultiSampleCount() const override { return multiSampleCount_; }

        /**
         * @brief Regenerates every mip level above 0 from the current level-0 content via a raw
         * `glGenerateMipmap`, GL-only. No-op when this target was not created with `mipMap=true`.
         * Called on unbind, mirroring `EasyGLRenderTargetRenderer`'s identical "auto-generate on
         * unbind" contract (plan_sokol.md SOKOL-39). CNAEXT.
         */
        CNAEXT void RegenerateMipmapsIfNeededEXT() const;

        /**
         * @brief Returns the raw sokol_gfx colour image handle id -- the single-sample image that
         * is sampled as a texture (the MSAA resolve target when MSAA is active). CNAEXT.
         * @return sg_image id, or 0 when creation failed.
         */
        CNAEXT [[nodiscard]] std::uint32_t GetColorImageIdEXT() const { return colorImageId_; }

        /**
         * @brief Returns the raw sokol_gfx colour-attachment view handle id, used when this
         * target is the active render target. This names the multisample image's attachment view
         * when MSAA is active, and the single-sample image's otherwise. CNAEXT.
         * @return sg_view id, or 0 when creation failed.
         */
        CNAEXT [[nodiscard]] std::uint32_t GetColorAttachmentViewIdEXT() const { return colorAttachmentViewId_; }

        /**
         * @brief Returns the raw sokol_gfx resolve-attachment view handle id, wired into a pass's
         * `attachments.resolves[0]` so sokol_gfx resolves the MSAA colour image into the
         * single-sample image at `sg_end_pass()`. CNAEXT.
         * @return sg_view id, or 0 when this target is not multisampled.
         */
        CNAEXT [[nodiscard]] std::uint32_t GetResolveAttachmentViewIdEXT() const { return resolveAttachmentViewId_; }

        /**
         * @brief Returns the raw sokol_gfx texture-view handle id, used to sample this target as
         * an ordinary texture in a later pass. CNAEXT.
         * @return sg_view id, or 0 when creation failed.
         */
        CNAEXT [[nodiscard]] std::uint32_t GetColorTextureViewIdEXT() const { return colorTextureViewId_; }

        /**
         * @brief Returns the raw sokol_gfx depth-stencil-attachment view handle id. CNAEXT.
         * @return sg_view id, or 0 when this target has no depth-stencil attachment.
         */
        CNAEXT [[nodiscard]] std::uint32_t GetDepthStencilAttachmentViewIdEXT() const { return depthStencilAttachmentViewId_; }

    private:
        int width_ = 0;
        int height_ = 0;
        int multiSampleCount_ = 0;
        bool mipMap_ = false;
        int levelCount_ = 1;
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
     * @brief Renderer handle for a RenderTargetCube: a real sokol_gfx CUBE image plus one
     * colour-attachment view per face and a single shared depth-stencil attachment.
     *
     * plan_sokol.md SOKOL-26. The depth-stencil buffer is genuinely ONE resource shared by all six
     * faces (matching FNA3D's own cube render-target convention -- see
     * rendertarget_depthstencil_usage_test.cpp's U2 check), not per-face: it is a plain 2D
     * depth-stencil image, reused as the pass's depth attachment regardless of which face is
     * currently the colour attachment.
     *
     * Unlike `SokolRenderTargetRenderer`, this class never attempts MSAA: sokol_gfx's own validation
     * layer hard-rejects a CUBE image with `sample_count > 1`
     * (`VALIDATE_IMAGEDESC_ATTACHMENT_MSAA_CUBE_IMAGE`), so a multisampled `RenderTargetCube` is a
     * permanent sokol_gfx API boundary, not a "not implemented yet" gap -- `multiSampleCount` is
     * always silently clamped to 1 (`GetMultiSampleCount()` always answers 0), the same declared
     * boundary `WebGPURenderer`/`D3D9RenderTargetCubeRenderer` report for their own reasons.
     */
    class SokolRenderTargetCubeRenderer : public IRenderTargetCubeRenderer
    {
    public:
        /**
         * @brief Creates the six colour-attachment views (and optional shared depth-stencil
         * attachment) for a cube render target.
         * @param size            Edge length of each face in pixels.
         * @param hasDepthStencil True to also allocate a shared depth-stencil attachment.
         * @param mipMap          True to allocate the full mip chain (regenerated on unbind).
         */
        SokolRenderTargetCubeRenderer(int size, bool hasDepthStencil, bool mipMap);

        /** @brief Destroys every sokol_gfx view and image owned by this target. */
        ~SokolRenderTargetCubeRenderer() override;

        SokolRenderTargetCubeRenderer(const SokolRenderTargetCubeRenderer&) = delete;
        SokolRenderTargetCubeRenderer& operator=(const SokolRenderTargetCubeRenderer&) = delete;

        /**
         * @brief Returns the edge length of each face in pixels.
         * @return Face edge length.
         */
        [[nodiscard]] int GetSize() const override;

        /** @brief Bind is driven entirely by SokolRenderer's own bound-target tracking;
         *         this override exists only to satisfy the pure-virtual interface. */
        void BindAsRenderTargetFace(int face) override;

        /** @brief Unbind is driven entirely by SokolRenderer's own bound-target tracking;
         *         this override exists only to satisfy the pure-virtual interface. */
        void UnbindAsRenderTarget() override;

        /**
         * @brief Returns whether this target actually allocated a depth-stencil attachment.
         * @param depthFormatWasRequested Unused: this renderer allocates one iff it was asked to.
         * @return True if a depth-stencil attachment exists.
         */
        [[nodiscard]] bool HasRealDepthBuffer(bool depthFormatWasRequested) const override;

        /**
         * @brief Reads back a rectangle of one face's colour content via a throwaway GL FBO.
         *
         * plan_sokol.md SOKOL-38/39. GL-only. @p level may be any allocated mip level.
         *
         * @param face       Cube face index (0=+X, 1=-X, 2=+Y, 3=-Y, 4=+Z, 5=-Z).
         * @param level      Requested mip level; out of range returns false.
         * @param x          Left edge of the requested region, in texels (that level's own space).
         * @param y          Top edge of the requested region, in texels.
         * @param w          Width of the requested region, in texels.
         * @param h          Height of the requested region, in texels.
         * @param data       Destination for tightly packed RGBA8 rows, top row first.
         * @param dataLength Size of @p data in bytes.
         * @return True if the whole region was written; false on a non-GL `CNA_SOKOL_API`, an
         *         out-of-range request, an out-of-range level, or an invalid @p face.
         */
        [[nodiscard]] bool GetData(int face, int level, int x, int y, int w, int h,
                                   void* data, int dataLength) const override;

        /**
         * @brief Regenerates every mip level above 0 (on all six faces) from the current level-0
         * content via a raw `glGenerateMipmap`, GL-only. No-op when this target was not created
         * with `mipMap=true`. Called on unbind (plan_sokol.md SOKOL-39). CNAEXT.
         */
        CNAEXT void RegenerateMipmapsIfNeededEXT() const;

        /**
         * @brief Returns the raw sokol_gfx cube image handle id. CNAEXT.
         * @return sg_image id, or 0 when creation failed.
         */
        CNAEXT [[nodiscard]] std::uint32_t GetImageIdEXT() const { return imageId_; }

        /**
         * @brief Returns the raw sokol_gfx colour-attachment view handle id for one face. CNAEXT.
         * @param face Cube face index (0=+X, 1=-X, 2=+Y, 3=-Y, 4=+Z, 5=-Z).
         * @return sg_view id, or 0 when creation failed or @p face is out of range.
         */
        CNAEXT [[nodiscard]] std::uint32_t GetColorAttachmentViewIdEXT(int face) const
        {
            return (face >= 0 && face < 6) ? colorAttachmentViewIds_[static_cast<std::size_t>(face)] : 0;
        }

        /**
         * @brief Returns the raw sokol_gfx texture-view handle id, used to sample the whole cube.
         * CNAEXT.
         * @return sg_view id, or 0 when creation failed.
         */
        CNAEXT [[nodiscard]] std::uint32_t GetTextureViewIdEXT() const { return textureViewId_; }

        /**
         * @brief Returns the raw sokol_gfx depth-stencil-attachment view handle id, shared by every
         * face. CNAEXT.
         * @return sg_view id, or 0 when this target has no depth-stencil attachment.
         */
        CNAEXT [[nodiscard]] std::uint32_t GetDepthStencilAttachmentViewIdEXT() const
        {
            return depthStencilAttachmentViewId_;
        }

    private:
        int size_ = 0;
        bool mipMap_ = false;
        int levelCount_ = 1;
        std::uint32_t imageId_ = 0;
        std::array<std::uint32_t, 6> colorAttachmentViewIds_{};
        std::uint32_t textureViewId_ = 0;
        std::uint32_t depthStencilImageId_ = 0;
        std::uint32_t depthStencilAttachmentViewId_ = 0;
    };

    /**
     * @brief Renderer handle for a cube texture: a real `sg_image`/`sg_view` pair, sampled by
     * `EnvironmentMapEffect` (plan_sokol.md SOKOL-34).
     *
     * A CPU-side RGBA8 shadow (`levels_[level][face]`) is still kept, mirroring
     * `SokolTextureRenderer`'s own shape: sokol_gfx has no sub-image upload, so every `SetData()`
     * recreates the whole immutable image from the shadow (`RecreateImage()`), the same "creating
     * an immutable image with fresh initial data has no per-frame update-count limit, unlike
     * `sg_update_image()`" reasoning `SokolTextureRenderer::RecreateImage()`'s own comment gives --
     * relevant here too since XNB/CNJ content loaders can write several faces/levels in one frame.
     * `sg_image_data.mip_levels[level]` is one contiguous range per mip level for a
     * `SG_IMAGETYPE_CUBE` image (all 6 faces concatenated, `[0]=+X,[1]=-X,[2]=+Y,[3]=-Y,[4]=+Z,
     * [5]=-Z` -- sokol_gfx.h's own `sg_image_data` doc comment), which happens to be byte-identical
     * to this class's own `face` parameter order (0=+X..5=-Z, matching
     * `Microsoft::Xna::Framework::Graphics::CubeMapFace`), so no reordering is needed when
     * concatenating the per-face shadow buffers.
     */
    class SokolTextureCubeRenderer : public ITextureCubeRenderer
    {
    public:
        /**
         * @brief Allocates zeroed RGBA8 storage for all six faces of every mip level and creates
         * the backing sokol_gfx cube image.
         * @param size   Edge length of one cube face at mip 0, in texels.
         * @param mipMap Allocate the full mip chain down to 1x1 as well as level 0.
         */
        SokolTextureCubeRenderer(int size, bool mipMap);

        /** @brief Destroys the sokol_gfx view and image owned by this cube texture. */
        ~SokolTextureCubeRenderer() override;

        SokolTextureCubeRenderer(const SokolTextureCubeRenderer&) = delete;
        SokolTextureCubeRenderer& operator=(const SokolTextureCubeRenderer&) = delete;

        /**
         * @brief Copies one face's RGBA8 sub-rectangle into this renderer's CPU storage and
         * recreates the sokol_gfx image from the updated shadow.
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
         * @brief Reads one face's stored RGBA8 sub-rectangle back from this renderer's CPU shadow.
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
         * @brief Returns the edge length of mip level 0 in texels. CNAEXT.
         * @return Cube face edge length.
         */
        CNAEXT [[nodiscard]] int GetSizeEXT() const noexcept override { return size_; }

        /**
         * @brief Returns the raw sokol_gfx texture-view handle id used to sample the whole cube.
         * CNAEXT.
         * @return sg_view id, or 0 when creation failed.
         */
        CNAEXT [[nodiscard]] std::uint32_t GetViewIdEXT() const { return viewId_; }

        /**
         * @brief Returns the raw sokol_gfx image handle id backing this cube texture. CNAEXT.
         * @return sg_image id, or 0 when creation failed.
         */
        CNAEXT [[nodiscard]] std::uint32_t GetImageIdEXT() const { return imageId_; }

    private:
        /// Face edge length at @p level, never below 1 -- mirrors TextureCube.cpp's own mip
        /// dimension helper.
        [[nodiscard]] int LevelDim(int level) const;

        void RecreateImage();
        void DestroyImage();

        int size_ = 0;
        int levelCount_ = 1;
        /// levels_[level][face] -- one tightly packed RGBA8 buffer per face per allocated mip level.
        std::vector<std::array<std::vector<std::uint8_t>, 6>> levels_;
        std::uint32_t imageId_ = 0;
        std::uint32_t viewId_ = 0;
    };

    /**
     * @brief Renderer handle for a volume (3D) texture: pure CPU-side RGBA8 storage, no sokol_gfx
     * image.
     *
     * plan_sokol.md SOKOL-27. Same rationale as `SokolTextureCubeRenderer`: nothing on this renderer
     * samples a volume texture (there is no 3D-sampler shader variant), so a real `sg_image` would
     * be a GPU resource with no consumer. Mirrors Software's own choice to leave `Texture3D`
     * unimplemented entirely -- this renderer goes one step further and at least stores real pixels.
     */
    class SokolTexture3DRenderer : public ITexture3DRenderer
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
        SokolTexture3DRenderer(int width, int height, int depth, bool mipMap);

        /**
         * @brief Copies an RGBA8 sub-volume into this renderer's CPU storage.
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
         * @brief Reads an RGBA8 sub-volume back from this renderer's CPU shadow.
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
     * @brief Renderer handle for a vertex buffer.
     *
     * plan_sokol.md SOKOL-24: the underlying sokol_gfx buffer is `dynamic_update`, sized once (to
     * the greater of the owning `VertexBuffer`'s own declared capacity and the first upload) and
     * reused across SetData() calls via `sg_update_buffer()` whenever the new data still fits and
     * this buffer has not already been updated once this frame. sokol_gfx permits at most one
     * `sg_update_buffer()` per buffer per frame; a same-frame repeat upload, or one that outgrows
     * what is currently allocated, destroys and recreates the buffer instead (the same "recreate
     * on every SetData()" behaviour this class used unconditionally before SOKOL-24, and the only
     * safe option `owner` being null degrades to, since there is then no frame counter to compare
     * against).
     */
    class SokolVertexBufferRenderer : public IVertexBufferRenderer
    {
    public:
        /**
         * @brief Creates an empty vertex buffer handle with the given capacity hint.
         * @param vertexCapacity Number of vertices the owning VertexBuffer was created for.
         * @param owner CNAEXT (plan_sokol.md SOKOL-24). Owning renderer, consulted for the current
         *              frame index so repeated same-shape uploads can reuse the sokol_gfx buffer
         *              via `sg_update_buffer()` instead of recreating it every time. Null falls
         *              back to always recreating (still correct, just not the cheap path).
         */
        explicit SokolVertexBufferRenderer(int vertexCapacity, SokolRenderer* owner = nullptr);

        /** @brief Destroys the sokol_gfx buffer owned by this vertex buffer. */
        ~SokolVertexBufferRenderer() override;

        SokolVertexBufferRenderer(const SokolVertexBufferRenderer&) = delete;
        SokolVertexBufferRenderer& operator=(const SokolVertexBufferRenderer&) = delete;

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
         * @brief Returns the byte stride of the last uploaded vertex data. CNAEXT.
         * @return Stride in bytes, or 0 if nothing has been uploaded.
         */
        CNAEXT [[nodiscard]] std::size_t GetStrideEXT() const { return stride_; }

        /**
         * @brief Returns the vertex declaration recorded by SetVertexDeclaration(). CNAEXT.
         * @return The declaration, or null when the owning VertexBuffer supplied none.
         */
        CNAEXT [[nodiscard]] const VertexDeclaration* GetDeclarationEXT() const
        {
            return hasDeclaration_ ? &declaration_ : nullptr;
        }

        /**
         * @brief Returns the raw sokol_gfx buffer handle id. CNAEXT.
         * @return sg_buffer id, or 0 when no data has been uploaded.
         */
        CNAEXT [[nodiscard]] std::uint32_t GetBufferIdEXT() const { return bufferId_; }

    private:
        int capacity_ = 0;
        int vertexCount_ = 0;
        std::size_t stride_ = 0;
        std::uint32_t bufferId_ = 0;
        SokolRenderer* ownerEXT_ = nullptr;
        /// plan_sokol.md SOKOL-24: byte size the current bufferId_ was actually created with --
        /// an sg_update_buffer() is only legal while the new data still fits inside this.
        std::size_t allocatedBytesEXT_ = 0;
        /// plan_sokol.md SOKOL-24: ownerEXT_->GetFrameIndexEXT() as of the last sg_update_buffer()
        /// on the current bufferId_, or 0 (never a real frame index) when it has not been
        /// dynamically updated since it was (re)created.
        std::uint64_t lastUpdateFrameEXT_ = 0;
        bool hasDeclaration_ = false;
        VertexDeclaration declaration_;
    };

    /// Implementation detail of RequireFaithfulDeclarationEXT below; not part of any contract.
    namespace DeclarationGuardDetail
    {
        using Microsoft::Xna::Framework::Graphics::VertexElementFormat;
        using Microsoft::Xna::Framework::Graphics::VertexElementUsage;

        /** @brief Byte width of one element of @p format, or 0 when the value is not an enumerator. */
        [[nodiscard]] inline int FormatSize(VertexElementFormat format) noexcept
        {
            switch (format)
            {
                case VertexElementFormat::Single:           return 4;
                case VertexElementFormat::Vector2:          return 8;
                case VertexElementFormat::Vector3:          return 12;
                case VertexElementFormat::Vector4:          return 16;
                case VertexElementFormat::Color:            return 4;
                case VertexElementFormat::Byte4:            return 4;
                case VertexElementFormat::Short2:           return 4;
                case VertexElementFormat::Short4:           return 8;
                case VertexElementFormat::NormalizedShort2: return 4;
                case VertexElementFormat::NormalizedShort4: return 8;
                case VertexElementFormat::HalfVector2:      return 4;
                case VertexElementFormat::HalfVector4:      return 8;
            }
            return 0;
        }

        /**
         * @brief Whether the 3D pipeline binds @p usage at all.
         *
         * The six semantics `DrawColored3D` resolves into `Pipeline3DKey`. Everything else --
         * Tangent, Binormal, Fog, PointSize, Depth, Sample, TessellateFactor -- is genuinely
         * unread by every stock shader this renderer generates, so declaring one is a superset
         * declaration rather than a misread, exactly like a declaration shorter than the selected
         * program's input list.
         */
        [[nodiscard]] inline bool IsBoundSemantic(VertexElementUsage usage) noexcept
        {
            return usage == VertexElementUsage::Position
                || usage == VertexElementUsage::Color
                || usage == VertexElementUsage::TextureCoordinate
                || usage == VertexElementUsage::Normal
                || usage == VertexElementUsage::BlendWeight
                || usage == VertexElementUsage::BlendIndices;
        }

        /** @brief Human-readable name of @p usage, for the diagnostic. */
        [[nodiscard]] inline const char* UsageName(VertexElementUsage usage) noexcept
        {
            switch (usage)
            {
                case VertexElementUsage::Position:          return "Position";
                case VertexElementUsage::Color:             return "Color";
                case VertexElementUsage::TextureCoordinate: return "TextureCoordinate";
                case VertexElementUsage::Normal:            return "Normal";
                case VertexElementUsage::Binormal:          return "Binormal";
                case VertexElementUsage::Tangent:           return "Tangent";
                case VertexElementUsage::BlendIndices:      return "BlendIndices";
                case VertexElementUsage::BlendWeight:       return "BlendWeight";
                case VertexElementUsage::Depth:             return "Depth";
                case VertexElementUsage::Fog:               return "Fog";
                case VertexElementUsage::PointSize:         return "PointSize";
                case VertexElementUsage::Sample:            return "Sample";
                case VertexElementUsage::TessellateFactor:  return "TessellateFactor";
            }
            return "?";
        }
    }

    /**
     * @brief CNAEXT. REMED-GFX-DECL-GUARD: refuses a vertex declaration this renderer cannot
     *        represent faithfully, at draw time and before any native object exists.
     *
     * The shared `RequireFaithfulVertexDeclaration()` helper is deliberately NOT reused here: it
     * models a renderer that infers its native layout from the byte stride alone and then asks
     * whether the declaration agrees with that inference. This renderer does the opposite -- it
     * programs `sg_pipeline_desc::layout` from the declaration's OWN offsets and formats -- so
     * every semantic it binds is faithful by construction, and applying the stride-table rule
     * would refuse correct draws.
     *
     * What is left are the three ways a declaration can still be misread here, plus the one way an
     * element can be silently dropped:
     *
     * - the declaration states a stride the buffer was not uploaded with, so the pipeline advances
     *   records at a pitch the data does not have and every record after the first is fetched from
     *   the wrong address;
     * - an element that does not lie wholly inside the declared record;
     * - two elements claiming the same bytes;
     * - a second set of a semantic the pipeline binds (usage index other than 0). Every stock
     *   shader here declares at most one input per semantic, so the extra set has nowhere to go
     *   and the vertex fetch would supply the shader's unbound default instead of the caller's
     *   data. A semantic the pipeline never binds at all is NOT refused -- see IsBoundSemantic().
     *
     * The check is asymmetric: only what the caller actually declared is inspected, never equality
     * against this renderer's own template. It is pure -- nothing is created, queued or bound
     * before it runs -- so a rejected draw leaves the device usable and the next valid draw works.
     * A buffer that carries no declaration at all is left to `DrawColored3D`'s own stride-table
     * fallback and its established refusals.
     *
     * Header-only by necessity: `cna_renderer_sokol` links only
     * `cna_renderer_common` and SharpRuntime, never the CNA library.
     *
     * @param declaration  The declaration the buffer carries, or null when it carries none.
     * @param uploadStride Byte stride the buffer's data was actually uploaded with.
     * @param route        Name of the draw route, for the diagnostic message.
     * @throws System::NotSupportedException When the declaration cannot be represented.
     */
    inline void RequireFaithfulDeclarationEXT(const VertexDeclaration* declaration,
                                              std::size_t uploadStride,
                                              const char* route)
    {
        namespace detail = DeclarationGuardDetail;
        using Microsoft::Xna::Framework::Graphics::VertexElement;

        if (declaration == nullptr) return;
        const std::vector<VertexElement>& elements = declaration->GetVertexElements();
        if (elements.empty()) return;
        if (uploadStride == 0) return;

        const int declaredStride = declaration->getVertexStrideProperty();
        const auto refuse = [route](const std::string& why) {
            throw System::NotSupportedException(
                std::string("Sokol renderer: this VertexDeclaration cannot be represented on the ") +
                route + " route -- " + why +
                ". The draw is refused rather than rendered from the wrong bytes.");
        };

        if (declaredStride != static_cast<int>(uploadStride))
        {
            refuse("the declaration states a stride of " + std::to_string(declaredStride) +
                   " bytes but the buffer was uploaded with a stride of " +
                   std::to_string(uploadStride) +
                   " bytes, so every record after the first would be read from the wrong address");
        }

        for (std::size_t i = 0; i < elements.size(); ++i)
        {
            const VertexElement& e = elements[i];
            const int offset = e.getOffsetProperty();
            const int size = detail::FormatSize(e.getVertexElementFormatProperty());
            const auto usage = e.getVertexElementUsageProperty();
            const int usageIndex = e.getUsageIndexProperty();
            const std::string described = std::string(detail::UsageName(usage)) + std::to_string(usageIndex) +
                                          " at offset " + std::to_string(offset);

            if (offset < 0 || size <= 0 || offset + size > declaredStride)
            {
                refuse("declared element " + described + " does not fit inside the declared " +
                       std::to_string(declaredStride) + "-byte record");
            }

            for (std::size_t j = i + 1; j < elements.size(); ++j)
            {
                const VertexElement& f = elements[j];
                const int otherOffset = f.getOffsetProperty();
                const int otherSize = detail::FormatSize(f.getVertexElementFormatProperty());
                if (offset < otherOffset + otherSize && otherOffset < offset + size)
                {
                    refuse("declared elements " + described + " and " +
                           std::string(detail::UsageName(f.getVertexElementUsageProperty())) +
                           std::to_string(f.getUsageIndexProperty()) + " at offset " +
                           std::to_string(otherOffset) + " claim the same bytes");
                }
            }

            if (usageIndex != 0 && detail::IsBoundSemantic(usage))
            {
                refuse("the declaration carries " + described +
                       ", and every stock shader here declares exactly one input per semantic, so "
                       "that set would never reach the shader");
            }
        }
    }

    /**
     * @brief Renderer handle for a 16- or 32-bit index buffer.
     *
     * plan_sokol.md SOKOL-24: same dynamic-buffer reuse strategy as SokolVertexBufferRenderer --
     * see that class's own doc comment.
     */
    class SokolIndexBufferRenderer : public IIndexBufferRenderer
    {
    public:
        /**
         * @brief Creates an empty index buffer handle with the given capacity hint.
         * @param indexCapacity  Number of indices the owning IndexBuffer was created for.
         * @param thirtyTwoBit   True when the owning IndexBuffer uses 32-bit indices.
         * @param owner CNAEXT (plan_sokol.md SOKOL-24). Same role as
         *              `SokolVertexBufferRenderer`'s own `owner` parameter.
         */
        SokolIndexBufferRenderer(int indexCapacity, bool thirtyTwoBit,
                                 SokolRenderer* owner = nullptr);

        /** @brief Destroys the sokol_gfx buffer owned by this index buffer. */
        ~SokolIndexBufferRenderer() override;

        SokolIndexBufferRenderer(const SokolIndexBufferRenderer&) = delete;
        SokolIndexBufferRenderer& operator=(const SokolIndexBufferRenderer&) = delete;

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
         * @brief Returns the raw sokol_gfx buffer handle id. CNAEXT.
         * @return sg_buffer id, or 0 when no data has been uploaded.
         */
        CNAEXT [[nodiscard]] std::uint32_t GetBufferIdEXT() const { return bufferId_; }

    private:
        void Upload(const void* data, int indexCount, std::size_t indexSize);

        int capacity_ = 0;
        int indexCount_ = 0;
        bool thirtyTwoBit_ = false;
        std::uint32_t bufferId_ = 0;
        SokolRenderer* ownerEXT_ = nullptr;
        /// plan_sokol.md SOKOL-24: see SokolVertexBufferRenderer::allocatedBytesEXT_.
        std::size_t allocatedBytesEXT_ = 0;
        /// plan_sokol.md SOKOL-24: see SokolVertexBufferRenderer::lastUpdateFrameEXT_.
        std::uint64_t lastUpdateFrameEXT_ = 0;
    };

    /**
     * @brief SpriteBatch implementation drawing through a single streamed sokol_gfx vertex buffer.
     *
     * Quads are accumulated between Begin() and End() and flushed whenever the source texture
     * changes. Each flush appends its vertices to a per-frame streaming buffer (sg_append_buffer)
     * and draws them against a pre-built, immutable quad index buffer, so an arbitrary number of
     * flushes per frame is legal despite sokol_gfx's one-update-per-buffer-per-frame rule.
     */
    class SokolSpriteBatchRenderer : public ISpriteBatchRenderer
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
         * @brief Creates a sprite batch bound to the given renderer.
         * @param renderer Owning graphics renderer; supplies the shared pipeline/sampler caches.
         */
        explicit SokolSpriteBatchRenderer(SokolRenderer& renderer);

        /** @brief Destroys this sprite batch. Shared GPU resources stay owned by the renderer. */
        ~SokolSpriteBatchRenderer() override;

        SokolSpriteBatchRenderer(const SokolSpriteBatchRenderer&) = delete;
        SokolSpriteBatchRenderer& operator=(const SokolSpriteBatchRenderer&) = delete;

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
         * @brief Sets a custom `ShaderEffect` to use for sprite rendering instead of the built-in
         * sprite shader (plan_sokol.md SOKOL-28), or null to restore it. Flushes any pending
         * sprites first, matching `EasyGLSpriteBatchRenderer::SetCustomEffect`'s own contract: a
         * flush must happen while the OLD effect (or lack of one) is still in effect, not the new
         * one.
         * @param effect Custom effect, or null for the built-in sprite shader.
         */
        void SetCustomEffect(Effect* effect) override;

        /**
         * @brief Draws a whole texture at the given position, untinted.
         * @param texture Source texture.
         * @param x       Destination left edge, in pixels.
         * @param y       Destination top edge, in pixels.
         */
        void Draw(const ITextureRenderer& texture, float x, float y) override;

        /**
         * @brief Draws a source region of a texture into a destination rectangle.
         * @param texture              Source texture.
         * @param destinationRectangle Destination rectangle, in pixels.
         * @param sourceRectangle      Source region, in texels.
         * @param color                Tint colour.
         */
        void Draw(const ITextureRenderer& texture,
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
        void Draw(const ITextureRenderer& texture,
                  const Rectangle& destinationRectangle,
                  const Rectangle& sourceRectangle,
                  const Color& color,
                  float rotation,
                  const Vector2& origin,
                  SpriteEffects effects,
                  float layerDepth) override;

    private:
        void FlushBatch();

        SokolRenderer& renderer_;
        bool begun_ = false;
        std::vector<Vertex> pendingVertices_;
        const ITextureRenderer* currentTexture_ = nullptr;
        Matrix transform_ = Matrix::getIdentityProperty();
        int pendingFilter_ = 0;   // TextureFilter::Linear
        int pendingAddressU_ = 1; // TextureAddressMode::Clamp
        int pendingAddressV_ = 1; // TextureAddressMode::Clamp
        /// Non-owning; see SetCustomEffect()'s own doc comment. Null selects the built-in sprite
        /// shader (SOKOL-16..21's cna_sprite_shader_desc).
        Effect* customEffect_ = nullptr;
    };

    /**
     * @brief Renderer handle for an occlusion query, implemented with a raw GL query object.
     *
     * plan_sokol.md SOKOL-29. sokol_gfx exposes no query API of its own, but on this renderer's
     * target platform (SOKOL_GLCORE) sokol renders through an ordinary GL context this class can
     * issue raw `glBeginQuery`/`glEndQuery` calls against directly -- a query records whatever the
     * GL context rasterizes between them regardless of which layer (sokol_gfx or this class) issued
     * the draw calls. Restricted to the GL APIs the same way `ReadBackbuffer` is:
     * `SokolRenderer::CreateOcclusionQuery` returns null on any other `CNA_SOKOL_API`.
     */
    class SokolOcclusionQueryRenderer : public IOcclusionQueryRenderer
    {
    public:
        /**
         * @brief Allocates the underlying GL query object.
         * @param owner The renderer whose GL context this query's Begin/End calls target
         *              (plan_sokol.md SOKOL-43); null is legal (matches the default constructor's
         *              prior behaviour) and simply disables cross-object coordination.
         */
        explicit SokolOcclusionQueryRenderer(SokolRenderer* owner = nullptr);

        /** @brief Destroys the GL query object. */
        ~SokolOcclusionQueryRenderer() override;

        SokolOcclusionQueryRenderer(const SokolOcclusionQueryRenderer&) = delete;
        SokolOcclusionQueryRenderer& operator=(const SokolOcclusionQueryRenderer&) = delete;

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
         *
         * plan_sokol.md SOKOL-43: OpenGL permits only one active `GL_SAMPLES_PASSED` query per
         * context at a time, not per query object -- `active_` alone cannot see a DIFFERENT
         * object's outstanding Begin(). When `owner` was supplied, this also coordinates through
         * `owner`'s single shared "which query owns the context's one active slot" tracking, so a
         * second object's overlapping Begin() is silently absorbed exactly like a repeated Begin()
         * on the same object, instead of reaching a second real `glBeginQuery` call.
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
         *         other GL query renderer, EasyGL's).
         */
        [[nodiscard]] int PixelCount() const override;

    private:
        /// plan_sokol.md SOKOL-43: the renderer whose context-wide active-query slot this instance
        /// coordinates through; null disables coordination (see the constructor's own doc).
        SokolRenderer* owner_ = nullptr;
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
     * @brief Renderer handle for a custom `ShaderEffect`: real runtime GLSL compilation via raw GL
     * calls bracketed by `sg_reset_state_cache()` (plan_sokol.md SOKOL-28), GL-only (matching
     * `ReadBackbuffer`/`SokolOcclusionQueryRenderer`'s own `CNA_SOKOL_HAS_GL_READBACK` boundary).
     *
     * A custom-effect draw bypasses `sg_shader`/`sg_pipeline` entirely: sokol_gfx's own uniform
     * model is block-oriented and slot-numbered (`sg_apply_uniforms(slot, wholeBlob)`), incompatible
     * with `IEffectRenderer`'s per-name, call-anytime `SetUniformXxx()` contract without a runtime
     * GLSL-uniform-block parser this codebase has no other use for. Instead this class issues real
     * `glCreateProgram`/`glCompileShader`/`glLinkProgram` at `CompileProgram()` time and
     * `glUseProgram`/`glGetUniformLocation`/`glUniform*`/`glDrawArrays`/`glDrawElements` directly
     * against the live GL context sokol_gfx itself renders through -- explicitly supported by
     * sokol_gfx's own documented "call `sg_reset_state_cache()` after calling native 3D-API
     * functions, and before calling any sokol_gfx function" interleaving contract, the same escape
     * hatch `ReadBackbuffer`/`SokolOcclusionQueryRenderer` already use for their own raw GL calls
     * (those never mutate program/VAO binding state the way a custom draw does, so they need no
     * `sg_reset_state_cache()` of their own -- this class's draw-time bracketing, in
     * `SokolRenderer::DrawColored3D`, is new).
     *
     * Ported from `EasyGLEffectRenderer`'s own shape and behaviour: no upfront reflection, each
     * `SetUniformXxx()` call does its own `glGetUniformLocation()` lookup and silently no-ops for
     * an unknown name. Vertex attributes use the SAME "`layout(location=N)` == Nth field of the
     * `VertexDeclaration`" fixed-position convention `EasyGLRenderer::ApplyLayout()` already
     * established, not name-based reflection -- matching the caller-supplied GLSL's own expected
     * contract 1:1 with no new parsing/reflection code needed for attributes either.
     *
     * `BindTexture3D()` is not overridden (the interface's own silent no-op default applies):
     * `SokolTexture3DRenderer` has no real `sg_image` to query a GL texture handle from (it remains
     * a pure CPU-shadow store, unaffected by this task).
     */
    class SokolEffectRenderer : public IEffectRenderer
    {
    public:
        SokolEffectRenderer() = default;

        /** @brief Deletes the compiled GL program, if any. */
        ~SokolEffectRenderer() override;

        SokolEffectRenderer(const SokolEffectRenderer&) = delete;
        SokolEffectRenderer& operator=(const SokolEffectRenderer&) = delete;

        bool CompileProgram(const std::string& vertSrc, const std::string& fragSrc) override;
        void Bind() override;
        void Unbind() override;
        [[nodiscard]] bool IsValid() const override;
        [[nodiscard]] std::string GetCompileError() const override;
        void SetUniformFloat(const char* name, float value) override;
        void SetUniformInt(const char* name, int value) override;
        void SetUniformVec2(const char* name, float x, float y) override;
        void SetUniformVec3(const char* name, float x, float y, float z) override;
        void SetUniformVec4(const char* name, float x, float y, float z, float w) override;
        void SetUniformMat4(const char* name, const float* matrix) override;
        void SetUniformFloatArray(const char* name, const float* values, int count) override;
        void SetUniformVec2Array(const char* name, const float* values, int count) override;
        void BindTexture(int unit, ITextureRenderer* texture) override;
        void BindTextureCube(int unit, ITextureCubeRenderer* texture) override;

        /**
         * @brief Issues the raw `glActiveTexture`/`glBindTexture`/`glBindSampler` calls for every
         * texture unit `BindTexture()`/`BindTextureCube()` recorded since the last call. CNAEXT,
         * called by `SokolRenderer::DrawCustomEffect3D`/`DrawSpriteRunEXT`.
         *
         * `BindTexture()`/`BindTextureCube()` only ever RECORD a pending bind rather than applying
         * it immediately: real XNA/`ShaderEffect` usage calls `Effect::Apply()` -- which does not
         * bind this renderer's GL program or textures at all, only `Effect::OnApply()`/
         * `SetCurrentEffect()` -- then `SetTexture()`/`SetUniformXxx()`, all *before* the actual
         * draw call. If those bound textures immediately, `BeginPassIfNeeded()` (called much later,
         * inside the eventual draw) would run its own sokol_gfx pass-begin logic in between,
         * observed to clear/reassign GL texture-unit bindings sokol_gfx itself does not know this
         * class ever made -- silently unbinding them before the draw ever samples them (found via
         * `GL_TEXTURE_BINDING_2D` reading back 0 at draw time despite a successful earlier bind, no
         * GL error either side). Deferring the real GL calls to right before the draw --
         * specifically after `BeginPassIfNeeded()`/`sg_reset_state_cache()` have already run --
         * avoids the whole class of ordering bug, mirroring sokol_gfx's own
         * record-now/apply-at-`sg_apply_bindings()`-time binding model. `SetUniformXxx()` needs no
         * equivalent deferral: it uses `glProgramUniform*` (writes directly to the named program
         * object's own uniform storage, independent of any current GL binding state), immune to
         * this timing issue by construction.
         */
        CNAEXT void ApplyPendingTextureBindsEXT();

    private:
        /// One texture unit's pending raw-GL bind, recorded by BindTexture()/BindTextureCube() and
        /// realized by ApplyPendingTextureBindsEXT() -- see that method's own doc comment for why
        /// this is deferred rather than applied immediately.
        ///
        /// plan_sokol.md SOKOL-44: holds the SOURCE renderer, not a resolved sg_image id. A texture's
        /// id is not stable across `SetData()` -- `SokolTextureRenderer::RecreateImage()` (and the
        /// cube/render-target equivalents) destroy the old `sg_image` and allocate a new one on every
        /// upload -- so resolving eagerly here would let `effect.SetTexture(...); texture.SetData(...);
        /// draw;` sample an already-destroyed image. ApplyPendingTextureBindsEXT() re-resolves the
        /// CURRENT image from the source at draw time instead.
        struct PendingTextureBind
        {
            int unit;
            const ITextureRenderer* texture = nullptr;
            const ITextureCubeRenderer* cubeTexture = nullptr;
            bool isCube;
        };

        std::uint32_t programId_ = 0;
        std::string compileError_;
        std::vector<PendingTextureBind> pendingTextureBinds_;
    };

    /**
     * @brief CNA graphics renderer implemented on sokol_gfx (https://github.com/floooh/sokol).
     *
     * CNA keeps ownership of the SDL window and the game loop; this class creates only the GPU
     * context (SDL_GL_CreateContext for the GL APIs) and drives sokol_gfx inside it, so sokol_app
     * is deliberately not used.
     *
     * Scope: 2D (`Texture2D`, `SpriteBatch`, `VertexBuffer`/`IndexBuffer`), 3D (`BasicEffect`
     * incl. textured/lit/fog, `DualTextureEffect`, `EnvironmentMapEffect`, `SkinnedEffect`,
     * instanced draws, a custom `ShaderEffect` via raw runtime GLSL compilation), render targets
     * (`RenderTarget2D`/`RenderTargetCube` incl. MSAA+resolve, mip-mapped rendering, MRT, direct
     * `GetData()` readback), `TextureCube`/`Texture3D` storage and `OcclusionQuery` are all
     * implemented. Anything genuinely unsupported (PBR shading, `RasterizerState.FillMode`,
     * `RenderTargetCube` MSAA -- the last two are permanent sokol_gfx API boundaries, not
     * "not implemented yet") fails loudly rather than silently no-opping. See plan_sokol.md and
     * docs/sokol-renderer.md for the current, test-verified capability boundary.
     */
    class SokolRenderer : public IGraphicsRenderer
    {
    public:
        /**
         * @brief Creates the GPU context for @p window and initialises sokol_gfx in it.
         *
         * @param args Renderer creation arguments; window, virtual resolution, presentation mode,
         *             multisample count and swap interval are honoured.
         */
        explicit SokolRenderer(const GraphicsRendererCreateArgs& args);

        /**
         * @brief CNAEXT test-only constructor (plan_sokol.md SOKOL-45) that can force the first
         * `SDL_GL_MakeCurrent()` call this instance makes to fail regardless of its real return
         * value, and counts every `SDL_GL_DestroyContext()` call this instance makes -- so a
         * regression test can prove construction stays fully transactional even when
         * `SDL_GL_CreateContext()` itself already succeeded before the failure.
         *
         * @param args Renderer creation arguments, identical meaning to the public constructor.
         * @param forceMakeCurrentFailureEXT When true, treat the first `SDL_GL_MakeCurrent()` call
         *                                   as failed even if SDL itself reports success.
         * @param contextDestroyCountEXT Optional counter incremented on every real
         *                               `SDL_GL_DestroyContext()` call this instance makes; left
         *                               untouched when null.
         */
        CNAEXT SokolRenderer(const GraphicsRendererCreateArgs& args,
                                    bool forceMakeCurrentFailureEXT,
                                    int* contextDestroyCountEXT);

        /** @brief Shuts sokol_gfx down and destroys the GPU context. */
        ~SokolRenderer() override;

        SokolRenderer(const SokolRenderer&) = delete;
        SokolRenderer& operator=(const SokolRenderer&) = delete;

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
         * @brief Creates a sokol_gfx-backed 2D texture.
         * @param data Decoded RGBA8 source image.
         * @return The new texture renderer.
         */
        std::unique_ptr<ITextureRenderer> CreateTexture(const ImageData& data) override;

        /**
         * @brief Creates a CPU-storage-only cube texture (plan_sokol.md SOKOL-27).
         *
         * No real GPU resource is allocated: nothing on this renderer samples a cube texture yet
         * (dual-texture/environment-map/skinned/PBR 3D draws all throw), so `SokolTextureCubeRenderer`
         * only stores the six faces' pixels `SetData()`/`GetData()` need.
         *
         * @param size         Edge length of one cube face at mip 0, in texels.
         * @param mipMap       Allocate the full mip chain down to 1x1 as well as level 0.
         * @param surfaceFormat Unused -- this renderer always stores RGBA8, matching CreateTexture.
         * @return The new cube texture renderer.
         */
        std::unique_ptr<ITextureCubeRenderer> CreateTextureCube(
            int size, bool mipMap, int surfaceFormat) override;

        /**
         * @brief Creates a CPU-storage-only volume texture (plan_sokol.md SOKOL-27).
         *
         * No real GPU resource is allocated: nothing on this renderer samples a volume texture yet
         * (there is no 3D-sampler shader variant), so `SokolTexture3DRenderer` only stores the
         * voxels `SetData()`/`GetData()` need.
         *
         * @param w            Level-0 width in texels.
         * @param h            Level-0 height in texels.
         * @param depth        Level-0 depth in texels.
         * @param mipMap       Allocate the full mip chain down to 1x1x1 as well as level 0.
         * @param surfaceFormat Unused -- this renderer always stores RGBA8, matching CreateTexture.
         * @return The new volume texture renderer.
         */
        std::unique_ptr<ITexture3DRenderer> CreateTexture3D(
            int w, int h, int depth, bool mipMap, int surfaceFormat) override;

        /**
         * @brief Creates a raw-GL occlusion query (plan_sokol.md SOKOL-29).
         * @return The new occlusion query renderer, or null on a non-GL `CNA_SOKOL_API`.
         */
        std::unique_ptr<IOcclusionQueryRenderer> CreateOcclusionQuery() override;

        /**
         * @brief CNAEXT (plan_sokol.md SOKOL-43). Claims this context's one `GL_SAMPLES_PASSED`
         * active-query slot for @p query.
         *
         * OpenGL permits only one active occlusion query per context, not per query object, so
         * `SokolOcclusionQueryRenderer::Begin()` calls this before ever issuing a real
         * `glBeginQuery` -- a second, different query's overlapping `Begin()` is refused here and
         * silently absorbed by the caller, the same "no exception" contract an already-active
         * Begin() on the SAME object already has.
         *
         * @param query The query object requesting the slot.
         * @return True if @p query now owns the slot (no other query was active); false if a
         *         different query already holds it.
         */
        CNAEXT bool TryActivateOcclusionQueryEXT(SokolOcclusionQueryRenderer* query);

        /**
         * @brief CNAEXT (plan_sokol.md SOKOL-43). Releases this context's active-query slot if
         * @p query currently owns it (a no-op otherwise -- e.g. @p query never held the slot, or
         * this is a redundant release).
         *
         * Called from both `SokolOcclusionQueryRenderer::End()` and its destructor, so a query
         * destroyed while still active (see `IOcclusionQueryRenderer`'s own dispose-while-active
         * contract) cannot leave the slot permanently claimed by a now-dangling pointer.
         *
         * @param query The query object releasing the slot.
         */
        CNAEXT void ReleaseOcclusionQueryEXT(SokolOcclusionQueryRenderer* query);

        /**
         * @brief CNAEXT (plan_sokol.md SOKOL-24). Monotonic counter incremented once per
         * `Present()`/`sg_commit()`.
         *
         * `SokolVertexBufferRenderer`/`SokolIndexBufferRenderer` compare this against the frame
         * index of their own last `sg_update_buffer()` call to decide whether a further in-place
         * update is legal this frame -- sokol_gfx permits at most one `sg_update_buffer()` per
         * buffer per frame (a second call in the same frame trips a hard `SOKOL_ASSERT`, not just
         * a validation-layer warning).
         *
         * @return The current frame index (starts at 1, matching sokol_gfx's own internal
         *         `_sg.frame_index` convention; never 0, used as this renderer's own "no update
         *         yet" sentinel).
         */
        CNAEXT [[nodiscard]] std::uint64_t GetFrameIndexEXT() const { return frameIndexEXT_; }

        /**
         * @brief Compiles a custom `ShaderEffect` from raw GLSL source (plan_sokol.md SOKOL-28).
         *
         * GL-only (see `SokolEffectRenderer`'s own doc comment for why); on a non-GL
         * `CNA_SOKOL_API`, `SokolEffectRenderer::CompileProgram` deterministically fails and
         * `IsValid()` returns false rather than this factory returning null, matching
         * `ShaderEffect`'s own "compile failure surfaces through `IsValid()`/`GetCompileError()`,
         * not a null renderer" contract.
         *
         * @param vertSrc GLSL vertex shader source.
         * @param fragSrc GLSL fragment shader source.
         * @return The new effect renderer (never null; check `IsValid()`).
         */
        std::unique_ptr<IEffectRenderer> CreateEffectRenderer(const std::string& vertSrc,
                                                             const std::string& fragSrc) override;

        /**
         * @brief Creates a sokol_gfx-backed SpriteBatch.
         * @return The new sprite batch renderer.
         */
        std::unique_ptr<ISpriteBatchRenderer> CreateSpriteBatch() override;

        /**
         * @brief Creates a sokol_gfx-backed vertex buffer.
         * @param vertexCapacity Number of vertices to size the buffer for.
         * @return The new vertex buffer renderer.
         */
        std::unique_ptr<IVertexBufferRenderer> CreateVertexBuffer(int vertexCapacity) override;

        /**
         * @brief Creates a sokol_gfx-backed 16-bit index buffer.
         * @param indexCapacity Number of indices to size the buffer for.
         * @return The new index buffer renderer.
         */
        std::unique_ptr<IIndexBufferRenderer> CreateIndexBuffer16(int indexCapacity) override;

        /**
         * @brief Creates a sokol_gfx-backed 32-bit index buffer.
         * @param indexCapacity Number of indices to size the buffer for.
         * @return The new index buffer renderer.
         */
        std::unique_ptr<IIndexBufferRenderer> CreateIndexBuffer32(int indexCapacity) override;

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
         * @brief Creates an off-screen render target, optionally multisampled and/or mip-mapped.
         *
         * @param w                Target width in pixels.
         * @param h                Target height in pixels.
         * @param depthFormat      Raw DepthFormat ordinal; None (0) allocates no depth-stencil
         *                         attachment, any other value allocates a combined one.
         * @param preserveContents Unused: this renderer always preserves a target's content across
         *                         binds (a real FBO naturally does), matching the EasyGL
         *                         renderer's own documented simplification -- an explicit `Clear()`
         *                         is what actually discards content, on every renderer.
         * @param mipMap           When true, only level 0 is ever rendered into directly (matching
         *                         real D3D9 XNA's `D3DUSAGE_AUTOGENMIPMAP`); the rest of the mip
         *                         chain is regenerated via `glGenerateMipmap` on unbind
         *                         (plan_sokol.md SOKOL-39).
         * @param multiSampleCount Clamped to the driver's real `GL_MAX_SAMPLES`, following
         *                         sokol_gfx.h's own documented offscreen-MSAA workflow: a separate
         *                         multisample-only colour image plus a single-sample resolve image
         *                         (plan_sokol.md SOKOL-26); observable via IRenderTargetRenderer::
         *                         GetMultiSampleCount(), matching every other renderer's own
         *                         device-clamped-count convention.
         * @return The new render target renderer.
         */
        std::unique_ptr<IRenderTargetRenderer> CreateRenderTarget2D(
            int w, int h, int depthFormat, bool preserveContents, bool mipMap,
            int multiSampleCount) override;

        /**
         * @brief Creates a cube render target, optionally mip-mapped; MSAA is a permanent
         * sokol_gfx API boundary for cube images -- see @p multiSampleCount below.
         *
         * @param size             Edge length of each face in pixels.
         * @param depthFormat      Raw DepthFormat ordinal; None (0) allocates no depth-stencil
         *                         attachment, any other value allocates a combined one, shared by
         *                         all six faces (plan_sokol.md SOKOL-26 -- see
         *                         SokolRenderTargetCubeRenderer's own doc comment).
         * @param preserveContents Unused, for the same reason CreateRenderTarget2D's identically
         *                         named parameter is: a real sokol_gfx image naturally preserves
         *                         its content across binds.
         * @param mipMap           When true, only level 0 of each face is ever rendered into
         *                         directly; the rest of the chain is regenerated via
         *                         `glGenerateMipmap` on unbind (plan_sokol.md SOKOL-39), the same
         *                         convention CreateRenderTarget2D uses.
         * @param multiSampleCount Silently clamped to 1 -- **permanent, not "not implemented
         *                         yet"**: sokol_gfx's own validation layer hard-rejects any
         *                         `SG_IMAGETYPE_CUBE` image with `sample_count > 1`
         *                         (`VALIDATE_IMAGEDESC_ATTACHMENT_MSAA_CUBE_IMAGE`), confirmed
         *                         empirically while prototyping the same per-face multisample +
         *                         resolve layout `RenderTarget2D` uses successfully
         *                         (plan_sokol.md SOKOL-26).
         * @return The new cube render target renderer.
         */
        std::unique_ptr<IRenderTargetCubeRenderer> CreateRenderTargetCube(
            int size, int depthFormat, bool preserveContents, bool mipMap,
            int multiSampleCount) override;

        /**
         * @brief Activates a single render target, or restores the back buffer.
         * @param rt The target to activate, or null to restore the back buffer.
         */
        void SetRenderTarget2D(IRenderTargetRenderer* rt) override;

        /**
         * @brief Binds a render-target set. A single RenderTarget2D, a single RenderTargetCube
         * face, the back buffer (null / count 0), or 2-4 RenderTarget2D targets bound together
         * (plan_sokol.md SOKOL-26 MRT) is supported. A RenderTargetCube face combined with any
         * other target in the same set is not implemented (matches EasyGLRenderer's own
         * choice to reject that combination outright).
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
        void DrawColoredPrimitives(const IVertexBufferRenderer& vb,
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
        void DrawIndexedColoredPrimitives(const IVertexBufferRenderer& vb,
                                          const IIndexBufferRenderer& ib,
                                          const Matrix& world,
                                          const Matrix& view,
                                          const Matrix& projection,
                                          PrimitiveType primitive,
                                          int primitiveCount) override;

        /**
         * @brief Effect-aware non-indexed draw.
         *
         * Textured, lit (up to 3 real per-pixel directional lights), `DualTextureEffect`,
         * `EnvironmentMapEffect`, `SkinnedEffect` and a custom `ShaderEffect` are all implemented.
         * Only an effect requesting PBR shading throws rather than quietly rendering an unshaded
         * approximation of it -- `PbrEffect`/`SkinnedPbrEffect` exist on every other CNA renderer
         * (EasyGL, D3D9/11/12, Vulkan, WebGPU, Bgfx, SdlGpu) but have not yet been ported here.
         *
         * @param vb             Vertex buffer to read from.
         * @param world          World matrix.
         * @param view           View matrix.
         * @param projection     Projection matrix.
         * @param primitive      Primitive topology.
         * @param primitiveCount Number of primitives.
         * @param params         Per-draw effect parameters.
         */
        void DrawPrimitivesEx(const IVertexBufferRenderer& vb,
                              const Matrix& world,
                              const Matrix& view,
                              const Matrix& projection,
                              PrimitiveType primitive,
                              int primitiveCount,
                              const GpuDrawParams& params) override;

        /**
         * @brief Indexed counterpart of DrawPrimitivesEx(); same effect coverage.
         * @param vb             Vertex buffer to read from.
         * @param ib             Index buffer to read from.
         * @param world          World matrix.
         * @param view           View matrix.
         * @param projection     Projection matrix.
         * @param primitive      Primitive topology.
         * @param primitiveCount Number of primitives.
         * @param params         Per-draw effect parameters.
         */
        void DrawIndexedPrimitivesEx(const IVertexBufferRenderer& vb,
                                     const IIndexBufferRenderer& ib,
                                     const Matrix& world,
                                     const Matrix& view,
                                     const Matrix& projection,
                                     PrimitiveType primitive,
                                     int primitiveCount,
                                     const GpuDrawParams& params) override;

        /**
         * @brief GraphicsDevice.DrawInstancedPrimitives -- one draw call rendering `instanceCount`
         * copies of the same mesh, each transformed by its own World matrix read from
         * `params.instanceVb` (plan_sokol.md SOKOL-36).
         *
         * A dedicated, deliberately simplified shader (instanced3d.glsl, ported from
         * VulkanRenderer's own instanced3d.{vert,frag}.glsl): flat `DiffuseColor` only, no
         * vertex colour, texturing or lighting -- the same established scope reduction every other
         * CNA renderer with real instancing already makes. When `params.instanceVb` is null this
         * falls back to a real, working `DrawIndexedPrimitivesEx()` draw instead of throwing,
         * matching `VulkanRenderer`/`DirectX11Renderer`'s own identical fallback
         * contract.
         *
         * @param vb             Per-vertex mesh buffer; only its Position element is read.
         * @param ib             Index buffer to read from.
         * @param world          World matrix -- unused when instancing (each instance supplies its
         *                       own World via `params.instanceVb`); only reached by the
         *                       `instanceVb == nullptr` fallback path.
         * @param view           View matrix.
         * @param projection     Projection matrix.
         * @param primitive      Primitive topology.
         * @param primitiveCount Number of primitives per instance.
         * @param instanceCount  Number of instances to draw.
         * @param params         Per-draw effect parameters; `instanceVb` supplies the per-instance
         *                       World-matrix stream.
         */
        void DrawInstancedPrimitivesEx(const IVertexBufferRenderer& vb,
                                       const IIndexBufferRenderer& ib,
                                       const Matrix& world,
                                       const Matrix& view,
                                       const Matrix& projection,
                                       PrimitiveType primitive,
                                       int primitiveCount,
                                       int instanceCount,
                                       const GpuDrawParams& params) override;

        /**
         * @brief Reports which features this renderer's current baseline actually supports.
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
         * @brief Returns the native API sokol_gfx was compiled to dispatch onto. CNAEXT.
         * @return The resolved SokolApiEXT value for this build.
         */
        CNAEXT [[nodiscard]] static SokolApiEXT GetApiEXT();

        /**
         * @brief Returns the maximum number of sprite quads one frame may draw. CNAEXT.
         *
         * The sprite streaming buffer is allocated once at construction, so a frame that exceeds
         * this cap raises an error rather than silently dropping sprites.
         *
         * @return Per-frame sprite quad capacity.
         */
        CNAEXT [[nodiscard]] static int GetMaxSpriteQuadsPerFrameEXT();

        /**
         * @brief Draws one flushed run of sprite quads. CNAEXT, called by SokolSpriteBatchRenderer.
         *
         * @param texture  Source texture for the run.
         * @param vertices Quad vertices, four per quad in top-left, top-right, bottom-right,
         *                 bottom-left order.
         * @param transform Transform applied on top of the orthographic projection.
         * @param filter   Raw TextureFilter value.
         * @param addressU Raw TextureAddressMode value for U.
         * @param addressV Raw TextureAddressMode value for V.
         * @param customEffect Custom `ShaderEffect` bound via `SpriteBatch.Begin(..., effect)`
         *                     (plan_sokol.md SOKOL-28), or null for the built-in sprite shader. A
         *                     non-null but invalid (failed-to-compile) effect falls back to the
         *                     built-in shader too, matching `EasyGLSpriteBatchRenderer::FlushBatch`'s
         *                     own `renderer && renderer->IsValid()` gate.
         */
        CNAEXT void DrawSpriteRunEXT(const ITextureRenderer& texture,
                                    const std::vector<SokolSpriteBatchRenderer::Vertex>& vertices,
                                    const Matrix& transform,
                                    int filter, int addressU, int addressV,
                                    Effect* customEffect = nullptr);

        /**
         * @brief Returns the logical (virtual) presentation size. CNAEXT.
         * @param width  Receives the logical width in pixels.
         * @param height Receives the logical height in pixels.
         */
        CNAEXT void GetLogicalSizeEXT(int& width, int& height) const;

        /**
         * @brief Returns the physical window size in pixels. CNAEXT.
         * @param width  Receives the physical width.
         * @param height Receives the physical height.
         */
        CNAEXT void GetPhysicalSizeEXT(int& width, int& height) const;

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
            /// See Pipeline3DKey's identical field (plan_sokol.md SOKOL-26 MRT): sokol_gfx requires
            /// a pipeline's color_count to exactly match the active pass's real attachment count.
            int colorAttachmentCount;
            /// GraphicsDevice.BlendFactor, packed 0xRRGGBBAA from the same 8-bit Color the public
            /// API exposes (plan_sokol.md SOKOL-40). sokol_gfx bakes blend_color into the pipeline
            /// object at creation time -- there is no dynamic blend-constant call, unlike most
            /// APIs -- so a pipeline built under one BlendFactor is wrong for a draw under another
            /// and must not be reused; only Blend::BlendFactor/InverseBlendFactor actually read it.
            std::uint32_t blendFactorPacked;

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
             *  (a real one, or the renderer's 1x1 white fallback). */
            Lit,
            /** @brief dualtextured3d.glsl -- DualTextureEffect: two textures sampled at the SAME
             *  texcoord0 (this codebase has no second UV-set concept anywhere, matching every other
             *  CNA renderer's own DualTextureEffect shape), `base.rgb *= 2` then multiplied by the
             *  overlay and DiffuseColor. No alpha test (FNA's PSDualTexture has none). */
            DualTextured,
            /** @brief skinned3d.glsl -- SkinnedEffect: always textured and lit (matching FNA, which
             *  has no unlit/untextured skinned shader permutation), a per-vertex weighted sum of up
             *  to 4 bone matrices (GpuDrawParams::weightsPerVertex) blends Position/Normal before
             *  the same lighting math lit3d.glsl uses. */
            Skinned,
            /** @brief envmap3d.glsl -- EnvironmentMapEffect: always textured and lit, no vertex
             *  colour support (real XNA's EnvironmentMapEffect has no VertexColorEnabled property).
             *  A world-space reflection vector and Fresnel blend factor are computed per-vertex,
             *  the cube map is sampled per-fragment and lerped with the lit base colour. */
            EnvMapped
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
            /// plan_sokol.md SOKOL-26 MRT: sokol_gfx requires a pipeline's color_count to exactly
            /// match the active pass's real attachment count (1 for a single target/the swapchain,
            /// 2-4 while a multi-render-target set is bound). Every stock 3D fragment shader has
            /// only ever declared output location 0 (see DrawColored3D's own comment), so slots
            /// beyond 0 simply receive no write from this pipeline -- matching
            /// EasyGLRenderer's identical "the 2D/3D stock pipeline writes colour attachment
            /// 0 only" MRT behaviour.
            int colorAttachmentCount;
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
            /// Byte offset of the BlendWeight element, or -1 when absent (only Skinned kind reads
            /// this).
            int blendWeightOffset;
            int blendWeightFormat;
            /// Byte offset of the BlendIndices element, or -1 when absent (only Skinned kind reads
            /// this).
            int blendIndicesOffset;
            int blendIndicesFormat;
            /// See PipelineKey's identical field (plan_sokol.md SOKOL-40).
            std::uint32_t blendFactorPacked;

            bool operator==(const Pipeline3DKey& other) const;
        };

        struct Pipeline3DKeyHash
        {
            std::size_t operator()(const Pipeline3DKey& key) const;
        };

        /**
         * @brief Identity of an instanced-3D pipeline (plan_sokol.md SOKOL-36).
         *
         * A separate, smaller key than Pipeline3DKey: instanced3d.glsl only ever reads Position
         * from vertex-buffer slot 0 (any stride -- see instanced3d.glsl's own doc comment) and the
         * always-fixed-layout 4-vec4 World-matrix columns from slot 1, so there is no
         * color/texCoord/normal/blendWeight/blendIndices attribute set to key on, and no
         * Shader3DKind (this key only ever targets one shader).
         */
        struct PipelineInstanced3DKey
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
            bool hasDepthAttachment;
            bool stencilEnabled;
            int stencilFunc;
            int stencilPass;
            int stencilFail;
            int stencilDepthFail;
            int ccwStencilFunc;
            int ccwStencilPass;
            int ccwStencilFail;
            int ccwStencilDepthFail;
            bool twoSidedStencilMode;
            int stencilMask;
            int stencilWriteMask;
            int referenceStencil;
            float depthBias;
            float slopeScaleDepthBias;
            int sampleCount;
            /// See Pipeline3DKey's identical field (plan_sokol.md SOKOL-26 MRT).
            int colorAttachmentCount;
            int cullMode;
            int primitiveType;
            int indexType;
            /// Per-vertex buffer (slot 0) stride; the instance buffer (slot 1) is always exactly
            /// 64 bytes (4 column-major vec4s), so it needs no key field of its own.
            int stride;
            /// REMED-GFX-202: the bound per-instance stream's
            /// `VertexBufferBinding.InstanceFrequency`, which sokol_gfx expresses as
            /// `sg_vertex_buffer_layout_state.step_rate` (a `glVertexAttribDivisor` on the GL
            /// renderers). Part of the key because sokol_gfx bakes it into the pipeline object.
            int instanceStepRate;
            int positionOffset;
            int positionFormat;
            /// See PipelineKey's identical field (plan_sokol.md SOKOL-40).
            std::uint32_t blendFactorPacked;

            bool operator==(const PipelineInstanced3DKey& other) const;
        };

        struct PipelineInstanced3DKeyHash
        {
            std::size_t operator()(const PipelineInstanced3DKey& key) const;
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
        /// Destroys glContext_ if one was ever created, nulls it out, and increments
        /// contextDestroyCountEXT_ when a test supplied one (plan_sokol.md SOKOL-45) -- the single
        /// path both the constructor's transactional-cleanup catch block and the destructor use, so
        /// a leaked or double-destroyed context cannot happen via one path but not the other.
        void DestroyGpuContextIfAnyEXT();
        void SetupSokol();
        void CreateSpriteResources();
        void BeginPassIfNeeded();
        void EndPassIfActive();
        void QueueClear(bool color, float r, float g, float b, float a,
                        bool depth, float depthValue,
                        bool stencil, int stencilValue);
        void DrawColored3D(const IVertexBufferRenderer& vb,
                           const IIndexBufferRenderer* ib,
                           const Matrix& world,
                           const Matrix& view,
                           const Matrix& projection,
                           PrimitiveType primitive,
                           int primitiveCount,
                           const GpuDrawParams& params);
        /**
         * @brief CNAEXT (plan_sokol.md SOKOL-23). Builds a doubled-edge index list (a-b, b-c, c-a
         * per triangle) for `RasterizerState.FillMode == WireFrame` -- the same CPU-side
         * triangle-to-`GL_LINES` re-expansion `EasyGLRenderer::DrawWireframe()` uses.
         * sokol_gfx has no native polygon-fill-mode API, but this technique needs none: it only
         * changes which indices are drawn and with what primitive topology, both already fully
         * controlled per draw. Reads the *original* index values back off the GPU via
         * `glGetBufferSubData` (this renderer keeps no CPU shadow of index data) when @p ib is
         * non-null; generates sequential `vertexStart + i` values otherwise, matching the
         * non-indexed draw's own vertex order.
         *
         * @param ib          Source index buffer, or null for a non-indexed draw.
         * @param primitive   Only `TriangleList`/`TriangleStrip` produce a non-empty result --
         *                    line/point primitives are already "wireframe".
         * @param primitiveCount Number of triangles.
         * @param startIndex  First index to read, when @p ib is non-null.
         * @param vertexStart First vertex to read, when @p ib is null.
         * @return The doubled-edge index list (empty when @p primitive is not a triangle type).
         */
        std::vector<std::uint32_t> BuildWireframeLineIndicesEXT(
            const SokolIndexBufferRenderer* ib, PrimitiveType primitive, int primitiveCount,
            int startIndex, int vertexStart);
        /**
         * @brief Custom-`ShaderEffect` 3D draw (plan_sokol.md SOKOL-28): bypasses `sg_pipeline`
         * entirely via raw GL calls bracketed by `sg_reset_state_cache()`. See
         * `SokolEffectRenderer`'s own doc comment for why. Attribute layout comes from
         * @p declaration when non-null with at least one element, else the same fixed set of
         * recognised undeclared-VertexBuffer byte strides (16/20/24/32/52) the stock-effect
         * `Shader3DKind` dispatch's own fallback switch recognises -- throws for anything else.
         */
        void DrawCustomEffect3D(const SokolVertexBufferRenderer& vb,
                                const SokolIndexBufferRenderer* ib,
                                const VertexDeclaration* declaration,
                                const Matrix& world,
                                const Matrix& view,
                                const Matrix& projection,
                                PrimitiveType primitive,
                                int primitiveCount,
                                const GpuDrawParams& params);
        /**
         * @brief CNAEXT (plan_sokol.md SOKOL-41). Applies the device's full current graphics state
         * -- depth test/write, stencil, blend (including the constant colour), face culling and
         * winding, and depth bias -- as raw GL calls, for the two draw paths that bypass
         * `sg_pipeline` entirely (`DrawCustomEffect3D` and `DrawSpriteRunEXT`'s custom-effect
         * branch). Both bracket their whole raw-GL detour with `sg_reset_state_cache()`; call this
         * AFTER that reset and BEFORE the draw call, on both sides of which real GL state is
         * otherwise only ever set by an `sg_apply_pipeline()` call these draws never make -- so
         * without this, a custom-effect draw silently keeps whatever blend/stencil/cull/depth-bias
         * state a PRIOR stock-pipeline draw happened to leave configured in the GL context.
         *
         * Does not touch colour write masks -- see ApplyCustomEffectColorMasksEXT()'s own doc
         * comment for why that needs a separate before/after pair instead.
         */
        void ApplyCustomEffectRasterStateEXT();
        /**
         * @brief CNAEXT (plan_sokol.md SOKOL-41/SOKOL-26 MRT). Applies `ColorWriteChannels0..3` via
         * `glColorMaski` per active colour-attachment slot, for the same two raw-GL draw paths
         * ApplyCustomEffectRasterStateEXT() serves.
         *
         * A separate call (not folded into that function) because it must be undone with
         * ResetCustomEffectColorMasksEXT() immediately AFTER the draw, not left in place like the
         * rest of the raster state: every stock `sg_pipeline` bakes `desc.colors[slot].write_mask`
         * from the SAME single `colorWriteChannels_[0]` value for every slot (stock pipelines have
         * no per-slot write-mask concept), so a restricted mask this raw-GL path left on slot 1..N
         * would otherwise silently survive into a later stock-pipeline draw that never intended to
         * touch it.
         */
        void ApplyCustomEffectColorMasksEXT();
        /** @brief Restores every active colour-attachment slot's write mask to all-enabled. See
         *         ApplyCustomEffectColorMasksEXT()'s own doc comment for why this exists. */
        void ResetCustomEffectColorMasksEXT();
        [[nodiscard]] std::uint32_t Get3DPipeline(const Pipeline3DKey& key);
        [[nodiscard]] std::uint32_t GetInstanced3DPipeline(const PipelineInstanced3DKey& key);
        [[nodiscard]] SokolTextureRenderer& GetDefaultWhiteTexture();
        [[nodiscard]] std::uint32_t GetSpritePipeline();
        [[nodiscard]] std::uint32_t GetSampler(int filter, int addressU, int addressV,
                                               int maxAnisotropy);
        void ApplyPendingViewportAndScissor();
        /// Shared by SetRenderTarget2D and the single-RenderTarget2D case of SetRenderTargets --
        /// both public GraphicsDevice entry points (the singular SetRenderTarget(RenderTarget2D*)
        /// convenience and the vector-based SetRenderTargets) reach the renderer through genuinely
        /// different IGraphicsRenderer virtuals, so there is no single call site to put this in.
        void BindSingleRenderTarget2D(SokolRenderTargetRenderer* rt);
        /// Cube-face counterpart of BindSingleRenderTarget2D -- mutually exclusive with it, so each
        /// clears the other's tracking field.
        void BindRenderTargetCubeFace(SokolRenderTargetCubeRenderer* rt, int face);
        /// MRT counterpart of BindSingleRenderTarget2D (plan_sokol.md SOKOL-26): @p targets.size()
        /// is always >= 2 (SetRenderTargets already special-cases count == 1). targets[0] becomes
        /// currentRenderTarget_ -- so every existing depth/sample-count/size/mip-regen path that
        /// already reads currentRenderTarget_ continues to answer about "the" target that owns
        /// those properties, matching this renderer's (and EasyGLRenderer's) "slot 0 owns
        /// depth, size and sample count" MRT convention -- and targets[1..] become mrtExtraTargets_.
        void BindRenderTargets2D(const std::vector<SokolRenderTargetRenderer*>& targets);
        /// Regenerates the mip chain (if any) of every render target about to be replaced by the
        /// next bind -- currentRenderTarget_, currentRenderTargetCube_ and every entry of
        /// mrtExtraTargets_ -- shared by BindSingleRenderTarget2D, BindRenderTargetCubeFace and
        /// BindRenderTargets2D so each keeps identical "regenerate the OUTGOING target's mips"
        /// behaviour regardless of which bind path is switching away from it.
        void RegenerateOutgoingMipsIfNeededEXT();
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
        /// Returns the active pass's real colour-attachment count: 1 for a single target or the
        /// swapchain, 2-4 while a multi-render-target set is bound (plan_sokol.md SOKOL-26 MRT).
        /// sokol_gfx requires a pipeline's color_count to exactly match the pass it draws into, the
        /// same reasoning CurrentPassSampleCountEXT's own doc comment gives for sample_count.
        [[nodiscard]] int CurrentPassColorAttachmentCountEXT() const;

        SDL_Window* window_ = nullptr;
        void* glContext_ = nullptr;
        /// plan_sokol.md SOKOL-45 test-only failure injection -- see the CNAEXT constructor's doc
        /// comment. Always false/null via the public constructor.
        bool forceMakeCurrentFailureEXT_ = false;
        int* contextDestroyCountEXT_ = nullptr;
        int virtualWidth_ = 0;
        int virtualHeight_ = 0;
        CnaPresentationMode presentationMode_ = CnaPresentationMode::FixedHeightDynamicWidth;
        int sampleCount_ = 1;
        int swapInterval_ = 1;

        bool passActive_ = false;
        /// The active off-screen render target, or null for the back buffer. Not owned -- the
        /// public RenderTarget2D (via its renderer unique_ptr) owns the lifetime; a target that
        /// outlives its binding is unbound the same way GraphicsDevice::Dispose() unbinds anything
        /// else, by the caller issuing SetRenderTarget(nullptr)/SetRenderTargets({}) first.
        SokolRenderTargetRenderer* currentRenderTarget_ = nullptr;
        /// The active cube-face render target, or null. Mutually exclusive with
        /// currentRenderTarget_ -- BindSingleRenderTarget2D and BindRenderTargetCubeFace each
        /// clear the other. Not owned, same lifetime contract as currentRenderTarget_.
        SokolRenderTargetCubeRenderer* currentRenderTargetCube_ = nullptr;
        /// Which face of currentRenderTargetCube_ is the active colour attachment. Meaningless
        /// while currentRenderTargetCube_ is null.
        int currentRenderTargetCubeFace_ = 0;
        /// plan_sokol.md SOKOL-26 MRT: slots 1..N-1 of a multi-render-target bind, empty when not
        /// MRT. Slot 0 stays currentRenderTarget_ (see BindRenderTargets2D's own comment) --
        /// mutually exclusive with currentRenderTargetCube_ being non-null, since a RenderTargetCube
        /// face combined with any other target in one set is not implemented.
        std::vector<SokolRenderTargetRenderer*> mrtExtraTargets_;
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
        /// Per-attachment ColorWriteChannels0..3 (plan_sokol.md SOKOL-26 MRT); slot 0 is what every
        /// stock-effect Pipeline3DKey/PipelineKey/PipelineInstanced3DKey still reads (see
        /// DrawColored3D/GetSpritePipeline/DrawInstancedPrimitivesEx), slots 1-3 are only consulted
        /// by DrawCustomEffect3D's own raw-GL glColorMaski calls while more than one render target
        /// is bound -- a stock-pipeline draw while MRT is active is refused outright (see
        /// DrawColored3D/DrawSpriteRunEXT's own MRT guards), so slots 1-3 have no pipeline-key
        /// consumer to keep in sync with.
        int colorWriteChannels_[4] = {15, 15, 15, 15};
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
        std::uint32_t dualTextured3dShaderId_ = 0;
        std::uint32_t skinned3dShaderId_ = 0;
        std::uint32_t instanced3dShaderId_ = 0;
        std::uint32_t envmap3dShaderId_ = 0;
        /// Raw GL VAO used only by the custom-`ShaderEffect` draw path (plan_sokol.md SOKOL-28) --
        /// sokol_gfx's own pipeline objects own their vertex layout state internally and are not
        /// reused here, since a custom-effect draw bypasses sg_pipeline entirely. Created lazily on
        /// first use, destroyed alongside the rest of this renderer's GL resources.
        std::uint32_t customEffectVaoId_ = 0;
        /// Lazily created 1x1 opaque-white texture, bound by the Lit shader whenever
        /// GpuDrawParams::textureEnabled is false -- lets one shader serve both "textured and lit"
        /// and "vertex-coloured and lit" (the multiply is then a no-op), the same convention the
        /// EasyGL renderer's own default-white-texture fallback uses.
        std::unique_ptr<SokolTextureRenderer> defaultWhiteTexture_;
        std::unordered_map<PipelineKey, std::uint32_t, PipelineKeyHash> pipelineCache_;
        std::unordered_map<Pipeline3DKey, std::uint32_t, Pipeline3DKeyHash> pipeline3dCache_;
        std::unordered_map<PipelineInstanced3DKey, std::uint32_t, PipelineInstanced3DKeyHash>
            pipelineInstanced3dCache_;
        std::unordered_map<SamplerKey, std::uint32_t, SamplerKeyHash> samplerCache_;
        /// plan_sokol.md SOKOL-43: which SokolOcclusionQueryRenderer, if any, currently owns this
        /// context's one GL_SAMPLES_PASSED active-query slot. See
        /// TryActivateOcclusionQueryEXT()/ReleaseOcclusionQueryEXT()'s own doc comments.
        SokolOcclusionQueryRenderer* activeOcclusionQueryEXT_ = nullptr;
        /// plan_sokol.md SOKOL-23: scratch line-topology index buffer for
        /// RasterizerState.FillMode == WireFrame, recreated (immutable, with data) on every
        /// wireframe draw -- same "every upload recreates the resource" convention every other
        /// buffer in this renderer already uses, see BuildWireframeLineIndicesEXT()'s own doc
        /// comment.
        std::uint32_t wireframeIndexBufferId_ = 0;
        /// plan_sokol.md SOKOL-24: see GetFrameIndexEXT()'s own doc comment.
        std::uint64_t frameIndexEXT_ = 1;
    };
}
