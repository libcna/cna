// SPDX-License-Identifier: MS-PL
#include "CNA/Graphics/RenderTargetPool.hpp"

#ifdef CNA_CNAEXT

#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture.hpp"

#include <stdexcept>

namespace CNA::Graphics {

    using Microsoft::Xna::Framework::Graphics::DepthFormat;
    using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
    using Microsoft::Xna::Framework::Graphics::RenderTarget2D;
    using Microsoft::Xna::Framework::Graphics::SurfaceFormat;
    using Microsoft::Xna::Framework::Graphics::Texture;

    struct RenderTargetPool::Entry
    {
        int           width  = 0;
        int           height = 0;
        SurfaceFormat format = SurfaceFormat::Color;
        DepthFormat   depth  = DepthFormat::None;
        int           slot   = 0;
        std::unique_ptr<RenderTarget2D> target;
    };

    RenderTargetPool::RenderTargetPool(GraphicsDevice& device) : device_(device) {}

    RenderTargetPool::~RenderTargetPool() = default;

    RenderTarget2D* RenderTargetPool::acquire(const int width, const int height,
                                              const SurfaceFormat format,
                                              const DepthFormat depthFormat, const int slot)
    {
        if (width <= 0 || height <= 0)
            throw std::invalid_argument(
                "CNA::Graphics::RenderTargetPool::acquire: size must be positive");

        for (const std::unique_ptr<Entry>& entry : entries_)
        {
            if (entry->width == width && entry->height == height && entry->format == format
                && entry->depth == depthFormat && entry->slot == slot)
            {
                return entry->target.get();
            }
        }

        auto entry    = std::make_unique<Entry>();
        entry->width  = width;
        entry->height = height;
        entry->format = format;
        entry->depth  = depthFormat;
        entry->slot   = slot;
        entry->target = std::make_unique<RenderTarget2D>(device_, width, height, false, format,
                                                         depthFormat);
        RenderTarget2D* raw = entry->target.get();
        entries_.push_back(std::move(entry));
        return raw;
    }

    void RenderTargetPool::reset()
    {
        entries_.clear();
    }

    std::size_t RenderTargetPool::getTargetCount() const
    {
        return entries_.size();
    }

    std::size_t RenderTargetPool::getEstimatedBytes() const
    {
        std::size_t total = 0;
        for (const std::unique_ptr<Entry>& entry : entries_)
        {
            total += static_cast<std::size_t>(entry->width) * static_cast<std::size_t>(entry->height)
                   * static_cast<std::size_t>(Texture::GetFormatSizeEXT(entry->format));
        }
        return total;
    }

} // namespace CNA::Graphics

#endif // CNA_CNAEXT
