// SPDX-License-Identifier: MS-PL
#include "CNA/Internal/Renderers/Igl/IglRenderer.hpp"

#include "IglConversions.hpp"

#include <igl/CommandQueue.h>
#include <igl/Device.h>

#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <vector>

namespace CNA::Internal::Renderers::Igl
{
    namespace
    {
        /// Chunks are rounded up to this size so a frame of small sprite batches does not create a
        /// separate GPU allocation per flush.
        constexpr std::size_t kDynamicChunkBytes = 256 * 1024;

        /// Vulkan only guarantees VkPhysicalDeviceLimits::maxUniformBufferRange >= 65536, and IGL's
        /// Vulkan backend asserts a uniform-typed buffer never exceeds it (real hardware and Mesa's
        /// software rasterizer both report exactly the guaranteed minimum). A uniform chunk therefore
        /// cannot share the larger vertex/index chunk size above.
        constexpr std::size_t kDynamicUniformChunkBytes = 64 * 1024;

        [[nodiscard]] std::size_t AlignUp(const std::size_t value, const std::size_t alignment)
        {
            if (alignment <= 1)
                return value;
            return (value + alignment - 1) / alignment * alignment;
        }

        /// Flips a tightly packed RGBA8 image vertically, in place.
        void FlipRowsInPlace(std::uint8_t* pixels, const int width, const int height)
        {
            const std::size_t rowBytes = static_cast<std::size_t>(width) * 4;
            std::vector<std::uint8_t> scratch(rowBytes);
            for (int y = 0; y < height / 2; ++y)
            {
                std::uint8_t* top = pixels + static_cast<std::size_t>(y) * rowBytes;
                std::uint8_t* bottom = pixels + static_cast<std::size_t>(height - 1 - y) * rowBytes;
                std::memcpy(scratch.data(), top, rowBytes);
                std::memcpy(top, bottom, rowBytes);
                std::memcpy(bottom, scratch.data(), rowBytes);
            }
        }
    }

    // ---------------------------------------------------------------------------------------------
    // IglDynamicBufferPool
    // ---------------------------------------------------------------------------------------------

    IglDynamicBufferPool::IglDynamicBufferPool(igl::IDevice& device, const std::uint8_t bufferType,
                                               std::string debugName)
        : device_(device), bufferType_(bufferType), debugName_(std::move(debugName))
    {
    }

    void IglDynamicBufferPool::BeginFrame(const int frameIndex)
    {
        frameIndex_ = ((frameIndex % kIglFramesInFlight) + kIglFramesInFlight) % kIglFramesInFlight;
        for (Chunk& chunk : frames_[frameIndex_])
            chunk.used = 0;
    }

    IglDynamicBufferPool::Allocation IglDynamicBufferPool::Upload(const void* data,
                                                                  const std::size_t sizeInBytes,
                                                                  const std::size_t alignment)
    {
        if (data == nullptr || sizeInBytes == 0)
            throw std::runtime_error("IGL renderer: refusing to stage an empty dynamic upload");

        std::vector<Chunk>& chunks = frames_[frameIndex_];
        for (Chunk& chunk : chunks)
        {
            const std::size_t offset = AlignUp(chunk.used, alignment);
            if (offset + sizeInBytes > chunk.capacity)
                continue;

            const igl::Result result =
                chunk.buffer->upload(data, igl::BufferRange(sizeInBytes, offset));
            if (!result.isOk())
            {
                throw std::runtime_error("IGL renderer: dynamic upload failed (" +
                                         result.message + ")");
            }
            chunk.used = offset + sizeInBytes;
            return Allocation{chunk.buffer.get(), offset};
        }

        const bool isUniform = (bufferType_ & igl::BufferDesc::BufferTypeBits::Uniform) != 0;
        const std::size_t baseChunkBytes = isUniform ? kDynamicUniformChunkBytes : kDynamicChunkBytes;

        Chunk chunk;
        chunk.capacity = std::max(baseChunkBytes, AlignUp(sizeInBytes, alignment) + alignment);

        igl::BufferDesc desc(bufferType_, nullptr, chunk.capacity, igl::ResourceStorage::Shared);
        desc.debugName = debugName_;
        if (isUniform)
            desc.hint = igl::BufferDesc::BufferAPIHintBits::UniformBlock;

        igl::Result result;
        chunk.buffer = device_.createBuffer(desc, &result);
        if (!chunk.buffer || !result.isOk())
        {
            throw std::runtime_error("IGL renderer: could not create a dynamic buffer chunk (" +
                                     result.message + ")");
        }

        result = chunk.buffer->upload(data, igl::BufferRange(sizeInBytes, 0));
        if (!result.isOk())
        {
            throw std::runtime_error("IGL renderer: dynamic upload failed (" + result.message + ")");
        }
        chunk.used = sizeInBytes;

        chunks.push_back(std::move(chunk));
        return Allocation{chunks.back().buffer.get(), 0};
    }

