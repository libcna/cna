// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/CNAHelper.hpp"
#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "CNA/Internal/Renderers/Igl/IglPlatformSurface.hpp"
#include "CNA/Internal/Renderers/Igl/IglRendererSelection.hpp"
#include "CNA/Internal/Renderers/Igl/IglShaderLibrary.hpp"

#include <igl/Buffer.h>
#include <igl/DepthStencilState.h>
#include <igl/Framebuffer.h>
#include <igl/RenderPass.h>
#include <igl/RenderPipelineState.h>
#include <igl/SamplerState.h>
#include <igl/Texture.h>
#include <igl/Uniform.h>
#include <igl/VertexInputState.h>

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace igl
{
    class ICommandBuffer;
    class IRenderCommandEncoder;
    class IShaderStages;
}

namespace CNA::Internal::Renderers::Igl
{
    class IglRenderer;

    /** @brief Number of frames of dynamic buffer storage kept in flight. CNAEXT. */
    inline constexpr int kIglFramesInFlight = 3;

    /** @brief Texture slots this renderer tracks an independent XNA `SamplerState` for. CNAEXT. */
    inline constexpr int kIglTrackedSamplerSlots = static_cast<int>(TextureUnit::Count);

    /**
     * @brief Streaming vertex/index storage for the routes that own no public buffer. CNAEXT.
     *
     * `SpriteBatch`, `DrawUserPrimitives` and `DrawColoredPrimitives` all build their geometry on
     * the CPU each frame. Uploading into a buffer a still-unsubmitted command buffer references
     * would corrupt that earlier draw, so allocations rotate through @ref kIglFramesInFlight
     * independent chunk lists: a chunk is only reused once its frame has come round again, by which
     * point the GPU has long finished with it.
     */
    class IglDynamicBufferPool final
    {
    public:
        /** @brief One suballocation: the buffer to bind and the byte offset within it. */
        struct Allocation
        {
            /** @brief Buffer to bind; null only when the allocation failed. */
            igl::IBuffer* buffer = nullptr;
            /** @brief Byte offset of the allocated range. */
            std::size_t offset = 0;
        };

        /**
         * @brief Binds the pool to a device and fixes what kind of buffer it allocates.
         *
         * @param device     Device that will own the chunks.
         * @param bufferType Bitmask of `igl::BufferDesc::BufferTypeBits`.
         * @param debugName  Name given to every chunk, for GPU debuggers.
         */
        IglDynamicBufferPool(igl::IDevice& device, std::uint8_t bufferType, std::string debugName);

        /** @brief Advances to the next frame's chunk list and rewinds its high-water mark. */
        void BeginFrame(int frameIndex);

        /**
         * @brief Uploads @p sizeInBytes of @p data and returns where it landed.
         *
         * @param data        Source bytes; must not be null.
         * @param sizeInBytes Number of bytes to upload.
         * @param alignment   Required alignment of the returned offset, in bytes.
         * @return The buffer and offset to bind.
         * @throws std::runtime_error If a chunk could not be created or uploaded to.
         */
        [[nodiscard]] Allocation Upload(const void* data, std::size_t sizeInBytes,
                                        std::size_t alignment);

    private:
        struct Chunk
        {
            std::unique_ptr<igl::IBuffer> buffer;
            std::size_t capacity = 0;
            std::size_t used = 0;
        };

        igl::IDevice& device_;
        std::uint8_t bufferType_;
        std::string debugName_;
        int frameIndex_ = 0;
        std::array<std::vector<Chunk>, kIglFramesInFlight> frames_;
    };

    /**
     * @brief A 2D texture living in IGL. CNAEXT.
     *
     * Backs `Microsoft::Xna::Framework::Graphics::Texture2D`. Pixels are always RGBA8, the format
     * the shared texture layer hands every renderer.
     */
    class IglTextureRenderer final : public ITextureRenderer
    {
    public:
        /**
         * @brief Adopts an already-created IGL texture.
         *
         * @param owner     Renderer whose frame must be flushed before a readback; may be null.
         * @param texture   The texture resource; must not be null.
         * @param width     Width in pixels of mip level 0.
         * @param height    Height in pixels of mip level 0.
         * @param mipLevels Number of mip levels the texture was created with.
         */
        IglTextureRenderer(IglRenderer* owner, std::shared_ptr<igl::ITexture> texture,
                           int width, int height, int mipLevels);

        ~IglTextureRenderer() override = default;

        IglTextureRenderer(const IglTextureRenderer&) = delete;
        IglTextureRenderer& operator=(const IglTextureRenderer&) = delete;

        /** @brief Returns the width in pixels of mip level 0. */
        [[nodiscard]] int GetWidth() const override { return width_; }

        /** @brief Returns the height in pixels of mip level 0. */
        [[nodiscard]] int GetHeight() const override { return height_; }

        /**
         * @brief Replaces the whole of mip level 0.
         *
         * @param rgba   Source pixels, RGBA8.
         * @param stride Row pitch of @p rgba in bytes.
         */
        void UpdatePixels(const std::uint8_t* rgba, int stride) override;

        /**
         * @brief Replaces the whole of one mip level.
         *
         * @param level  Mip level to write.
         * @param rgba   Source pixels, RGBA8, tightly packed.
         * @param levelW Width of that mip level.
         * @param levelH Height of that mip level.
         */
        void UpdatePixelsLevel(int level, const std::uint8_t* rgba, int levelW, int levelH) override;

        /** @brief Reports that a level below @ref GetMipLevels has real, uploaded storage. */
        [[nodiscard]] bool HasDefinedMipLevel(int level) const noexcept override;

        /**
         * @brief Reads pixels back from the GPU.
         *
         * @param level      Mip level to read.
         * @param x          Left edge of the region in pixels.
         * @param y          Top edge of the region in pixels.
         * @param w          Width of the region in pixels.
         * @param h          Height of the region in pixels.
         * @param data       Destination for tightly packed RGBA8 rows, top row first.
         * @param dataLength Size of @p data in bytes; at least `w * h * 4`.
         * @return True if the whole region was read back; false if it could not be.
         */
        [[nodiscard]] bool GetData(int level, int x, int y, int w, int h,
                                   void* data, int dataLength) const override;

        /** @brief Returns the underlying IGL texture. */
        [[nodiscard]] const std::shared_ptr<igl::ITexture>& GetIglTexture() const { return texture_; }

        /** @brief Returns how many mip levels this texture was allocated with. */
        [[nodiscard]] int GetMipLevels() const noexcept { return mipLevels_; }

    private:
        IglRenderer* owner_ = nullptr;
        std::shared_ptr<igl::ITexture> texture_;
        int width_ = 0;
        int height_ = 0;
        int mipLevels_ = 1;
        // Highest mip level that has actually had pixels uploaded into it, plus one. Allocated GPU
        // storage is not readable content, so HasDefinedMipLevel answers from this rather than from
        // the allocation.
        int definedMipLevels_ = 0;
    };

    /**
     * @brief A cube texture living in IGL. CNAEXT.
     *
     * Backs `Microsoft::Xna::Framework::Graphics::TextureCube`. One `igl::TextureType::Cube`
     * resource whose faces follow the project-wide order (0=+X, 1=-X, 2=+Y, 3=-Y, 4=+Z, 5=-Z),
     * which is the order `igl::TextureCubeFace` already uses, so a face index maps straight through.
     */
    class IglTextureCubeRenderer final : public ITextureCubeRenderer
    {
    public:
        /**
         * @brief Adopts an already-created IGL cube texture.
         *
         * @param owner     Renderer whose frame must be flushed before a readback; may be null.
         * @param texture   The texture resource; must not be null.
         * @param size      Width and height in pixels of each square face at mip level 0.
         * @param mipLevels Number of mip levels the texture was created with.
         */
        IglTextureCubeRenderer(IglRenderer* owner, std::shared_ptr<igl::ITexture> texture,
                               int size, int mipLevels);

        ~IglTextureCubeRenderer() override = default;

        IglTextureCubeRenderer(const IglTextureCubeRenderer&) = delete;
        IglTextureCubeRenderer& operator=(const IglTextureCubeRenderer&) = delete;

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

        /** @brief Returns the width and height of one face at mip level 0. */
        [[nodiscard]] int GetSizeEXT() const noexcept override { return size_; }

        /** @brief Returns the underlying IGL texture. */
        [[nodiscard]] const std::shared_ptr<igl::ITexture>& GetIglTexture() const { return texture_; }

    private:
        IglRenderer* owner_ = nullptr;
        std::shared_ptr<igl::ITexture> texture_;
        int size_ = 0;
        int mipLevels_ = 1;
    };

