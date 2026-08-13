// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/CNAHelper.hpp"
#include "CNA/Internal/Renderers/Llgl/LlglRendererSelection.hpp"
#include "CNA/Internal/Renderers/Llgl/LlglPlatformSurface.hpp"
#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"

#include <LLGL/LLGL.h>

#include <array>
#include <cstdint>
#include <map>
#include <memory>
#include <tuple>
#include <vector>

namespace CNA::Internal::Renderers::Llgl
{
    class LlglRenderer;

    /**
     * @brief A 2D texture living in LLGL. CNAEXT.
     *
     * Backs `Microsoft::Xna::Framework::Graphics::Texture2D`. Pixels are always stored as RGBA8,
     * the format the shared texture layer hands every renderer.
     */
    class LlglTextureRenderer final : public ITextureRenderer
    {
    public:
        /**
         * @brief Takes ownership of an already-created LLGL texture.
         *
         * @param renderSystem Render system that created @p texture and will release it.
         * @param owner        Renderer to defer this texture's release through (see the destructor's
         *                     own comment); null falls back to releasing immediately.
         * @param texture      The texture resource, or null for the supported OpenGL contract's
         *                     transfer-only CPU storage.
         * @param width        Width in pixels of mip level 0.
         * @param height       Height in pixels of mip level 0.
         * @param mipLevels    Number of mip levels the texture was created with.
         */
        LlglTextureRenderer(LLGL::RenderSystem* renderSystem, LlglRenderer* owner,
                           LLGL::Texture* texture, int width, int height, int mipLevels);

        /**
         * @brief Defers releasing the LLGL texture, mirroring `LlglVertexBufferRenderer`'s own
         * `ScheduleBufferReleaseEXT` precedent: a `Texture2D` that goes out of scope before
         * `Present()` (create it, draw it via `SpriteBatch`, let it die, all within one `Draw()`) is
         * a perfectly normal XNA pattern, and the still-queued `FrameCommand`/`FrameCommandBucket`
         * this texture was drawn through may not replay until `Present()`/`GetBackBufferData()`
         * actually flushes the frame -- releasing here immediately left that replay pointing at
         * freed memory (found via `backbuffer_readback_dimension_test.cpp`'s own A1 leg, a plain
         * SpriteBatch draw of a locally-scoped `Texture2D` followed by an unflushed readback,
         * segfaulting inside `VKDescriptorCache::EmplaceDescriptor`).
         */
        ~LlglTextureRenderer() override;

        LlglTextureRenderer(const LlglTextureRenderer&) = delete;
        LlglTextureRenderer& operator=(const LlglTextureRenderer&) = delete;

        /** @brief Returns the width in pixels of mip level 0. */
        [[nodiscard]] int GetWidth() const override { return width_; }

        /** @brief Returns the height in pixels of mip level 0. */
        [[nodiscard]] int GetHeight() const override { return height_; }

        /**
         * @brief Replaces the whole of mip level 0.
         *
         * @param rgba   Source pixels, RGBA8.
         * @param stride Row pitch in bytes of @p rgba.
         */
        void UpdatePixels(const std::uint8_t* rgba, int stride) override;

        /**
         * @brief Replaces the whole of one mip level.
         *
         * @param rgba   Source pixels, RGBA8, tightly packed.
         * @param level  Mip level to write.
         * @param levelW Width of that mip level.
         * @param levelH Height of that mip level.
         */
        void UpdatePixelsLevel(int level, const std::uint8_t* rgba, int levelW, int levelH) override;

        /**
         * @brief Reads pixels back from the GPU.
         *
         * @param level      Mip level to read.
         * @param x          Left edge of the region in pixels.
         * @param y          Top edge of the region in pixels.
         * @param w          Width of the region in pixels.
         * @param h          Height of the region in pixels.
         * @param data       Destination for tightly packed RGBA8 rows, top row first.
         * @param dataLength Size of @p data in bytes; must be at least w * h * 4.
         * @return True if the whole region was read back; false if it could not be.
         */
        [[nodiscard]] bool GetData(int level, int x, int y, int w, int h,
                                   void* data, int dataLength) const override;

        /** @brief Returns the underlying LLGL texture, or null for a transfer-only cube. */
        [[nodiscard]] LLGL::Texture* GetLlglTexture() const { return texture_; }

    private:
        LLGL::RenderSystem*  renderSystem_ = nullptr;
        LlglRenderer* owner_        = nullptr;
        LLGL::Texture*       texture_      = nullptr;
        int                  width_        = 0;
        int                  height_       = 0;
        int                  mipLevels_    = 1;
    };

    /**
     * @brief A cube texture living in LLGL. CNAEXT.
     *
     * Backs `Microsoft::Xna::Framework::Graphics::TextureCube`. One `LLGL::TextureType::TextureCube`
     * resource with 6 array layers, one per face, in the project-wide face order (0=+X, 1=-X, 2=+Y,
     * 3=-Y, 4=+Z, 5=-Z) -- the same order both LLGL and the underlying Vulkan/GL APIs already use
     * for a cube-compatible image, so face index maps directly to `baseArrayLayer` with no
     * remapping. Pixels are always RGBA8, matching `LlglTextureRenderer`.
     */
    class LlglTextureCubeRenderer final : public ITextureCubeRenderer
    {
    public:
        /**
         * @brief Takes ownership of an already-created LLGL cube texture.
         *
         * @param renderSystem Render system that created @p texture and will release it.
         * @param owner        Renderer to defer this texture's release through; null falls back to
         *                     releasing immediately. See `LlglTextureRenderer`'s own destructor
         *                     comment for why this matters.
         * @param texture      The texture resource; must not be null.
         * @param size         Width and height in pixels of mip level 0 (cube faces are square).
         * @param mipLevels    Number of mip levels the texture was created with.
         */
        LlglTextureCubeRenderer(LLGL::RenderSystem* renderSystem, LlglRenderer* owner,
                               LLGL::Texture* texture, int size, int mipLevels);

        /** @brief Defers releasing the LLGL texture; see `LlglTextureRenderer`'s own destructor. */
        ~LlglTextureCubeRenderer() override;

        LlglTextureCubeRenderer(const LlglTextureCubeRenderer&) = delete;
        LlglTextureCubeRenderer& operator=(const LlglTextureCubeRenderer&) = delete;

        /**
         * @brief Uploads raw RGBA8 pixels into a sub-rectangle of a single cube face.
         * @return True if the whole region was stored; false if this renderer stored nothing.
         */
        [[nodiscard]] bool SetData(int face, int level, int x, int y, int w, int h,
                                   const void* data, int dataLength) override;

        /**
         * @brief Reads back raw RGBA8 pixels from a sub-rectangle of a single cube face.
         * @return True if the whole region was written; false if this renderer read nothing back.
         */
        [[nodiscard]] bool GetData(int face, int level, int x, int y, int w, int h,
                                   void* data, int dataLength) const override;

        /** @brief Returns the underlying LLGL texture. */
        [[nodiscard]] LLGL::Texture* GetLlglTexture() const { return texture_; }

    private:
        LLGL::RenderSystem*  renderSystem_ = nullptr;
        LlglRenderer* owner_        = nullptr;
        LLGL::Texture*       texture_      = nullptr;
        int                  size_         = 0;
        int                  mipLevels_    = 1;
        /// Six tightly-packed RGBA8 faces per mip when `texture_` is null. This preserves the
        /// XNA TextureCube construction/upload/readback contract without pretending the validated
        /// OpenGL runtime can sample a cube texture in a shader.
        std::vector<std::vector<std::uint8_t>> cpuFaces_;
    };

    /**
     * @brief A volume (3D) texture living in LLGL. CNAEXT.
     *
     * Backs `Microsoft::Xna::Framework::Graphics::Texture3D`. One `LLGL::TextureType::Texture3D`
     * resource with a real depth extent (not array layers). Pixels are always RGBA8, matching
     * `LlglTextureRenderer`.
     */
    class LlglTexture3DRenderer final : public ITexture3DRenderer
    {
    public:
        /**
         * @brief Takes ownership of an already-created LLGL volume texture.
         *
         * @param renderSystem Render system that created @p texture and will release it.
         * @param owner        Renderer to defer this texture's release through; null falls back to
         *                     releasing immediately. See `LlglTextureRenderer`'s own destructor
         *                     comment for why this matters.
         * @param texture      The texture resource; must not be null.
         * @param width        Width in voxels of mip level 0.
         * @param height       Height in voxels of mip level 0.
         * @param depth        Depth in voxels of mip level 0.
         * @param mipLevels    Number of mip levels the texture was created with.
         */
        LlglTexture3DRenderer(LLGL::RenderSystem* renderSystem, LlglRenderer* owner,
                             LLGL::Texture* texture, int width, int height, int depth, int mipLevels);

        /** @brief Defers releasing the LLGL texture; see `LlglTextureRenderer`'s own destructor. */
        ~LlglTexture3DRenderer() override;

        LlglTexture3DRenderer(const LlglTexture3DRenderer&) = delete;
        LlglTexture3DRenderer& operator=(const LlglTexture3DRenderer&) = delete;

        /**
         * @brief Uploads raw RGBA8 voxels into a sub-volume of the given mip level.
         * @return True if the whole box was stored; false if this renderer stored nothing.
         */
        [[nodiscard]] bool SetData(int level, int x, int y, int z, int w, int h, int depth,
                                   const void* data, int dataLength) override;

        /**
         * @brief Reads back raw RGBA8 voxels from a sub-volume of the given mip level.
         * @return True if the whole box was written; false if this renderer read nothing back.
         */
        [[nodiscard]] bool GetData(int level, int x, int y, int z, int w, int h, int depth,
                                   void* data, int dataLength) const override;

        /** @brief Returns the underlying LLGL texture. */
        [[nodiscard]] LLGL::Texture* GetLlglTexture() const { return texture_; }

    private:
        LLGL::RenderSystem*  renderSystem_ = nullptr;
        LlglRenderer* owner_        = nullptr;
        LLGL::Texture*       texture_      = nullptr;
        int                  width_        = 0;
        int                  height_       = 0;
        int                  depth_        = 0;
        int                  mipLevels_    = 1;
    };

    /**
     * @brief A vertex buffer living in LLGL. CNAEXT.
     *
     * Uploads are real: the data reaches GPU memory and the vertex count round-trips. Drawing from
     * one is a 3D-pipeline operation, which this renderer does not implement yet -- see
     * `LlglRenderer::DrawColoredPrimitives`.
     */
    class LlglVertexBufferRenderer final : public IVertexBufferRenderer
    {
    public:
        /**
         * @brief Creates a buffer sized for @p vertexCapacity vertices of unknown stride.
         *
         * The final byte size is only known once `SetData` supplies a stride, so the LLGL buffer is
         * created lazily on the first upload.
         *
         * @param renderSystem   Render system that will own the buffer.
         * @param vertexCapacity Number of vertices the caller intends to store.
         */
        LlglVertexBufferRenderer(LLGL::RenderSystem* renderSystem, LlglRenderer* owner,
                                int vertexCapacity);

        /** @brief Releases the LLGL buffer. */
        ~LlglVertexBufferRenderer() override;

        LlglVertexBufferRenderer(const LlglVertexBufferRenderer&) = delete;
        LlglVertexBufferRenderer& operator=(const LlglVertexBufferRenderer&) = delete;

        /**
         * @brief Uploads vertex data.
         *
         * LLGL-46: a draw already queued (but not yet submitted) this frame may still reference
         * this buffer's CURRENT content by raw `LLGL::Buffer*` -- overwriting it in place, or
         * releasing it to grow, would corrupt or crash that earlier draw once it finally replays.
         * A re-upload after this buffer's first one therefore flushes any pending frame first
         * (`LlglRenderer::FlushPendingFrameEXT()`, a no-op when nothing is queued), so the
         * earlier draw genuinely runs against the content it was queued with before this call
         * changes it.
         *
         * @param data           Packed vertex data.
         * @param vertexCount    Number of vertices in @p data.
         * @param strideInBytes  Size of one vertex in bytes.
         */
        void SetData(const void* data, int vertexCount, std::size_t strideInBytes) override;

        /**
         * @brief Records the caller's vertex declaration.
         *
         * Kept so a later 3D draw path can build the matching LLGL vertex attribute list; the 2D
         * path has its own fixed sprite layout and never consults it.
         *
         * @param vertexDeclaration Declaration in declaration order, including its stride.
         */
        void SetVertexDeclaration(const VertexDeclaration& vertexDeclaration) override;

        /** @brief Returns the number of vertices most recently uploaded. */
        [[nodiscard]] int GetVertexCount() const override { return vertexCount_; }

        /** @brief Returns the underlying LLGL buffer, or null before the first upload. */
        [[nodiscard]] LLGL::Buffer* GetLlglBuffer() const { return buffer_; }

        /** @brief Returns the stride in bytes of the most recent upload, or 0 before it. */
        [[nodiscard]] std::size_t GetStride() const { return stride_; }

        /**
         * @brief Returns the LLGL vertex attributes this buffer's contents are described by.
         *
         * Translated once from the caller's `VertexDeclaration` (or, when none was supplied, from
         * the upload stride) and used for two different things that must agree: the vertex array
         * OpenGL builds for this buffer, and the input layout the 3D pipeline's vertex shader is
         * created with.
         *
         * @return The attributes, or an empty list before the first upload.
         */
        [[nodiscard]] const std::vector<LLGL::VertexAttribute>& GetVertexAttributes() const
        {
            return attributes_;
        }

    private:
        void ResolveVertexAttributes(std::size_t strideInBytes);

        LLGL::RenderSystem*  renderSystem_  = nullptr;
        LlglRenderer* owner_         = nullptr;
        LLGL::Buffer*       buffer_         = nullptr;
        int                 vertexCapacity_ = 0;
        int                 vertexCount_    = 0;
        std::size_t         stride_         = 0;
        std::size_t         byteCapacity_   = 0;
        bool                hasDeclaration_ = false;
        VertexDeclaration   declaration_;
        std::vector<LLGL::VertexAttribute> attributes_;
    };

    /**
     * @brief An index buffer living in LLGL. CNAEXT.
     *
     * Like `LlglVertexBufferRenderer`, uploads are real; consuming one in a draw call belongs to the
     * not-yet-implemented 3D pipeline.
     */
    class LlglIndexBufferRenderer final : public IIndexBufferRenderer
    {
    public:
        /**
         * @brief Creates an index buffer.
         *
         * @param renderSystem  Render system that will own the buffer.
         * @param indexCapacity Number of indices the caller intends to store.
         * @param thirtyTwoBit  True for 32-bit indices, false for 16-bit.
         */
        LlglIndexBufferRenderer(LLGL::RenderSystem* renderSystem, LlglRenderer* owner,
                               int indexCapacity, bool thirtyTwoBit);

        /** @brief Releases the LLGL buffer. */
        ~LlglIndexBufferRenderer() override;

        LlglIndexBufferRenderer(const LlglIndexBufferRenderer&) = delete;
        LlglIndexBufferRenderer& operator=(const LlglIndexBufferRenderer&) = delete;

        /**
         * @brief Uploads 16-bit index data.
         *
         * See `Upload()`'s own doc comment (LLGL-46) for why a re-upload flushes any pending frame
         * first.
         *
         * @param data       Packed 16-bit indices.
         * @param indexCount Number of indices.
         */
        void SetData16(const void* data, int indexCount) override;

        /**
         * @brief Uploads 32-bit index data.
         *
         * See `Upload()`'s own doc comment (LLGL-46) for why a re-upload flushes any pending frame
         * first.
         *
         * @param data       Packed 32-bit indices.
         * @param indexCount Number of indices.
         */
        void SetData32(const void* data, int indexCount) override;

        /** @brief Returns the number of indices most recently uploaded. */
        [[nodiscard]] int GetIndexCount() const override { return indexCount_; }

        /** @brief Returns whether this buffer stores 32-bit indices. */
        [[nodiscard]] bool IsThirtyTwoBit() const override { return thirtyTwoBit_; }

        /** @brief Returns the underlying LLGL buffer, or null before the first upload. */
        [[nodiscard]] LLGL::Buffer* GetLlglBuffer() const { return buffer_; }

    private:
        /**
         * @brief Common upload path for `SetData16()`/`SetData32()`.
         *
         * LLGL-46: mirrors `LlglVertexBufferRenderer::SetData()`'s own fix -- a re-upload after
         * this buffer's first one (`buffer_ != nullptr`) flushes any pending frame first
         * (`LlglRenderer::FlushPendingFrameEXT()`, a no-op when nothing is queued), so a
         * draw already queued this frame against this buffer's CURRENT content genuinely replays
         * with that content before this call overwrites or replaces it.
         */
        void Upload(const void* data, int indexCount, std::size_t indexSize);

        LLGL::RenderSystem*  renderSystem_ = nullptr;
        LlglRenderer* owner_        = nullptr;
        LLGL::Buffer*       buffer_        = nullptr;
        int                 indexCapacity_ = 0;
        int                 indexCount_    = 0;
        bool                thirtyTwoBit_  = false;
        std::size_t         byteCapacity_  = 0;
    };

