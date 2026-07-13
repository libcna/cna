#include "CNA/Internal/Backends/Software/SoftwareGraphicsBackend.hpp"

#include <algorithm>
#include <cstring>
#include <stdexcept>

namespace CNA::Internal::Backends::Software
{
    // ---- SoftwareFramebuffer ----

    void SoftwareFramebuffer::Resize(int w, int h)
    {
        width = w;
        height = h;
        color.assign(static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4u, 0u);
        depthBuffer.assign(static_cast<std::size_t>(w) * static_cast<std::size_t>(h), 1.0f);
    }

    void SoftwareFramebuffer::ClearColor(float r, float g, float b, float a)
    {
        const std::uint8_t rb = static_cast<std::uint8_t>(std::clamp(r, 0.0f, 1.0f) * 255.0f);
        const std::uint8_t gb = static_cast<std::uint8_t>(std::clamp(g, 0.0f, 1.0f) * 255.0f);
        const std::uint8_t bb = static_cast<std::uint8_t>(std::clamp(b, 0.0f, 1.0f) * 255.0f);
        const std::uint8_t ab = static_cast<std::uint8_t>(std::clamp(a, 0.0f, 1.0f) * 255.0f);
        const std::size_t pixelCount = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
        for (std::size_t i = 0; i < pixelCount; ++i)
        {
            color[i * 4 + 0] = rb;
            color[i * 4 + 1] = gb;
            color[i * 4 + 2] = bb;
            color[i * 4 + 3] = ab;
        }
    }

    void SoftwareFramebuffer::ClearDepthValue(float depthValue)
    {
        std::fill(depthBuffer.begin(), depthBuffer.end(), depthValue);
    }

    // ---- SoftwareVertexBufferBackend ----

    SoftwareVertexBufferBackend::SoftwareVertexBufferBackend(int vertexCapacity)
        : capacity_(vertexCapacity)
    {
    }

    void SoftwareVertexBufferBackend::SetData(const void* data, int vertex_count, std::size_t stride_in_bytes)
    {
        if (vertex_count < 0 || vertex_count > capacity_)
            throw std::runtime_error("SoftwareVertexBufferBackend::SetData: vertex_count exceeds capacity");
        if (stride_in_bytes == 0)
            throw std::runtime_error("SoftwareVertexBufferBackend::SetData: stride_in_bytes must be > 0");

        vertexCount_ = vertex_count;
        stride_ = stride_in_bytes;
        const std::size_t byteCount = static_cast<std::size_t>(vertex_count) * stride_in_bytes;
        data_.assign(static_cast<const std::uint8_t*>(data), static_cast<const std::uint8_t*>(data) + byteCount);
    }

    void SoftwareVertexBufferBackend::SetDataWithOptions(const void* data, int vertex_count,
                                                         std::size_t stride_in_bytes, SetDataOptions)
    {
        SetData(data, vertex_count, stride_in_bytes);
    }

    // ---- SoftwareIndexBufferBackend ----

    SoftwareIndexBufferBackend::SoftwareIndexBufferBackend(int indexCapacity, bool thirtyTwoBit)
        : capacity_(indexCapacity), thirtyTwoBit_(thirtyTwoBit)
    {
    }

    void SoftwareIndexBufferBackend::Upload(const void* data, int index_count, bool dataIsThirtyTwoBit)
    {
        if (index_count < 0 || index_count > capacity_)
            throw std::runtime_error("SoftwareIndexBufferBackend: index_count exceeds capacity");
        if (dataIsThirtyTwoBit != thirtyTwoBit_)
            throw std::runtime_error("SoftwareIndexBufferBackend: SetData bit-width does not match the buffer's declared width");

        indexCount_ = index_count;
        const std::size_t elementSize = dataIsThirtyTwoBit ? sizeof(std::uint32_t) : sizeof(std::uint16_t);
        const std::size_t byteCount = static_cast<std::size_t>(index_count) * elementSize;
        data_.assign(static_cast<const std::uint8_t*>(data), static_cast<const std::uint8_t*>(data) + byteCount);
    }

    void SoftwareIndexBufferBackend::SetData16(const void* data, int index_count) { Upload(data, index_count, false); }
    void SoftwareIndexBufferBackend::SetData32(const void* data, int index_count) { Upload(data, index_count, true); }
    void SoftwareIndexBufferBackend::SetData16WithOptions(const void* data, int index_count, SetDataOptions)
    { Upload(data, index_count, false); }
    void SoftwareIndexBufferBackend::SetData32WithOptions(const void* data, int index_count, SetDataOptions)
    { Upload(data, index_count, true); }