    /**
     * @brief A volume (3D) texture living in IGL. CNAEXT.
     *
     * Backs `Microsoft::Xna::Framework::Graphics::Texture3D` with a real `igl::TextureType::ThreeD`
     * resource -- a depth extent, not array layers.
     */
    class IglTexture3DRenderer final : public ITexture3DRenderer
    {
    public:
        /**
         * @brief Adopts an already-created IGL volume texture.
         *
         * @param owner     Renderer whose frame must be flushed before a readback; may be null.
         * @param texture   The texture resource; must not be null.
         * @param width     Width in voxels at mip level 0.
         * @param height    Height in voxels at mip level 0.
         * @param depth     Depth in voxels at mip level 0.
         * @param mipLevels Number of mip levels the texture was created with.
         */
        IglTexture3DRenderer(IglRenderer* owner, std::shared_ptr<igl::ITexture> texture,
                             int width, int height, int depth, int mipLevels);

        ~IglTexture3DRenderer() override = default;

        IglTexture3DRenderer(const IglTexture3DRenderer&) = delete;
        IglTexture3DRenderer& operator=(const IglTexture3DRenderer&) = delete;

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

        /** @brief Returns the dimensions in voxels at mip level 0. */
        void GetDimensionsEXT(int& width, int& height, int& depth) const noexcept override;

        /** @brief Returns the underlying IGL texture. */
        [[nodiscard]] const std::shared_ptr<igl::ITexture>& GetIglTexture() const { return texture_; }

    private:
        IglRenderer* owner_ = nullptr;
        std::shared_ptr<igl::ITexture> texture_;
        int width_ = 0;
        int height_ = 0;
        int depth_ = 0;
        int mipLevels_ = 1;
    };

    /**
     * @brief A vertex buffer living in IGL. CNAEXT.
     *
     * The final byte size is only known once `SetData` supplies a stride, so the IGL buffer is
     * created on the first upload and grown when a later one needs more room.
     */
    class IglVertexBufferRenderer final : public IVertexBufferRenderer
    {
    public:
        /**
         * @brief Records the intended capacity; the GPU buffer is created on first upload.
         *
         * @param owner          Renderer that owns the device and the pending frame.
         * @param vertexCapacity Number of vertices the caller intends to store.
         */
        IglVertexBufferRenderer(IglRenderer* owner, int vertexCapacity);

        ~IglVertexBufferRenderer() override = default;

        IglVertexBufferRenderer(const IglVertexBufferRenderer&) = delete;
        IglVertexBufferRenderer& operator=(const IglVertexBufferRenderer&) = delete;

        /**
         * @brief Uploads vertex data.
         *
         * A re-upload flushes any frame already recording, because a draw queued earlier this frame
         * references this buffer's current bytes by handle and must run against them before they
         * are replaced.
         *
         * @param data          Packed vertex data.
         * @param vertexCount   Number of vertices in @p data.
         * @param strideInBytes Size of one vertex in bytes.
         */
        void SetData(const void* data, int vertexCount, std::size_t strideInBytes) override;

        /**
         * @brief Records the caller's own vertex declaration.
         *
         * Kept so the draw path can build a matching `igl::VertexInputStateDesc` generically,
         * instead of recognising only the fixed strides of XNA's built-in vertex types.
         *
         * @param vertexDeclaration Declaration in declaration order, including its stride.
         */
        void SetVertexDeclaration(const VertexDeclaration& vertexDeclaration) override;

        /** @brief Returns the number of vertices most recently uploaded. */
        [[nodiscard]] int GetVertexCount() const override { return vertexCount_; }

        /** @brief Returns the underlying IGL buffer, or null before the first upload. */
        [[nodiscard]] igl::IBuffer* GetIglBuffer() const { return buffer_.get(); }

        /** @brief Returns the stride in bytes of the most recent upload, or 0 before it. */
        [[nodiscard]] std::size_t GetStride() const { return stride_; }

        /** @brief Returns whether a caller-supplied declaration is available. */
        [[nodiscard]] bool HasDeclaration() const { return hasDeclaration_; }

        /** @brief Returns the caller-supplied declaration; only meaningful when it exists. */
        [[nodiscard]] const VertexDeclaration& GetDeclaration() const { return declaration_; }

    private:
        IglRenderer* owner_ = nullptr;
        std::unique_ptr<igl::IBuffer> buffer_;
        int vertexCapacity_ = 0;
        int vertexCount_ = 0;
        std::size_t stride_ = 0;
        std::size_t byteCapacity_ = 0;
        bool hasDeclaration_ = false;
        VertexDeclaration declaration_;
    };

    /** @brief A 16- or 32-bit index buffer living in IGL. CNAEXT. */
    class IglIndexBufferRenderer final : public IIndexBufferRenderer
    {
    public:
        /**
         * @brief Records the intended capacity; the GPU buffer is created on first upload.
         *
         * @param owner         Renderer that owns the device and the pending frame.
         * @param indexCapacity Number of indices the caller intends to store.
         * @param thirtyTwoBit  True for 32-bit indices, false for 16-bit.
         */
        IglIndexBufferRenderer(IglRenderer* owner, int indexCapacity, bool thirtyTwoBit);

        ~IglIndexBufferRenderer() override = default;

        IglIndexBufferRenderer(const IglIndexBufferRenderer&) = delete;
        IglIndexBufferRenderer& operator=(const IglIndexBufferRenderer&) = delete;

        /**
         * @brief Uploads 16-bit index data.
         * @param data       Packed 16-bit indices.
         * @param indexCount Number of indices.
         */
        void SetData16(const void* data, int indexCount) override;

        /**
         * @brief Uploads 32-bit index data.
         * @param data       Packed 32-bit indices.
         * @param indexCount Number of indices.
         */
        void SetData32(const void* data, int indexCount) override;

        /** @brief Returns the number of indices most recently uploaded. */
        [[nodiscard]] int GetIndexCount() const override { return indexCount_; }

        /** @brief Returns whether this buffer stores 32-bit indices. */
        [[nodiscard]] bool IsThirtyTwoBit() const override { return thirtyTwoBit_; }

        /** @brief Returns the underlying IGL buffer, or null before the first upload. */
        [[nodiscard]] igl::IBuffer* GetIglBuffer() const { return buffer_.get(); }

    private:
        void Upload(const void* data, int indexCount, std::size_t indexSize);

        IglRenderer* owner_ = nullptr;
        std::unique_ptr<igl::IBuffer> buffer_;
        int indexCapacity_ = 0;
        int indexCount_ = 0;
        bool thirtyTwoBit_ = false;
        std::size_t byteCapacity_ = 0;
    };

    /**
     * @brief What the renderer needs from whatever surface is currently bound. CNAEXT.
     *
     * Lets one pointer stand for the back buffer, a `RenderTarget2D`, one face of a
     * `RenderTargetCube`, or a multi-target set, so the pass, pipeline and viewport code never has
     * to ask which it is.
     */
    class IglBoundTarget
    {
    public:
        virtual ~IglBoundTarget() = default;
        /** @brief Returns the framebuffer a render pass is opened against. */
        [[nodiscard]] virtual const std::shared_ptr<igl::IFramebuffer>& GetFramebuffer() const = 0;
        /** @brief Returns the target width in pixels. */
        [[nodiscard]] virtual int GetTargetWidth() const = 0;
        /** @brief Returns the target height in pixels. */
        [[nodiscard]] virtual int GetTargetHeight() const = 0;
        /** @brief Returns how many colour attachments a pipeline drawn here must declare. */
        [[nodiscard]] virtual int GetColorAttachmentCount() const { return 1; }
        /** @brief Returns the device-clamped MSAA sample count of this target (1 = none). */
        [[nodiscard]] virtual int GetTargetSampleCount() const { return 1; }
        /** @brief Returns true when a draw here should preserve what the target already holds. */
        [[nodiscard]] virtual bool PreservesContents() const { return false; }
        /** @brief Returns the cube face index a pass must select, or -1 when not a cube face. */
        [[nodiscard]] virtual int GetCubeFace() const { return -1; }
        /** @brief Regenerates the mip chain after a pass ends; a no-op for most targets. */
        virtual void GenerateMipsAfterPass(igl::ICommandQueue& /*queue*/) {}
    };

    /**
     * @brief An off-screen 2D render target living in IGL. CNAEXT.
     *
     * Backs `Microsoft::Xna::Framework::Graphics::RenderTarget2D`. Owns a sampleable colour texture,
     * an optional depth/stencil texture and, when multisampled, a separate multisample colour
     * texture that IGL resolves into the sampleable one at the end of every pass.
     */
    class IglRenderTargetRenderer final : public IRenderTargetRenderer, public IglBoundTarget
    {
    public:
        /**
         * @brief Adopts the textures and framebuffer that make up this target.
         *
         * @param owner            Renderer that owns the device.
         * @param color            Sampleable colour texture (the resolve target when multisampled).
         * @param multisampleColor Multisampled colour texture, or null when not multisampled.
         * @param depth            Depth/stencil texture, or null when none was allocated.
         * @param framebuffer      Framebuffer binding the above together.
         * @param width            Width in pixels.
         * @param height           Height in pixels.
         * @param mipLevels        Mip levels of @p color.
         * @param sampleCount      Device-clamped sample count actually applied.
         * @param preserveContents Whether a bind must load the target's previous contents.
         * @param appliedDepthStencilFormat Applied `DepthFormat` ordinal.
         */
        IglRenderTargetRenderer(IglRenderer* owner,
                                std::shared_ptr<igl::ITexture> color,
                                std::shared_ptr<igl::ITexture> multisampleColor,
                                std::shared_ptr<igl::ITexture> depth,
                                std::shared_ptr<igl::IFramebuffer> framebuffer,
                                int width, int height, int mipLevels, int sampleCount,
                                bool preserveContents, int appliedDepthStencilFormat);