    /**
     * @brief What `LlglRenderer::currentRenderTargetRenderer_` needs from whatever is
     * currently bound, regardless of whether that is a `RenderTarget2D` or one face of a
     * `RenderTargetCube`. CNAEXT.
     *
     * `QueueClear`/`QueueSpriteEXT`/`QueuePrimitives`/`GetActiveDrawRect` only ever need these
     * four things from the bound target -- this lets `currentRenderTargetRenderer_` point at
     * either kind without any of those call sites needing to know which.
     */
    class LlglBoundRenderTarget
    {
    public:
        virtual ~LlglBoundRenderTarget() = default;
        /** @brief Returns the underlying LLGL render target to begin a pass against. */
        [[nodiscard]] virtual LLGL::RenderTarget* GetLlglRenderTarget() const = 0;
        /** @brief Returns the width in pixels. */
        [[nodiscard]] virtual int GetWidth() const = 0;
        /** @brief Returns the height in pixels. */
        [[nodiscard]] virtual int GetHeight() const = 0;
        /** @brief Returns this target's own fixed pixel-to-clip-space sprite projection buffer. */
        [[nodiscard]] virtual LLGL::Buffer* GetSpriteProjectionBuffer() const = 0;
        /** @brief Returns how many simultaneous colour attachments this target has (1 for a plain
         *  `RenderTarget2D`/cube face, >1 for a multi-render-target bind -- see
         *  `LlglMRTBinding`). Pipeline creation needs this to build a matching `LLGL::RenderPass`
         *  and the right number of `blend.targets[]` entries. */
        [[nodiscard]] virtual int GetColorAttachmentCount() const { return 1; }
        /** @brief Returns the real, device-clamped MSAA sample count this target's own render pass
         *  was built with (1 = no MSAA). A pipeline drawn against this target must be built with a
         *  matching `rasterizer.multiSampleEnabled`/sample count, or a software Vulkan module can
         *  silently rasterize single-sample into a multisampled framebuffer instead of erroring --
         *  see `LlglRenderer::GetPrimarySampleCountEXT()`. Cube faces support MSAA (LLGL-34,
         *  see `LlglRenderTargetCubeFaceBinding`'s own override) and MRT binds do too now (LLGL-54,
         *  see `LlglMRTBinding`'s own override) -- the default of 1 (no MSAA) here only still
         *  applies to a plain single-target `LlglRenderTargetRenderer` bound without MSAA. */
        [[nodiscard]] virtual int GetSampleCount() const { return 1; }
        /** @brief Returns the colour texture that needs its mip chain regenerated after this
         *  target's render pass ends, or null when this target has no real mip chain to
         *  regenerate (the common case: a plain single-level target, a cube face, or an MRT
         *  bind, none of which support `mipMap` yet). Only `LlglRenderTargetRenderer` overrides
         *  this. See `LlglRenderer::RecordAndSubmitFrame()`'s own `GenerateMips()` call. */
        [[nodiscard]] virtual LLGL::Texture* GetMipRegenColorTextureEXT() const { return nullptr; }
    };

    /**
     * @brief An off-screen 2D render target living in LLGL. CNAEXT.
     *
     * Backs `Microsoft::Xna::Framework::Graphics::RenderTarget2D`. Owns a colour texture (and,
     * when requested, a depth/stencil texture) plus the `LLGL::RenderTarget` that binds them
     * together as a framebuffer.
     *
     * `BindAsRenderTarget()`/`UnbindAsRenderTarget()` are deliberately no-ops here: this renderer
     * defers every draw to `Present()`, so "which target is active" is tracked by
     * `LlglRenderer::SetRenderTarget2D()` recording a pointer, consulted when each Clear/
     * sprite/primitive command is queued -- not by an immediate GPU bind at the moment XNA code
     * calls `GraphicsDevice.SetRenderTarget()`.
     */
    class LlglRenderTargetRenderer final : public IRenderTargetRenderer, public LlglBoundRenderTarget
    {
    public:
        /**
         * @brief Takes ownership of an already-created LLGL render target and its textures.
         *
         * @param renderSystem  Render system that created these resources and will release them.
         * @param renderTarget  The framebuffer object; must not be null.
         * @param colorTexture  The colour attachment; must not be null.
         * @param depthTexture  The depth/stencil attachment, or null when none was requested.
         * @param width         Width in pixels.
         * @param height        Height in pixels.
         * @param hasRealDepthBuffer Whether @p depthTexture is non-null.
         * @param spriteProjectionBuffer Constant buffer holding this target's own pixel-to-clip-
         *        space projection matrix; must not be null. A target's resolution never changes
         *        after construction, unlike the swap chain's (which can resize), so this is built
         *        once here rather than re-uploaded every frame -- see `QueueSpriteEXT`'s use of
         *        it in place of the owning renderer's own `spriteProjectionBuffer_` whenever a
         *        sprite is queued while this target is bound.
         * @param owner Renderer whose frame this target may be referenced by; the destructor defers
         *        releasing the underlying LLGL objects to it (`ScheduleRenderTargetReleaseEXT`)
         *        rather than releasing them immediately, exactly like
         *        `LlglVertexBufferRenderer`/`LlglIndexBufferRenderer` already do for GPU buffers --
         *        a `RenderTarget2D` that goes out of scope before `Present()` (a perfectly normal
         *        pattern: create it, draw into it, sample it, let it die, all within one `Draw()`)
         *        must not free a resource `frameCommands_` still points at.
         * @param multiSampleCount The REAL, device-clamped sample count this target's own
         *        `LLGL::RenderTarget` was actually created with (`renderTarget->GetSamples()`),
         *        not the raw constructor request -- matches `IRenderTargetRenderer::
         *        GetMultiSampleCount()`'s own documented FNA-parity semantics. 1 (not 0) means no
         *        MSAA; `GetMultiSampleCountProperty()` reports 0 in that case (see this class's
         *        own `GetMultiSampleCount()` override).
         * @param mipLevels The number of mip levels @p colorTexture was actually created with
         *        (matches `RenderTarget2D.LevelCount`'s own `CalculateMipLevels(width, height)`
         *        formula, computed identically on the XNA side and here -- see
         *        `LlglRenderer::CreateRenderTarget2D()`). 1 means no real mip chain; a
         *        value greater than 1 makes `GetMipRegenColorTextureEXT()` return @p colorTexture
         *        so `RecordAndSubmitFrame()`/`CaptureBackbuffer()` regenerate levels 1.. from the
         *        just-rendered level 0 after every render pass this target appears in.
         */
        LlglRenderTargetRenderer(LLGL::RenderSystem* renderSystem, LLGL::RenderTarget* renderTarget,
                                LLGL::Texture* colorTexture, LLGL::Texture* depthTexture,
                                int width, int height, bool hasRealDepthBuffer,
                                LLGL::Buffer* spriteProjectionBuffer, LlglRenderer* owner,
                                int multiSampleCount = 1, int mipLevels = 1);

        /** @brief Releases the render target and its textures. */
        ~LlglRenderTargetRenderer() override;

        LlglRenderTargetRenderer(const LlglRenderTargetRenderer&) = delete;
        LlglRenderTargetRenderer& operator=(const LlglRenderTargetRenderer&) = delete;

        /** @brief Returns the width in pixels. */
        [[nodiscard]] int GetWidth() const override { return width_; }

        /** @brief Returns the height in pixels. */
        [[nodiscard]] int GetHeight() const override { return height_; }

        /**
         * @brief Reads pixels back from the colour attachment.
         *
         * @param level      Mip level to read; @p x/@p y/@p w/@p h are already in THAT level's own
         *                   pixel space (the caller, `Texture2D::GetData()`, scales them down from
         *                   level 0 before calling this). Returns false for a level outside
         *                   `[0, mipLevels_)`.
         * @param x          Left edge of the region in pixels, at @p level.
         * @param y          Top edge of the region in pixels, at @p level.
         * @param w          Width of the region in pixels, at @p level.
         * @param h          Height of the region in pixels, at @p level.
         * @param data       Destination for tightly packed RGBA8 rows, top row first.
         * @param dataLength Size of @p data in bytes; must be at least w * h * 4.
         * @return True if the whole region was read back; false if it could not be.
         */
        [[nodiscard]] bool GetData(int level, int x, int y, int w, int h,
                                   void* data, int dataLength) const override;

        /** @brief No-op; see this class's own doc comment for why. */
        void BindAsRenderTarget() override {}

        /** @brief No-op; see this class's own doc comment for why. */
        void UnbindAsRenderTarget() override {}

        /** @brief Returns whether this target has a real depth/stencil attachment. */
        [[nodiscard]] bool HasRealDepthBuffer(bool /*depthFormatWasRequested*/) const override
        {
            return hasRealDepthBuffer_;
        }

        /** @brief Returns the underlying LLGL render target. */
        [[nodiscard]] LLGL::RenderTarget* GetLlglRenderTarget() const override { return renderTarget_; }

        /** @brief Returns the underlying LLGL colour texture, for sampling as a `Texture2D`. */
        [[nodiscard]] LLGL::Texture* GetLlglColorTexture() const { return colorTexture_; }

        /** @brief Returns this target's own fixed pixel-to-clip-space sprite projection buffer. */
        [[nodiscard]] LLGL::Buffer* GetSpriteProjectionBuffer() const override { return spriteProjectionBuffer_; }

        /** @brief Returns the real, device-clamped MSAA sample count (0 = none), matching
         *  `RenderTarget2D.MultiSampleCount`'s own FNA-parity semantics -- see this class's own
         *  constructor doc comment for why the stored value is 1, not 0, for "no MSAA". */
        [[nodiscard]] int GetMultiSampleCount() const override
        {
            return multiSampleCount_ > 1 ? multiSampleCount_ : 0;
        }

        /** @brief Returns the real, device-clamped sample count this target's `LLGL::RenderTarget`
         *  was actually built with (1 = no MSAA) -- see `LlglBoundRenderTarget::GetSampleCount()`. */
        [[nodiscard]] int GetSampleCount() const override { return multiSampleCount_; }

        /** @brief Returns this target's own colour texture when it has a real mip chain
         *  (`mipLevels_ > 1`), or null otherwise -- see `LlglBoundRenderTarget::
         *  GetMipRegenColorTextureEXT()`'s own doc comment for why and how this is consumed. */
        [[nodiscard]] LLGL::Texture* GetMipRegenColorTextureEXT() const override
        {
            return mipLevels_ > 1 ? colorTexture_ : nullptr;
        }

    private:
        LLGL::RenderSystem* renderSystem_       = nullptr;
        LLGL::RenderTarget* renderTarget_        = nullptr;
        LLGL::Texture*      colorTexture_        = nullptr;
        LLGL::Texture*      depthTexture_        = nullptr;
        int                 width_               = 0;
        int                 height_              = 0;
        bool                hasRealDepthBuffer_  = false;
        LLGL::Buffer*       spriteProjectionBuffer_ = nullptr;
        LlglRenderer* owner_               = nullptr;
        int                 multiSampleCount_    = 1;
        int                 mipLevels_           = 1;
    };

    /**
     * @brief One face of an `LlglRenderTargetCubeRenderer`, as seen by
     * `LlglRenderer::currentRenderTargetRenderer_`. CNAEXT.
     *
     * A thin, non-owning view -- all 6 of these live as plain value members of the cube renderer
     * that owns the real LLGL resources; this class exists only so `currentRenderTargetRenderer_`
     * (typed `LlglBoundRenderTarget*`) can point at "face N of this cube" without a heap
     * allocation or a separate owning type.
     */
    class LlglRenderTargetCubeFaceBinding final : public LlglBoundRenderTarget
    {
    public:
        LlglRenderTargetCubeFaceBinding() = default;
        /**
         * @param mipRegenColorTexture The cube's own SHARED colour texture when it has a real mip
         *        chain (`mipLevels_ > 1` on the owning `LlglRenderTargetCubeRenderer`), or null
         *        otherwise -- see this class's own `GetMipRegenColorTextureEXT()` override (LLGL-35).
         */
        LlglRenderTargetCubeFaceBinding(LLGL::RenderTarget* renderTarget, int size,
                                        LLGL::Buffer* spriteProjectionBuffer, int sampleCount = 1,
                                        LLGL::Texture* mipRegenColorTexture = nullptr)
            : renderTarget_(renderTarget), size_(size), spriteProjectionBuffer_(spriteProjectionBuffer)
            , sampleCount_(sampleCount > 0 ? sampleCount : 1)
            , mipRegenColorTexture_(mipRegenColorTexture)
        {
        }

        [[nodiscard]] LLGL::RenderTarget* GetLlglRenderTarget() const override { return renderTarget_; }
        [[nodiscard]] int GetWidth() const override { return size_; }
        [[nodiscard]] int GetHeight() const override { return size_; }
        [[nodiscard]] LLGL::Buffer* GetSpriteProjectionBuffer() const override { return spriteProjectionBuffer_; }
        /** @brief Returns this face's own real, device-clamped sample count (LLGL-34; 1 = no MSAA)
         *  -- see `LlglBoundRenderTarget::GetSampleCount()`'s own doc comment. */
        [[nodiscard]] int GetSampleCount() const override { return sampleCount_; }
        /** @brief Returns the cube's own SHARED colour texture when it has a real mip chain, or
         *  null otherwise (LLGL-35). Unlike `LlglRenderTargetRenderer`'s own override, this is NOT
         *  unique per binding -- every one of the 6 `LlglRenderTargetCubeFaceBinding`s sharing one
         *  cube returns the SAME texture pointer, since `GenerateMips()` on
         *  `LLGL::TextureType::TextureCube` always regenerates every face's own mip chain at once
         *  (a plain, non-array `TextureCube`'s `TextureSubresource::baseArrayLayer` is documented
         *  as ignored, unlike `TextureCubeArray` -- confirmed by reading LLGL's own
         *  TextureFlags.h -- so there is no cheaper, single-face-only regeneration call available).
         *  Whichever face's render pass ends first in a given frame regenerates the whole cube's
         *  mip chain from every face's own CURRENT level-0 content; every other face bound in the
         *  same frame repeats the same whole-cube call again, which is redundant but not incorrect
         *  -- see `LlglRenderer::CreateRenderTargetCube()`'s own doc comment. */
        [[nodiscard]] LLGL::Texture* GetMipRegenColorTextureEXT() const override
        {
            return mipRegenColorTexture_;
        }

    private:
        LLGL::RenderTarget* renderTarget_ = nullptr;
        int                 size_         = 0;
        LLGL::Buffer*       spriteProjectionBuffer_ = nullptr;
        int                 sampleCount_  = 1;
        LLGL::Texture*      mipRegenColorTexture_ = nullptr;
    };

    /**
     * @brief An off-screen cube-map render target living in LLGL. CNAEXT.
     *
     * Backs `Microsoft::Xna::Framework::Graphics::RenderTargetCube`. Owns ONE
     * `LLGL::TextureType::TextureCube` colour texture (`arrayLayers=6`, sampled as a whole cube
     * afterwards -- e.g. by `EnvironmentMapEffect`, via `ResolveSampledTextureCube()`) and ONE
     * SHARED depth/stencil texture, matching FNA's own `RenderTargetCube`, which allocates exactly
     * one depth/stencil buffer for the whole cube rather than one per face (the Vulkan renderer's
     * own `VulkanRenderTargetCubeRenderer` follows the identical convention). Six
     * `LLGL::RenderTarget`s are built once at construction, each attaching the SAME colour texture
     * at a different `arrayLayer` (`LLGL::AttachmentDescriptor`'s own documented cube-face
     * convention) alongside the shared depth texture -- never recreated per bind.
     *
     * Like `LlglRenderTargetRenderer`, `BindAsRenderTargetFace()`/`UnbindAsRenderTarget()` are
     * no-ops: "which face is active" is tracked by `LlglRenderer::currentRenderTargetRenderer_`
     * pointing at one of `faceBindings_` below, exactly like a plain `RenderTarget2D` bind.
     */
    class LlglRenderTargetCubeRenderer final : public IRenderTargetCubeRenderer
    {
    public:
        /**
         * @brief Takes ownership of an already-created cube texture, shared depth texture, and the
         * six per-face render targets built from them.
         *
         * @param renderSystem  Render system that created these resources and will release them.
         * @param cubeTexture   The shared colour attachment (6 array layers); must not be null.
         * @param depthTexture  The shared depth/stencil attachment; must not be null.
         * @param faceTargets   Six framebuffer objects, one per face (0=+X..5=-Z), each attaching
         *                      @p cubeTexture at its own array layer and @p depthTexture; none may
         *                      be null.
         * @param size          Width/height of each face in pixels.
         * @param hasRealDepthBuffer Whether a real `DepthFormat` was requested (the physical
         *        buffer is always allocated regardless -- see `HasRealDepthBuffer()`'s own doc).
         * @param spriteProjectionBuffer This cube's own fixed pixel-to-clip-space projection --
         *        shared by all 6 faces, since every face is the same size (a cube is always
         *        square) and a face's resolution never changes after construction.
         * @param owner Renderer whose frame this target may be referenced by; the destructor defers
         *        releasing the underlying LLGL objects to it, exactly like `LlglRenderTargetRenderer`.
         * @param multiSampleCount The REAL, device-clamped sample count every face's own
         *        `LLGL::RenderTarget` was actually created with (`faceTargets[0]->GetSamples()`,
         *        since all 6 share one request) -- matches `LlglRenderTargetRenderer`'s own
         *        constructor parameter (LLGL-34). 1 (not 0) means no MSAA.
         * @param mipLevels The number of mip levels @p cubeTexture was actually created with
         *        (matches `RenderTargetCube.LevelCount`'s own `CalculateMipLevels(size)` formula,
         *        computed identically on the XNA side and here -- see
         *        `LlglRenderer::CreateRenderTargetCube()`). 1 means no real mip chain; a
         *        value greater than 1 makes every `LlglRenderTargetCubeFaceBinding`'s own
         *        `GetMipRegenColorTextureEXT()` return @p cubeTexture, matching
         *        `LlglRenderTargetRenderer`'s own constructor parameter (LLGL-35).
         */
        LlglRenderTargetCubeRenderer(LLGL::RenderSystem* renderSystem, LLGL::Texture* cubeTexture,
                                    LLGL::Texture* depthTexture,
                                    const std::array<LLGL::RenderTarget*, 6>& faceTargets,
                                    int size, bool hasRealDepthBuffer,
                                    LLGL::Buffer* spriteProjectionBuffer, LlglRenderer* owner,
                                    int multiSampleCount = 1, int mipLevels = 1);

