// SPDX-License-Identifier: MS-PL
#include "CNA/Internal/Renderers/Magnum/MagnumRenderTargets.hpp"

#include "CNA/Internal/Renderers/Magnum/MagnumTextures.hpp"

#include <Corrade/Containers/ArrayView.h>
#include <Magnum/GL/PixelFormat.h>
#include <Magnum/GL/RenderbufferFormat.h>
#include <Magnum/GL/TextureFormat.h>
#include <Magnum/Image.h>
#include <Magnum/ImageView.h>
#include <Magnum/PixelFormat.h>

#include <algorithm>
#include <cstring>
#include <vector>

namespace CNA::Internal::Renderers::Magnum
{
    namespace
    {
        constexpr int kBytesPerPixel = 4;

        Mg::GL::CubeMapCoordinate FaceCoordinate(int face)
        {
            switch (face)
            {
                case 1:  return Mg::GL::CubeMapCoordinate::NegativeX;
                case 2:  return Mg::GL::CubeMapCoordinate::PositiveY;
                case 3:  return Mg::GL::CubeMapCoordinate::NegativeY;
                case 4:  return Mg::GL::CubeMapCoordinate::PositiveZ;
                case 5:  return Mg::GL::CubeMapCoordinate::NegativeZ;
                default: return Mg::GL::CubeMapCoordinate::PositiveX;
            }
        }

        /// XNA DepthFormat ordinals: None=0, Depth16=1, Depth24=2, Depth24Stencil8=3.
        Mg::GL::RenderbufferFormat DepthStorageFormat(int depthFormat)
        {
            switch (depthFormat)
            {
                case 1:  return Mg::GL::RenderbufferFormat::DepthComponent16;
                case 2:  return Mg::GL::RenderbufferFormat::DepthComponent24;
                default: return Mg::GL::RenderbufferFormat::Depth24Stencil8;
            }
        }

        Mg::GL::Framebuffer::BufferAttachment DepthAttachmentFor(int depthFormat)
        {
            return depthFormat == 3
                ? Mg::GL::Framebuffer::BufferAttachment::DepthStencil
                : Mg::GL::Framebuffer::BufferAttachment::Depth;
        }

        int ClampSampleCount(int requested)
        {
            if (requested <= 1)
                return 0;
            return std::min(requested, Mg::GL::Renderbuffer::maxSamples());
        }

        /**
         * @brief Reads a rectangle out of a framebuffer and returns it top row first.
         *
         * A rasterized image is stored bottom-up in GL memory, so both the read rectangle and the
         * resulting rows are converted here -- once, at the single place readback happens -- rather
         * than leaving the orientation for callers to rediscover.
         */
        bool ReadFramebufferRegion(Mg::GL::Framebuffer& framebuffer, int surfaceHeight,
                                   int x, int y, int w, int h, void* data)
        {
            std::vector<uint8_t> scratch(static_cast<std::size_t>(w) * h * kBytesPerPixel);
            const int glY = surfaceHeight - y - h;
            framebuffer.read(
                Mg::Range2Di{Mg::Vector2i{x, glY}, Mg::Vector2i{x + w, glY + h}},
                Mg::MutableImageView2D{Mg::PixelFormat::RGBA8Unorm, Mg::Vector2i{w, h},
                                       Corrade::Containers::ArrayView<void>{
                                           scratch.data(), scratch.size()}});

            auto* out = static_cast<uint8_t*>(data);
            const std::size_t rowBytes = static_cast<std::size_t>(w) * kBytesPerPixel;
            for (int row = 0; row < h; ++row)
            {
                std::memcpy(out + static_cast<std::size_t>(row) * rowBytes,
                            scratch.data() + static_cast<std::size_t>(h - 1 - row) * rowBytes,
                            rowBytes);
            }
            return true;
        }
    }

    bool SampledRowOrderIsBottomUp(const ITextureRenderer* texture)
    {
        return dynamic_cast<const IRenderTargetRenderer*>(texture) != nullptr;
    }

    // ---- MagnumRenderTargetRenderer ----