        ~IglRenderTargetRenderer() override = default;

        /** @brief Returns the width in pixels. */
        [[nodiscard]] int GetWidth() const override { return width_; }

        /** @brief Returns the height in pixels. */
        [[nodiscard]] int GetHeight() const override { return height_; }

        /**
         * @brief Uploads pixels into mip level 0 of the colour texture.
         * @param rgba   Source pixels, RGBA8.
         * @param stride Row pitch of @p rgba in bytes.
         */
        void UpdatePixels(const std::uint8_t* rgba, int stride) override;

        /**
         * @brief Uploads pixels into one mip level of the colour texture.
         * @param level  Mip level to write.
         * @param rgba   Source pixels, RGBA8, tightly packed.
         * @param levelW Width of that mip level.
         * @param levelH Height of that mip level.
         */
        void UpdatePixelsLevel(int level, const std::uint8_t* rgba, int levelW, int levelH) override;

        /** @brief Reports that every allocated mip level of a rendered target is readable. */
        [[nodiscard]] bool HasDefinedMipLevel(int level) const noexcept override;

        /**
         * @brief Reads rendered pixels back from the colour attachment.
         *
         * Both backends present a bottom-left origin (IGL's Vulkan encoder uses a negative viewport
         * height to match OpenGL), so the rows this renderer draws into a target are already stored
         * top-first: it renders off-screen targets with a flipped projection precisely so that a
         * target reads back, and samples, in the same orientation as an uploaded texture.
         *
         * @param level      Mip level to read; only level 0 is attached and therefore readable.
         * @param x          Left edge of the region in pixels.
         * @param y          Top edge of the region in pixels.
         * @param w          Width of the region in pixels.
         * @param h          Height of the region in pixels.
         * @param data       Destination for tightly packed RGBA8 rows, top row first.
         * @param dataLength Size of @p data in bytes; at least `w * h * 4`.
         * @return True if the whole region was written; false otherwise.
         */
        [[nodiscard]] bool GetData(int level, int x, int y, int w, int h,
                                   void* data, int dataLength) const override;

        /** @brief No-op: this renderer binds targets through `SetRenderTargets`, not per-resource. */
        void BindAsRenderTarget() override {}

        /** @brief No-op: see @ref BindAsRenderTarget. */
        void UnbindAsRenderTarget() override {}

        /** @brief Returns the device-clamped sample count this target was created with. */
        [[nodiscard]] int GetMultiSampleCount() const override { return sampleCount_; }

        /** @brief Returns the `DepthFormat` ordinal actually backing this target. */
        [[nodiscard]] int GetAppliedDepthStencilFormatEXT(int requestedDepthStencilFormat) const override;

        /** @brief Returns whether real depth storage was allocated for this target. */
        [[nodiscard]] bool HasRealDepthBuffer(bool depthFormatWasRequested) const override;

        /** @brief Returns the framebuffer a render pass is opened against. */
        [[nodiscard]] const std::shared_ptr<igl::IFramebuffer>& GetFramebuffer() const override
        {
            return framebuffer_;
        }

        /** @brief Returns the target width in pixels. */
        [[nodiscard]] int GetTargetWidth() const override { return width_; }

        /** @brief Returns the target height in pixels. */
        [[nodiscard]] int GetTargetHeight() const override { return height_; }

        /** @brief Returns the device-clamped sample count of this target. */
        [[nodiscard]] int GetTargetSampleCount() const override { return sampleCount_; }

        /** @brief Returns whether a bind must load this target's previous contents. */
        [[nodiscard]] bool PreservesContents() const override { return preserveContents_; }

        /** @brief Regenerates the colour texture's mip chain when it has one. */
        void GenerateMipsAfterPass(igl::ICommandQueue& queue) override;

        /** @brief Returns the sampleable colour texture. */
        [[nodiscard]] const std::shared_ptr<igl::ITexture>& GetColorTexture() const { return color_; }

    private:
        IglRenderer* owner_ = nullptr;
        std::shared_ptr<igl::ITexture> color_;
        std::shared_ptr<igl::ITexture> multisampleColor_;
        std::shared_ptr<igl::ITexture> depth_;
        std::shared_ptr<igl::IFramebuffer> framebuffer_;
        int width_ = 0;
        int height_ = 0;
        int mipLevels_ = 1;
        int sampleCount_ = 1;
        bool preserveContents_ = false;
        int appliedDepthStencilFormat_ = 0;
    };

    /**
     * @brief One selected face of a `RenderTargetCube`, as something the renderer can bind. CNAEXT.
     */
    class IglRenderTargetCubeFace final : public IglBoundTarget
    {
    public:
        /**
         * @brief Binds a framebuffer to a face index.
         *
         * @param framebuffer      Framebuffer whose colour attachment is that face.
         * @param size             Face width and height in pixels.
         * @param face             Face index (0=+X, 1=-X, 2=+Y, 3=-Y, 4=+Z, 5=-Z).
         * @param sampleCount      Device-clamped sample count.
         * @param preserveContents Whether a bind must load the face's previous contents.
         */
        IglRenderTargetCubeFace(std::shared_ptr<igl::IFramebuffer> framebuffer, int size, int face,
                                int sampleCount, bool preserveContents);

        /** @brief Returns the framebuffer a render pass is opened against. */
        [[nodiscard]] const std::shared_ptr<igl::IFramebuffer>& GetFramebuffer() const override
        {
            return framebuffer_;
        }

        /** @brief Returns the face width in pixels. */
        [[nodiscard]] int GetTargetWidth() const override { return size_; }

        /** @brief Returns the face height in pixels. */
        [[nodiscard]] int GetTargetHeight() const override { return size_; }

        /** @brief Returns the device-clamped sample count of this face. */
        [[nodiscard]] int GetTargetSampleCount() const override { return sampleCount_; }

        /** @brief Returns whether a bind must load this face's previous contents. */
        [[nodiscard]] bool PreservesContents() const override { return preserveContents_; }

        /** @brief Returns the selected face index. */
        [[nodiscard]] int GetCubeFace() const override { return face_; }

    private:
        std::shared_ptr<igl::IFramebuffer> framebuffer_;
        int size_ = 0;
        int face_ = 0;
        int sampleCount_ = 1;
        bool preserveContents_ = false;
    };

    /**
     * @brief A cube-map render target living in IGL. CNAEXT.
     *
     * One cube colour texture shared by all six faces plus, matching FNA, one depth/stencil buffer
     * for the whole cube. Each face owns its own framebuffer so binding a face is a pointer swap
     * rather than an attachment edit.
     */
    class IglRenderTargetCubeRenderer final : public IRenderTargetCubeRenderer
    {
    public:
        /**
         * @brief Adopts the shared textures and the six per-face framebuffers.
         *
         * @param owner            Renderer that owns the device.
         * @param color            Shared cube colour texture.
         * @param depth            Shared depth/stencil texture, or null.
         * @param faces            Six framebuffers, one per face, in project face order.
         * @param size             Face width and height in pixels.
         * @param mipLevels        Mip levels of @p color.
         * @param sampleCount      Device-clamped sample count actually applied.
         * @param preserveContents Whether a bind must load a face's previous contents.
         */
        IglRenderTargetCubeRenderer(IglRenderer* owner,
                                    std::shared_ptr<igl::ITexture> color,
                                    std::shared_ptr<igl::ITexture> depth,
                                    std::array<std::shared_ptr<igl::IFramebuffer>, 6> faces,
                                    int size, int mipLevels, int sampleCount,
                                    bool preserveContents);

        ~IglRenderTargetCubeRenderer() override = default;

        /** @brief Returns the width and height of each face in pixels. */
        [[nodiscard]] int GetSize() const override { return size_; }

        /** @brief Returns the width and height of one face at mip level 0. */
        [[nodiscard]] int GetSizeEXT() const noexcept override { return size_; }

        /** @brief Returns the shared cube colour texture, so a later draw can sample it. */
        [[nodiscard]] const std::shared_ptr<igl::ITexture>& GetIglTexture() const { return color_; }

        /** @brief No-op: faces are bound through `SetRenderTargets`, not per-resource. */
        void BindAsRenderTargetFace(int /*face*/) override {}