        /** @brief Releases the six render targets and the shared cube/depth textures and buffer. */
        ~LlglRenderTargetCubeRenderer() override;

        LlglRenderTargetCubeRenderer(const LlglRenderTargetCubeRenderer&) = delete;
        LlglRenderTargetCubeRenderer& operator=(const LlglRenderTargetCubeRenderer&) = delete;

        /** @brief Returns the width/height of each cube face in pixels. */
        [[nodiscard]] int GetSize() const override { return size_; }

        /** @brief No-op; see this class's own doc comment for why. */
        void BindAsRenderTargetFace(int /*face*/) override {}

        /** @brief No-op; see this class's own doc comment for why. */
        void UnbindAsRenderTarget() override {}

        /** @brief Returns whether this target has a real depth/stencil attachment. */
        [[nodiscard]] bool HasRealDepthBuffer(bool /*depthFormatWasRequested*/) const override
        {
            return hasRealDepthBuffer_;
        }

        /** @brief Returns the real, device-clamped MSAA sample count (0 = none), matching
         *  `RenderTargetCube.MultiSampleCount`'s own FNA-parity semantics (LLGL-34) -- see
         *  `LlglRenderTargetRenderer::GetMultiSampleCount()`'s own identical convention. */
        [[nodiscard]] int GetMultiSampleCount() const override
        {
            return multiSampleCount_ > 1 ? multiSampleCount_ : 0;
        }

        /**
         * @brief Reads pixels back from one face of the colour attachment.
         *
         * @param face       Which face to read (0=+X..5=-Z).
         * @param level      Mip level to read; @p x/@p y/@p w/@p h are already in THAT level's own
         *                   pixel space (the caller, `TextureCube::GetData()`, scales them down
         *                   from level 0 before calling this). Returns false for a level outside
         *                   `[0, mipLevels_)` (LLGL-35; previously any non-zero level).
         * @param x          Left edge of the region in pixels, at @p level.
         * @param y          Top edge of the region in pixels.
         * @param w          Width of the region in pixels.
         * @param h          Height of the region in pixels.
         * @param data       Destination for tightly packed RGBA8 rows, top row first.
         * @param dataLength Size of @p data in bytes; must be at least w * h * 4.
         * @return True if the whole region was read back; false if it could not be.
         */
        [[nodiscard]] bool GetData(int face, int level, int x, int y, int w, int h,
                                   void* data, int dataLength) const override;

        /** @brief Returns the underlying LLGL cube texture, for sampling as a `TextureCube`. */
        [[nodiscard]] LLGL::Texture* GetLlglTexture() const { return cubeTexture_; }

        /** @brief Returns the bindable view of face @p face (0=+X..5=-Z). */
        [[nodiscard]] LlglBoundRenderTarget* GetFaceBinding(int face)
        {
            return &faceBindings_[static_cast<std::size_t>(face)];
        }

    private:
        LLGL::RenderSystem* renderSystem_       = nullptr;
        LLGL::Texture*      cubeTexture_        = nullptr;
        LLGL::Texture*      depthTexture_       = nullptr;
        std::array<LLGL::RenderTarget*, 6> faceTargets_{};
        std::array<LlglRenderTargetCubeFaceBinding, 6> faceBindings_{};
        int                 size_               = 0;
        bool                hasRealDepthBuffer_ = false;
        LLGL::Buffer*       spriteProjectionBuffer_ = nullptr;
        LlglRenderer* owner_              = nullptr;
        int                 multiSampleCount_   = 1;
        int                 mipLevels_          = 1;
    };

    /**
     * @brief A multiple-render-target (MRT) bind, combining N independently-owned
     * `RenderTarget2D`s into ONE `LLGL::RenderTarget` for the duration of the bind. CNAEXT.
     *
     * Unlike `RenderTarget2D`/`RenderTargetCube`, which each own a real, explicit XNA-level C++
     * object (`RenderTarget2D`/`RenderTargetCube`) with its own construction/destruction lifetime,
     * an MRT bind has no owning XNA object at all -- `GraphicsDevice.SetRenderTargets(const
     * std::vector<RenderTargetBinding>&)` just names N ALREADY-EXISTING render targets as slots.
     * `LlglMRTBinding` is therefore owned by `LlglRenderer` itself
     * (`currentMrtBinding_`), replaced (and the old one deferred-released, exactly like a
     * destroyed `RenderTarget2D` is) on every subsequent `SetRenderTargets()`/`SetRenderTarget2D()`
     * call, not by RAII on a game-visible object.
     *
     * `renderTarget_` is the only thing this class actually owns -- its N colour attachments are
     * BORROWED `LLGL::Texture*`s from the N bound `RenderTarget2D`s' own renderers (still owned and
     * released by THEM, not duplicated or double-released here), and its depth/stencil attachment
     * is a fresh ANONYMOUS one LLGL allocates and owns internally as part of `renderTarget_`
     * itself -- exactly like `CreateRenderTarget2D`'s own single-target depth attachment -- rather
     * than trying to share any one slot's own depth buffer. This means depth content is not
     * preserved from a target's own prior individual (non-MRT) binds, matching this renderer's
     * already-established `RenderTargetUsage.PreserveContents`-is-not-honoured-across-binds
     * scope (see `CreateRenderTarget2D`'s own doc comment) rather than inventing a new one.
     *
     * `spriteProjectionBuffer_` is BORROWED from slot 0 too (every slot shares the same
     * width/height by the time `GraphicsDevice::SetRenderTargets` validates them, so slot 0's
     * projection is correct for all of them) -- no new buffer is created.
     *
     * Scoped to `RenderTarget2D` slots only in this first cut: mixing `RenderTargetCube` faces
     * into an MRT set is refused by name (`SetRenderTargets`), and 3D (`DrawPrimitivesEx`/
     * `DrawIndexedPrimitivesEx`) draws while an MRT set is bound are refused by name too
     * (`QueuePrimitives`) -- real XNA MRT is meaningfully useful/testable only through a custom
     * `ShaderEffect` with multiple `layout(location=N) out` fragment outputs drawn via
     * `SpriteBatch`, which is the one path this first cut supports.
     */
    class LlglMRTBinding final : public LlglBoundRenderTarget
    {
    public:
        LlglMRTBinding(LLGL::RenderTarget* renderTarget, int width, int height,
                      int colorAttachmentCount, LLGL::Buffer* spriteProjectionBuffer,
                      int sampleCount)
            : renderTarget_(renderTarget), width_(width), height_(height)
            , colorAttachmentCount_(colorAttachmentCount)
            , spriteProjectionBuffer_(spriteProjectionBuffer)
            , sampleCount_(sampleCount)
        {
        }

        [[nodiscard]] LLGL::RenderTarget* GetLlglRenderTarget() const override { return renderTarget_; }
        [[nodiscard]] int GetWidth() const override { return width_; }
        [[nodiscard]] int GetHeight() const override { return height_; }
        [[nodiscard]] LLGL::Buffer* GetSpriteProjectionBuffer() const override { return spriteProjectionBuffer_; }
        [[nodiscard]] int GetColorAttachmentCount() const override { return colorAttachmentCount_; }
        /** @brief LLGL-54: the real, device-clamped MSAA sample count every bound slot shares --
         *  `GraphicsDevice::SetRenderTargets()`'s own shared validation layer already requires
         *  every slot's OWN applied `MultiSampleCount` to agree before this binding is ever built,
         *  so slot 0's is authoritative for the whole bind. Previously unset (inherited the base
         *  class's hardcoded `1`), so a pipeline drawn against a multisampled MRT bind was built
         *  single-sample regardless of what the render target itself was actually created with. */
        [[nodiscard]] int GetSampleCount() const override { return sampleCount_; }

    private:
        LLGL::RenderTarget* renderTarget_          = nullptr;
        int                 width_                 = 0;
        int                 height_                = 0;
        int                 colorAttachmentCount_  = 1;
        LLGL::Buffer*       spriteProjectionBuffer_ = nullptr;
        int                 sampleCount_            = 1;
    };

    /**
     * @brief An occlusion query living in LLGL. CNAEXT.
     *
     * Backs `Microsoft::Xna::Framework::Graphics::OcclusionQuery`. `Begin()`/`End()` record
     * `LLGL::CommandBuffer::BeginQuery()`/`EndQuery()` into the owning renderer's deferred frame,
     * exactly like a `Clear`/`Sprite`/`Primitives` command -- LLGL requires both to be issued
     * inside an open render pass, which this renderer only ever opens at `Present()`/
     * `ReadBackbuffer()`/`FlushPendingFrameEXT()` time.
     *
     * A fresh `LLGL::QueryHeap` is created for every `Begin()`, never reused across cycles: LLGL
     * 0.04b's own Vulkan module never issues the `vkCmdResetQueryPool` a query needs before a
     * second `vkCmdBeginQuery` on the same query index (`VKCommandBuffer::ResetQueryPoolsInFlight`
     * exists but is `#if 0`'d out in the vendored source, confirmed by reading it directly) --
     * reusing one would be undefined behaviour by the Vulkan spec's own query-reset rule. A fresh
     * query pool is always in the valid "unavailable" state for its first use, so this sidesteps
     * the gap entirely rather than working around it with an explicit reset LLGL does not expose.
     *
     * `IsComplete()`/`PixelCount()` answer synchronously: if this query's `End()` is still sitting
     * unsubmitted in the owning renderer's frame, they force a full submit-and-wait
     * (`FlushPendingFrameEXT()`) first. Real hardware occlusion queries are meant to be polled
     * asynchronously across frames to avoid a CPU stall; this renderer trades that performance
     * characteristic for a result that is always immediately correct rather than genuinely async --
     * a documented, deliberate simplification, not a silent behavioural gap.
     */
    class LlglOcclusionQueryRenderer final : public IOcclusionQueryRenderer
    {
    public:
        /**
         * @brief Binds this query to the renderer that will record and submit it.
         *
         * @param renderSystem Render system that creates and releases each query heap.
         * @param queue        Command queue used to read back the query result.
         * @param owner        Renderer whose frame this query's Begin/End commands are queued into.
         */
        LlglOcclusionQueryRenderer(LLGL::RenderSystem* renderSystem, LLGL::CommandQueue* queue,
                                  LlglRenderer* owner);

        /** @brief Releases the current query heap, if any (deferred if a frame may reference it). */
        ~LlglOcclusionQueryRenderer() override;

        LlglOcclusionQueryRenderer(const LlglOcclusionQueryRenderer&) = delete;
        LlglOcclusionQueryRenderer& operator=(const LlglOcclusionQueryRenderer&) = delete;

        /** @brief Creates a fresh query heap and queues `BeginQuery` into the current frame. */
        void Begin() override;

        /** @brief Queues `EndQuery` into the current frame. */
        void End() override;

        /** @brief Forces any pending frame containing this query to submit, then polls the result. */
        [[nodiscard]] bool IsComplete() const override;

        /** @brief Forces any pending frame containing this query to submit, then reads the result. */
        [[nodiscard]] int PixelCount() const override;

    private:
        void ResolveResultEXT() const;

        LLGL::RenderSystem*  renderSystem_ = nullptr;
        LLGL::CommandQueue*  queue_        = nullptr;
        LlglRenderer* owner_        = nullptr;
        LLGL::QueryHeap*     queryHeap_    = nullptr;
        mutable bool         hasResult_    = false;
        mutable std::uint64_t pixelCount_  = 0;
    };

    /**
     * @brief A custom, hand-authored GLSL shader living in LLGL. CNAEXT.
     *
     * Backs `Microsoft::Xna::Framework::Graphics::ShaderEffect`. Scoped to `SpriteBatch` draws
     * only (the same scope the project's native Vulkan renderer's own `VulkanEffectRenderer`
     * already established for exactly the same reason: a custom effect's vertex shader is bound
     * to the fixed `position/texCoord/color` sprite vertex layout, not an arbitrary
     * `VertexDeclaration` a game's own 3D draw might use).
     *
     * `vertSrc`/`fragSrc` are always GLSL source text -- unlike the native Vulkan renderer (which
     * expects the caller to hand it pre-compiled SPIR-V, since it names one fixed native API),
     * this renderer picks its module at runtime, so the game has no reliable way to know in
     * advance whether it needs to hand over GLSL or SPIR-V. `CompileProgram()` compiles the GLSL
     * directly when the loaded module accepts it (OpenGL), or through a real runtime
     * GLSL-\>SPIR-V compile via `libshaderc` when it does not (Vulkan) -- the same problem another
     * runtime-selected renderer already solved the same way.
     *
     * Named-uniform setters (`SetUniformMat4`/`Vec4`/... ) do not do real name-based reflection --
     * LLGL exposes none for a raw GLSL/SPIR-V module, and adding one would need a new dependency
     * (e.g. SPIRV-Cross) -- they map onto the SAME fixed 32-float (128-byte) staging block
     * `VulkanEffectRenderer::pushConst_` already documents and this class's own `.cpp` doc comment
     * repeats verbatim, uploaded to a real constant buffer instead of a Vulkan push constant.
     * `name` is accepted (matching the shared `IEffectRenderer` signature) but not consulted,
     * exactly like the existing precedent. A custom shader's own GLSL therefore declares a
     * `uniform PC { vec4 vpSize_pad; mat4 uMatrix; vec4 uColor; float uFloat0; ... } pc;`-shaped
     * block at binding 1, matching every other CNA renderer that has this same convention.
     */
    class LlglEffectRenderer final : public IEffectRenderer
    {
    public:
        /**
         * @brief Binds this effect to the renderer that will compile and draw it.
         *
         * @param owner Renderer that owns the render system, the shared custom-effect pipeline
         *              layout, and the frame this effect's draws are queued into.
         */
        explicit LlglEffectRenderer(LlglRenderer& owner);

        /** @brief Releases this effect's shader modules and cached pipelines (deferred if a frame
         *  may reference them). */
        ~LlglEffectRenderer() override;

        LlglEffectRenderer(const LlglEffectRenderer&) = delete;
        LlglEffectRenderer& operator=(const LlglEffectRenderer&) = delete;

        /**
         * @brief Compiles the vertex/fragment GLSL source.
         *
         * @param vertSrc GLSL vertex shader source (not a file path).
         * @param fragSrc GLSL fragment shader source (not a file path).
         * @return True on success; false if compilation failed (see `GetCompileError()`).
         */
        bool CompileProgram(const std::string& vertSrc, const std::string& fragSrc) override;

        /** @brief Marks this effect as the one `SpriteBatch` draws through until `Unbind()`/a new
         *  `Bind()`. No-op if `CompileProgram()` did not succeed. */
        void Bind() override;

        /** @brief Clears this effect as the active one, restoring the stock sprite shader. */
        void Unbind() override;

        /** @brief Returns whether `CompileProgram()` succeeded. */
        [[nodiscard]] bool IsValid() const override;

        /** @brief Returns the last compilation error, or empty if none. */
        [[nodiscard]] std::string GetCompileError() const override;

        /** @brief Sets the 4x4 `uMatrix` uniform (column-major), taking effect for draws queued
         *  from this call onward. `name` is accepted but not consulted -- see this class's own
         *  doc comment. */
        void SetUniformMat4(const char* name, const float* matrix) override;
        /** @brief Sets the `uColor` uniform's x/y/z/w. `name` is accepted but not consulted. */
        void SetUniformVec4(const char* name, float x, float y, float z, float w) override;
        /** @brief Sets the `uColor` uniform's x/y/z, leaving w unchanged. `name` is accepted but
         *  not consulted. */
        void SetUniformVec3(const char* name, float x, float y, float z) override;
        /** @brief Sets the `uColor` uniform's x/y, leaving z/w unchanged. `name` is accepted but
         *  not consulted. */
        void SetUniformVec2(const char* name, float x, float y) override;
        /** @brief Sets the single `uFloat0` uniform. `name` is accepted but not consulted -- see
         *  this class's own doc comment. */
        void SetUniformFloat(const char* name, float value) override;
        /** @brief Sets the single `uFloat0` uniform from an int. `name` is accepted but not
         *  consulted. */
        void SetUniformInt(const char* name, int value) override;