    // ---------------------------------------------------------------------------------------------
    // IglTextureRenderer
    // ---------------------------------------------------------------------------------------------

    IglTextureRenderer::IglTextureRenderer(IglRenderer* owner,
                                           std::shared_ptr<igl::ITexture> texture,
                                           const int width, const int height, const int mipLevels)
        : owner_(owner)
        , texture_(std::move(texture))
        , width_(width)
        , height_(height)
        , mipLevels_(std::max(1, mipLevels))
    {
    }

    void IglTextureRenderer::UpdatePixels(const std::uint8_t* rgba, const int stride)
    {
        if (!texture_ || rgba == nullptr)
            return;

        const igl::TextureRangeDesc range = igl::TextureRangeDesc::new2D(
            0, 0, static_cast<std::uint32_t>(width_), static_cast<std::uint32_t>(height_));
        const igl::Result result = texture_->upload(range, rgba, static_cast<std::size_t>(stride));
        if (!result.isOk())
            throw std::runtime_error("IGL renderer: texture upload failed (" + result.message + ")");

        definedMipLevels_ = std::max(definedMipLevels_, 1);
    }

    void IglTextureRenderer::UpdatePixelsLevel(const int level, const std::uint8_t* rgba,
                                               const int levelW, const int levelH)
    {
        if (!texture_ || rgba == nullptr || level < 0 || level >= mipLevels_)
            return;
        if (levelW <= 0 || levelH <= 0)
            return;

        const igl::TextureRangeDesc range = igl::TextureRangeDesc::new2D(
            0, 0, static_cast<std::uint32_t>(levelW), static_cast<std::uint32_t>(levelH),
            static_cast<std::uint32_t>(level));
        const igl::Result result = texture_->upload(range, rgba, 0);
        if (!result.isOk())
        {
            throw std::runtime_error("IGL renderer: mip level upload failed (" + result.message +
                                     ")");
        }

        definedMipLevels_ = std::max(definedMipLevels_, level + 1);
    }

    bool IglTextureRenderer::HasDefinedMipLevel(const int level) const noexcept
    {
        return level >= 0 && level < definedMipLevels_;
    }

    bool IglTextureRenderer::GetData(int /*level*/, int /*x*/, int /*y*/, int /*w*/, int /*h*/,
                                     void* /*data*/, int /*dataLength*/) const
    {
        // A plain Texture2D is only routed here when the shared layer has no CPU shadow, which in
        // practice means a RenderTarget2D -- and that overrides this with a real framebuffer
        // readback. Answering false rather than zero-filling keeps the "never invent content"
        // contract: the shared layer raises NotSupportedException instead of handing the caller a
        // fabricated transparent-black image.
        return false;
    }

    // ---------------------------------------------------------------------------------------------
    // IglTextureCubeRenderer
    // ---------------------------------------------------------------------------------------------

    IglTextureCubeRenderer::IglTextureCubeRenderer(IglRenderer* owner,
                                                   std::shared_ptr<igl::ITexture> texture,
                                                   const int size, const int mipLevels)
        : owner_(owner)
        , texture_(std::move(texture))
        , size_(size)
        , mipLevels_(std::max(1, mipLevels))
    {
    }