    // ---- SoftwareTextureBackend ----

    SoftwareTextureBackend::SoftwareTextureBackend(const ImageData& data)
        : width_(data.width), height_(data.height)
    {
        pixels_.assign(data.pixels.begin(), data.pixels.end());
    }

    SoftwareTextureBackend::SoftwareTextureBackend(int width, int height)
        : width_(width), height_(height)
    {
        pixels_.assign(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u, 0u);
    }

    void SoftwareTextureBackend::UpdatePixels(const uint8_t* rgba, int stride)
    {
        if (rgba == nullptr)
            throw std::runtime_error("SoftwareTextureBackend::UpdatePixels: rgba must not be null");
        const std::size_t rowBytes = static_cast<std::size_t>(width_) * 4u;
        const std::size_t effectiveStride = stride > 0 ? static_cast<std::size_t>(stride) : rowBytes;
        pixels_.resize(rowBytes * static_cast<std::size_t>(height_));
        for (int y = 0; y < height_; ++y)
        {
            std::copy(rgba + static_cast<std::size_t>(y) * effectiveStride,
                     rgba + static_cast<std::size_t>(y) * effectiveStride + rowBytes,
                     pixels_.begin() + static_cast<std::ptrdiff_t>(y) * static_cast<std::ptrdiff_t>(rowBytes));
        }
    }

    void SoftwareTextureBackend::UpdatePixelsLevel(int, const uint8_t*, int, int)
    {
        // Mip levels beyond level 0 aren't stored in v1 (no mipmapping support, plan_software.md
        // Boundaries) -- accepted as a no-op rather than throwing, matching HEADLESS-12's own
        // precedent for the same real scope trim.
    }

    // ---- SoftwareRenderTargetBackend ----

    SoftwareRenderTargetBackend::SoftwareRenderTargetBackend(int w, int h, int depthFormat, bool mipMap,
                                                             int multiSampleCount)
        : depthFormat_(depthFormat), mipMap_(mipMap), multiSampleCount_(multiSampleCount)
    {
        framebuffer_.Resize(w, h);
    }

    void SoftwareRenderTargetBackend::UpdatePixels(const uint8_t* rgba, int)
    {
        if (rgba == nullptr) return;
        const std::size_t byteCount = static_cast<std::size_t>(framebuffer_.width) *
                                       static_cast<std::size_t>(framebuffer_.height) * 4u;
        framebuffer_.color.assign(rgba, rgba + byteCount);
    }

    // ---- SoftwareEffectBackend ----

    bool SoftwareEffectBackend::CompileProgram(const std::string& vertSrc, const std::string& fragSrc)
    {
        if (vertSrc.empty() && fragSrc.empty())
            throw std::runtime_error("SoftwareEffectBackend::CompileProgram: both vertSrc and fragSrc are empty");
        compiled_ = true;
        return true;
    }

    // ---- SoftwareSpriteBatchBackend ----
    // Phase S6 (SOFTWARE-51) wires these Draw() calls to the shared rasterizer core -- a textured
    // quad through the same DrawPrimitivesEx-equivalent path used for 3D draws. Stubbed as no-ops
    // here so the class is complete and linkable before that phase lands.

    SoftwareSpriteBatchBackend::SoftwareSpriteBatchBackend(SoftwareGraphicsBackend& owner) : owner_(owner) {}

    void SoftwareSpriteBatchBackend::Begin()
    {
        if (begun_)
            throw std::runtime_error("SoftwareSpriteBatchBackend::Begin: Begin() called without a matching End()");
        begun_ = true;
    }

    void SoftwareSpriteBatchBackend::End()
    {
        if (!begun_)
            throw std::runtime_error("SoftwareSpriteBatchBackend::End: End() called without a matching Begin()");
        begun_ = false;
    }

    void SoftwareSpriteBatchBackend::Draw(const ITextureBackend&, float, float) {}
    void SoftwareSpriteBatchBackend::Draw(const ITextureBackend&, const Rectangle&, const Rectangle&, const Color&) {}
    void SoftwareSpriteBatchBackend::Draw(const ITextureBackend&, const Rectangle&, const Rectangle&, const Color&,
                                          float, const Vector2&, SpriteEffects, float) {}

    // ---- SoftwareGraphicsBackend ----