        /** @brief Returns the compiled vertex shader, or null before a successful CompileProgram(). */
        [[nodiscard]] LLGL::Shader* GetVertexShader() const { return vertexShader_; }
        /** @brief Returns the compiled fragment shader, or null before a successful CompileProgram(). */
        [[nodiscard]] LLGL::Shader* GetFragmentShader() const { return fragmentShader_; }
        /** @brief Returns (creating if needed) the pipeline for the given blend/scissor state. */
        [[nodiscard]] LLGL::PipelineState* AcquirePipeline(std::uint64_t blendKey, bool scissorEnabled,
                                                             int colorAttachmentCount = 1);
        /** @brief Returns this effect's current 32-float uniform staging block (see this class's
         *  own doc comment for the byte layout); `[0]`/`[1]` (vpSize) are overwritten by the
         *  caller just before queuing, every other field reflects the last `SetUniformX()` call. */
        [[nodiscard]] const float* GetUniformStaging() const { return uniformStaging_; }

    private:
        LlglRenderer& owner_;
        LLGL::Shader* vertexShader_   = nullptr;
        LLGL::Shader* fragmentShader_ = nullptr;
        std::string compileError_;
        bool valid_ = false;
        std::map<std::uint64_t, LLGL::PipelineState*> pipelineCache_;
        // [0..1]=vpSize (overwritten per draw by the caller), [2..3]=padding,
        // [4..19]=uMatrix, [20..23]=uColor, [24..31]=uFloats (only [24]=uFloat0 is ever written).
        float uniformStaging_[32] = {};
    };

    /**
     * @brief Sprite rendering for `Microsoft::Xna::Framework::Graphics::SpriteBatch`. CNAEXT.
     *
     * Each `Draw` turns into six vertices appended to the owning renderer's frame, so sprites from
     * one Begin/End block reach the GPU in submission order together with the frame's clears.
     */
    class LlglSpriteBatchRenderer final : public ISpriteBatchRenderer
    {
    public:
        /**
         * @brief Binds this sprite batch to the renderer that will draw its sprites.
         *
         * @param owner Renderer that owns the swap chain and the frame's command list.
         */
        explicit LlglSpriteBatchRenderer(LlglRenderer& owner);

        /** @brief Resets the per-block sampler and transform state. */
        void Begin() override;

        /** @brief Ends the block; the queued sprites are drawn when the frame is presented. */
        void End() override;

        /**
         * @brief Sets the transform applied on top of the 2D projection.
         *
         * @param m Transform matrix, in XNA row-vector convention.
         */
        void SetTransformMatrix(const Matrix& m) override;

        /**
         * @brief Sets the texture filter used by subsequent draws in this block.
         *
         * @param textureFilter Raw `TextureFilter` ordinal.
         */
        void SetSamplerFilter(int textureFilter) override;

        /**
         * @brief Sets the texture addressing modes used by subsequent draws in this block.
         *
         * @param addressU Raw `TextureAddressMode` ordinal for U.
         * @param addressV Raw `TextureAddressMode` ordinal for V.
         */
        void SetSamplerAddressMode(int addressU, int addressV) override;

        /**
         * @brief Activates (or clears) the custom `ShaderEffect` this block's draws go through.
         *
         * Called by `SpriteBatch::Begin()`/`End()` (LLGL-27), not by the game directly. A non-null
         * @p effect is `Apply()`'d immediately, which -- for a `ShaderEffect` -- binds its
         * `LlglEffectRenderer` (`IEffectRenderer::Bind()`) and makes it the one every `Draw()` in
         * this block resolves through; null restores the stock sprite shader. Mirrors the native
         * Vulkan renderer's own `VulkanSpriteBatchRenderer` integration point.
         *
         * @param effect Custom effect to apply, or null to restore the stock sprite shader.
         */
        void SetCustomEffect(Effect* effect) override;

        /**
         * @brief Draws a whole texture at a position, unscaled and untinted.
         *
         * @param texture Texture to draw.
         * @param x       Left edge in logical pixels.
         * @param y       Top edge in logical pixels.
         */
        void Draw(const ITextureRenderer& texture, float x, float y) override;

        /**
         * @brief Draws a source region of a texture into a destination rectangle with a tint.
         *
         * @param texture              Texture to draw.
         * @param destinationRectangle Destination in logical pixels.
         * @param sourceRectangle      Source region in texels.
         * @param color                Tint multiplied with the sampled texel.
         */
        void Draw(const ITextureRenderer& texture,
                  const Rectangle& destinationRectangle,
                  const Rectangle& sourceRectangle,
                  const Color& color) override;

        /**
         * @brief Draws a sprite with rotation, origin and flipping.
         *
         * @param texture              Texture to draw.
         * @param destinationRectangle Destination in logical pixels.
         * @param sourceRectangle      Source region in texels.
         * @param color                Tint multiplied with the sampled texel.
         * @param rotation             Rotation in radians about @p origin.
         * @param origin               Rotation origin in source texels.
         * @param effects              Horizontal/vertical flip flags.
         * @param layerDepth           Layer depth; ignored, this renderer draws in submission order.
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
        LlglRenderer& owner_;
        Matrix               transform_;
        int                  textureFilter_ = 0;
        int                  addressU_      = 1;
        int                  addressV_      = 1;
    };

    /**
     * @brief CNA graphics renderer built on LLGL. CNAEXT.
     *
     * Unlike the other renderers in this project, this one names no native graphics API: LLGL is
     * itself an abstraction layer, and the module it drives (OpenGL or Vulkan) is chosen when the
     * process starts -- see `Detail::ResolveRendererModule`.
     *
     * Scope of the current implementation is the 2D pipeline: swap chain, clears, presentation
     * (including virtual-resolution letterboxing), `Texture2D` upload and readback, and
     * `SpriteBatch` with real blend and sampler state. The 3D pipeline, render targets, cube and
     * volume textures and custom effects are not implemented; each either reports itself
     * unsupported through the shared interface's own "no renderer" convention or fails loudly.
     */
    class LlglRenderer final : public IGraphicsRenderer
    {
    public:
        /**
         * @brief Creates the render system, swap chain and sprite pipeline.
         *
         * @param args Window, virtual resolution, presentation mode, sample count and swap
         *             interval requested by `GraphicsDevice`.
         * @throws std::runtime_error If no renderer module can be loaded, the window cannot be
         *         expressed as an LLGL surface, or any GPU resource fails to be created.
         */
        explicit LlglRenderer(const GraphicsRendererCreateArgs& args);

        /** @brief Releases every GPU resource and unloads the render system. */
        ~LlglRenderer() override;

        LlglRenderer(const LlglRenderer&) = delete;
        LlglRenderer& operator=(const LlglRenderer&) = delete;

        /**
         * @brief Queues a colour clear of the whole back buffer.
         *
         * @param r,g,b,a Clear colour components in the range 0..1.
         */
        void Clear(float r, float g, float b, float a) override;

        /**
         * @brief Queues a colour and depth clear.
         *
         * @param r,g,b,a Clear colour components in the range 0..1.
         * @param depth   Depth value to clear with.
         */
        void ClearColorAndDepth(float r, float g, float b, float a, float depth) override;

        /**
         * @brief Queues a depth-only clear.
         *
         * @param depth Depth value to clear with.
         */
        void ClearDepth(float depth) override;

        /**
         * @brief Queues a stencil-only clear.
         *
         * @param stencil Stencil value to clear with.
         */
        void ClearStencil(int stencil) override;

        /**
         * @brief Queues a depth and stencil clear.
         *
         * @param depth   Depth value to clear with.
         * @param stencil Stencil value to clear with.
         */
        void ClearDepthAndStencil(float depth, int stencil) override;

        /**
         * @brief Queues a colour and stencil clear.
         *
         * @param r,g,b,a Clear colour components in the range 0..1.
         * @param stencil Stencil value to clear with.
         */
        void ClearColorAndStencil(float r, float g, float b, float a, int stencil) override;

        /**
         * @brief Queues a colour, depth and stencil clear.
         *
         * @param r,g,b,a Clear colour components in the range 0..1.
         * @param depth   Depth value to clear with.
         * @param stencil Stencil value to clear with.
         */
        void ClearColorDepthAndStencil(float r, float g, float b, float a, float depth, int stencil) override;

        /** @brief Records the frame's commands, submits them and presents the back buffer. */
        void Present() override;

        /**
         * @brief Reports the logical (virtual-resolution) size games draw into.
         *
         * @param width  Receives the logical width in pixels.
         * @param height Receives the logical height in pixels.
         */
        void GetViewportSize(int& width, int& height) override;

        /** @brief Refreshes size/density and resizes the swap chain when the drawable changed. */
        void OnSurfaceChanged(const RendererSurfaceInfo& surface) override;

        /**
         * @brief Changes the logical resolution at runtime.
         *
         * @param width  New logical width in pixels.
         * @param height New logical height in pixels.
         */
        void SetVirtualResolution(int width, int height) override;

        /**
         * @brief Changes how the logical resolution is fitted onto the window.
         *
         * @param mode Raw `CnaPresentationMode` ordinal.
         */
        void SetPresentationMode(int mode) override;

        /**
         * @brief Changes the swap interval.
         *
         * @param interval 0 for immediate, 1 for vsync, 2 for half refresh rate.
         */
        void SetSwapInterval(int interval) override;

        /** @brief Returns the back buffer's actual sample count, or 0 when not multisampled. */
        [[nodiscard]] int GetMultiSampleCount() const override;

        /**
         * @brief Converts a point from window pixels to logical game coordinates.
         *
         * @param windowX Window-space X in pixels.
         * @param windowY Window-space Y in pixels.
         * @param logX    Receives the logical X.
         * @param logY    Receives the logical Y.
         * @return True when the conversion was performed.
         */
        bool TransformWindowToLogical(float windowX, float windowY,
                                      float& logX, float& logY) const override;

        /**
         * @brief Converts a point from logical game coordinates to window pixels.
         *
         * @param logX    Logical X.
         * @param logY    Logical Y.
         * @param windowX Receives the window-space X in pixels.
         * @param windowY Receives the window-space Y in pixels.
         * @return True when the conversion was performed.
         */
        bool TransformLogicalToWindow(float logX, float logY,
                                      float& windowX, float& windowY) const override;

        /**
         * @brief Creates a 2D texture from RGBA8 pixels.
         *
         * @param data Image dimensions, mip level count and pixel data.
         * @return The new texture renderer.
         */
        std::unique_ptr<ITextureRenderer> CreateTexture(const ImageData& data) override;

        /**
         * @brief Creates a cube texture, one RGBA8 face per side.
         *
         * @param size         Width and height in pixels of each square face.
         * @param mipMap       Whether to allocate a full mip chain.
         * @param surfaceFormat Ignored, matching the Vulkan renderer -- every texture in this
         *                      renderer is RGBA8.
         * @return The new cube texture renderer.
         */
        std::unique_ptr<ITextureCubeRenderer> CreateTextureCube(int size, bool mipMap,
                                                                int surfaceFormat) override;

        /**
         * @brief Creates a volume (3D) texture, RGBA8.
         *
         * @param w            Width in voxels.
         * @param h            Height in voxels.
         * @param depth        Depth in voxels.
         * @param mipMap       Whether to allocate a full mip chain.
         * @param surfaceFormat Ignored, matching the Vulkan renderer -- every texture in this
         *                      renderer is RGBA8.
         * @return The new volume texture renderer.
         */
        std::unique_ptr<ITexture3DRenderer> CreateTexture3D(int w, int h, int depth, bool mipMap,
                                                            int surfaceFormat) override;

        /** @brief Creates a sprite batch bound to this renderer. */
        std::unique_ptr<ISpriteBatchRenderer> CreateSpriteBatch() override;

        /**
         * @brief Creates a vertex buffer.
         *
         * @param vertex_capacity Number of vertices the caller intends to store.
         * @return The new vertex buffer renderer.
         */
        std::unique_ptr<IVertexBufferRenderer> CreateVertexBuffer(int vertex_capacity) override;

        /**
         * @brief Creates a 16-bit index buffer.
         *
         * @param index_capacity Number of indices the caller intends to store.
         * @return The new index buffer renderer.
         */
        std::unique_ptr<IIndexBufferRenderer> CreateIndexBuffer16(int index_capacity) override;

        /**
         * @brief Creates a 32-bit index buffer.
         *
         * @param index_capacity Number of indices the caller intends to store.
         * @return The new index buffer renderer.
         */
        std::unique_ptr<IIndexBufferRenderer> CreateIndexBuffer32(int index_capacity) override;

        /**
         * @brief Reads rendered back-buffer pixels.
         *
         * @param x      Left edge of the region in window pixels.
         * @param y      Top edge of the region in window pixels.
         * @param w      Width of the region in pixels.
         * @param h      Height of the region in pixels.
         * @param pixels Destination for w * h * 4 bytes of RGBA8, top row first.
         * @note Under a scaled or letterboxed presentation one logical pixel covers a block of
         *       window pixels; the block's centre pixel is returned. Nearest-neighbour rather than
         *       an average, so every value handed back is a colour the frame genuinely contained.
         * @throws std::runtime_error If the copy could not be performed.
         */
        void ReadBackbuffer(int x, int y, int w, int h, std::uint8_t* pixels) override;

        /**
         * @brief Creates an occlusion query.
         *
         * @return The new occlusion query renderer; never null.
         */
        std::unique_ptr<IOcclusionQueryRenderer> CreateOcclusionQuery() override;

        /**
         * @brief Creates an off-screen `RenderTarget2D`.
         *
         * The colour attachment always takes the swap chain's own colour format, and a
         * depth/stencil attachment matching the swap chain's own depth/stencil format is always
         * allocated regardless of @p depthFormat -- every cached sprite/primitive pipeline was
         * built against the swap chain's render pass, and Vulkan's render-pass-compatibility rule
         * requires a matching attachment signature to reuse a `LLGL::PipelineState` across render
         * passes. Requesting `DepthFormat::None` therefore still allocates the buffer; it only
         * changes whether the game's own depth-stencil state ends up testing or writing to it.
         *
         * @p multiSampleCount is real (LLGL-26 MSAA follow-up): when greater than 1, the colour
         * attachment becomes an anonymous, LLGL-owned multisampled buffer, resolved automatically
         * into the returned renderer's own single-sample colour texture (what `SpriteBatch`
         * sampling/`GetData()` read) at the end of every render pass this renderer replays. The
         * REAL, device-clamped sample count actually applied (LLGL "silently reduces" an
         * unsupported request rather than failing) is reported back through the returned renderer's
         * own `GetMultiSampleCount()`, matching `RenderTarget2D.MultiSampleCount`'s own FNA-parity
         * semantics -- mirrors the back buffer's own already-established, module-dependent MSAA
         * reality (`LLGL-23`): whether a request actually widens the sample count at all depends on
         * which renderer module is loaded.
         *
         * @p mipMap is real too (LLGL-26 mip-mapped render target follow-up): the colour texture is
         * allocated with a full mip chain (`RenderTarget2D.LevelCount`'s own `CalculateMipLevels`
         * formula, computed identically here), the render-target ATTACHMENT itself binds only level
         * 0 (`LLGL::AttachmentDescriptor::mipLevel` defaults to 0), and `RecordAndSubmitFrame()`/
         * `CaptureBackbuffer()` call `LLGL::CommandBuffer::GenerateMips()` on the colour texture
         * right after every render pass this target appears in, regenerating levels 1.. from
         * whatever level 0 just had rendered into it -- the LLGL equivalent of the Vulkan renderer's
         * own `vkCmdBlitImage` cascade (`VulkanTargetPassEXT::MaybeGenerateMips`), and of EasyGL's
         * `glGenerateMipmap`-on-unbind.
         *
         * @param w                Width in pixels.
         * @param h                Height in pixels.
         * @param depthFormat      Raw `DepthFormat` ordinal; only used to decide whether a real
         *                         depth buffer is reported through `HasRealDepthBuffer`.
         * @param preserveContents Ignored in this first cut -- see this renderer's render-pass
         *                         grouping note on `FrameCommandBucket`.
         * @param mipMap           Whether to allocate a full mip chain and regenerate it after
         *                         every render pass. See this method's own doc comment above.
         * @param multiSampleCount Requested MSAA sample count; 0/1 means none. See this method's
         *                         own doc comment above for how the applied count is reported back.
         * @return The new render target renderer.
         * @throws std::runtime_error If @p w or @p h is not positive, or GPU resource creation
         *         fails.
         */
        std::unique_ptr<IRenderTargetRenderer> CreateRenderTarget2D(
            int w, int h, int depthFormat, bool preserveContents = false, bool mipMap = false,
            int multiSampleCount = 0) override;