    MagnumRenderTargetRenderer::MagnumRenderTargetRenderer(int width, int height, int depthFormat,
                                                         std::weak_ptr<MagnumBoundTarget> binding,
                                                         bool mipMap, int multiSampleCount)
        : width_(std::max(1, width))
        , height_(std::max(1, height))
        , depthFormat_(depthFormat)
        , mipMap_(mipMap)
        , multiSampleCount_(ClampSampleCount(multiSampleCount))
        , binding_(std::move(binding))
    {
        levelCount_ = mipMap_ ? FullMipLevelCount(width_, height_) : 1;

        colorTexture_ = std::make_unique<Mg::GL::Texture2D>();
        colorTexture_->setStorage(levelCount_, Mg::GL::TextureFormat::RGBA8,
                                  Mg::Vector2i{width_, height_});
        ApplySamplerStateTo(*colorTexture_, MagnumSamplerState{}, levelCount_);

        const Mg::Range2Di viewport{{}, Mg::Vector2i{width_, height_}};
        framebuffer_ = std::make_unique<Mg::GL::Framebuffer>(viewport);

        if (multiSampleCount_ > 0)
        {
            multisampleColor_ = std::make_unique<Mg::GL::Renderbuffer>();
            multisampleColor_->setStorageMultisample(multiSampleCount_,
                                                     Mg::GL::RenderbufferFormat::RGBA8,
                                                     Mg::Vector2i{width_, height_});
            framebuffer_->attachRenderbuffer(Mg::GL::Framebuffer::ColorAttachment{0},
                                             *multisampleColor_);

            resolveFramebuffer_ = std::make_unique<Mg::GL::Framebuffer>(viewport);
            resolveFramebuffer_->attachTexture(Mg::GL::Framebuffer::ColorAttachment{0},
                                               *colorTexture_, 0);
        }
        else
        {
            framebuffer_->attachTexture(Mg::GL::Framebuffer::ColorAttachment{0}, *colorTexture_, 0);
        }

        if (depthFormat_ != 0)
        {
            depth_ = std::make_unique<Mg::GL::Renderbuffer>();
            if (multiSampleCount_ > 0)
            {
                depth_->setStorageMultisample(multiSampleCount_, DepthStorageFormat(depthFormat_),
                                              Mg::Vector2i{width_, height_});
            }
            else
            {
                depth_->setStorage(DepthStorageFormat(depthFormat_), Mg::Vector2i{width_, height_});
            }
            framebuffer_->attachRenderbuffer(DepthAttachmentFor(depthFormat_), *depth_);
        }
    }

    MagnumRenderTargetRenderer::~MagnumRenderTargetRenderer()
    {
        DetachFromBinding();
    }

    void MagnumRenderTargetRenderer::DetachFromBinding()
    {
        const auto binding = binding_.lock();
        if (!binding)
            return;
        if (binding->renderTarget2D == this)
            binding->renderTarget2D = nullptr;
        for (int slot = 0; slot < binding->mrtCount; ++slot)
        {
            if (binding->mrt[static_cast<std::size_t>(slot)] == this)
                binding->mrt[static_cast<std::size_t>(slot)] = nullptr;
        }
    }

    void MagnumRenderTargetRenderer::BindGL(int /*unit*/) const
    {
        colorTexture_->bind(0);
    }

    void MagnumRenderTargetRenderer::BindAsRenderTarget()
    {
        framebuffer_->bind();
        framebuffer_->setViewport(Mg::Range2Di{{}, Mg::Vector2i{width_, height_}});
    }

    void MagnumRenderTargetRenderer::ResolveColor() const
    {
        if (multiSampleCount_ <= 0 || !resolveFramebuffer_)
            return;
        Mg::GL::AbstractFramebuffer::blit(
            *framebuffer_, *resolveFramebuffer_,
            Mg::Range2Di{{}, Mg::Vector2i{width_, height_}},
            Mg::GL::FramebufferBlit::Color);
    }

    void MagnumRenderTargetRenderer::UnbindAsRenderTarget()
    {
        ResolveColor();
        if (mipMap_ && levelCount_ > 1)
            colorTexture_->generateMipmap();
    }