        /** @brief No-op: see @ref BindAsRenderTargetFace. */
        void UnbindAsRenderTarget() override {}

        /** @brief Returns the device-clamped sample count this target was created with. */
        [[nodiscard]] int GetMultiSampleCount() const override { return sampleCount_; }

        /** @brief Returns whether real depth storage was allocated for this cube. */
        [[nodiscard]] bool HasRealDepthBuffer(bool depthFormatWasRequested) const override;

        /**
         * @brief Uploads CPU pixels into a face of the shared cube colour texture.
         *
         * `IRenderTargetCubeRenderer` refuses this by default because most renderers cannot write
         * into a rendered cube face. This one can: the colour texture is an ordinary sampleable cube
         * image, so seeding a face before rendering into it is a real upload rather than an accepted
         * and discarded one.
         *
         * @param face       Cube face index (0=+X, 1=-X, 2=+Y, 3=-Y, 4=+Z, 5=-Z).
         * @param level      Mip level to write.
         * @param x          Left edge of the region in texels.
         * @param y          Top edge of the region in texels.
         * @param w          Width of the region in texels.
         * @param h          Height of the region in texels.
         * @param data       Source pixels, tightly packed RGBA8 rows, top row first.
         * @param dataLength Size of @p data in bytes; at least `w * h * 4`.
         * @return True if the whole region was stored; false otherwise.
         */
        [[nodiscard]] bool SetData(int face, int level, int x, int y, int w, int h,
                                   const void* data, int dataLength) override;

        /**
         * @brief Reads back raw RGBA8 pixels from a rendered cube face.
         * @return True if the whole region was written; false if it could not be read.
         */
        [[nodiscard]] bool GetData(int face, int level, int x, int y, int w, int h,
                                   void* data, int dataLength) const override;

        /**
         * @brief Returns the bindable view of one face.
         *
         * @param face Face index (0=+X, 1=-X, 2=+Y, 3=-Y, 4=+Z, 5=-Z).
         * @return The face binding, or null when @p face is out of range.
         */
        [[nodiscard]] IglBoundTarget* GetFaceBinding(int face);

    private:
        IglRenderer* owner_ = nullptr;
        std::shared_ptr<igl::ITexture> color_;
        std::shared_ptr<igl::ITexture> depth_;
        int size_ = 0;
        int mipLevels_ = 1;
        std::array<std::shared_ptr<igl::IFramebuffer>, 6> faceFramebuffers_;
        std::array<std::unique_ptr<IglRenderTargetCubeFace>, 6> faceBindings_;
        int sampleCount_ = 1;
        bool preserveContents_ = false;
    };

    /**
     * @brief A multi-render-target binding of 2 to 4 `RenderTarget2D`s. CNAEXT.
     */
    class IglMultiRenderTarget final : public IglBoundTarget
    {
    public:
        /**
         * @brief Binds an ordered set of targets to one framebuffer.
         *
         * @param framebuffer      Framebuffer with one colour attachment per slot.
         * @param targets          The bound targets, in slot order.
         * @param width            Width in pixels of slot 0.
         * @param height           Height in pixels of slot 0.
         * @param sampleCount      Device-clamped sample count shared by every slot.
         * @param preserveContents Whether a bind must load previous contents.
         */
        IglMultiRenderTarget(std::shared_ptr<igl::IFramebuffer> framebuffer,
                             std::vector<IglRenderTargetRenderer*> targets,
                             int width, int height, int sampleCount, bool preserveContents);

        /** @brief Returns the framebuffer a render pass is opened against. */
        [[nodiscard]] const std::shared_ptr<igl::IFramebuffer>& GetFramebuffer() const override
        {
            return framebuffer_;
        }

        /** @brief Returns the width in pixels of slot 0. */
        [[nodiscard]] int GetTargetWidth() const override { return width_; }

        /** @brief Returns the height in pixels of slot 0. */
        [[nodiscard]] int GetTargetHeight() const override { return height_; }

        /** @brief Returns how many colour attachments this set binds. */
        [[nodiscard]] int GetColorAttachmentCount() const override
        {
            return static_cast<int>(targets_.size());
        }

        /** @brief Returns the sample count shared by every bound slot. */
        [[nodiscard]] int GetTargetSampleCount() const override { return sampleCount_; }

        /** @brief Returns whether a bind must load previous contents. */
        [[nodiscard]] bool PreservesContents() const override { return preserveContents_; }

        /** @brief Regenerates every bound target's mip chain. */
        void GenerateMipsAfterPass(igl::ICommandQueue& queue) override;

        /** @brief Returns the bound targets, in slot order. */
        [[nodiscard]] const std::vector<IglRenderTargetRenderer*>& GetTargets() const
        {
            return targets_;
        }

    private:
        std::shared_ptr<igl::IFramebuffer> framebuffer_;
        std::vector<IglRenderTargetRenderer*> targets_;
        int width_ = 0;
        int height_ = 0;
        int sampleCount_ = 1;
        bool preserveContents_ = false;
    };

    /**
     * @brief A compiled custom `ShaderEffect` program. CNAEXT.
     *
     * Holds the compiled `igl::IShaderStages` plus every uniform the game has set on it. IGL binds
     * uniforms per draw through the encoder rather than per program, so the values are recorded here
     * and replayed when a draw selects this effect.
     */
    class IglEffectRenderer final : public IEffectRenderer
    {
    public:
        /** @brief One recorded loose uniform: its GLSL type, element count and packed floats. */
        struct UniformValue
        {
            /** @brief The GLSL type the value is bound as. */
            igl::UniformType type = igl::UniformType::Float;
            /** @brief Array element count; 1 for a scalar, vector or single matrix. */
            int numElements = 1;
            /** @brief The value itself, packed as floats (ints are stored bit-exact). */
            std::vector<float> data;
        };

        /**
         * @brief Binds this effect renderer to the renderer that will compile and draw with it.
         *
         * @param owner Renderer owning the IGL device.
         */
        explicit IglEffectRenderer(IglRenderer& owner);

        ~IglEffectRenderer() override = default;

        /**
         * @brief Compiles the program from GLSL source.
         *
         * @param vertSrc Vertex shader source.
         * @param fragSrc Fragment shader source.
         * @return True on success; false when compilation failed (see @ref GetCompileError).
         */
        bool CompileProgram(const std::string& vertSrc, const std::string& fragSrc) override;

        /** @brief Makes this the program subsequent draws select. */
        void Bind() override;

        /** @brief Restores the renderer's built-in effect shader. */
        void Unbind() override;

        /** @brief Returns whether the program compiled successfully. */
        [[nodiscard]] bool IsValid() const override { return stages_ != nullptr; }

        /** @brief Returns the last compilation error, or an empty string. */
        [[nodiscard]] std::string GetCompileError() const override { return compileError_; }

        /**
         * @brief Sets a float uniform.
         * @param name  Uniform name.
         * @param value New value.
         */
        void SetUniformFloat(const char* name, float value) override;

        /**
         * @brief Sets an int uniform.
         * @param name  Uniform name.
         * @param value New value.
         */
        void SetUniformInt(const char* name, int value) override;

        /**
         * @brief Sets a `vec2` uniform.
         * @param name Uniform name.
         * @param x    First component.
         * @param y    Second component.
         */
        void SetUniformVec2(const char* name, float x, float y) override;

        /**
         * @brief Sets a `vec3` uniform.
         * @param name Uniform name.
         * @param x    First component.
         * @param y    Second component.
         * @param z    Third component.
         */
        void SetUniformVec3(const char* name, float x, float y, float z) override;

        /**
         * @brief Sets a `vec4` uniform.
         * @param name Uniform name.
         * @param x    First component.
         * @param y    Second component.
         * @param z    Third component.
         * @param w    Fourth component.
         */
        void SetUniformVec4(const char* name, float x, float y, float z, float w) override;

        /**
         * @brief Sets a column-major 4x4 matrix uniform.
         * @param name   Uniform name.
         * @param matrix Sixteen floats, column-major.
         */
        void SetUniformMat4(const char* name, const float* matrix) override;

        /**
         * @brief Sets a float array uniform.
         * @param name   Uniform name.
         * @param values Values.
         * @param count  Number of scalar elements in @p values.
         */
        void SetUniformFloatArray(const char* name, const float* values, int count) override;

        /**
         * @brief Sets a `vec2` array uniform.
         * @param name   Uniform name.
         * @param values `count * 2` floats.
         * @param count  Number of `vec2` elements.
         */
        void SetUniformVec2Array(const char* name, const float* values, int count) override;

        /**
         * @brief Binds a 2D texture to a sampler unit.
         * @param unit    Zero-based texture unit.
         * @param texture Texture to bind, or null to clear the unit.
         */
        void BindTexture(int unit, ITextureRenderer* texture) override;