        /**
         * @brief Creates an off-screen `RenderTargetCube`.
         *
         * Six `LLGL::RenderTarget`s are built once here, each attaching a different `arrayLayer`
         * of ONE shared `LLGL::TextureType::TextureCube` colour texture, alongside ONE shared
         * depth/stencil texture -- matching FNA's own `RenderTargetCube`, which allocates exactly
         * one depth/stencil buffer for the whole cube (see `LlglRenderTargetCubeRenderer`'s own doc
         * comment). Mip-mapping and multisampling are ignored in this first cut, matching
         * `CreateRenderTarget2D`'s own identical scope; `preserveContents` is ignored too, for the
         * same render-pass-grouping reason `CreateRenderTarget2D` ignores it.
         *
         * @param size             Width/height of each face in pixels.
         * @param depthFormat      Raw `DepthFormat` ordinal; only used to decide whether a real
         *                         depth buffer is reported through `HasRealDepthBuffer`.
         * @param preserveContents Ignored in this first cut.
         * @param mipMap           Ignored; not yet implemented.
         * @param multiSampleCount Ignored; not yet implemented.
         * @return The new render target cube renderer.
         * @throws std::runtime_error If @p size is not positive, or GPU resource creation fails.
         */
        std::unique_ptr<IRenderTargetCubeRenderer> CreateRenderTargetCube(
            int size, int depthFormat, bool preserveContents = false, bool mipMap = false,
            int multiSampleCount = 0) override;

        /**
         * @brief Activates a `RenderTarget2D` (or restores the back buffer).
         *
         * @param rt Target to draw into, or null to restore the back buffer.
         */
        void SetRenderTarget2D(IRenderTargetRenderer* rt) override;

        /**
         * @brief Binds a set of render targets.
         *
         * Accepts the "restore the back buffer" request, exactly one `RenderTarget2D` slot,
         * exactly one `RenderTargetCube` face, or 2-4 `RenderTarget2D` slots (a real MRT bind --
         * see `SetMultipleRenderTargetsEXT()`/`LlglMRTBinding`). Mixing a `RenderTargetCube` face
         * into a multi-target set is not implemented yet and fails loudly by name.
         *
         * @param renderTargets Ordered target descriptors, or null.
         * @param count         Number of descriptors; 0 restores the back buffer.
         * @throws std::runtime_error If a multi-target set includes a `RenderTargetCube` face.
         */
        void SetRenderTargets(const RenderTargetBindingDescriptor* renderTargets, int count) override;

        /**
         * @brief Creates a custom `ShaderEffect` renderer.
         *
         * Compiling immediately if both @p vertSrc and @p fragSrc are non-empty, matching the
         * shared `ShaderEffect` constructor's own convention (see `CreateEffectRenderer`'s use in
         * `ShaderEffect.cpp`).
         *
         * @param vertSrc GLSL vertex shader source.
         * @param fragSrc GLSL fragment shader source.
         * @return The new effect renderer; never null (check `IsEffectValid()` for compile success).
         */
        std::unique_ptr<IEffectRenderer> CreateEffectRenderer(const std::string& vertSrc,
                                                             const std::string& fragSrc) override;

        /**
         * @brief Applies a `BlendState`.
         *
         * @param colorSrcBlend  Raw `Blend` ordinal for the colour source factor.
         * @param alphaSrcBlend  Raw `Blend` ordinal for the alpha source factor.
         * @param colorDstBlend  Raw `Blend` ordinal for the colour destination factor.
         * @param alphaDstBlend  Raw `Blend` ordinal for the alpha destination factor.
         * @param colorBlendFunc Raw `BlendFunction` ordinal for colour.
         * @param alphaBlendFunc Raw `BlendFunction` ordinal for alpha.
         * @param writeState     Colour write masks and the coverage sample mask.
         */
        void ApplyBlendState(int colorSrcBlend, int alphaSrcBlend,
                             int colorDstBlend, int alphaDstBlend,
                             int colorBlendFunc, int alphaBlendFunc,
                             const BlendWriteState& writeState) override;

        /**
         * @brief Sets the constant blend colour used by the BlendFactor blend modes.
         *
         * @param r,g,b,a Blend factor components in the range 0..1.
         */
        void SetBlendFactor(float r, float g, float b, float a) override;

        /**
         * @brief Applies a `SamplerState` to a texture slot.
         *
         * LLGL-49: tracked independently per slot (0..`kTrackedSamplerSlotCount`-1) -- the only
         * slots this renderer's stock effects ever consume (see `samplerFilter_`'s own doc comment
         * for the exact slot-to-texture-unit mapping). A slot outside that range is a no-op: no
         * shader binding on this renderer reaches it.
         *
         * @param slot          Texture slot index.
         * @param filter        Raw `TextureFilter` ordinal.
         * @param addressU      Raw `TextureAddressMode` ordinal for U.
         * @param addressV      Raw `TextureAddressMode` ordinal for V.
         * @param maxAnisotropy Maximum anisotropy, 1..16.
         */
        void ApplySamplerState(int slot, int filter, int addressU, int addressV,
                               int maxAnisotropy) override;

        /**
         * @brief Applies a `RasterizerState`.
         *
         * Only the scissor-enable bit affects the 2D pipeline; cull mode and fill mode belong to
         * the 3D pipeline and are recorded for it.
         *
         * @param cullMode            Raw `CullMode` ordinal.
         * @param fillMode            Raw `FillMode` ordinal.
         * @param scissorTestEnable   Whether the scissor test is enabled.
         * @param depthBias           Constant depth bias.
         * @param slopeScaleDepthBias Slope-scaled depth bias.
         */
        void ApplyRasterizerState(int cullMode, int fillMode, bool scissorTestEnable,
                                  float depthBias, float slopeScaleDepthBias) override;

        /**
         * @brief Sets the scissor rectangle in logical coordinates.
         *
         * @param x Left edge.
         * @param y Top edge.
         * @param w Width.
         * @param h Height.
         */
        void SetScissorRect(int x, int y, int w, int h) override;

        /**
         * @brief Sets the viewport rectangle in logical coordinates.
         *
         * @param x        Left edge.
         * @param y        Top edge.
         * @param w        Width.
         * @param h        Height.
         * @param minDepth Minimum depth.
         * @param maxDepth Maximum depth.
         */
        void SetViewport(int x, int y, int w, int h, float minDepth, float maxDepth) override;

        /**
         * @brief Applies a `DepthStencilState`.
         *
         * Depth test, depth write and the depth comparison reach the 3D pipeline. The stencil
         * fields are recorded only: the swap chain has a real stencil buffer and `ClearStencil`
         * works, but no draw path consumes a stencil test yet.
         *
         * @param depthEnable          Whether depth testing is enabled.
         * @param depthWriteEnable     Whether depth writes are enabled.
         * @param depthFunc            Raw `CompareFunction` ordinal for the depth test.
         * @param stencilEnable        Whether stencil testing was requested.
         * @param stencilFunc          Raw `CompareFunction` ordinal for the stencil test.
         * @param stencilPass          Raw `StencilOperation` ordinal applied when the test passes.
         * @param stencilFail          Raw `StencilOperation` ordinal applied when the test fails.
         * @param stencilDepthFail     Raw `StencilOperation` ordinal for a depth failure.
         * @param stencilMask          Stencil read mask.
         * @param stencilWriteMask     Stencil write mask.
         * @param referenceStencil     Stencil reference value.
         * @param twoSidedStencilMode  Whether the counter-clockwise face has its own stencil state.
         * @param ccwStencilFunc       Counter-clockwise `CompareFunction` ordinal.
         * @param ccwStencilPass       Counter-clockwise pass operation ordinal.
         * @param ccwStencilFail       Counter-clockwise fail operation ordinal.
         * @param ccwStencilDepthFail  Counter-clockwise depth-fail operation ordinal.
         */
        void ApplyDepthStencilState(bool depthEnable, bool depthWriteEnable, int depthFunc,
                                    bool stencilEnable, int stencilFunc,
                                    int stencilPass, int stencilFail, int stencilDepthFail,
                                    int stencilMask, int stencilWriteMask, int referenceStencil,
                                    bool twoSidedStencilMode,
                                    int ccwStencilFunc, int ccwStencilPass,
                                    int ccwStencilFail, int ccwStencilDepthFail) override;

        /**
         * @brief Records `GraphicsDevice.ReferenceStencil`, independent of a full
         * `DepthStencilState` re-application (LLGL-53) -- applied per-draw via
         * `LLGL::CommandBuffer::SetStencilReference()` at replay time, since the pipeline's own
         * `stencil.referenceDynamic` is always true.
         *
         * @param value New reference stencil value.
         */
        void SetReferenceStencil(int value) override;

        /**
         * @brief Records whether depth testing is enabled.
         *
         * @param enabled Requested depth test state.
         */
        void SetDepthTestEnabled(bool enabled) override;

        /**
         * @brief Records whether blending is enabled.
         *
         * @param enabled Requested blend state.
         */
        void SetBlendEnabled(bool enabled) override;

        /**
         * @brief Records whether depth writes are enabled.
         *
         * @param enabled Requested depth write state.
         */
        void SetDepthWriteEnabled(bool enabled) override;

        /**
         * @brief Draws colour-only primitives from a vertex buffer.
         *
         * @param vb             Vertex buffer to draw from.
         * @param world          World matrix.
         * @param view           View matrix.
         * @param projection     Projection matrix.
         * @param primitive      Primitive topology.
         * @param primitiveCount Number of primitives.
         * @throws std::runtime_error If the buffer is empty, belongs to another renderer, carries a
         *         vertex layout this path cannot express, or holds fewer vertices than the draw needs.
         */
        void DrawColoredPrimitives(const IVertexBufferRenderer& vb,
                                   const Matrix& world, const Matrix& view, const Matrix& projection,
                                   PrimitiveType primitive, int primitiveCount) override;

        /**
         * @brief Draws indexed colour-only primitives.
         *
         * @param vb             Vertex buffer to draw from.
         * @param ib             Index buffer to draw with.
         * @param world          World matrix.
         * @param view           View matrix.
         * @param projection     Projection matrix.
         * @param primitive      Primitive topology.
         * @param primitiveCount Number of primitives.
         * @throws std::runtime_error If either buffer is empty, belongs to another renderer, carries
         *         a vertex layout this path cannot express, or holds too few elements.
         */
        void DrawIndexedColoredPrimitives(const IVertexBufferRenderer& vb, const IIndexBufferRenderer& ib,
                                          const Matrix& world, const Matrix& view, const Matrix& projection,
                                          PrimitiveType primitive, int primitiveCount) override;

        /**
         * @brief Effect-aware draw; honours the vertex-start offset the shared layer supplies.
         *
         * @param vb             Vertex buffer to draw from.
         * @param world          World matrix.
         * @param view           View matrix.
         * @param projection     Projection matrix.
         * @param primitive      Primitive topology.
         * @param primitiveCount Number of primitives.
         * @param params         Per-draw effect parameters.
         * @throws std::runtime_error If @p params asks for anything beyond vertex colours -- a
         *         texture, lighting, fog, skinning, a custom effect or instancing.
         */
        void DrawPrimitivesEx(const IVertexBufferRenderer& vb,
                              const Matrix& world, const Matrix& view, const Matrix& projection,
                              PrimitiveType primitive, int primitiveCount,
                              const GpuDrawParams& params) override;

        /**
         * @brief Indexed effect-aware draw; honours the start index and base vertex.
         *
         * @param vb             Vertex buffer to draw from.
         * @param ib             Index buffer to draw with.
         * @param world          World matrix.
         * @param view           View matrix.
         * @param projection     Projection matrix.
         * @param primitive      Primitive topology.
         * @param primitiveCount Number of primitives.
         * @param params         Per-draw effect parameters.
         * @throws std::runtime_error If @p params asks for anything beyond vertex colours.
         */
        void DrawIndexedPrimitivesEx(const IVertexBufferRenderer& vb, const IIndexBufferRenderer& ib,
                                     const Matrix& world, const Matrix& view, const Matrix& projection,
                                     PrimitiveType primitive, int primitiveCount,
                                     const GpuDrawParams& params) override;

        /**
         * @brief Reports whether a graphics capability is supported by this renderer today.
         *
         * @param capability Capability to query.
         * @return True only for capabilities the current 2D implementation genuinely provides.
         */
        [[nodiscard]] bool SupportsCapability(CNA::GraphicsCapability capability) const override;

        /** @brief Reports the validated LLGL OpenGL stencil boundary to legacy capability users. */
        [[nodiscard]] bool SupportsStencilBuffer() const override { return false; }

        /** @brief Returns the largest single-axis texture dimension the device reports. */
        [[nodiscard]] int GetMaxTextureDimension() const override;

        /**
         * @brief Returns whether the loaded renderer module can draw in wireframe fill mode.
         *
         * Module-dependent, and LLGL exposes no capability flag for it: the OpenGL module draws
         * real wireframe edges, the Vulkan module draws nothing at all. Requesting it on a module
         * that cannot do it fails loudly rather than producing an empty frame.
         *
         * @return True when `FillMode::WireFrame` is honoured.
         */
        [[nodiscard]] bool SupportsWireFrameEXT() const;

        /** @brief Returns the renderer module this instance actually loaded. */
        [[nodiscard]] Detail::RendererModule GetRendererModule() const { return module_; }

        /** @brief Returns the LLGL renderer name reported by the loaded module. */
        [[nodiscard]] const char* GetRendererNameEXT() const;

        /**
         * @brief Takes over releasing a buffer whose owning resource is being destroyed.
         *
         * This renderer records a whole frame and submits it at `Present()`, and a 3D draw refers to
         * the caller's own vertex and index buffers rather than copying their contents. A game may
         * legitimately destroy a `VertexBuffer` in the same frame it drew with -- XNA allows it --
         * and releasing the underlying GPU buffer there and then would leave the recorded frame
         * pointing at freed memory. Deferring the release to just after the frame is submitted
         * keeps the pointer valid for exactly as long as it is needed, without copying geometry
         * that is already resident on the GPU.
         *
         * @param buffer Buffer to release once the frame referencing it has been submitted.
         */
        void ScheduleBufferReleaseEXT(LLGL::Buffer* buffer);

        /**
         * @brief Defers releasing a `RenderTarget2D`'s LLGL objects, same reasoning as
         * `ScheduleBufferReleaseEXT`: `LlglRenderTargetRenderer`'s destructor calls this instead of
         * releasing immediately, because a `RenderTarget2D` that goes out of scope before
         * `Present()` is a perfectly normal pattern (create it, draw into it, sample it, let it
         * die, all within one `Draw()`) and `frameCommands_`/`FrameCommandBucket` may still
         * reference the render target by raw pointer.
         *
         * @param renderTarget           The framebuffer object, or null.
         * @param colorTexture           The colour attachment, or null.
         * @param depthTexture           The depth/stencil attachment, or null (anonymous
         *                               attachments -- the only kind this renderer creates today --
         *                               have none of their own; LLGL releases that buffer as part
         *                               of releasing @p renderTarget itself).
         * @param spriteProjectionBuffer This target's own sprite projection buffer, or null.
         */
        void ScheduleRenderTargetReleaseEXT(LLGL::RenderTarget* renderTarget,
                                            LLGL::Texture* colorTexture,
                                            LLGL::Texture* depthTexture,
                                            LLGL::Buffer* spriteProjectionBuffer);

        /**
         * @brief Defers releasing a plain `Texture2D`/`TextureCube`/`Texture3D`'s own LLGL texture,
         * same reasoning as `ScheduleBufferReleaseEXT`: `LlglTextureRenderer`/`LlglTextureCubeRenderer`/
         * `LlglTexture3DRenderer`'s destructors call this instead of releasing immediately, because a
         * texture that goes out of scope before `Present()`/`GetBackBufferData()` flushes the frame
         * is a perfectly normal XNA pattern and `FrameCommand::texture`/`envMapTexture`/
         * `pbrNormalTexture` (etc.) may still reference it by raw pointer.
         *
         * @param texture Texture to release once the frame referencing it has been submitted.
         */
        void ScheduleTextureReleaseEXT(LLGL::Texture* texture);

        /**
         * @brief Defers releasing an occlusion query's `LLGL::QueryHeap`, same reasoning as
         * `ScheduleRenderTargetReleaseEXT`.
         *
         * @param queryHeap The query heap to release once the frame that may reference it (a
         *                  queued `QueryBegin`/`QueryEnd`) has been submitted, or null.
         */
        void ScheduleQueryHeapReleaseEXT(LLGL::QueryHeap* queryHeap);

        /**
         * @brief Appends a `BeginQuery` to the current frame.
         *
         * Called by `LlglOcclusionQueryRenderer::Begin()`; not part of the shared renderer interface.
         *
         * @param queryHeap Query heap to begin; must not be null.
         */
        void QueueQueryBeginEXT(LLGL::QueryHeap* queryHeap);

        /**
         * @brief Appends an `EndQuery` to the current frame.
         *
         * Called by `LlglOcclusionQueryRenderer::End()`; not part of the shared renderer interface.
         *
         * @param queryHeap Query heap to end; must not be null.
         */
        void QueueQueryEndEXT(LLGL::QueryHeap* queryHeap);

