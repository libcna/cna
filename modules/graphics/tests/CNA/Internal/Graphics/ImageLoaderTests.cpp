// SPDX-License-Identifier: MS-PL

#include "CNA/Internal/Graphics/ImageLoader.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <stdexcept>
#include <vector>

namespace {

using CNA::Internal::Graphics::ImageLoader;

TEST(ImageLoaderTests, BilinearResizeSamplesPixelCentres)
{
    // The 1x1 destination samples the exact centre of this 2x2 image, so every channel is the
    // rounded average of all four source texels. This pins the platform-neutral scaler rather
    // than merely checking dimensions or a solid colour, both of which let nearest-neighbour and
    // broken row-stride implementations pass unnoticed.
    constexpr std::array<std::uint8_t, 16> pixels{
          0,   0,  20,   0, 100,   0,  40, 100,
          0, 100,  60, 200, 100, 100,  80, 255,
    };

    const auto resized = ImageLoader::ResizeRgba(pixels.data(), 2, 2, 1, 1, false);

    EXPECT_EQ(resized.width, 1);
    EXPECT_EQ(resized.height, 1);
    EXPECT_EQ(resized.pixels, (std::vector<std::uint8_t>{50, 50, 50, 139}));
}

TEST(ImageLoaderTests, RejectsMalformedBuffersAndDimensions)
{
    constexpr std::array<std::uint8_t, 4> pixel{1, 2, 3, 4};

    EXPECT_THROW((void)ImageLoader::LoadFromMemory(nullptr, 1), std::invalid_argument);
    EXPECT_THROW((void)ImageLoader::LoadFromMemory(pixel.data(), 0), std::invalid_argument);
    EXPECT_THROW(
        (void)ImageLoader::ResizeRgba(nullptr, 1, 1, 1, 1, false), std::invalid_argument);
    EXPECT_THROW(
        (void)ImageLoader::ResizeRgba(pixel.data(), 1, 1, 0, 1, false),
        std::invalid_argument);
}

} // namespace