    bool MagnumRenderTargetRenderer::GetData(int level, int x, int y, int w, int h,
                                            void* data, int dataLength) const
    {
        if (data == nullptr || w <= 0 || h <= 0)
            return false;
        if (dataLength < w * h * kBytesPerPixel)
            return false;
        // Only level 0 is ever rendered into; a higher level exists only as generated mip storage,
        // which is read through the colour texture rather than through the framebuffer.
        if (level < 0 || level >= levelCount_)
            return false;

        if (level > 0)
        {
            const int levelW = std::max(1, width_ >> level);
            const int levelH = std::max(1, height_ >> level);
            if (x < 0 || y < 0 || x + w > levelW || y + h > levelH)
                return false;

            Mg::Image2D image{Mg::PixelFormat::RGBA8Unorm};
            colorTexture_->image(level, image);
            if (image.size() != Mg::Vector2i{levelW, levelH})
                return false;

            // A generated level inherits level 0's bottom-up storage, so its rows are flipped for
            // the same reason the framebuffer read below flips its own.
            auto* out = static_cast<uint8_t*>(data);
            const std::size_t rowBytes = static_cast<std::size_t>(w) * kBytesPerPixel;
            for (int row = 0; row < h; ++row)
            {
                const int sourceRow = levelH - 1 - (y + row);
                std::memcpy(out + static_cast<std::size_t>(row) * rowBytes,
                            image.data().data()
                                + (static_cast<std::size_t>(sourceRow) * levelW + x) * kBytesPerPixel,
                            rowBytes);
            }
            return true;
        }

        if (x < 0 || y < 0 || x + w > width_ || y + h > height_)
            return false;

        // The multisample storage cannot be read directly, so the resolve that a destination switch
        // would run is forced here -- a readback of a still-bound target must see the samples it
        // just rendered, not the previous resolve.
        ResolveColor();
        Mg::GL::Framebuffer& readSource =
            multiSampleCount_ > 0 ? *resolveFramebuffer_ : *framebuffer_;
        return ReadFramebufferRegion(readSource, height_, x, y, w, h, data);
    }

    // ---- MagnumRenderTargetCubeRenderer ----

    MagnumRenderTargetCubeRenderer::MagnumRenderTargetCubeRenderer(
        int size, int depthFormat, std::weak_ptr<MagnumBoundTarget> binding,
        bool mipMap, int multiSampleCount)
        : size_(std::max(1, size))
        , depthFormat_(depthFormat)
        , mipMap_(mipMap)
        , multiSampleCount_(ClampSampleCount(multiSampleCount))
        , binding_(std::move(binding))
    {
        levelCount_ = mipMap_ ? FullMipLevelCount(size_, size_) : 1;

        texture_ = std::make_unique<Mg::GL::CubeMapTexture>();
        texture_->setStorage(levelCount_, Mg::GL::TextureFormat::RGBA8, Mg::Vector2i{size_, size_});
        ApplySamplerStateTo(*texture_, MagnumSamplerState{}, levelCount_);

        const Mg::Range2Di viewport{{}, Mg::Vector2i{size_, size_}};
        framebuffer_ = std::make_unique<Mg::GL::Framebuffer>(viewport);

        if (multiSampleCount_ > 0)
        {
            for (std::size_t face = 0; face < multisampleColor_.size(); ++face)
            {
                multisampleColor_[face] = std::make_unique<Mg::GL::Renderbuffer>();
                multisampleColor_[face]->setStorageMultisample(
                    multiSampleCount_, Mg::GL::RenderbufferFormat::RGBA8,
                    Mg::Vector2i{size_, size_});
            }
            resolveFramebuffer_ = std::make_unique<Mg::GL::Framebuffer>(viewport);
        }

        if (depthFormat_ != 0)
        {
            depth_ = std::make_unique<Mg::GL::Renderbuffer>();
            if (multiSampleCount_ > 0)
            {
                depth_->setStorageMultisample(multiSampleCount_, DepthStorageFormat(depthFormat_),
                                              Mg::Vector2i{size_, size_});
            }
            else
            {
                depth_->setStorage(DepthStorageFormat(depthFormat_), Mg::Vector2i{size_, size_});
            }
            framebuffer_->attachRenderbuffer(DepthAttachmentFor(depthFormat_), *depth_);
        }

        BindAsRenderTargetFace(0);
    }

    MagnumRenderTargetCubeRenderer::~MagnumRenderTargetCubeRenderer()
    {
        DetachFromBinding();
    }

    void MagnumRenderTargetCubeRenderer::DetachFromBinding()
    {
        const auto binding = binding_.lock();
        if (binding && binding->renderTargetCube == this)
            binding->renderTargetCube = nullptr;
    }