        /**
         * @brief Defers releasing a `ShaderEffect`'s shader modules and cached pipelines, same
         * reasoning as `ScheduleRenderTargetReleaseEXT`/`ScheduleQueryHeapReleaseEXT`: a
         * `ShaderEffect` that goes out of scope before `Present()` is a perfectly ordinary
         * pattern, and `frameCommands_` may still reference one of its cached pipelines by raw
         * pointer.
         *
         * @param vertexShader   The effect's vertex shader, or null.
         * @param fragmentShader The effect's fragment shader, or null.
         * @param pipelines      Every pipeline this effect's own cache built; may be empty.
         */
        void ScheduleEffectResourceReleaseEXT(LLGL::Shader* vertexShader, LLGL::Shader* fragmentShader,
                                              const std::map<std::uint64_t, LLGL::PipelineState*>& pipelines);

        /**
         * @brief Returns (creating the shared layout on first use) the pipeline a custom
         * `ShaderEffect` draws through for the current blend/scissor state.
         *
         * Called by `LlglEffectRenderer::AcquirePipeline()`; not part of the shared renderer
         * interface. Every custom effect shares the same `LLGL::PipelineLayout`
         * (`customEffectLayout_`) -- only the shader modules and cached `LLGL::PipelineState`
         * differ per effect -- so this only builds the layout once, on the first `ShaderEffect`
         * any game constructs.
         *
         * @return The shared custom-effect pipeline layout; never null.
         */
        [[nodiscard]] LLGL::PipelineLayout* AcquireCustomEffectLayoutEXT();

        /** @brief Returns the render system, for `LlglEffectRenderer` to create/release its own
         *  shader modules and pipelines with. */
        [[nodiscard]] LLGL::RenderSystem* GetRenderSystemEXT() const { return renderer_.get(); }

        /** @brief Returns the render pass a pipeline for the CURRENTLY bound target should be
         *  built against -- the bound target's own render pass if one is bound (a plain
         *  `RenderTarget2D`, a `RenderTargetCube` face, or an MRT bind), the swap chain's own
         *  otherwise. A single-colour-attachment target's render pass and the swap chain's are
         *  compatible (see `plan_llgl.md` LLGL-26), so either was interchangeable here before
         *  MRT existed; a multi-attachment bind's render pass genuinely differs (a different
         *  attachment COUNT, not just a different concrete object) and needs its own. Null before
         *  the swap chain exists. */
        [[nodiscard]] const LLGL::RenderPass* GetPrimaryRenderPassEXT() const
        {
            if (currentRenderTargetRenderer_ != nullptr)
                return currentRenderTargetRenderer_->GetLlglRenderTarget()->GetRenderPass();
            return swapChain_ != nullptr ? swapChain_->GetRenderPass() : nullptr;
        }

        /** @brief Returns how many simultaneous colour attachments the currently bound target has
         *  (1 when nothing is bound, or a plain `RenderTarget2D`/cube face is; >1 during an MRT
         *  bind). Pipeline creation folds this into its cache key and its blend-target count. */
        [[nodiscard]] int GetActiveColorAttachmentCountEXT() const
        {
            return currentRenderTargetRenderer_ != nullptr
                ? currentRenderTargetRenderer_->GetColorAttachmentCount() : 1;
        }

        /** @brief Returns the real sample count a pipeline for the CURRENTLY bound target should be
         *  built with (1 = no MSAA) -- the bound target's own (a `RenderTarget2D` with a genuine
         *  `MultiSampleCount`), or the swap chain's own otherwise. Mirrors
         *  `GetPrimaryRenderPassEXT()`'s own "bound target wins" rule; the two must never disagree,
         *  since a pipeline's `multiSampleEnabled` and its `renderPass`'s sample count both have to
         *  match whatever is actually bound when the draw replays, not the swap chain's, or a
         *  software Vulkan module can rasterize single-sample into a multisampled attachment without
         *  erroring (no antialiasing, not a crash -- measured, not assumed). */
        [[nodiscard]] int GetPrimarySampleCountEXT() const
        {
            if (currentRenderTargetRenderer_ != nullptr)
                return currentRenderTargetRenderer_->GetSampleCount();
            return swapChain_ != nullptr ? static_cast<int>(swapChain_->GetSamples()) : 1;
        }

        /** @brief Returns the CURRENTLY bound target's colour texture when it has a real mip chain
         *  to regenerate, or null otherwise (nothing bound, a single-level target, a cube face, or
         *  an MRT bind). Captured onto every `FrameCommand` that names a target (`QueueClear`/
         *  `QueueSpriteEXT`/`QueuePrimitives`/`QueueQueryBeginEXT`/`QueueQueryEndEXT`) at QUEUE
         *  time, exactly like `command.target`/`projectionBuffer` already are -- the target may be
         *  destroyed (and this accessor's own answer lost) before the frame that references it is
         *  replayed. See `LlglBoundRenderTarget::GetMipRegenColorTextureEXT()`'s own doc comment. */
        [[nodiscard]] LLGL::Texture* GetActiveMipRegenColorTextureEXT() const
        {
            return currentRenderTargetRenderer_ != nullptr
                ? currentRenderTargetRenderer_->GetMipRegenColorTextureEXT() : nullptr;
        }

        /** @brief Returns the loaded module's rendering capabilities, for `LlglEffectRenderer` to
         *  decide whether it can hand its GLSL source to LLGL directly or must compile it to
         *  SPIR-V first. */
        [[nodiscard]] const LLGL::RenderingCapabilities& GetRenderingCapsEXT() const
        {
            return renderer_->GetRenderingCaps();
        }

        /**
         * @brief Fills a graphics pipeline descriptor's rasterizer/blend/depth fields from the
         * CURRENT device state, identically to what the stock sprite pipeline
         * (`AcquireSpritePipeline`) itself is built from.
         *
         * Shared rather than duplicated so a custom effect's pipeline responds to
         * `ApplyBlendState`/`SetScissorRect` exactly the same way the stock one does.
         *
         * @param pipelineDesc  Descriptor to fill; `vertexShader`/`fragmentShader`/`pipelineLayout`/
         *                      `renderPass`/`primitiveTopology` are the caller's own responsibility.
         * @param scissorEnabled Whether the current scissor rectangle applies to this draw.
         * @param colorAttachmentCount Number of `blend.targets[]` entries to fill. Every slot
         *                      shares the SAME blend factors/functions (XNA's `BlendState` has
         *                      only one set of those, not one per slot) but its OWN colour write
         *                      mask (`colorWriteChannels_[slot]`, `BlendState.ColorWriteChannels1..3`
         *                      -- real now, LLGL-21 follow-up). Defaults to 1, the pre-MRT
         *                      behaviour, which only ever reads slot 0.
         */
        void FillCurrentBlendAndRasterStateEXT(LLGL::GraphicsPipelineDescriptor& pipelineDesc,
                                               bool scissorEnabled, int colorAttachmentCount = 1) const;

        /** @brief Marks @p effect as the one `SpriteBatch` draws through. Called by
         *  `LlglEffectRenderer::Bind()`; not part of the shared renderer interface. */
        void SetCurrentCustomEffectEXT(LlglEffectRenderer* effect) { currentCustomEffect_ = effect; }

        /** @brief Clears @p effect as the active custom effect, but only if it still is one --
         *  guards against a stale `Unbind()`/destructor call after a DIFFERENT effect was already
         *  bound. Called by `LlglEffectRenderer::Unbind()`/its destructor. */
        void ClearCurrentCustomEffectEXT(LlglEffectRenderer* effect)
        {
            if (currentCustomEffect_ == effect)
                currentCustomEffect_ = nullptr;
        }

        /**
         * @brief Submits any queued-but-not-yet-submitted frame commands and waits for them to
         * finish, without presenting.
         *
         * `LlglRenderTargetRenderer::GetData()` calls this first: unlike a plain `Texture2D` (whose
         * content arrives through an immediate `WriteTexture`), a `RenderTarget2D`'s content comes
         * only from draws recorded into `frameCommands_`, which otherwise stay unsubmitted until
         * `Present()`/`ReadBackbuffer()` -- reading the colour attachment before that point sees
         * whatever the GPU allocator happened to leave there, not what was actually drawn. No-op
         * when there is nothing queued.
         */
        void FlushPendingFrameEXT();

        /**
         * @brief Appends one sprite's geometry to the current frame.
         *
         * Called by `LlglSpriteBatchRenderer`; not part of the shared renderer interface.
         *
         * @param texture     Texture to sample -- either a plain `Texture2D` or a `RenderTarget2D`
         *                    sampled back as a texture; both resolve to a real `LLGL::Texture`.
         * @param destination Destination rectangle in logical pixels.
         * @param source      Source region in texels.
         * @param color       Tint multiplied with the sampled texel.
         * @param rotation    Rotation in radians about @p origin.
         * @param origin      Rotation origin in source texels.
         * @param effects     Horizontal/vertical flip flags.
         * @param transform   SpriteBatch transform matrix.
         * @param filter      Raw `TextureFilter` ordinal for this draw.
         * @param addressU    Raw `TextureAddressMode` ordinal for U.
         * @param addressV    Raw `TextureAddressMode` ordinal for V.
         * @throws std::runtime_error If @p texture belongs to another renderer.
         */
        void QueueSpriteEXT(const ITextureRenderer& texture,
                            const Rectangle& destination,
                            const Rectangle& source,
                            const Color& color,
                            float rotation,
                            const Vector2& origin,
                            SpriteEffects effects,
                            const Matrix& transform,
                            int filter, int addressU, int addressV);

    private:
        /** @brief One recorded frame operation, replayed in submission order at Present(). */
        struct FrameCommand
        {
            enum class Kind { Clear, Sprite, Primitives, QueryBegin, QueryEnd };

            /** @brief Which operation this entry replays. */
            Kind             kind         = Kind::Clear;
            /** @brief The render target this command draws into, or null for the swap chain. */
            LLGL::RenderTarget* target    = nullptr;
            /** @brief This command's own target's colour texture, ONLY when that target has a
             *  real mip chain to regenerate (null otherwise) -- captured at queue time from
             *  `LlglRenderer::GetActiveMipRegenColorTextureEXT()`, exactly like `target`
             *  itself, since the target may be destroyed before this command's frame replays.
             *  `RecordAndSubmitFrame()`/`CaptureBackbuffer()` read it off any one command in a
             *  target's own `FrameCommandBucket` once that target's render pass ends. */
            LLGL::Texture*   mipRegenColorTexture = nullptr;
            long             clearFlags   = 0;
            float            clearColor[4] = {0.0f, 0.0f, 0.0f, 0.0f};
            float            clearDepth   = 1.0f;
            std::uint32_t    clearStencil = 0;
            /** @brief Sprite only: this target's own projection buffer, or null for the swap
             *  chain's (see `LlglRenderTargetRenderer::GetSpriteProjectionBuffer`). */
            LLGL::Buffer*    projectionBuffer = nullptr;
            LLGL::Texture*   texture      = nullptr;
            LLGL::Sampler*   sampler      = nullptr;
            /** @brief Primitives only, `DualTextureEffect`: the second texture/sampler pair. */
            LLGL::Texture*   texture2     = nullptr;
            LLGL::Sampler*   sampler2     = nullptr;
            /** @brief Primitives only, `EnvironmentMapEffect`: the cube map texture/sampler pair. */
            LLGL::Texture*   envMapTexture = nullptr;
            LLGL::Sampler*   envMapSampler = nullptr;
            LLGL::PipelineState* pipeline = nullptr;
            std::uint32_t    firstVertex  = 0;
            std::uint32_t    vertexCount  = 0;
            float            blendFactor[4] = {1.0f, 1.0f, 1.0f, 1.0f};
            bool             usesBlendFactor = false;
            /** @brief Primitives only: `GraphicsDevice.ReferenceStencil` at queue time -- the
             *  pipeline's own `stencil.referenceDynamic = true` (LLGL-53) means this is applied via
             *  `CommandBuffer::SetStencilReference()` at replay, the same per-command-capture
             *  reason `blendFactor` above exists (a standalone device property, independent of the
             *  DepthStencilState that built the cached pipeline object). */
            std::uint32_t    referenceStencilValue = 0;
            std::int32_t     scissor[4]   = {0, 0, 0, 0};
            bool             scissorEnabled = false;
            /** @brief The physical-pixel GPU viewport rectangle (x, y, width, height) this command
             *  must be drawn with -- filled by `CaptureFrameCommandViewportEXT()` for every kind,
             *  since the render-pass-wide `SetViewport()` at the top of a `FrameCommandBucket` is
             *  only a default that a narrowed Primitives command can leave behind for whatever
             *  replays after it. See that method's own doc comment for why only Primitives ever
             *  narrows this. */
            float            viewport[4]  = {0.0f, 0.0f, 0.0f, 0.0f};
            /** @brief LLGL-53: the GPU depth range (XNA `Viewport.MinDepth`/`MaxDepth`) this
             *  command's own viewport must be drawn with -- captured alongside `viewport[4]` by
             *  `CaptureFrameCommandViewportEXT()`, same restriction (only `Primitives` narrows away
             *  from the default `[0,1]`). */
            float            viewportMinDepth = 0.0f;
            float            viewportMaxDepth = 1.0f;

            /** @brief Primitives only: the caller's own vertex buffer, and index buffer if any. */
            LLGL::Buffer*    vertexBuffer = nullptr;
            LLGL::Buffer*    indexBuffer  = nullptr;
            /** @brief Primitives only: index into the frame's per-draw transform buffer pool.
             *  Unused when `envMapping` is set -- `envMapUniformIndex` names the buffer instead,
             *  since EnvironmentMapEffect's uniform block has a different shape and size. */
            std::uint32_t    transformIndex = 0;
            /** @brief Primitives only: true when this draw is `EnvironmentMapEffect`, in which
             *  case `envMapUniformIndex` (not `transformIndex`) names this draw's own uniform
             *  buffer, from a differently-sized pool (`envMapUniformBuffers_`). */
            bool             envMapping   = false;
            /** @brief Primitives only, when `envMapping`: index into the frame's per-draw
             *  EnvironmentMapEffect uniform buffer pool. */
            std::uint32_t    envMapUniformIndex = 0;
            /** @brief Primitives only: true when this draw is `SkinnedEffect`, in which case
             *  `skinnedUniformIndex`/`skinnedBoneIndex` (not `transformIndex`) name this draw's
             *  own uniform/bone buffers, from their own differently-sized pools. */
            bool             skinned      = false;
            /** @brief Primitives only, when `skinned`: index into the frame's per-draw
             *  SkinnedEffect parameter uniform buffer pool. */
            std::uint32_t    skinnedUniformIndex = 0;
            /** @brief Primitives only, when `skinned`: index into the frame's per-draw bone
             *  transform buffer pool (72 `mat4`s each). */
            std::uint32_t    skinnedBoneIndex = 0;
            /** @brief Primitives only: true when this draw is `PbrEffect`, in which case
             *  `pbrUniformIndex` (not `transformIndex`) names this draw's own uniform buffer, from
             *  its own differently-shaped pool. `texture`/`sampler` (set by the generic `textured`
             *  handling above) carry the base colour map; the 4 fields below carry PbrEffect's
             *  remaining optional maps, each resolved to a 1x1 default (white, or flat-normal for
             *  `pbrNormalTexture`) when the game left it null -- see `EnsureDefaultPbrTexturesEXT()`. */
            bool             pbr          = false;
            /** @brief Primitives only, when `pbr`: index into the frame's per-draw PbrEffect
             *  parameter uniform buffer pool. */
            std::uint32_t    pbrUniformIndex = 0;
            /** @brief Primitives only, when `pbr`: tangent-space normal map, or the 1x1 default
             *  flat-normal texture when `PbrEffect::NormalMap` was null. */
            LLGL::Texture*   pbrNormalTexture = nullptr;
            /** @brief Primitives only, when `pbr`: this draw's own slot-1 sampler (LLGL-49) --
             *  see `pbrMetallicRoughnessSampler`'s own doc comment for why each PBR map gets its
             *  own sampler now instead of all five sharing `sampler`. */
            LLGL::Sampler*   pbrNormalSampler = nullptr;
            /** @brief Primitives only, when `pbr`: glTF-packed (G=roughness, B=metallic)
             *  metallic-roughness map, or the 1x1 default white texture when null. */
            LLGL::Texture*   pbrMetallicRoughnessTexture = nullptr;
            /** @brief Primitives only, when `pbr`: this draw's own slot-2 sampler (LLGL-49).
             *  PbrEffect's 5 maps occupy `GraphicsDevice.SamplerStates[0..4]` (matching the
             *  Vulkan renderer's own `PbrSlotSamplersRawEXT()` convention), so each needs its own
             *  captured sampler rather than all reusing slot 0's -- previously every one of these
             *  4 fields (see the 3 siblings below) was absent and `ReplayFrameCommandsList` bound
             *  `sampler` (slot 0) at every one of the 5 texture units instead. */
            LLGL::Sampler*   pbrMetallicRoughnessSampler = nullptr;
            /** @brief Primitives only, when `pbr`: emissive map, or the 1x1 default white texture
             *  when null. */
            LLGL::Texture*   pbrEmissiveTexture = nullptr;
            /** @brief Primitives only, when `pbr`: this draw's own slot-3 sampler (LLGL-49). */
            LLGL::Sampler*   pbrEmissiveSampler = nullptr;
            /** @brief Primitives only, when `pbr`: occlusion map (R channel), or the 1x1 default
             *  white texture when null. */
            LLGL::Texture*   pbrOcclusionTexture = nullptr;
            /** @brief Primitives only, when `pbr`: this draw's own slot-4 sampler (LLGL-49). */
            LLGL::Sampler*   pbrOcclusionSampler = nullptr;
            /** @brief Primitives only: first index of an indexed draw; ignored otherwise. */
            std::uint32_t    firstIndex   = 0;
            /** @brief Primitives only: value added to each index before vertex fetch. */
            std::int32_t     baseVertex   = 0;