        /**
         * @brief Binds a cube texture to a sampler unit.
         * @param unit    Zero-based texture unit.
         * @param texture Texture to bind, or null to clear the unit.
         */
        void BindTextureCube(int unit, ITextureCubeRenderer* texture) override;

        /**
         * @brief Binds a volume texture to a sampler unit.
         * @param unit    Zero-based texture unit.
         * @param texture Texture to bind, or null to clear the unit.
         */
        void BindTexture3D(int unit, ITexture3DRenderer* texture) override;

        /** @brief Returns the compiled program, or null when compilation failed. */
        [[nodiscard]] const std::shared_ptr<igl::IShaderStages>& GetStages() const { return stages_; }

        /** @brief Returns the identity used to key pipelines on this program. */
        [[nodiscard]] std::uint64_t GetProgramId() const { return programId_; }

        /** @brief Returns the textures this effect wants bound, by unit. */
        [[nodiscard]] const std::array<igl::ITexture*, igl::IGL_TEXTURE_SAMPLERS_MAX>& GetBoundTextures() const
        {
            return boundTextures_;
        }

        /** @brief Returns the recorded loose uniform values, by name. */
        [[nodiscard]] const std::unordered_map<std::string, UniformValue>& GetUniforms() const
        {
            return uniforms_;
        }

    private:
        void RecordUniform(const char* name, igl::UniformType type, int numElements,
                           const float* values, int floatCount);

        IglRenderer& owner_;
        std::shared_ptr<igl::IShaderStages> stages_;
        std::string compileError_;
        std::uint64_t programId_ = 0;
        std::unordered_map<std::string, UniformValue> uniforms_;
        std::array<igl::ITexture*, igl::IGL_TEXTURE_SAMPLERS_MAX> boundTextures_{};
    };

    /** @brief A GPU occlusion query. CNAEXT. */
    class IglOcclusionQueryRenderer final : public IOcclusionQueryRenderer
    {
    public:
        /**
         * @brief Creates a query bound to a renderer.
         * @param owner Renderer that will service the query.
         */
        explicit IglOcclusionQueryRenderer(IglRenderer& owner);

        /** @brief Starts counting samples. */
        void Begin() override;

        /** @brief Stops counting samples. */
        void End() override;

        /** @brief Returns whether a result is available. */
        [[nodiscard]] bool IsComplete() const override { return complete_; }

        /** @brief Returns the number of samples that passed. */
        [[nodiscard]] int PixelCount() const override { return pixelCount_; }

    private:
        IglRenderer& owner_;
        bool complete_ = false;
        int pixelCount_ = 0;
    };

    /** @brief One queued sprite quad's four vertices, in the renderer's own 2D vertex layout. */
    struct IglSpriteVertex
    {
        /** @brief Position in logical game pixels; z carries layer depth. */
        float position[3] = {0, 0, 0};
        /** @brief Tint, RGBA8. */
        std::uint8_t color[4] = {255, 255, 255, 255};
        /** @brief Texture coordinate. */
        float texCoord[2] = {0, 0};
    };

    /** @brief 2D drawing for `Microsoft::Xna::Framework::Graphics::SpriteBatch`. CNAEXT. */
    class IglSpriteBatchRenderer final : public ISpriteBatchRenderer
    {
    public:
        /**
         * @brief Binds this batch to the renderer that draws its sprites.
         * @param owner Renderer owning the device and the frame's command buffer.
         */
        explicit IglSpriteBatchRenderer(IglRenderer& owner);

        /** @brief Resets the per-block sampler, transform and effect state. */
        void Begin() override;

        /** @brief Flushes whatever the block queued. */
        void End() override;

        /**
         * @brief Sets the transform applied on top of the 2D projection.
         * @param m Transform matrix, in XNA row-vector convention.
         */
        void SetTransformMatrix(const Matrix& m) override;

        /**
         * @brief Sets the texture filter used by subsequent draws.
         * @param textureFilter Raw `TextureFilter` ordinal.
         */
        void SetSamplerFilter(int textureFilter) override;

        /**
         * @brief Sets the addressing modes used by subsequent draws.
         * @param addressU Raw `TextureAddressMode` ordinal for U.
         * @param addressV Raw `TextureAddressMode` ordinal for V.
         */
        void SetSamplerAddressMode(int addressU, int addressV) override;

        /**
         * @brief Activates (or clears) the custom effect this block's draws go through.
         * @param effect Custom effect to apply, or null to restore the stock sprite shader.
         */
        void SetCustomEffect(Effect* effect) override;

        /**
         * @brief Records that this block is `SpriteSortMode::Immediate`.
         * @param immediate True when immediate mode is active.
         */
        void SetImmediateMode(bool immediate) override;

        /**
         * @brief Draws a whole texture at a position, unscaled and untinted.
         * @param texture Texture to draw.
         * @param x       Left edge in logical pixels.
         * @param y       Top edge in logical pixels.
         */
        void Draw(const ITextureRenderer& texture, float x, float y) override;

        /**
         * @brief Draws a source region into a destination rectangle with a tint.
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
         * @param texture              Texture to draw.
         * @param destinationRectangle Destination in logical pixels.
         * @param sourceRectangle      Source region in texels.
         * @param color                Tint multiplied with the sampled texel.
         * @param rotation             Rotation in radians about @p origin.
         * @param origin               Rotation origin in source texels.
         * @param effects              Horizontal/vertical flip flags.
         * @param layerDepth           Layer depth, written into the vertex Z.
         */
        void Draw(const ITextureRenderer& texture,
                  const Rectangle& destinationRectangle,
                  const Rectangle& sourceRectangle,
                  const Color& color,
                  float rotation,
                  const Vector2& origin,
                  SpriteEffects effects,
                  float layerDepth) override;

        /** @brief Submits everything queued since the last flush. */
        void Flush();

    private:
        void QueueQuad(const ITextureRenderer& texture,
                       const Rectangle& destinationRectangle,
                       const Rectangle& sourceRectangle,
                       const Color& color,
                       float rotation,
                       const Vector2& origin,
                       SpriteEffects effects,
                       float layerDepth);

        IglRenderer& owner_;
        Matrix transform_;
        const ITextureRenderer* batchTexture_ = nullptr;
        Effect* customEffect_ = nullptr;
        std::vector<IglSpriteVertex> vertices_;
        std::vector<std::uint16_t> indices_;
        int textureFilter_ = 0;
        int addressU_ = 1;
        int addressV_ = 1;
        bool immediate_ = false;
        bool inBlock_ = false;
    };

    /**
     * @brief CNA graphics renderer built on IGL (facebook/igl). CNAEXT.
     *
     * Like `LlglRenderer`, this names no native graphics API: IGL is itself an abstraction and the
     * backend it drives -- OpenGL through GLX, or Vulkan -- is fixed for the process by
     * `Detail::ResolveRendererBackend()`.
     *
     * The frame model is a lazily opened render pass. A clear ends whatever pass is open and starts
     * a new one whose load actions carry the clear values; a draw joins the open pass, opening one
     * that loads previous contents when none is. `Present()` closes the pass, presents and submits.
     * That keeps the renderer's own bookkeeping small while still expressing exactly the
     * load/store semantics a Vulkan-shaped API needs.
     */
    class IglRenderer final : public IGraphicsRenderer
    {
    public:
        /**
         * @brief Brings up the IGL device, its swap surface and the built-in shader pipeline.
         *
         * @param args Window, virtual resolution, presentation mode, sample count and swap interval
         *             requested by `GraphicsDevice`.
         * @throws std::runtime_error If no backend can be brought up on this window, or a GPU
         *         resource fails to be created.
         */
        explicit IglRenderer(const GraphicsRendererCreateArgs& args);

        /** @brief Releases every GPU resource and tears the device down. */
        ~IglRenderer() override;

        IglRenderer(const IglRenderer&) = delete;
        IglRenderer& operator=(const IglRenderer&) = delete;

        // ---- Presentation ----

        /**
         * @brief Clears the bound target's colour.
         * @param r,g,b,a Clear colour components in the range 0..1.
         */
        void Clear(float r, float g, float b, float a) override;

        /**
         * @brief Clears colour and depth.
         * @param r,g,b,a Clear colour components in the range 0..1.
         * @param depth   Depth value to clear with.
         */
        void ClearColorAndDepth(float r, float g, float b, float a, float depth) override;

        /**
         * @brief Clears depth only.
         * @param depth Depth value to clear with.
         */
        void ClearDepth(float depth) override;

        /**
         * @brief Clears stencil only.
         * @param stencil Stencil value to clear with.
         */
        void ClearStencil(int stencil) override;

        /**
         * @brief Clears depth and stencil.
         * @param depth   Depth value to clear with.
         * @param stencil Stencil value to clear with.
         */
        void ClearDepthAndStencil(float depth, int stencil) override;

        /**
         * @brief Clears colour and stencil.
         * @param r,g,b,a Clear colour components in the range 0..1.
         * @param stencil Stencil value to clear with.
         */
        void ClearColorAndStencil(float r, float g, float b, float a, int stencil) override;