    void MagnumRenderTargetCubeRenderer::BindAsRenderTargetFace(int face)
    {
        const int clamped = (face < 0 || face > 5) ? 0 : face;

        // The previously rendered face is resolved before the attachment moves, or its samples
        // would be abandoned in a renderbuffer that the next face's blit would overwrite.
        if (multiSampleCount_ > 0 && clamped != lastFace_)
            ResolveFace(lastFace_);

        if (multiSampleCount_ > 0)
        {
            framebuffer_->attachRenderbuffer(
                Mg::GL::Framebuffer::ColorAttachment{0},
                *multisampleColor_[static_cast<std::size_t>(clamped)]);
        }
        else
        {
            framebuffer_->attachCubeMapTexture(Mg::GL::Framebuffer::ColorAttachment{0},
                                               *texture_, FaceCoordinate(clamped), 0);
        }

        lastFace_ = clamped;
        framebuffer_->bind();
        framebuffer_->setViewport(Mg::Range2Di{{}, Mg::Vector2i{size_, size_}});
    }

    void MagnumRenderTargetCubeRenderer::ResolveFace(int face) const
    {
        if (multiSampleCount_ <= 0 || !resolveFramebuffer_)
            return;
        resolveFramebuffer_->attachCubeMapTexture(Mg::GL::Framebuffer::ColorAttachment{0},
                                                  *texture_, FaceCoordinate(face), 0);
        Mg::GL::AbstractFramebuffer::blit(
            *framebuffer_, *resolveFramebuffer_,
            Mg::Range2Di{{}, Mg::Vector2i{size_, size_}},
            Mg::GL::FramebufferBlit::Color);
    }

    void MagnumRenderTargetCubeRenderer::UnbindAsRenderTarget()
    {
        ResolveFace(lastFace_);
        if (mipMap_ && levelCount_ > 1)
            texture_->generateMipmap();
    }

    void MagnumRenderTargetCubeRenderer::BindGL(int /*unit*/) const
    {
        texture_->bind(0);
    }

    bool MagnumRenderTargetCubeRenderer::SetData(int face, int level, int x, int y, int w, int h,
                                                const void* data, int dataLength)
    {
        if (data == nullptr || face < 0 || face > 5 || level < 0 || level >= levelCount_)
            return false;
        if (w <= 0 || h <= 0 || dataLength < w * h * kBytesPerPixel)
            return false;

        const int levelSize = std::max(1, size_ >> level);
        if (x < 0 || y < 0 || x + w > levelSize || y + h > levelSize)
            return false;

        texture_->setSubImage(FaceCoordinate(face), level, Mg::Vector2i{x, y}, Mg::ImageView2D{
            Mg::PixelFormat::RGBA8Unorm, Mg::Vector2i{w, h},
            Corrade::Containers::ArrayView<const void>{
                data, static_cast<std::size_t>(w) * h * kBytesPerPixel}});
        return true;
    }

    bool MagnumRenderTargetCubeRenderer::GetData(int face, int level, int x, int y, int w, int h,
                                                void* data, int dataLength) const
    {
        if (data == nullptr || face < 0 || face > 5 || level < 0 || level >= levelCount_)
            return false;
        if (w <= 0 || h <= 0 || dataLength < w * h * kBytesPerPixel)
            return false;

        const int levelSize = std::max(1, size_ >> level);
        if (x < 0 || y < 0 || x + w > levelSize || y + h > levelSize)
            return false;

        // Whichever face was rendered last has already been resolved into the cube texture, so an
        // MSAA target is read through its resolved single-sample image.
        ResolveFace(lastFace_);

        Mg::Image2D image{Mg::PixelFormat::RGBA8Unorm};
        texture_->image(FaceCoordinate(face), level, image);
        if (image.size() != Mg::Vector2i{levelSize, levelSize})
            return false;

        // This content came from rasterization, so it is stored bottom-up exactly as a 2D target's
        // is; the rows are flipped back so the public result is top row first like every other
        // readback.
        auto* out = static_cast<uint8_t*>(data);
        const std::size_t rowBytes = static_cast<std::size_t>(w) * kBytesPerPixel;
        for (int row = 0; row < h; ++row)
        {
            const int sourceRow = levelSize - 1 - (y + row);
            std::memcpy(out + static_cast<std::size_t>(row) * rowBytes,
                        image.data().data()
                            + (static_cast<std::size_t>(sourceRow) * levelSize + x) * kBytesPerPixel,
                        rowBytes);
        }
        return true;
    }
}