            /** @brief QueryBegin/QueryEnd only: the occlusion query heap to begin/end. */
            LLGL::QueryHeap* queryHeap    = nullptr;

            /** @brief Sprite only: true when a custom `ShaderEffect` was bound at queue time, in
             *  which case `customEffectUniformIndex` (not `projectionBuffer`) names this draw's
             *  own uniform buffer. */
            bool             hasCustomEffectUniform = false;
            /** @brief Sprite only, when `hasCustomEffectUniform`: index into the frame's per-draw
             *  custom-effect uniform buffer pool. */
            std::uint32_t    customEffectUniformIndex = 0;
        };

        /** @brief The letterboxed destination rectangle of the logical canvas, in window pixels. */
        struct PresentationRect
        {
            float x      = 0.0f;
            float y      = 0.0f;
            float width  = 0.0f;
            float height = 0.0f;
            float logicalWidth  = 0.0f;
            float logicalHeight = 0.0f;
        };

        void CreateSpritePipelineResources();
        void CreatePrimitivePipelineResources();
        LLGL::Shader* AcquirePrimitiveVertexShader(const std::vector<LLGL::VertexAttribute>& attributes,
                                                   bool textured, bool lit);
        /// EnvironmentMapEffect's own vertex shader (env_map3d.vert.glsl): always textured and
        /// lit, never variant-selected by (textured, lit) like AcquirePrimitiveVertexShader --
        /// only the vertex layout (attribute trimming/caching) varies.
        LLGL::Shader* AcquirePrimitiveEnvMapVertexShader(const std::vector<LLGL::VertexAttribute>& attributes);
        /// SkinnedEffect's own vertex shader (skinned3d.vert.glsl): always textured and lit, and
        /// needs the two extra bone attributes (`aBoneWeights`/`aBoneIndices`) present -- never
        /// variant-selected like AcquirePrimitiveVertexShader, only the vertex layout varies.
        LLGL::Shader* AcquirePrimitiveSkinnedVertexShader(const std::vector<LLGL::VertexAttribute>& attributes);
        /// PbrEffect's own vertex shader (pbr3d.vert.glsl): needs a `Tangent` vertex element (the
        /// TBN basis the fragment stage builds for normal mapping) alongside texture coordinates
        /// and normals -- never variant-selected like AcquirePrimitiveVertexShader, only the
        /// vertex layout varies.
        LLGL::Shader* AcquirePrimitivePbrVertexShader(const std::vector<LLGL::VertexAttribute>& attributes);
        /// SkinnedPbrEffect's own vertex shader (pbr3d_skinned.vert.glsl): PbrEffect's TBN basis
        /// plus SkinnedEffect's own weightsPerVertex-gated bone blend, needing all of `Tangent`,
        /// `BlendWeight` and `BlendIndices` present. The FRAGMENT shader is `primitivePbrFragmentShader_`
        /// itself, reused verbatim -- skinning is a vertex-stage-only concern, and `BoneBlock` is
        /// deliberately placed at a binding number AFTER every PBR texture/sampler pair rather than
        /// shifting them, precisely so the compiled fragment shader binary stays valid unchanged.
        LLGL::Shader* AcquirePrimitivePbrSkinnedVertexShader(const std::vector<LLGL::VertexAttribute>& attributes);
        LLGL::PipelineState* AcquirePrimitivePipeline(const LlglVertexBufferRenderer& vertexBuffer,
                                                      PrimitiveType primitive, bool scissorEnabled,
                                                      bool textured, bool lit, bool dualTexture,
                                                      bool envMapping, bool skinned, bool pbr);
        void QueuePrimitives(const LlglVertexBufferRenderer& vertexBuffer,
                             const LlglIndexBufferRenderer* indexBuffer,
                             const Matrix& world, const Matrix& view, const Matrix& projection,
                             PrimitiveType primitive, int primitiveCount,
                             int vertexStart, int startIndex, int baseVertex,
                             const GpuDrawParams* params);
        void RejectUnsupportedDrawParams(const GpuDrawParams& params) const;
        /// Fills one draw's 400-byte uniform block (100 floats -- see
        /// shaders/effect3d_common.glsl.inc for the byte layout). The unlit shaders only read the
        /// first 32; the lit ones read all 100.
        static void FillEffectUniforms(float (&uniforms)[100], const float matrix[16],
                                       const GpuDrawParams* params);
        /// Fills one EnvironmentMapEffect draw's own 336-byte uniform block (84 floats -- see
        /// shaders/env_map3d.vert.glsl's EnvMapParams for the byte layout). Unlike
        /// FillEffectUniforms, `params` is never null here (only called when `envMapping` is set).
        static void FillEnvMapUniforms(float (&uniforms)[84], const float matrix[16],
                                       const GpuDrawParams& params);
        /// Fills one SkinnedEffect draw's own 368-byte parameter uniform block (92 floats -- see
        /// shaders/skinned3d.vert.glsl's SkinnedParams for the byte layout). `params` is never
        /// null here (only called when `skinned` is set).
        static void FillSkinnedUniforms(float (&uniforms)[92], const float matrix[16],
                                        const GpuDrawParams& params);
        /// Fills one SkinnedEffect draw's own 72-`mat4` (1152-float) bone transform buffer, copying
        /// only `params.boneCount` entries (the rest stay zeroed, matching a disabled/unused bone
        /// slot the shader never indexes into since `boneCount` bounds every real index).
        static void FillSkinnedBoneData(float (&bones)[72 * 16], const GpuDrawParams& params);
        /// Fills one PbrEffect draw's own 336-byte uniform block (84 floats -- see
        /// shaders/pbr3d.vert.glsl's PbrParams for the byte layout). `params` is never null here
        /// (only called when `pbr` is set).
        static void FillPbrUniforms(float (&uniforms)[84], const float matrix[16],
                                    const GpuDrawParams& params);
        /// Creates the 1x1 default white texture and 1x1 default flat-normal texture
        /// (RGBA (128,128,255,255), decoding to tangent-space (0,0,1)) PbrEffect draws sample
        /// whenever the game left `MetallicRoughnessMap`/`EmissiveMap`/`OcclusionMap`/`NormalMap`
        /// null -- real `PbrEffect::FillGpuDrawParams()` can legitimately leave all 4 null (only
        /// `Texture`/`MetallicFactor`/`RoughnessFactor` are required), and sampling a null texture
        /// is not an option, unlike this renderer's "throw if the REQUIRED texture is missing"
        /// convention for EnvironmentMapEffect/SkinnedEffect/DualTextureEffect. A no-op once both
        /// textures already exist.
        void EnsureDefaultPbrTexturesEXT();
        /**
         * @brief One contiguous render pass worth of frame commands, all sharing the same
         * destination (null means the swap chain).
         *
         * LLGL-45: replayed in true public call order, not grouped by target identity -- a target
         * revisited later in the same frame (after another target's or the swap chain's own
         * commands appeared in between) becomes its OWN new segment in its own original position,
         * rather than being merged into whichever segment first used that target. Two ADJACENT
         * commands sharing the same target (no other target's command in between) still land in
         * one segment, exactly as before, since nothing about their relative order changes either
         * way. Every segment's own `BeginRenderPass()` uses `AcquireLoadRenderPassEXT()`'s
         * `AttachmentLoadOp::Load` pass, so a target's real prior content genuinely survives being
         * revisited -- real `RenderTargetUsage.PreserveContents`, not merely an accident of bucket
         * merging.
         */
        struct FrameCommandBucket
        {
            LLGL::RenderTarget* target = nullptr;
            std::vector<const FrameCommand*> commands;
        };

        [[nodiscard]] std::vector<FrameCommandBucket> GroupFrameCommandsByTargetEXT() const;
        /**
         * @brief Returns a cached `LLGL::RenderPass` whose colour/depth/stencil attachments all
         * use `AttachmentLoadOp::Load`, for a target with @p numColorAttachments colour
         * attachments, @p hasDepthStencil, and @p sampleCount samples.
         *
         * Every render target and the swap chain in this renderer always share the same colour
         * format (`swapChain_->GetColorFormat()`) and depth/stencil format
         * (`swapChain_->GetDepthStencilFormat()`) -- confirmed by reading `CreateRenderTarget2D()`/
         * `CreateRenderTargetCube()`/`SetMultipleRenderTargetsEXT()`, all of which derive both from
         * the swap chain rather than taking an independent format -- so one small, renderer-lifetime
         * cache keyed only by the three numbers above is compatible with all of them; no per-target
         * render pass object is needed.
         *
         * Used by every segment's own `BeginRenderPass()` (`RecordAndSubmitFrame()`/
         * `CaptureBackbuffer()`) so a target genuinely revisited later in the same unflushed frame
         * reloads its real prior content instead of beginning from undefined memory. Applying this
         * unconditionally, even to a target's very first-ever segment, is safe: a `DiscardContents`
         * bind already queues its own explicit `Clear()` as that segment's first command (the
         * shared cross-renderer `GraphicsDevice`/`RenderTarget2D` layer does this, not this renderer),
         * so there is no case where "first use" needs to mean `Undefined` instead of `Load`.
         *
         * @param numColorAttachments Number of colour attachments (1 for a plain `RenderTarget2D`/
         *                            cube face/the swap chain; 2-4 for an MRT bind).
         * @param hasDepthStencil     Whether this target has a depth or stencil attachment (this
         *                            renderer always allocates one physically, so this is normally
         *                            true).
         * @param sampleCount         The target's own real, device-clamped sample count (1 = no
         *                            MSAA).
         * @return A render pass compatible with that attachment shape; never null.
         */
        [[nodiscard]] LLGL::RenderPass* AcquireLoadRenderPassEXT(std::uint32_t numColorAttachments,
                                                                 bool hasDepthStencil,
                                                                 std::uint32_t sampleCount);
        /** @brief Returns the colour texture to `GenerateMips()` after @p bucket's render pass
         *  ends, or null when this bucket's target has no real mip chain -- every command sharing
         *  one target carries the same `mipRegenColorTexture` answer (captured at queue time), so
         *  the first one is enough. Shared by `RecordAndSubmitFrame()`/`CaptureBackbuffer()`, the
         *  two places that record a `FrameCommandBucket`'s own render pass. */
        [[nodiscard]] static LLGL::Texture* FindMipRegenColorTextureEXT(const FrameCommandBucket& bucket)
        {
            return bucket.commands.empty() ? nullptr : bucket.commands.front()->mipRegenColorTexture;
        }
        void CaptureBackbuffer();
        void ReplayFrameCommandsList(const std::vector<const FrameCommand*>& commands);
        void ReleasePendingBuffers();
        void UpdateSwapChainResolution();
        [[nodiscard]] PresentationRect ComputePresentationRect() const;
        [[nodiscard]] PresentationRect GetActiveDrawRect() const;
        [[nodiscard]] bool IsOpaqueBlendState() const;
        [[nodiscard]] bool UsesConstantBlendFactorState() const;
        [[nodiscard]] std::uint64_t MakeBlendPipelineKey(bool scissorEnabled,
                                                         int colorAttachmentCount = 1,
                                                         int sampleCount = 1) const;
        LLGL::PipelineState* AcquireSpritePipeline(bool scissorEnabled);
        /// Combines N independently-owned RenderTarget2D's colour attachments (plus a fresh
        /// anonymous depth attachment) into ONE LLGL::RenderTarget for an MRT bind -- see
        /// LlglMRTBinding's own doc comment for the full architecture and its scope boundaries
        /// (RenderTarget2D slots only, no RenderTargetCube faces).
        void SetMultipleRenderTargetsEXT(const RenderTargetBindingDescriptor* renderTargets, int count);
        /// Deferred-releases the current MRT bind's combined LLGL::RenderTarget (if any) and
        /// clears currentMrtBinding_ -- called before EVERY SetRenderTarget2D()/SetRenderTargets()
        /// switches currentRenderTargetRenderer_ to something else, and from the destructor, so
        /// exactly one MRT bind is ever "live" and it is always properly torn down.
        void ReleaseCurrentMrtBindingEXT();
        LLGL::Sampler* AcquireSampler(int filter, int addressU, int addressV, int maxAnisotropy);
        void QueueClear(long flags, const float color[4], float depth, std::uint32_t stencil);
        void UploadFrameResources();
        void RecordAndSubmitFrame();
        [[nodiscard]] bool ComputeEffectiveScissor(std::int32_t outRect[4]) const;
        /// Fills `command.viewport` with the physical-pixel GPU viewport rectangle this command
        /// must be drawn with. Sprite/Clear commands always get the whole target (sprite geometry
        /// is baked into window pixels at queue time in QueueSpriteEXT, see its own comment) --
        /// only a Primitives command whose active XNA Viewport is a genuine sub-rectangle
        /// (viewportSet_) gets a narrower rect, since 3D vertex positions are only resolved to
        /// screen space by the GPU's own viewport transform at draw time, not pre-baked on the
        /// CPU like sprites are.
        void CaptureFrameCommandViewportEXT(FrameCommand& command) const;

        Detail::RendererModule      module_        = Detail::RendererModule::OpenGL;
        LLGL::RenderSystemPtr       renderer_;
        std::shared_ptr<LlglPlatformSurface> surface_;
        LLGL::SwapChain*            swapChain_     = nullptr;
        LLGL::CommandBuffer*        commands_      = nullptr;
        LLGL::CommandQueue*         queue_         = nullptr;

        LLGL::PipelineLayout*       spriteLayout_  = nullptr;
        LLGL::Shader*               spriteVertexShader_   = nullptr;
        LLGL::Shader*               spriteFragmentShader_ = nullptr;
        LLGL::Buffer*               spriteProjectionBuffer_ = nullptr;
        LLGL::Buffer*               spriteVertexBuffer_     = nullptr;
        std::size_t                 spriteVertexCapacity_   = 0;

        LLGL::PipelineLayout*       primitiveLayout_ = nullptr;
        LLGL::PipelineLayout*       primitiveTexturedLayout_ = nullptr;
        LLGL::Shader*               primitiveFragmentShader_ = nullptr;
        LLGL::Shader*               primitiveTexturedFragmentShader_ = nullptr;
        LLGL::Shader*               primitiveLitFragmentShader_ = nullptr;
        /// LLGL-31: lit, untextured 3D draws -- reuses primitiveLayout_ (no texture/sampler
        /// bindings), never primitiveTexturedLayout_.
        LLGL::Shader*               primitiveLitUntexturedFragmentShader_ = nullptr;
        /// `DualTextureEffect` (two independently bound samplers). Reuses the plain textured
        /// vertex shader as-is (identical vertex-side behaviour to a single-texture draw -- only
        /// the fragment shader and pipeline layout differ), so there is no dedicated vertex shader
        /// or `AcquirePrimitiveVertexShader()` branch for it.
        LLGL::PipelineLayout*       primitiveDualTextureLayout_ = nullptr;
        LLGL::Shader*               primitiveDualTextureFragmentShader_ = nullptr;

        /// `EnvironmentMapEffect` (LLGL-25 follow-up): its own dedicated vertex layout, fragment
        /// shader and pipeline layout -- unlike DualTextureEffect it does NOT reuse the plain
        /// textured vertex shader, since it needs a world-space normal/eye-vector/reflection
        /// vector the other vertex shaders never compute. See AcquirePrimitiveEnvMapVertexShader()
        /// and shaders/env_map3d.vert.glsl.
        LLGL::PipelineLayout*       primitiveEnvMapLayout_ = nullptr;
        LLGL::Shader*               primitiveEnvMapFragmentShader_ = nullptr;
        std::map<std::uint64_t, LLGL::Shader*> primitiveEnvMapVertexShaderCache_;
        /// One small constant buffer per EnvironmentMapEffect draw in a frame -- same reasoning as
        /// transformBuffers_/customEffectUniformBuffers_, but its own pool because this effect's
        /// uniform block has a different shape/size (kEnvMapUniformFloats, not kEffectUniformFloats).
        std::vector<LLGL::Buffer*>  envMapUniformBuffers_;
        std::vector<float>         envMapUniformData_;