        /**
         * @brief Clears colour, depth and stencil.
         * @param r,g,b,a Clear colour components in the range 0..1.
         * @param depth   Depth value to clear with.
         * @param stencil Stencil value to clear with.
         */
        void ClearColorDepthAndStencil(float r, float g, float b, float a, float depth,
                                       int stencil) override;

        /** @brief Ends the frame's last pass, presents the back buffer and submits the frame. */
        void Present() override;

        /**
         * @brief Reports the logical (virtual-resolution) size games draw into.
         * @param width  Receives the logical width in pixels.
         * @param height Receives the logical height in pixels.
         */
        void GetViewportSize(int& width, int& height) override;

        /**
         * @brief Reports the physical rectangle the logical surface is presented into.
         * @param x      Receives the left edge in drawable pixels.
         * @param y      Receives the top edge in drawable pixels.
         * @param width  Receives the width in drawable pixels.
         * @param height Receives the height in drawable pixels.
         */
        void GetDefaultViewportRect(int& x, int& y, int& width, int& height) override;

        /**
         * @brief Refreshes size and density, and resizes the swap chain when the drawable changed.
         * @param surface New platform surface snapshot.
         */
        void OnSurfaceChanged(const RendererSurfaceInfo& surface) override;

        /**
         * @brief Changes the logical resolution at runtime.
         * @param width  New logical width in pixels.
         * @param height New logical height in pixels.
         */
        void SetVirtualResolution(int width, int height) override;

        /**
         * @brief Changes how the logical resolution is fitted onto the window.
         * @param mode Raw `CnaPresentationMode` ordinal.
         */
        void SetPresentationMode(int mode) override;

        /**
         * @brief Changes the swap interval.
         * @param interval 0 for immediate, 1 for vsync, 2 for half refresh rate.
         */
        void SetSwapInterval(int interval) override;

        /** @brief Returns the back buffer's real, device-granted MSAA sample count; 0 when none. */
        [[nodiscard]] int GetMultiSampleCount() const override;

        /**
         * @brief Reports the sample count that could actually be applied to the back buffer.
         * @param requestedMultiSampleCount Requested count.
         * @return The count actually in effect.
         */
        int ApplyMultiSampleCount(int requestedMultiSampleCount) override;

        /**
         * @brief Converts a point from window pixels to logical game coordinates.
         * @param windowX Window-space X in logical client units.
         * @param windowY Window-space Y in logical client units.
         * @param logX    Receives the logical X.
         * @param logY    Receives the logical Y.
         * @return True when the conversion was performed.
         */
        bool TransformWindowToLogical(float windowX, float windowY,
                                      float& logX, float& logY) const override;

        /**
         * @brief Converts a point from logical game coordinates to window client units.
         * @param logX    Logical X.
         * @param logY    Logical Y.
         * @param windowX Receives the window-space X.
         * @param windowY Receives the window-space Y.
         * @return True when the conversion was performed.
         */
        bool TransformLogicalToWindow(float logX, float logY,
                                      float& windowX, float& windowY) const override;

        // ---- Resources ----

        /**
         * @brief Creates a 2D texture from RGBA8 pixels.
         * @param data Image dimensions, mip level count and pixel data.
         * @return The new texture renderer.
         */
        std::unique_ptr<ITextureRenderer> CreateTexture(const ImageData& data) override;

        /**
         * @brief Creates a cube texture.
         * @param size          Width and height in pixels of each square face.
         * @param mipMap        Whether to allocate a full mip chain.
         * @param surfaceFormat Requested `SurfaceFormat` ordinal.
         * @return The new cube texture renderer.
         */
        std::unique_ptr<ITextureCubeRenderer> CreateTextureCube(int size, bool mipMap,
                                                                int surfaceFormat) override;

        /**
         * @brief Creates a volume texture.
         * @param w             Width in voxels.
         * @param h             Height in voxels.
         * @param depth         Depth in voxels.
         * @param mipMap        Whether to allocate a full mip chain.
         * @param surfaceFormat Requested `SurfaceFormat` ordinal.
         * @return The new volume texture renderer.
         */
        std::unique_ptr<ITexture3DRenderer> CreateTexture3D(int w, int h, int depth, bool mipMap,
                                                            int surfaceFormat) override;

        /** @brief Creates a sprite batch bound to this renderer. */
        std::unique_ptr<ISpriteBatchRenderer> CreateSpriteBatch() override;

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
         * @brief Creates an occlusion query.
         * @return The new query renderer.
         */
        std::unique_ptr<IOcclusionQueryRenderer> CreateOcclusionQuery() override;

        /**
         * @brief Creates an off-screen `RenderTarget2D`.
         * @param w                Width in pixels.
         * @param h                Height in pixels.
         * @param depthFormat      Raw `DepthFormat` ordinal.
         * @param preserveContents Whether a bind must load the target's previous contents.
         * @param mipMap           Whether to allocate and regenerate a full mip chain.
         * @param multiSampleCount Requested MSAA sample count; 0 or 1 means none.
         * @return The new render target renderer.
         * @throws std::runtime_error If the dimensions are invalid or a GPU resource fails.
         */
        std::unique_ptr<IRenderTargetRenderer> CreateRenderTarget2D(
            int w, int h, int depthFormat, bool preserveContents = false, bool mipMap = false,
            int multiSampleCount = 0) override;

        /**
         * @brief Creates an off-screen `RenderTarget2D` with an explicit surface format.
         * @param w                Width in pixels.
         * @param h                Height in pixels.
         * @param depthFormat      Raw `DepthFormat` ordinal.
         * @param preserveContents Whether a bind must load the target's previous contents.
         * @param mipMap           Whether to allocate and regenerate a full mip chain.
         * @param multiSampleCount Requested MSAA sample count; 0 or 1 means none.
         * @param surfaceFormat    Raw `SurfaceFormat` ordinal.
         * @return The new render target renderer.
         */
        std::unique_ptr<IRenderTargetRenderer> CreateRenderTarget2DEXT(
            int w, int h, int depthFormat, bool preserveContents, bool mipMap,
            int multiSampleCount, int surfaceFormat) override;

        /**
         * @brief Creates a cube-map render target.
         * @param size             Face width and height in pixels.
         * @param depthFormat      Raw `DepthFormat` ordinal.
         * @param preserveContents Whether a bind must load a face's previous contents.
         * @param mipMap           Whether to allocate a full mip chain.
         * @param multiSampleCount Requested MSAA sample count; 0 or 1 means none.
         * @return The new render target cube renderer.
         */
        std::unique_ptr<IRenderTargetCubeRenderer> CreateRenderTargetCube(
            int size, int depthFormat, bool preserveContents = false, bool mipMap = false,
            int multiSampleCount = 0) override;

        /**
         * @brief Creates a custom `ShaderEffect` renderer.
         * @param vertSrc GLSL vertex shader source.
         * @param fragSrc GLSL fragment shader source.
         * @return The new effect renderer; never null.
         */
        std::unique_ptr<IEffectRenderer> CreateEffectRenderer(const std::string& vertSrc,
                                                              const std::string& fragSrc) override;

        /**
         * @brief Activates a `RenderTarget2D`, or restores the back buffer.
         * @param rt Target to draw into, or null.
         */
        void SetRenderTarget2D(IRenderTargetRenderer* rt) override;

        /**
         * @brief Binds a normalized ordered render-target set.
         * @param renderTargets Ordered target descriptors, or null.
         * @param count         Number of descriptors; 0 restores the back buffer.
         * @throws std::runtime_error If the combination cannot be expressed by this renderer.
         */
        void SetRenderTargets(const RenderTargetBindingDescriptor* renderTargets, int count) override;

        /**
         * @brief Reads rendered back-buffer pixels.
         * @param x      Left edge of the region in logical pixels.
         * @param y      Top edge of the region in logical pixels.
         * @param w      Width of the region in logical pixels.
         * @param h      Height of the region in logical pixels.
         * @param pixels Destination for `w * h * 4` bytes of RGBA8, top row first.
         * @throws std::runtime_error If the copy could not be performed.
         */
        void ReadBackbuffer(int x, int y, int w, int h, std::uint8_t* pixels) override;

        // ---- Graphics state ----

        /**
         * @brief Applies a `BlendState`.
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
         * @param r,g,b,a Blend factor components in the range 0..1.
         */
        void SetBlendFactor(float r, float g, float b, float a) override;