    SoftwareGraphicsBackend::SoftwareGraphicsBackend(int virtualWidth, int virtualHeight)
        : virtualWidth_(virtualWidth), virtualHeight_(virtualHeight)
    {
        backbuffer_.Resize(virtualWidth > 0 ? virtualWidth : 1024, virtualHeight > 0 ? virtualHeight : 768);
    }

    SoftwareGraphicsBackend::~SoftwareGraphicsBackend() = default;

    SoftwareFramebuffer& SoftwareGraphicsBackend::CurrentFramebuffer()
    {
        return currentRenderTarget_ != nullptr ? currentRenderTarget_->Framebuffer() : backbuffer_;
    }

    const SoftwareFramebuffer& SoftwareGraphicsBackend::CurrentFramebuffer() const
    {
        return currentRenderTarget_ != nullptr ? currentRenderTarget_->Framebuffer() : backbuffer_;
    }

    void SoftwareGraphicsBackend::Clear(float r, float g, float b, float a)
    {
        CurrentFramebuffer().ClearColor(r, g, b, a);
    }

    void SoftwareGraphicsBackend::Present() {}

    void SoftwareGraphicsBackend::GetViewportSize(int& width, int& height)
    {
        const SoftwareFramebuffer& fb = CurrentFramebuffer();
        width = fb.width;
        height = fb.height;
    }

    void SoftwareGraphicsBackend::SetVirtualResolution(int width, int height)
    {
        virtualWidth_ = width;
        virtualHeight_ = height;
        if (currentRenderTarget_ == nullptr)
            backbuffer_.Resize(width, height);
    }

    void SoftwareGraphicsBackend::SetPresentationMode(int) {}

    void SoftwareGraphicsBackend::ReadBackbuffer(int x, int y, int w, int h, uint8_t* pixels)
    {
        if (w < 0 || h < 0)
            throw std::runtime_error("SoftwareGraphicsBackend::ReadBackbuffer: negative width/height");

        const SoftwareFramebuffer& fb = CurrentFramebuffer();
        for (int row = 0; row < h; ++row)
        {
            const int srcY = y + row;
            for (int col = 0; col < w; ++col)
            {
                const int srcX = x + col;
                const std::size_t dstIndex = (static_cast<std::size_t>(row) * static_cast<std::size_t>(w) +
                                              static_cast<std::size_t>(col)) * 4u;
                if (srcX < 0 || srcX >= fb.width || srcY < 0 || srcY >= fb.height)
                {
                    pixels[dstIndex + 0] = 0;
                    pixels[dstIndex + 1] = 0;
                    pixels[dstIndex + 2] = 0;
                    pixels[dstIndex + 3] = 0;
                    continue;
                }
                const std::size_t srcIndex = (static_cast<std::size_t>(srcY) * static_cast<std::size_t>(fb.width) +
                                              static_cast<std::size_t>(srcX)) * 4u;
                pixels[dstIndex + 0] = fb.color[srcIndex + 0];
                pixels[dstIndex + 1] = fb.color[srcIndex + 1];
                pixels[dstIndex + 2] = fb.color[srcIndex + 2];
                pixels[dstIndex + 3] = fb.color[srcIndex + 3];
            }
        }
    }

    std::unique_ptr<ITextureBackend> SoftwareGraphicsBackend::CreateTexture(const ImageData& data)
    {
        return std::make_unique<SoftwareTextureBackend>(data);
    }

    std::unique_ptr<ISpriteBatchBackend> SoftwareGraphicsBackend::CreateSpriteBatch()
    {
        return std::make_unique<SoftwareSpriteBatchBackend>(*this);
    }

    std::unique_ptr<IRenderTargetBackend> SoftwareGraphicsBackend::CreateRenderTarget2D(
        int w, int h, int depthFormat, bool, bool mipMap, int multiSampleCount)
    {
        return std::make_unique<SoftwareRenderTargetBackend>(w, h, depthFormat, mipMap, multiSampleCount);
    }

    void SoftwareGraphicsBackend::SetRenderTarget2D(IRenderTargetBackend* rt)
    {
        if (currentRenderTarget_ != nullptr)
            currentRenderTarget_->UnbindAsRenderTarget();
        currentRenderTarget_ = static_cast<SoftwareRenderTargetBackend*>(rt);
        if (currentRenderTarget_ != nullptr)
            currentRenderTarget_->BindAsRenderTarget();
    }

    std::unique_ptr<IEffectBackend> SoftwareGraphicsBackend::CreateEffectBackend(const std::string& vertSrc,
                                                                                const std::string& fragSrc)
    {
        auto effect = std::make_unique<SoftwareEffectBackend>();
        effect->CompileProgram(vertSrc, fragSrc);
        return effect;
    }