        /// `SkinnedEffect` (LLGL-25 follow-up): its own dedicated vertex layout, fragment shader
        /// and pipeline layout, exactly like `EnvironmentMapEffect` above -- the skinning transform
        /// (bone weight/index blend) is a per-vertex-attribute concern no other vertex shader here
        /// has. See AcquirePrimitiveSkinnedVertexShader() and shaders/skinned3d.vert.glsl.
        LLGL::PipelineLayout*       primitiveSkinnedLayout_ = nullptr;
        LLGL::Shader*               primitiveSkinnedFragmentShader_ = nullptr;
        /// `SkinnedEffect.VertexColorEnabled` (LLGL-37, CNAEXT extension property): a SEPARATE
        /// fragment shader reading the extra `vColor` varying, selected instead of
        /// `primitiveSkinnedFragmentShader_` only when the bound vertex layout carries a colour
        /// attribute (stride 56) -- see AcquirePrimitiveSkinnedVertexShader()'s own doc comment for
        /// why this is a separate shader file rather than an always-declared attribute, and
        /// shaders/skinned3d_color.vert.glsl.
        LLGL::Shader*               primitiveSkinnedColorFragmentShader_ = nullptr;
        std::map<std::uint64_t, LLGL::Shader*> primitiveSkinnedVertexShaderCache_;
        /// One small constant buffer per SkinnedEffect draw in a frame for the non-bone parameters
        /// (kSkinnedUniformFloats) -- same reasoning as envMapUniformBuffers_ above.
        std::vector<LLGL::Buffer*>  skinnedUniformBuffers_;
        std::vector<float>         skinnedUniformData_;
        /// A SECOND, much larger per-draw buffer pool for the 72-bone `mat4` array
        /// (kSkinnedBoneFloats = 72*16 floats = 4608 bytes) -- kept separate from
        /// skinnedUniformBuffers_ rather than folded into one buffer, mirroring the Vulkan
        /// renderer's own two-UBO split (`BoneBlock`/`FogParams`), since the two buffers have
        /// wildly different sizes and this project's own established growth/reuse pool pattern is
        /// already per-buffer-shape (transformBuffers_/customEffectUniformBuffers_/
        /// envMapUniformBuffers_ are three separate pools for exactly this reason).
        std::vector<LLGL::Buffer*>  skinnedBoneBuffers_;
        std::vector<float>         skinnedBoneData_;

        /// `PbrEffect`: its own dedicated vertex layout, fragment shader and pipeline layout, like
        /// `EnvironmentMapEffect`/`SkinnedEffect` above -- the tangent-space TBN basis and 5-texture
        /// (base colour, normal, metallic-roughness, emissive, occlusion) glTF metallic-roughness
        /// BRDF are a field set no other vertex/fragment shader here has. See
        /// AcquirePrimitivePbrVertexShader() and shaders/pbr3d.vert.glsl.
        LLGL::PipelineLayout*       primitivePbrLayout_ = nullptr;
        LLGL::Shader*               primitivePbrFragmentShader_ = nullptr;
        std::map<std::uint64_t, LLGL::Shader*> primitivePbrVertexShaderCache_;
        /// One small constant buffer per PbrEffect draw in a frame -- same reasoning as
        /// envMapUniformBuffers_/skinnedUniformBuffers_ above, own pool because this effect's
        /// uniform block has its own shape/size.
        std::vector<LLGL::Buffer*>  pbrUniformBuffers_;
        std::vector<float>         pbrUniformData_;
        /// Lazily created by EnsureDefaultPbrTexturesEXT() the first time a PbrEffect draw is
        /// queued with a null optional map; released in the destructor like every other texture
        /// this renderer itself (not a game object) owns.
        LLGL::Texture*              defaultWhitePbrTexture_ = nullptr;
        LLGL::Texture*              defaultFlatNormalPbrTexture_ = nullptr;

        /// `SkinnedPbrEffect`: its own dedicated pipeline layout and vertex-shader cache -- the
        /// fragment shader (`primitivePbrFragmentShader_`) and bone-transform buffer pool
        /// (`skinnedBoneBuffers_`/`skinnedBoneData_`, `FillSkinnedBoneData()`) are both reused
        /// verbatim from `PbrEffect`/`SkinnedEffect` respectively -- bone data is effect-agnostic,
        /// and the fragment stage never touches bones at all. See
        /// AcquirePrimitivePbrSkinnedVertexShader() and shaders/pbr3d_skinned.vert.glsl.
        LLGL::PipelineLayout*       primitivePbrSkinnedLayout_ = nullptr;
        std::map<std::uint64_t, LLGL::Shader*> primitivePbrSkinnedVertexShaderCache_;

        /// Shared by every `LlglEffectRenderer` (LLGL-27); built lazily by
        /// AcquireCustomEffectLayoutEXT() on the first `ShaderEffect` any game constructs. Unlike
        /// spriteLayout_, binding 1's constant buffer is readable from both stages -- a custom
        /// fragment shader reads pc.uColor from the very same buffer the vertex shader reads
        /// pc.vpSize_pad/pc.uMatrix from.
        LLGL::PipelineLayout*       customEffectLayout_ = nullptr;
        /// Non-owning; null means "no custom effect bound, use the stock sprite shader". Set by
        /// LlglEffectRenderer::Bind()/Unbind(), consulted when a Sprite command is queued.
        LlglEffectRenderer*          currentCustomEffect_ = nullptr;
        /// One small constant buffer per custom-effect sprite draw in a frame -- same reasoning as
        /// transformBuffers_/transformData_: SetUniformX() can legitimately change between two
        /// Draw() calls inside one Begin()/End() block, so one shared buffer overwritten in place
        /// would leak the LAST draw's values onto every earlier one once the frame is replayed.
        std::vector<LLGL::Buffer*>  customEffectUniformBuffers_;
        std::vector<float>         customEffectUniformData_;

        std::map<std::uint64_t, LLGL::PipelineState*> pipelineCache_;
        std::map<std::uint64_t, LLGL::Sampler*>       samplerCache_;
        /// LLGL-48/LLGL-53: widened from a single `std::uint64_t` to a 5-way tuple. Folding blend
        /// state, depth bias's
        /// two floats and the full front/back stencil state's ~10 fields into the SAME single
        /// `std::uint64_t` the rest of this key already used (via more multiply-add steps) was
        /// tried and MEASURED BROKEN: the cumulative multiplier of everything folded in AFTER
        /// `depthBias_`/`slopeScaleDepthBias_` exceeded 2^64, silently overflowing those two
        /// floats' bits away to nothing -- `rasterizerstate_depthbias_test.cpp`'s own A1 check
        /// (`DepthBias=3000000` vs `DepthBias=0`) computed the IDENTICAL key for both, reusing the
        /// zero-bias pipeline for the biased draw. `std::tuple` gets `std::map`'s ordering for
        /// free, so each of the 4 new elements can use its OWN full 64-bit budget instead of
        /// competing for bits with the original single-`uint64_t` key or with each other: element
        /// 1 is the ORIGINAL key computation unchanged, element 2 is the complete blend-state
        /// key (preventing the old low-16-bit ColorWriteChannels/blend-factor collision), element
        /// 3 packs both depth-bias floats losslessly (32+32 bits, zero collision risk), element 4
        /// packs both stencil masks losslessly (32+32 bits), element 5 packs the 8 stencil
        /// op/function fields plus the 2
        /// stencil bools (comfortably under 64 bits with the same small-multiplier style already
        /// used elsewhere in this file).
        std::map<std::tuple<std::uint64_t, std::uint64_t, std::uint64_t, std::uint64_t,
                            std::uint64_t>,
                 LLGL::PipelineState*> primitivePipelineCache_;
        /// One `AttachmentLoadOp::Load` render pass per distinct (colour-attachment-count,
        /// has-depth-stencil, sample-count) shape, shared by every render target AND the swap
        /// chain -- every one of them always uses the same colour/depth-stencil FORMAT
        /// (`swapChain_->GetColorFormat()`/`GetDepthStencilFormat()`), so this single,
        /// renderer-lifetime cache is compatible with all of them. See
        /// AcquireLoadRenderPassEXT()'s own doc comment (LLGL-45).
        std::map<std::uint64_t, LLGL::RenderPass*>    loadRenderPassCache_;
        /// One vertex shader per distinct vertex layout: LLGL carries the input layout (offsets and
        /// stride included) on the shader object, so a second layout genuinely needs a second one.
        std::map<std::uint64_t, LLGL::Shader*>        primitiveVertexShaderCache_;

        /// One small constant buffer per 3D draw in a frame, grown on demand and reused across
        /// frames. Each draw has its own matrix, and LLGL's individual (non-heap) resource bindings
        /// cannot offset into a shared buffer.
        std::vector<LLGL::Buffer*>  transformBuffers_;
        std::vector<float>          transformData_;
        /// Buffers whose owning resource has been destroyed, released once the frame that may
        /// reference them has been submitted. See ScheduleBufferReleaseEXT. Also holds a
        /// destroyed RenderTarget2D's own sprite projection buffer (ScheduleRenderTargetReleaseEXT).
        std::vector<LLGL::Buffer*>  pendingBufferReleases_;
        /// RenderTarget2D framebuffer objects destroyed before the frame that may still reference
        /// them (by raw pointer, in FrameCommand::target) was submitted. See
        /// ScheduleRenderTargetReleaseEXT.
        std::vector<LLGL::RenderTarget*> pendingRenderTargetReleases_;
        /// A destroyed RenderTarget2D's own colour (and, if ever non-anonymous, depth) textures,
        /// deferred for the same reason as pendingRenderTargetReleases_.
        std::vector<LLGL::Texture*> pendingTextureReleases_;
        /// Occlusion query heaps replaced (a new one is created on every Begin()) or destroyed
        /// before the frame that may still reference them was submitted. See
        /// ScheduleQueryHeapReleaseEXT.
        std::vector<LLGL::QueryHeap*> pendingQueryHeapReleases_;
        /// A destroyed ShaderEffect's own shader modules and cached pipelines, deferred for the
        /// same reason as pendingRenderTargetReleases_. See ScheduleEffectResourceReleaseEXT.
        std::vector<LLGL::Shader*> pendingShaderReleases_;
        std::vector<LLGL::PipelineState*> pendingPipelineReleases_;

        std::vector<float>          spriteVertexData_;
        std::vector<FrameCommand>   frameCommands_;
        /// True when ReadBackbuffer() already recorded and submitted the current frame, so
        /// Present() must not record it a second time.
        bool                        frameSubmitted_ = false;

        /// Whole-back-buffer capture serving every ReadBackbuffer() of the same frame; the swap
        /// chain's render pass discards its colour attachment on entry, so it can only be read once.
        std::vector<std::uint8_t>   backbufferCache_;
        int                         backbufferCacheWidth_  = 0;
        int                         backbufferCacheHeight_ = 0;
        bool                        backbufferCacheValid_  = false;

        int   virtualWidth_   = 0;
        int   virtualHeight_  = 0;
        int   presentationMode_ = 0;
        int   swapInterval_   = 1;
        int   requestedSampleCount_ = 1;

        int   colorSrcBlend_  = 0;
        int   alphaSrcBlend_  = 0;
        int   colorDstBlend_  = 1;
        int   alphaDstBlend_  = 1;
        int   colorBlendFunc_ = 0;
        int   alphaBlendFunc_ = 0;
        /// Raw XNA ColorWriteChannels int per MRT slot 0..3 (bit0=R,1=G,2=B,3=A; 15 = All) --
        /// slot 0 applies outside an MRT bind (colorAttachmentCount == 1); slots 1..3 apply only
        /// while a real MRT set (LLGL-26 follow-up) is bound (LLGL-21).
        int   colorWriteChannels_[4] = {15, 15, 15, 15};
        /// Raw XNA BlendState.MultiSampleMask (LLGL-33): a per-sample coverage bitmask, applied
        /// via LLGL::BlendDescriptor::sampleMask -- only meaningful while a real MSAA render
        /// target/back buffer is bound (samples > 1); 0xFFFFFFFF (every sample enabled) is XNA's
        /// own default and this member's own default, matching BlendWriteState::multiSampleMask.
        unsigned int multiSampleMask_ = 0xFFFFFFFFu;
        bool  blendEnabled_   = false;
        float blendFactor_[4] = {1.0f, 1.0f, 1.0f, 1.0f};

        /// LLGL-49: per-slot SamplerState, one entry per texture slot this renderer's stock effects
        /// actually consume -- slot 0 (every family's own base/primary texture), slot 1
        /// (DualTextureEffect's second texture OR EnvironmentMapEffect's cube map -- the two are
        /// mutually exclusive per draw, matching the Vulkan renderer's own `slotSamplers_[1]`
        /// convention), and slots 2-4 (PbrEffect's metallic-roughness/emissive/occlusion maps,
        /// matching the Vulkan renderer's own `PbrSlotSamplersRawEXT()` slot-0..4 convention --
        /// "EasyGL binds the same five to Texture0..4"). Previously a single, un-indexed set of
        /// scalars that `ApplySamplerState()` only ever wrote for slot 0, so every OTHER slot
        /// always sampled with whatever slot 0's current state was.
        static constexpr int kTrackedSamplerSlotCount = 5;
        int   samplerFilter_[kTrackedSamplerSlotCount]        = {0, 0, 0, 0, 0};
        int   samplerAddressU_[kTrackedSamplerSlotCount]      = {1, 1, 1, 1, 1};
        int   samplerAddressV_[kTrackedSamplerSlotCount]      = {1, 1, 1, 1, 1};
        int   samplerMaxAnisotropy_[kTrackedSamplerSlotCount] = {1, 1, 1, 1, 1};

        bool  scissorTestEnabled_ = false;
        bool  scissorRectSet_     = false;
        std::int32_t scissorRect_[4] = {0, 0, 0, 0};
        bool  viewportSet_        = false;
        std::int32_t viewportRect_[4] = {0, 0, 0, 0};
        /// XNA `Viewport.MinDepth`/`MaxDepth` (LLGL-53) -- 0/1 are XNA's own `Viewport` defaults.
        /// Folded into the GPU viewport's own depth range at replay time, the same as every other
        /// renderer's `glDepthRangef`/`VkViewport::minDepth`/`maxDepth` equivalent -- see
        /// `CaptureFrameCommandViewportEXT()`'s own doc comment for why only `Primitives` commands
        /// ever narrow away from the full-target default, matching the rect's own restriction.
        float minDepth_ = 0.0f;
        float maxDepth_ = 1.0f;

        bool  depthTestEnabled_  = false;
        bool  depthWriteEnabled_ = true;
        int   depthCompareFunction_ = 3;  // CompareFunction::LessEqual, the XNA default
        bool  stencilRequested_  = false;

        /// Non-owning; null means "draw to the swap chain". Points at a plain
        /// LlglRenderTargetRenderer (set by SetRenderTarget2D()), one face of an
        /// LlglRenderTargetCubeRenderer (an LlglRenderTargetCubeFaceBinding, set by SetRenderTargets()
        /// for a cube-face descriptor), or currentMrtBinding_.get() (set by
        /// SetMultipleRenderTargetsEXT() for a count>1 descriptor) -- consulted when each
        /// Clear/sprite/primitive command is QUEUED (not at bind time -- this renderer defers
        /// every draw to Present()).
        LlglBoundRenderTarget* currentRenderTargetRenderer_ = nullptr;
        /// Owning, unlike currentRenderTargetRenderer_ itself: an MRT bind has no XNA-level C++
        /// object of its own (see LlglMRTBinding's doc comment), so THIS renderer owns its
        /// lifetime instead. Non-null only while an MRT set (count>1) is the active target.
        std::unique_ptr<LlglMRTBinding> currentMrtBinding_;
        int   cullMode_ = 2;
        int   fillMode_ = 0;
        /// LLGL-53: `RasterizerState.DepthBias`/`SlopeScaleDepthBias`, folded straight into
        /// `LLGL::DepthBiasDescriptor::constantFactor`/`slopeFactor` (both share the native-API
        /// "raw, unconverted, implementation-dependent units" contract -- confirmed against the
        /// Vulkan renderer's own identical raw pass-through into `vkCmdSetDepthBias`). XNA's own
        /// default for both is `0.0f`.
        float depthBias_ = 0.0f;
        float slopeScaleDepthBias_ = 0.0f;

        /// LLGL-53: `DepthStencilState`'s full front/back stencil test, previously recorded only as
        /// `stencilRequested_` (enabled/disabled) with the other 8 fields discarded. Defaults match
        /// XNA's own `DepthStencilState.Default`/`StencilFunction.Always`/`StencilOperation.Keep`.
        int   stencilFunction_          = 0; // CompareFunction::Always
        int   stencilPass_              = 0; // StencilOperation::Keep
        int   stencilFail_              = 0;
        int   stencilDepthFail_         = 0;
        int   stencilMask_              = ~0;
        int   stencilWriteMask_         = ~0;
        int   referenceStencil_         = 0;
        bool  twoSidedStencilMode_      = false;
        int   ccwStencilFunction_       = 0; // CompareFunction::Always
        int   ccwStencilPass_           = 0; // StencilOperation::Keep
        int   ccwStencilFail_           = 0;
        int   ccwStencilDepthFail_      = 0;
    };
}