        /**
         * @brief Applies a `DepthStencilState`.
         * @param depthEnable         Whether depth testing is enabled.
         * @param depthWriteEnable    Whether depth writes are enabled.
         * @param depthFunc           Raw `CompareFunction` ordinal for the depth test.
         * @param stencilEnable       Whether stencil testing is enabled.
         * @param stencilFunc         Raw `CompareFunction` ordinal for the stencil test.
         * @param stencilPass         Raw `StencilOperation` applied when the test passes.
         * @param stencilFail         Raw `StencilOperation` applied when the test fails.
         * @param stencilDepthFail    Raw `StencilOperation` for a depth failure.
         * @param stencilMask         Stencil read mask.
         * @param stencilWriteMask    Stencil write mask.
         * @param referenceStencil    Stencil reference value.
         * @param twoSidedStencilMode Whether the counter-clockwise face has its own state.
         * @param ccwStencilFunc      Counter-clockwise `CompareFunction` ordinal.
         * @param ccwStencilPass      Counter-clockwise pass operation ordinal.
         * @param ccwStencilFail      Counter-clockwise fail operation ordinal.
         * @param ccwStencilDepthFail Counter-clockwise depth-fail operation ordinal.
         */
        void ApplyDepthStencilState(bool depthEnable, bool depthWriteEnable, int depthFunc,
                                    bool stencilEnable, int stencilFunc,
                                    int stencilPass, int stencilFail, int stencilDepthFail,
                                    int stencilMask, int stencilWriteMask, int referenceStencil,
                                    bool twoSidedStencilMode,
                                    int ccwStencilFunc, int ccwStencilPass,
                                    int ccwStencilFail, int ccwStencilDepthFail) override;

        /**
         * @brief Records `GraphicsDevice.ReferenceStencil` independently of a full state change.
         * @param value New reference stencil value.
         */
        void SetReferenceStencil(int value) override;

        /**
         * @brief Applies a `RasterizerState`.
         * @param cullMode            Raw `CullMode` ordinal.
         * @param fillMode            Raw `FillMode` ordinal.
         * @param scissorTestEnable   Whether the scissor test is enabled.
         * @param depthBias           Constant depth bias.
         * @param slopeScaleDepthBias Slope-scaled depth bias.
         */
        void ApplyRasterizerState(int cullMode, int fillMode, bool scissorTestEnable,
                                  float depthBias, float slopeScaleDepthBias) override;

        /**
         * @brief Applies a `SamplerState` to a texture slot.
         * @param slot          Texture slot index.
         * @param filter        Raw `TextureFilter` ordinal.
         * @param addressU      Raw `TextureAddressMode` ordinal for U.
         * @param addressV      Raw `TextureAddressMode` ordinal for V.
         * @param maxAnisotropy Maximum anisotropy, 1 to 16.
         */
        void ApplySamplerState(int slot, int filter, int addressU, int addressV,
                               int maxAnisotropy) override;

        /**
         * @brief Applies the sampler controls that filter and address mode do not cover.
         * @param slot        Texture slot index.
         * @param maxMipLevel Highest-resolution mip level that may be sampled.
         * @param lodBias     Mip level-of-detail bias.
         */
        void ApplySamplerMipState(int slot, int maxMipLevel, float lodBias) override;

        /**
         * @brief Sets the scissor clip rectangle in logical coordinates.
         * @param x Left edge.
         * @param y Top edge.
         * @param w Width.
         * @param h Height.
         */
        void SetScissorRect(int x, int y, int w, int h) override;

        /**
         * @brief Sets the viewport rectangle and depth range in logical coordinates.
         * @param x        Left edge.
         * @param y        Top edge.
         * @param w        Width.
         * @param h        Height.
         * @param minDepth Minimum depth.
         * @param maxDepth Maximum depth.
         */
        void SetViewport(int x, int y, int w, int h, float minDepth, float maxDepth) override;

        /**
         * @brief Enables or disables depth testing.
         * @param enabled New state.
         */
        void SetDepthTestEnabled(bool enabled) override;

        /**
         * @brief Enables or disables blending.
         * @param enabled New state.
         */
        void SetBlendEnabled(bool enabled) override;

        /**
         * @brief Enables or disables depth writes.
         * @param enabled New state.
         */
        void SetDepthWriteEnabled(bool enabled) override;

        // ---- 3D drawing ----

        /**
         * @brief Draws colour-only primitives with the built-in effect shader.
         * @param vb             Vertex buffer to read from.
         * @param world          World matrix.
         * @param view           View matrix.
         * @param projection     Projection matrix.
         * @param primitive      Primitive topology.
         * @param primitiveCount Number of primitives.
         */
        void DrawColoredPrimitives(const IVertexBufferRenderer& vb,
                                   const Matrix& world, const Matrix& view,
                                   const Matrix& projection,
                                   PrimitiveType primitive, int primitiveCount) override;

        /**
         * @brief Indexed counterpart of @ref DrawColoredPrimitives.
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
                                          const Matrix& world, const Matrix& view,
                                          const Matrix& projection,
                                          PrimitiveType primitive, int primitiveCount) override;

        /**
         * @brief Effect-aware draw.
         * @param vb             Vertex buffer named by `params.vertexStreams[0]`.
         * @param world          World matrix.
         * @param view           View matrix.
         * @param projection     Projection matrix.
         * @param primitive      Primitive topology.
         * @param primitiveCount Number of primitives.
         * @param params         Per-draw effect parameters.
         */
        void DrawPrimitivesEx(const IVertexBufferRenderer& vb,
                              const Matrix& world, const Matrix& view, const Matrix& projection,
                              PrimitiveType primitive, int primitiveCount,
                              const GpuDrawParams& params) override;

        /**
         * @brief Indexed counterpart of @ref DrawPrimitivesEx.
         * @param vb             Vertex buffer named by `params.vertexStreams[0]`.
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
                                     const Matrix& world, const Matrix& view,
                                     const Matrix& projection,
                                     PrimitiveType primitive, int primitiveCount,
                                     const GpuDrawParams& params) override;

        /**
         * @brief Instanced indexed draw.
         * @param vb             Per-vertex buffer named by `params.vertexStreams[0]`.
         * @param ib             Index buffer to read from.
         * @param world          World matrix.
         * @param view           View matrix.
         * @param projection     Projection matrix.
         * @param primitive      Primitive topology.
         * @param primitiveCount Number of primitives per instance.
         * @param instanceCount  Number of instances.
         * @param params         Per-draw effect parameters, including the instance stream.
         */
        void DrawInstancedPrimitivesEx(const IVertexBufferRenderer& vb,
                                       const IIndexBufferRenderer& ib,
                                       const Matrix& world, const Matrix& view,
                                       const Matrix& projection,
                                       PrimitiveType primitive, int primitiveCount,
                                       int instanceCount, const GpuDrawParams& params) override;

        // ---- Capability reporting ----

        /**
         * @brief Reports whether this renderer supports a capability.
         * @param capability Capability to test.
         * @return True when supported by this renderer and the running device.
         */
        [[nodiscard]] bool SupportsCapability(CNA::GraphicsCapability capability) const override;

        /** @brief Returns whether the presented surface has a depth/stencil buffer. */
        [[nodiscard]] bool SupportsDepthStencil() const override;

        /** @brief Returns the largest single-axis texture dimension the device accepts. */
        [[nodiscard]] int GetMaxTextureDimension() const override;

        /**
         * @brief Inserts a named GPU debug label into the command stream.
         * @param marker Label text.
         */
        void SetStringMarkerEXT(const char* marker) override;

        // ---- Renderer-internal services used by this family's resource classes ----

        /** @brief Returns the IGL device every resource is created from. CNAEXT. */
        [[nodiscard]] igl::IDevice& GetDevice() const { return surface_->GetDevice(); }

        /** @brief Returns the queue this renderer submits through. CNAEXT. */
        [[nodiscard]] igl::ICommandQueue& GetCommandQueue() const { return surface_->GetCommandQueue(); }

        /** @brief Returns which backend IGL actually brought up. CNAEXT. */
        [[nodiscard]] Detail::RendererBackend GetBackend() const { return surface_->GetBackend(); }

        /** @brief Returns true when the Vulkan backend is active. CNAEXT. */
        [[nodiscard]] bool IsVulkanBackend() const
        {
            return surface_->GetBackend() == Detail::RendererBackend::Vulkan;
        }

        /**
         * @brief Ends any open pass and submits the frame recorded so far. CNAEXT.
         *
         * A readback, or an upload into a buffer an already-recorded draw references, must see the
         * GPU work that preceded it. This is a no-op when nothing has been recorded.
         */
        void FlushPendingFrameEXT();

        /** @brief Returns the next monotonically increasing program identity. CNAEXT. */
        [[nodiscard]] std::uint64_t NextProgramIdEXT() { return ++nextProgramId_; }

        /**
         * @brief Draws a batch of sprite quads. CNAEXT.
         *
         * @param vertices     Four vertices per quad, in submission order.
         * @param indices      Six indices per quad.
         * @param texture      Source texture, or null for an untextured batch.
         * @param filter       Raw `TextureFilter` ordinal.
         * @param addressU     Raw `TextureAddressMode` ordinal for U.
         * @param addressV     Raw `TextureAddressMode` ordinal for V.
         * @param transform    Transform applied on top of the 2D projection.
         * @param customEffect Custom effect bound for this batch, or null.
         */
        void DrawSpriteBatchEXT(const std::vector<IglSpriteVertex>& vertices,
                                const std::vector<std::uint16_t>& indices,
                                const ITextureRenderer* texture,
                                int filter, int addressU, int addressV,
                                const Matrix& transform,
                                Effect* customEffect);