    void SoftwareGraphicsBackend::ApplyBlendState(int, int, int, int, int, int) {}

    void SoftwareGraphicsBackend::ApplyDepthStencilState(bool depthEnable, bool, int, bool, int, int, int, int, int,
                                                         int, int, bool, int, int, int, int)
    {
        depthTestEnabled_ = depthEnable;
    }

    void SoftwareGraphicsBackend::ApplyRasterizerState(int, int, bool, float, float) {}

    void SoftwareGraphicsBackend::ApplySamplerState(int slot, int, int, int, int)
    {
        if (slot < 0 || slot >= 16)
            throw std::runtime_error("SoftwareGraphicsBackend::ApplySamplerState: slot must be 0..15");
    }

    void SoftwareGraphicsBackend::SetScissorRect(int, int, int, int) {}

    void SoftwareGraphicsBackend::SetViewport(int, int, int, int, float, float) {}

    void SoftwareGraphicsBackend::ClearColorAndDepth(float r, float g, float b, float a, float depth)
    {
        SoftwareFramebuffer& fb = CurrentFramebuffer();
        fb.ClearColor(r, g, b, a);
        fb.ClearDepthValue(depth);
    }

    void SoftwareGraphicsBackend::ClearDepth(float depth) { CurrentFramebuffer().ClearDepthValue(depth); }
    void SoftwareGraphicsBackend::ClearStencil(int) {}
    void SoftwareGraphicsBackend::ClearDepthAndStencil(float depth, int) { CurrentFramebuffer().ClearDepthValue(depth); }
    void SoftwareGraphicsBackend::ClearColorAndStencil(float r, float g, float b, float a, int)
    { CurrentFramebuffer().ClearColor(r, g, b, a); }
    void SoftwareGraphicsBackend::ClearColorDepthAndStencil(float r, float g, float b, float a, float depth, int)
    { ClearColorAndDepth(r, g, b, a, depth); }

    void SoftwareGraphicsBackend::SetDepthTestEnabled(bool enabled) { depthTestEnabled_ = enabled; }
    void SoftwareGraphicsBackend::SetBlendEnabled(bool) {}
    void SoftwareGraphicsBackend::SetDepthWriteEnabled(bool) {}

    std::unique_ptr<IVertexBufferBackend> SoftwareGraphicsBackend::CreateVertexBuffer(int vertex_capacity)
    {
        return std::make_unique<SoftwareVertexBufferBackend>(vertex_capacity);
    }

    std::unique_ptr<IIndexBufferBackend> SoftwareGraphicsBackend::CreateIndexBuffer16(int index_capacity)
    {
        return std::make_unique<SoftwareIndexBufferBackend>(index_capacity, false);
    }

    std::unique_ptr<IIndexBufferBackend> SoftwareGraphicsBackend::CreateIndexBuffer32(int index_capacity)
    {
        return std::make_unique<SoftwareIndexBufferBackend>(index_capacity, true);
    }

    // Phase S4 (SOFTWARE-30..34) replaces these bodies with the real transform/rasterize/depth-test
    // pipeline. For now they validate the same primitive/vertex-count consistency every other
    // backend's shared GraphicsDevice layer already expects, without producing any pixels yet.
    void SoftwareGraphicsBackend::DrawColoredPrimitives(const IVertexBufferBackend&, const Matrix&, const Matrix&,
                                                        const Matrix&, PrimitiveType, int primitiveCount)
    {
        if (primitiveCount <= 0)
            throw std::runtime_error("SoftwareGraphicsBackend::DrawColoredPrimitives: primitiveCount must be > 0");
    }

    void SoftwareGraphicsBackend::DrawIndexedColoredPrimitives(const IVertexBufferBackend&, const IIndexBufferBackend&,
                                                               const Matrix&, const Matrix&, const Matrix&,
                                                               PrimitiveType, int primitiveCount)
    {
        if (primitiveCount <= 0)
            throw std::runtime_error("SoftwareGraphicsBackend::DrawIndexedColoredPrimitives: primitiveCount must be > 0");
    }
}

namespace CNA::Internal::Backends
{
    std::unique_ptr<IGraphicsBackend> CreateGraphicsBackend(const GraphicsBackendCreateArgs& args)
    {
        return std::make_unique<Software::SoftwareGraphicsBackend>(args.virtualWidth, args.virtualHeight);
    }
}