    bool IglTextureCubeRenderer::SetData(const int face, const int level, const int x, const int y,
                                         const int w, const int h, const void* data,
                                         const int dataLength)
    {
        if (!texture_ || data == nullptr)
            return false;
        if (face < 0 || face > 5 || level < 0 || level >= mipLevels_)
            return false;
        if (w <= 0 || h <= 0 || dataLength < w * h * 4)
            return false;

        const igl::TextureRangeDesc range = igl::TextureRangeDesc::newCubeFace(
            static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y),
            static_cast<std::uint32_t>(w), static_cast<std::uint32_t>(h),
            static_cast<std::uint32_t>(face), static_cast<std::uint32_t>(level));
        return texture_->upload(range, data, 0).isOk();
    }

    bool IglTextureCubeRenderer::GetData(int /*face*/, int /*level*/, int /*x*/, int /*y*/,
                                         int /*w*/, int /*h*/, void* /*data*/,
                                         int /*dataLength*/) const
    {
        // IGL exposes readback through IFramebuffer, not ITexture, and a plain TextureCube owns no
        // framebuffer. RenderTargetCube does, and overrides this. Refusing is the honest answer for
        // a resource whose bytes this renderer genuinely cannot fetch back.
        return false;
    }

    // ---------------------------------------------------------------------------------------------
    // IglTexture3DRenderer
    // ---------------------------------------------------------------------------------------------

    IglTexture3DRenderer::IglTexture3DRenderer(IglRenderer* owner,
                                               std::shared_ptr<igl::ITexture> texture,
                                               const int width, const int height, const int depth,
                                               const int mipLevels)
        : owner_(owner)
        , texture_(std::move(texture))
        , width_(width)
        , height_(height)
        , depth_(depth)
        , mipLevels_(std::max(1, mipLevels))
    {
    }

    bool IglTexture3DRenderer::SetData(const int level, const int x, const int y, const int z,
                                       const int w, const int h, const int depth, const void* data,
                                       const int dataLength)
    {
        if (!texture_ || data == nullptr)
            return false;
        if (level < 0 || level >= mipLevels_ || w <= 0 || h <= 0 || depth <= 0)
            return false;
        if (dataLength < w * h * depth * 4)
            return false;

        const igl::TextureRangeDesc range = igl::TextureRangeDesc::new3D(
            static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y),
            static_cast<std::uint32_t>(z), static_cast<std::uint32_t>(w),
            static_cast<std::uint32_t>(h), static_cast<std::uint32_t>(depth),
            static_cast<std::uint32_t>(level));
        return texture_->upload(range, data, 0).isOk();
    }

    bool IglTexture3DRenderer::GetData(int /*level*/, int /*x*/, int /*y*/, int /*z*/, int /*w*/,
                                       int /*h*/, int /*depth*/, void* /*data*/,
                                       int /*dataLength*/) const
    {
        // As for TextureCube above: IGL has no volume-texture readback path, so this refuses rather
        // than fabricating voxels.
        return false;
    }

    void IglTexture3DRenderer::GetDimensionsEXT(int& width, int& height, int& depth) const noexcept
    {
        width = width_;
        height = height_;
        depth = depth_;
    }

    // ---------------------------------------------------------------------------------------------
    // IglVertexBufferRenderer
    // ---------------------------------------------------------------------------------------------

    IglVertexBufferRenderer::IglVertexBufferRenderer(IglRenderer* owner, const int vertexCapacity)
        : owner_(owner), vertexCapacity_(std::max(0, vertexCapacity))
    {
    }

    void IglVertexBufferRenderer::SetData(const void* data, const int vertexCount,
                                          const std::size_t strideInBytes)
    {
        if (data == nullptr || vertexCount <= 0 || strideInBytes == 0)
        {
            vertexCount_ = 0;
            return;
        }

        const std::size_t requiredBytes = static_cast<std::size_t>(vertexCount) * strideInBytes;

        if (buffer_ && owner_ != nullptr)
        {
            // A draw already recorded this frame refers to this buffer's current bytes by handle.
            // Flushing first makes that draw run against the content it was recorded with, instead
            // of against whatever this call is about to write.
            owner_->FlushPendingFrameEXT();
        }

        if (!buffer_ || requiredBytes > byteCapacity_)
        {
            const std::size_t capacity = std::max(
                requiredBytes, static_cast<std::size_t>(vertexCapacity_) * strideInBytes);

            igl::BufferDesc desc(igl::BufferDesc::BufferTypeBits::Vertex, nullptr, capacity,
                                 igl::ResourceStorage::Shared);
            desc.debugName = "CNA vertex buffer";

            igl::Result result;
            buffer_ = owner_->GetDevice().createBuffer(desc, &result);
            if (!buffer_ || !result.isOk())
            {
                throw std::runtime_error("IGL renderer: could not create a vertex buffer (" +
                                         result.message + ")");
            }
            byteCapacity_ = capacity;
        }

        const igl::Result result = buffer_->upload(data, igl::BufferRange(requiredBytes, 0));
        if (!result.isOk())
        {
            throw std::runtime_error("IGL renderer: vertex buffer upload failed (" +
                                     result.message + ")");
        }

        vertexCount_ = vertexCount;
        stride_ = strideInBytes;
    }

    void IglVertexBufferRenderer::SetVertexDeclaration(const VertexDeclaration& vertexDeclaration)
    {
        declaration_ = vertexDeclaration;
        hasDeclaration_ = !vertexDeclaration.GetVertexElements().empty();
    }

    // ---------------------------------------------------------------------------------------------
    // IglIndexBufferRenderer
    // ---------------------------------------------------------------------------------------------

    IglIndexBufferRenderer::IglIndexBufferRenderer(IglRenderer* owner, const int indexCapacity,
                                                   const bool thirtyTwoBit)
        : owner_(owner), indexCapacity_(std::max(0, indexCapacity)), thirtyTwoBit_(thirtyTwoBit)
    {
    }

    void IglIndexBufferRenderer::SetData16(const void* data, const int indexCount)
    {
        Upload(data, indexCount, sizeof(std::uint16_t));
    }

    void IglIndexBufferRenderer::SetData32(const void* data, const int indexCount)
    {
        if (!thirtyTwoBit_)
        {
            throw std::runtime_error(
                "IGL renderer: SetData32 was called on a 16-bit index buffer");
        }
        Upload(data, indexCount, sizeof(std::uint32_t));
    }

    void IglIndexBufferRenderer::Upload(const void* data, const int indexCount,
                                        const std::size_t indexSize)
    {
        if (data == nullptr || indexCount <= 0)
        {
            indexCount_ = 0;
            return;
        }

        const std::size_t requiredBytes = static_cast<std::size_t>(indexCount) * indexSize;

        if (buffer_ && owner_ != nullptr)
            owner_->FlushPendingFrameEXT();

        if (!buffer_ || requiredBytes > byteCapacity_)
        {
            const std::size_t capacity =
                std::max(requiredBytes, static_cast<std::size_t>(indexCapacity_) * indexSize);

            igl::BufferDesc desc(igl::BufferDesc::BufferTypeBits::Index, nullptr, capacity,
                                 igl::ResourceStorage::Shared);
            desc.debugName = "CNA index buffer";

            igl::Result result;
            buffer_ = owner_->GetDevice().createBuffer(desc, &result);
            if (!buffer_ || !result.isOk())
            {
                throw std::runtime_error("IGL renderer: could not create an index buffer (" +
                                         result.message + ")");
            }
            byteCapacity_ = capacity;
        }

        const igl::Result result = buffer_->upload(data, igl::BufferRange(requiredBytes, 0));
        if (!result.isOk())
        {
            throw std::runtime_error("IGL renderer: index buffer upload failed (" +
                                     result.message + ")");
        }

        indexCount_ = indexCount;
    }

    // ---------------------------------------------------------------------------------------------
    // IglRenderTargetRenderer
    // ---------------------------------------------------------------------------------------------

    IglRenderTargetRenderer::IglRenderTargetRenderer(
        IglRenderer* owner, std::shared_ptr<igl::ITexture> color,
        std::shared_ptr<igl::ITexture> multisampleColor, std::shared_ptr<igl::ITexture> depth,
        std::shared_ptr<igl::IFramebuffer> framebuffer, const int width, const int height,
        const int mipLevels, const int sampleCount, const bool preserveContents,
        const int appliedDepthStencilFormat)
        : owner_(owner)
        , color_(std::move(color))
        , multisampleColor_(std::move(multisampleColor))
        , depth_(std::move(depth))
        , framebuffer_(std::move(framebuffer))
        , width_(width)
        , height_(height)
        , mipLevels_(std::max(1, mipLevels))
        , sampleCount_(std::max(1, sampleCount))
        , preserveContents_(preserveContents)
        , appliedDepthStencilFormat_(appliedDepthStencilFormat)
    {
    }

    void IglRenderTargetRenderer::UpdatePixels(const std::uint8_t* rgba, const int stride)
    {
        if (!color_ || rgba == nullptr)
            return;

        const igl::TextureRangeDesc range = igl::TextureRangeDesc::new2D(
            0, 0, static_cast<std::uint32_t>(width_), static_cast<std::uint32_t>(height_));
        const igl::Result result = color_->upload(range, rgba, static_cast<std::size_t>(stride));
        if (!result.isOk())
        {
            throw std::runtime_error("IGL renderer: render target upload failed (" +
                                     result.message + ")");
        }
    }

    void IglRenderTargetRenderer::UpdatePixelsLevel(const int level, const std::uint8_t* rgba,
                                                    const int levelW, const int levelH)
    {
        if (!color_ || rgba == nullptr || level < 0 || level >= mipLevels_)
            return;
        if (levelW <= 0 || levelH <= 0)
            return;

        const igl::TextureRangeDesc range = igl::TextureRangeDesc::new2D(
            0, 0, static_cast<std::uint32_t>(levelW), static_cast<std::uint32_t>(levelH),
            static_cast<std::uint32_t>(level));
        const igl::Result result = color_->upload(range, rgba, 0);
        if (!result.isOk())
        {
            throw std::runtime_error("IGL renderer: render target mip upload failed (" +
                                     result.message + ")");
        }
    }

    bool IglRenderTargetRenderer::HasDefinedMipLevel(const int level) const noexcept
    {
        // Level 0 is what every pass renders into, and levels above it are regenerated from it after
        // each pass when the target was created mipped -- so every allocated level really does hold
        // deterministic content.
        return level >= 0 && level < mipLevels_;
    }

    bool IglRenderTargetRenderer::GetData(const int level, const int x, const int y, const int w,
                                          const int h, void* data, const int dataLength) const
    {
        if (!framebuffer_ || data == nullptr || w <= 0 || h <= 0)
            return false;
        if (dataLength < w * h * 4)
            return false;
        if (level != 0)
        {
            // Only level 0 is a colour attachment, so any other level would have to be fetched
            // through a path IGL does not offer. Refusing beats guessing.
            return false;
        }
        if (owner_ == nullptr)
            return false;

        owner_->FlushPendingFrameEXT();

        const igl::TextureRangeDesc range = igl::TextureRangeDesc::new2D(
            static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y),
            static_cast<std::uint32_t>(w), static_cast<std::uint32_t>(h));
        framebuffer_->copyBytesColorAttachment(owner_->GetCommandQueue(), 0, data, range, 0);
        return true;
    }

    int IglRenderTargetRenderer::GetAppliedDepthStencilFormatEXT(
        int /*requestedDepthStencilFormat*/) const
    {
        return appliedDepthStencilFormat_;
    }

    bool IglRenderTargetRenderer::HasRealDepthBuffer(const bool depthFormatWasRequested) const
    {
        return depthFormatWasRequested && depth_ != nullptr;
    }

    void IglRenderTargetRenderer::GenerateMipsAfterPass(igl::ICommandQueue& queue)
    {
        if (color_ && mipLevels_ > 1)
            color_->generateMipmap(queue);
    }

    // ---------------------------------------------------------------------------------------------
    // IglRenderTargetCubeFace
    // ---------------------------------------------------------------------------------------------

    IglRenderTargetCubeFace::IglRenderTargetCubeFace(std::shared_ptr<igl::IFramebuffer> framebuffer,
                                                     const int size, const int face,
                                                     const int sampleCount,
                                                     const bool preserveContents)
        : framebuffer_(std::move(framebuffer))
        , size_(size)
        , face_(face)
        , sampleCount_(std::max(1, sampleCount))
        , preserveContents_(preserveContents)
    {
    }

    // ---------------------------------------------------------------------------------------------
    // IglRenderTargetCubeRenderer
    // ---------------------------------------------------------------------------------------------

    IglRenderTargetCubeRenderer::IglRenderTargetCubeRenderer(
        IglRenderer* owner, std::shared_ptr<igl::ITexture> color,
        std::shared_ptr<igl::ITexture> depth,
        std::array<std::shared_ptr<igl::IFramebuffer>, 6> faces, const int size,
        const int mipLevels, const int sampleCount, const bool preserveContents)
        : owner_(owner)
        , color_(std::move(color))
        , depth_(std::move(depth))
        , size_(size)
        , mipLevels_(std::max(1, mipLevels))
        , faceFramebuffers_(std::move(faces))
        , sampleCount_(std::max(1, sampleCount))
        , preserveContents_(preserveContents)
    {
        for (int face = 0; face < 6; ++face)
        {
            faceBindings_[face] = std::make_unique<IglRenderTargetCubeFace>(
                faceFramebuffers_[face], size_, face, sampleCount_, preserveContents_);
        }
    }

    bool IglRenderTargetCubeRenderer::HasRealDepthBuffer(const bool depthFormatWasRequested) const
    {
        return depthFormatWasRequested && depth_ != nullptr;
    }

    bool IglRenderTargetCubeRenderer::SetData(const int face, const int level, const int x,
                                              const int y, const int w, const int h,
                                              const void* data, const int dataLength)
    {
        if (!color_ || data == nullptr)
            return false;
        if (face < 0 || face > 5 || level < 0 || level >= mipLevels_)
            return false;
        if (w <= 0 || h <= 0 || dataLength < w * h * 4)
            return false;

        const igl::TextureRangeDesc range = igl::TextureRangeDesc::newCubeFace(
            static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y),
            static_cast<std::uint32_t>(w), static_cast<std::uint32_t>(h),
            static_cast<std::uint32_t>(face), static_cast<std::uint32_t>(level));
        return color_->upload(range, data, 0).isOk();
    }

    bool IglRenderTargetCubeRenderer::GetData(const int face, const int level, const int x,
                                              const int y, const int w, const int h, void* data,
                                              const int dataLength) const
    {
        if (data == nullptr || w <= 0 || h <= 0 || dataLength < w * h * 4)
            return false;
        if (face < 0 || face > 5 || level != 0 || owner_ == nullptr)
            return false;

        const std::shared_ptr<igl::IFramebuffer>& framebuffer = faceFramebuffers_[face];
        if (!framebuffer)
            return false;

        owner_->FlushPendingFrameEXT();

        const igl::TextureRangeDesc range = igl::TextureRangeDesc::new2D(
            static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y),
            static_cast<std::uint32_t>(w), static_cast<std::uint32_t>(h));
        framebuffer->copyBytesColorAttachment(owner_->GetCommandQueue(), 0, data, range, 0);
        return true;
    }

    IglBoundTarget* IglRenderTargetCubeRenderer::GetFaceBinding(const int face)
    {
        if (face < 0 || face > 5)
            return nullptr;
        return faceBindings_[face].get();
    }

    // ---------------------------------------------------------------------------------------------
    // IglMultiRenderTarget
    // ---------------------------------------------------------------------------------------------

    IglMultiRenderTarget::IglMultiRenderTarget(std::shared_ptr<igl::IFramebuffer> framebuffer,
                                               std::vector<IglRenderTargetRenderer*> targets,
                                               const int width, const int height,
                                               const int sampleCount, const bool preserveContents)
        : framebuffer_(std::move(framebuffer))
        , targets_(std::move(targets))
        , width_(width)
        , height_(height)
        , sampleCount_(std::max(1, sampleCount))
        , preserveContents_(preserveContents)
    {
    }

    void IglMultiRenderTarget::GenerateMipsAfterPass(igl::ICommandQueue& queue)
    {
        for (IglRenderTargetRenderer* target : targets_)
        {
            if (target != nullptr)
                target->GenerateMipsAfterPass(queue);
        }
    }

    // ---------------------------------------------------------------------------------------------
    // IglOcclusionQueryRenderer
    // ---------------------------------------------------------------------------------------------

    IglOcclusionQueryRenderer::IglOcclusionQueryRenderer(IglRenderer& owner) : owner_(owner) {}

    void IglOcclusionQueryRenderer::Begin()
    {
        complete_ = false;
        pixelCount_ = 0;
    }

    void IglOcclusionQueryRenderer::End()
    {
        // IGL exposes no occlusion-query object on any backend at the pinned revision: there is no
        // IDevice factory for one and no encoder call to begin or end one. Rather than return
        // nullptr from CreateOcclusionQuery -- which the shared layer reads as "this renderer has no
        // queries at all" and which would make OcclusionQuery.IsComplete spin forever -- the query
        // completes immediately and reports zero visible samples, and SupportsCapability reports
        // OcclusionQuery as unsupported so a game can ask before relying on the number.
        complete_ = true;
        pixelCount_ = 0;
    }
}
