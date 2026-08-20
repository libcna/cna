#pragma once

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"

#include <cstdint>
#include <string>
#include <vector>

// Task 7.1 (plans/plan_net.md Phase 7): shared, minimal backbuffer-to-PNG capture, reused by the
// avatar demos' own --screenshot flags for baseline/after documentation. Mirrors the same
// GetBackBufferData + Texture2D::SaveAsPng pattern examples/common/PixelTestGame.hpp already
// established for golden-image comparisons - no new image-encoding code.
inline void SaveBackBufferScreenshotEXT(Microsoft::Xna::Framework::Graphics::GraphicsDevice& device,
                                         const std::string& path)
{
    using Microsoft::Xna::Framework::Color;
    using Microsoft::Xna::Framework::Rectangle;
    using Microsoft::Xna::Framework::Graphics::Texture2D;

    const auto& vp = device.getViewportProperty();
    const int width = vp.getWidthProperty();
    const int height = vp.getHeightProperty();
    if (width <= 0 || height <= 0)
    {
        return;
    }

    const Rectangle region(0, 0, width, height);
    const std::size_t count = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    std::vector<Color> pixels(count, Color(0, 0, 0, 0));
    device.GetBackBufferData(&region, pixels.data(), 0, static_cast<int>(count));

    std::vector<std::uint8_t> rgba(count * 4);
    for (std::size_t i = 0; i < count; ++i)
    {
        rgba[i * 4 + 0] = static_cast<std::uint8_t>(pixels[i].getRProperty());
        rgba[i * 4 + 1] = static_cast<std::uint8_t>(pixels[i].getGProperty());
        rgba[i * 4 + 2] = static_cast<std::uint8_t>(pixels[i].getBProperty());
        rgba[i * 4 + 3] = static_cast<std::uint8_t>(pixels[i].getAProperty());
    }

    Texture2D shot = Texture2D::CreateFromPixels(device, width, height, rgba);
    shot.SaveAsPng(path);
}