    private:
        friend class IglSpriteBatchRenderer;
        friend class IglEffectRenderer;
        friend class IglOcclusionQueryRenderer;

        /** @brief Everything that identifies one cached `igl::IRenderPipelineState`. */
        struct PipelineKey
        {
            std::uint64_t vertexInputId = 0;
            std::uint64_t programId = 0;
            std::uint32_t attributeMask = 0;
            std::uint8_t topology = 0;
            std::uint8_t colorAttachmentCount = 1;
            std::uint8_t cullMode = 0;
            std::uint8_t fillMode = 0;
            std::uint8_t sampleCount = 1;
            std::uint8_t blendEnabled = 0;
            std::uint8_t srcRgb = 0, dstRgb = 0, srcAlpha = 0, dstAlpha = 0;
            std::uint8_t opRgb = 0, opAlpha = 0;
            std::array<std::uint8_t, 4> colorWriteMask{15, 15, 15, 15};
            std::array<std::uint8_t, 4> colorFormat{};
            std::uint8_t depthFormat = 0;
            std::uint8_t stencilFormat = 0;

            /** @brief Value equality over every field. */
            bool operator==(const PipelineKey& other) const noexcept;
        };

        /** @brief Hashes a @ref PipelineKey by folding its bytes. */
        struct PipelineKeyHash
        {
            /** @brief Returns the hash of @p key. */
            [[nodiscard]] std::size_t operator()(const PipelineKey& key) const noexcept;
        };

        /** @brief XNA's `DepthStencilState`, as this renderer tracks it. */
        struct DepthStencilTracking
        {
            bool depthEnable = false;
            bool depthWriteEnable = false;
            int depthFunc = 2;
            bool stencilEnable = false;
            int stencilFunc = 7;
            int stencilPass = 0, stencilFail = 0, stencilDepthFail = 0;
            int stencilMask = -1, stencilWriteMask = -1, referenceStencil = 0;
            bool twoSidedStencilMode = false;
            int ccwStencilFunc = 7;
            int ccwStencilPass = 0, ccwStencilFail = 0, ccwStencilDepthFail = 0;

            /** @brief Value equality over every field. */
            bool operator==(const DepthStencilTracking& other) const noexcept;
        };

        /** @brief XNA's `SamplerState` for one slot. */
        struct SamplerTracking
        {
            int filter = 0;
            int addressU = 1;
            int addressV = 1;
            int maxAnisotropy = 1;
            int maxMipLevel = 0;
            float lodBias = 0.0f;

            /** @brief Value equality over every field. */
            bool operator==(const SamplerTracking& other) const noexcept;
        };

        // Frame and pass management.
        void BeginFrameIfNeeded();
        void BeginPass(bool clearColor, bool clearDepth, bool clearStencil);
        void EndPass();
        void EnsurePassOpen();
        void SubmitFrame(bool present);
        void ApplyPassViewportAndScissor();

        // Target management.
        [[nodiscard]] IglBoundTarget& CurrentTarget();
        void RebuildBackBufferTarget();

        // Pipeline and state caches.
        [[nodiscard]] std::shared_ptr<igl::IVertexInputState> AcquireVertexInputState(
            const std::vector<igl::VertexAttribute>& attributes,
            const std::vector<igl::VertexInputBinding>& bindings,
            std::uint64_t& outId);
        [[nodiscard]] std::shared_ptr<igl::IShaderStages> AcquireBuiltInShader(
            std::uint32_t attributeMask, int colorAttachmentCount);
        [[nodiscard]] std::shared_ptr<igl::IRenderPipelineState> AcquirePipeline(
            const PipelineKey& key,
            const std::shared_ptr<igl::IVertexInputState>& vertexInput,
            const std::shared_ptr<igl::IShaderStages>& stages);
        [[nodiscard]] std::shared_ptr<igl::IDepthStencilState> AcquireDepthStencilState();
        [[nodiscard]] std::shared_ptr<igl::ISamplerState> AcquireSamplerState(int slot);

        // Draw plumbing shared by every 3D route.
        void SubmitDraw(const IglVertexBufferRenderer& vb,
                        const IglIndexBufferRenderer* ib,
                        const Matrix& world, const Matrix& view, const Matrix& projection,
                        PrimitiveType primitive, int primitiveCount, int instanceCount,
                        const GpuDrawParams& params);
        void FillEffectUniforms(const Matrix& world, const Matrix& view, const Matrix& projection,
                                const GpuDrawParams& params, IglEffectUniforms& uniforms);
        void BindEffectResources(igl::IRenderCommandEncoder& encoder,
                                 const GpuDrawParams& params,
                                 const IglEffectUniforms& uniforms,
                                 const IglBoneUniforms* bones);
        void ApplyCustomEffectUniforms(igl::IRenderCommandEncoder& encoder,
                                       const igl::IRenderPipelineState& pipeline,
                                       const IglEffectRenderer& effect);
        [[nodiscard]] static bool DescribeStrideLayoutForDraw(
            std::size_t stride, std::vector<igl::VertexAttribute>& attributes,
            std::uint32_t& attributeMask);
        [[nodiscard]] igl::ITexture* ResolveDummyTexture(bool cube);

        std::unique_ptr<IglPlatformSurface> surface_;

        // Frame state.
        std::shared_ptr<igl::ICommandBuffer> commandBuffer_;
        std::unique_ptr<igl::IRenderCommandEncoder> encoder_;
        bool frameRecording_ = false;
        bool passOpen_ = false;
        bool backBufferAcquired_ = false;
        int frameIndex_ = 0;

        // Pending clear values applied by the next opened pass.
        float clearColor_[4] = {0, 0, 0, 1};
        float clearDepth_ = 1.0f;
        int clearStencil_ = 0;

        // Presentation.
        int virtualWidth_ = 0;
        int virtualHeight_ = 0;
        int logicalWidth_ = 0;
        int logicalHeight_ = 0;
        CnaPresentationMode presentationMode_ = CnaPresentationMode::FixedHeightDynamicWidth;
        int presentX_ = 0, presentY_ = 0, presentWidth_ = 0, presentHeight_ = 0;

        // Bound targets.
        std::unique_ptr<IglBoundTarget> backBufferTarget_;
        IglBoundTarget* boundTarget_ = nullptr;
        std::unique_ptr<IglMultiRenderTarget> multiRenderTarget_;

        // XNA state.
        int blendColorSrc_ = 1, blendAlphaSrc_ = 1, blendColorDst_ = 0, blendAlphaDst_ = 0;
        int blendColorFunc_ = 0, blendAlphaFunc_ = 0;
        BlendWriteState blendWriteState_;
        bool blendEnabled_ = false;
        float blendFactor_[4] = {1, 1, 1, 1};
        DepthStencilTracking depthStencil_;
        int cullMode_ = 0;
        int fillMode_ = 0;
        bool scissorEnabled_ = false;
        float depthBias_ = 0.0f;
        float slopeScaleDepthBias_ = 0.0f;
        int scissorRect_[4] = {0, 0, 0, 0};
        bool viewportSet_ = false;
        int viewportRect_[4] = {0, 0, 0, 0};
        float viewportMinDepth_ = 0.0f;
        float viewportMaxDepth_ = 1.0f;
        std::array<SamplerTracking, kIglTrackedSamplerSlots> samplers_;

        // Caches.
        std::unordered_map<std::uint64_t, std::shared_ptr<igl::IVertexInputState>> vertexInputStates_;
        std::unordered_map<std::uint64_t, std::shared_ptr<igl::IShaderStages>> builtInShaders_;
        std::unordered_map<PipelineKey, std::shared_ptr<igl::IRenderPipelineState>, PipelineKeyHash>
            pipelines_;
        std::unordered_map<std::uint64_t, std::shared_ptr<igl::IDepthStencilState>> depthStencilStates_;
        std::unordered_map<std::uint64_t, std::shared_ptr<igl::ISamplerState>> samplerStates_;
        std::shared_ptr<igl::ITexture> dummyTexture2D_;
        std::shared_ptr<igl::ITexture> dummyTextureCube_;

        std::unique_ptr<IglDynamicBufferPool> dynamicVertexPool_;
        std::unique_ptr<IglDynamicBufferPool> dynamicIndexPool_;
        std::unique_ptr<IglDynamicBufferPool> dynamicUniformPool_;

        IglEffectRenderer* boundCustomEffect_ = nullptr;
        std::uint64_t nextProgramId_ = 0;
        std::uint64_t nextVertexInputId_ = 0;
    };
}
