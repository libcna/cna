// SPDX-License-Identifier: MS-PL
//
// plans/plan_xnapipeline.md XNAP-53: the BC1/BC2/BC3 encoder.
//
// Every quality assertion here decodes with CNA's own runtime decoder
// (CNA::Internal::Graphics::DxtUtil), which is the decoder a CNA game actually uses on a
// renderer without native BC support. That makes these round trips a statement about what a
// player will see, not about an idealized interpolation.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "CNA/Content/Pipeline/BlockCompression.hpp"
#include "CNA/Internal/Graphics/DxtUtil.hpp"

namespace Pipeline = CNA::Content::Pipeline;
using CNA::Internal::Graphics::DxtUtil;

namespace
{
    using Pipeline::BlockCompressionFormat;

    std::vector<std::uint8_t> Decode(const BlockCompressionFormat format,
                                     const std::vector<std::uint8_t>& blocks,
                                     const std::uint32_t width, const std::uint32_t height)
    {
        const int w = static_cast<int>(width);
        const int h = static_cast<int>(height);
        switch (format)
        {
        case BlockCompressionFormat::Bc1:
            return DxtUtil::DecompressDxt1(blocks.data(), blocks.size(), w, h);
        case BlockCompressionFormat::Bc2:
            return DxtUtil::DecompressDxt3(blocks.data(), blocks.size(), w, h);
        case BlockCompressionFormat::Bc3:
            return DxtUtil::DecompressDxt5(blocks.data(), blocks.size(), w, h);
        }
        return {};
    }

    std::vector<std::uint8_t> RoundTrip(const BlockCompressionFormat format,
                                        const std::vector<std::uint8_t>& rgba,
                                        const std::uint32_t width, const std::uint32_t height,
                                        const Pipeline::BlockCompressionOptions& options = {})
    {
        return Decode(format,
                      Pipeline::EncodeBlockCompressedImage(format, rgba, width, height, options),
                      width, height);
    }

    /** @brief Mean squared error per channel over the RGB channels only. */
    double ColorMeanSquaredError(const std::vector<std::uint8_t>& a,
                                 const std::vector<std::uint8_t>& b)
    {
        double total = 0.0;
        std::size_t samples = 0;
        for (std::size_t texel = 0; texel * 4u + 3u < a.size(); ++texel)
        {
            for (std::size_t channel = 0; channel < 3u; ++channel)
            {
                const double difference = static_cast<double>(a[texel * 4u + channel]) -
                                          static_cast<double>(b[texel * 4u + channel]);
                total += difference * difference;
                ++samples;
            }
        }
        return samples == 0u ? 0.0 : total / static_cast<double>(samples);
    }

    double ColorPsnr(const std::vector<std::uint8_t>& reference,
                     const std::vector<std::uint8_t>& decoded)
    {
        const double mse = ColorMeanSquaredError(reference, decoded);
        if (mse <= 0.0) { return 1000.0; }
        return 10.0 * std::log10(255.0 * 255.0 / mse);
    }

    /** @brief A deterministic image with smooth ramps, hard edges and a little noise. */
    std::vector<std::uint8_t> MakeTestImage(const std::uint32_t width, const std::uint32_t height,
                                            const bool withAlpha)
    {
        std::vector<std::uint8_t> rgba(static_cast<std::size_t>(width) * height * 4u);
        std::uint32_t state = 0x12345678u;
        for (std::uint32_t y = 0; y < height; ++y)
        {
            for (std::uint32_t x = 0; x < width; ++x)
            {
                state = state * 1664525u + 1013904223u;
                const int noise = static_cast<int>((state >> 24) % 9u) - 4;
                const std::size_t offset = (static_cast<std::size_t>(y) * width + x) * 4u;
                const int ramp = static_cast<int>(x * 255u / std::max(1u, width - 1u));
                const int band = static_cast<int>(y * 255u / std::max(1u, height - 1u));
                const bool square = ((x / 8u) + (y / 8u)) % 2u == 0u;
                rgba[offset] = static_cast<std::uint8_t>(std::clamp(ramp + noise, 0, 255));
                rgba[offset + 1u] = static_cast<std::uint8_t>(std::clamp(band + noise, 0, 255));
                rgba[offset + 2u] =
                    static_cast<std::uint8_t>(std::clamp((square ? 200 : 40) + noise, 0, 255));
                rgba[offset + 3u] =
                    withAlpha
                        ? static_cast<std::uint8_t>(std::clamp(255 - ramp + noise, 0, 255))
                        : 255u;
            }
        }
        return rgba;
    }

    std::vector<std::uint8_t> MakeSolid(const std::uint32_t width, const std::uint32_t height,
                                        const std::uint8_t r, const std::uint8_t g,
                                        const std::uint8_t b, const std::uint8_t a)
    {
        std::vector<std::uint8_t> rgba;
        rgba.reserve(static_cast<std::size_t>(width) * height * 4u);
        for (std::size_t texel = 0; texel < static_cast<std::size_t>(width) * height; ++texel)
        {
            rgba.insert(rgba.end(), {r, g, b, a});
        }
        return rgba;
    }
}

TEST(BlockCompressionTest, ByteCountsFollowTheBlockLayout)
{
    EXPECT_EQ(Pipeline::BlockCompressedBlockByteCount(BlockCompressionFormat::Bc1), 8u);
    EXPECT_EQ(Pipeline::BlockCompressedBlockByteCount(BlockCompressionFormat::Bc2), 16u);
    EXPECT_EQ(Pipeline::BlockCompressedBlockByteCount(BlockCompressionFormat::Bc3), 16u);

    // A partial block still costs a whole block, which is why 1x1 and 4x4 cost the same.
    EXPECT_EQ(Pipeline::BlockCompressedByteCount(BlockCompressionFormat::Bc1, 1u, 1u), 8u);
    EXPECT_EQ(Pipeline::BlockCompressedByteCount(BlockCompressionFormat::Bc1, 4u, 4u), 8u);
    EXPECT_EQ(Pipeline::BlockCompressedByteCount(BlockCompressionFormat::Bc1, 5u, 5u), 32u);
    EXPECT_EQ(Pipeline::BlockCompressedByteCount(BlockCompressionFormat::Bc3, 16u, 16u), 256u);
    EXPECT_EQ(Pipeline::BlockCompressionFormatName(BlockCompressionFormat::Bc1), "Dxt1");
    EXPECT_EQ(Pipeline::BlockCompressionFormatName(BlockCompressionFormat::Bc2), "Dxt3");
    EXPECT_EQ(Pipeline::BlockCompressionFormatName(BlockCompressionFormat::Bc3), "Dxt5");
}

TEST(BlockCompressionTest, EveryFormatProducesExactlyTheDeclaredByteCount)
{
    const std::vector<std::uint8_t> image = MakeTestImage(13u, 7u, true);
    for (const BlockCompressionFormat format :
         {BlockCompressionFormat::Bc1, BlockCompressionFormat::Bc2, BlockCompressionFormat::Bc3})
    {
        EXPECT_EQ(Pipeline::EncodeBlockCompressedImage(format, image, 13u, 7u).size(),
                  Pipeline::BlockCompressedByteCount(format, 13u, 7u))
            << Pipeline::BlockCompressionFormatName(format);
    }
}

TEST(BlockCompressionTest, ASolidColourSurvivesEveryFormatExactly)
{
    // Black is 565 code zero, which is the case the single-colour endpoint rule has to rescue:
    // there is no smaller code to put in the second endpoint, so the colour has to move.
    for (const auto& colour : std::vector<std::array<std::uint8_t, 3>>{
             {0u, 0u, 0u}, {255u, 255u, 255u}, {255u, 0u, 0u}, {0u, 255u, 0u}, {0u, 0u, 255u}})
    {
        const std::vector<std::uint8_t> image =
            MakeSolid(8u, 8u, colour[0], colour[1], colour[2], 255u);
        for (const BlockCompressionFormat format : {BlockCompressionFormat::Bc1,
                                                    BlockCompressionFormat::Bc2,
                                                    BlockCompressionFormat::Bc3})
        {
            EXPECT_EQ(RoundTrip(format, image, 8u, 8u), image)
                << Pipeline::BlockCompressionFormatName(format) << " of " <<
                static_cast<int>(colour[0]) << "," << static_cast<int>(colour[1]) << "," <<
                static_cast<int>(colour[2]);
        }
    }
}

TEST(BlockCompressionTest, ASolidBc1BlockStaysOpaqueRatherThanSelectingTheTransparentMode)
{
    // The failure this guards against is silent: equal endpoints encode as the three-colour mode,
    // whose fourth index is transparent black, so a solid opaque texture would come back with
    // holes rather than merely with the wrong shade.
    for (const std::uint8_t level : {std::uint8_t{0u}, std::uint8_t{8u}, std::uint8_t{128u},
                                     std::uint8_t{255u}})
    {
        const std::vector<std::uint8_t> image = MakeSolid(4u, 4u, level, level, level, 255u);
        const std::vector<std::uint8_t> decoded =
            RoundTrip(BlockCompressionFormat::Bc1, image, 4u, 4u);
        ASSERT_EQ(decoded.size(), image.size());
        for (std::size_t texel = 0; texel * 4u + 3u < decoded.size(); ++texel)
        {
            EXPECT_EQ(decoded[texel * 4u + 3u], 255u)
                << "texel " << texel << " at level " << static_cast<int>(level);
        }
    }
}

TEST(BlockCompressionTest, TwoExactlyRepresentableColoursAreReproducedWithoutError)
{
    std::vector<std::uint8_t> image(4u * 4u * 4u);
    for (std::size_t texel = 0; texel < 16u; ++texel)
    {
        const bool first = texel % 3u == 0u;
        image[texel * 4u] = first ? 255u : 0u;
        image[texel * 4u + 1u] = 0u;
        image[texel * 4u + 2u] = first ? 0u : 255u;
        image[texel * 4u + 3u] = 255u;
    }
    EXPECT_EQ(RoundTrip(BlockCompressionFormat::Bc1, image, 4u, 4u), image);
    EXPECT_EQ(RoundTrip(BlockCompressionFormat::Bc3, image, 4u, 4u), image);
}

TEST(BlockCompressionTest, Bc1CarriesACutoutMaskExactly)
{
    const std::uint32_t size = 16u;
    std::vector<std::uint8_t> image(static_cast<std::size_t>(size) * size * 4u, 0u);
    for (std::uint32_t y = 0; y < size; ++y)
    {
        for (std::uint32_t x = 0; x < size; ++x)
        {
            const std::size_t offset = (static_cast<std::size_t>(y) * size + x) * 4u;
            const bool inside = (x + y) % 5u != 0u;
            image[offset] = 200u;
            image[offset + 1u] = 100u;
            image[offset + 2u] = 50u;
            image[offset + 3u] = inside ? 255u : 0u;
        }
    }

    const std::vector<std::uint8_t> decoded =
        RoundTrip(BlockCompressionFormat::Bc1, image, size, size);
    ASSERT_EQ(decoded.size(), image.size());
    for (std::size_t texel = 0; texel * 4u + 3u < decoded.size(); ++texel)
    {
        EXPECT_EQ(decoded[texel * 4u + 3u], image[texel * 4u + 3u]) << "texel " << texel;
    }
}

TEST(BlockCompressionTest, AFullyTransparentBc1BlockDecodesFullyTransparent)
{
    const std::vector<std::uint8_t> image = MakeSolid(4u, 4u, 90u, 120u, 200u, 0u);
    const std::vector<std::uint8_t> decoded =
        RoundTrip(BlockCompressionFormat::Bc1, image, 4u, 4u);
    ASSERT_EQ(decoded.size(), image.size());
    for (std::size_t texel = 0; texel * 4u + 3u < decoded.size(); ++texel)
    {
        EXPECT_EQ(decoded[texel * 4u + 3u], 0u) << "texel " << texel;
    }
}

TEST(BlockCompressionTest, Bc2StoresAlphaAsTheNearestNibble)
{
    std::vector<std::uint8_t> image(16u * 1u * 4u);
    for (std::size_t texel = 0; texel < 16u; ++texel)
    {
        image[texel * 4u] = 128u;
        image[texel * 4u + 1u] = 128u;
        image[texel * 4u + 2u] = 128u;
        image[texel * 4u + 3u] = static_cast<std::uint8_t>(texel * 17u);
    }
    const std::vector<std::uint8_t> decoded =
        RoundTrip(BlockCompressionFormat::Bc2, image, 16u, 1u);
    ASSERT_EQ(decoded.size(), image.size());
    for (std::size_t texel = 0; texel < 16u; ++texel)
    {
        // Multiples of 17 are exactly the values a replicated nibble produces.
        EXPECT_EQ(decoded[texel * 4u + 3u], image[texel * 4u + 3u]) << "texel " << texel;
    }
}

TEST(BlockCompressionTest, Bc3ReproducesASmoothAlphaRampCloselyAndItsEndpointsExactly)
{
    const std::uint32_t width = 64u;
    std::vector<std::uint8_t> image(static_cast<std::size_t>(width) * 4u * 4u);
    for (std::uint32_t y = 0; y < 4u; ++y)
    {
        for (std::uint32_t x = 0; x < width; ++x)
        {
            const std::size_t offset = (static_cast<std::size_t>(y) * width + x) * 4u;
            image[offset] = 10u;
            image[offset + 1u] = 20u;
            image[offset + 2u] = 30u;
            image[offset + 3u] = static_cast<std::uint8_t>(x * 255u / (width - 1u));
        }
    }

    const std::vector<std::uint8_t> decoded =
        RoundTrip(BlockCompressionFormat::Bc3, image, width, 4u);
    ASSERT_EQ(decoded.size(), image.size());
    int worst = 0;
    for (std::size_t texel = 0; texel * 4u + 3u < decoded.size(); ++texel)
    {
        worst = std::max(worst, std::abs(static_cast<int>(decoded[texel * 4u + 3u]) -
                                         static_cast<int>(image[texel * 4u + 3u])));
    }
    // Eight levels across a block whose alpha spans about 16 values leaves at most a rounding
    // step of error; anything larger means the interpolated-alpha mode was not selected.
    EXPECT_LE(worst, 2) << "worst alpha error " << worst;
    EXPECT_EQ(decoded[3], 0u);
    EXPECT_EQ(decoded[(width - 1u) * 4u + 3u], 255u);
}

TEST(BlockCompressionTest, ARealisticImageMeetsAQualityBudgetInEveryFormat)
{
    const std::uint32_t size = 64u;
    const std::vector<std::uint8_t> opaque = MakeTestImage(size, size, false);
    const std::vector<std::uint8_t> translucent = MakeTestImage(size, size, true);

    const double bc1 = ColorPsnr(opaque, RoundTrip(BlockCompressionFormat::Bc1, opaque, size, size));
    const double bc2 =
        ColorPsnr(translucent, RoundTrip(BlockCompressionFormat::Bc2, translucent, size, size));
    const double bc3 =
        ColorPsnr(translucent, RoundTrip(BlockCompressionFormat::Bc3, translucent, size, size));

    // 30 dB is the usual floor for "no visible blocking" on this kind of content. The measured
    // values sit well above it; the budget is set where a real regression would trip it and
    // ordinary encoder tuning would not.
    EXPECT_GT(bc1, 30.0) << "BC1 PSNR " << bc1;
    EXPECT_GT(bc2, 30.0) << "BC2 PSNR " << bc2;
    EXPECT_GT(bc3, 30.0) << "BC3 PSNR " << bc3;
}

TEST(BlockCompressionTest, RefinementNeverMakesABlockWorse)
{
    const std::uint32_t size = 32u;
    const std::vector<std::uint8_t> image = MakeTestImage(size, size, true);
    for (const BlockCompressionFormat format :
         {BlockCompressionFormat::Bc1, BlockCompressionFormat::Bc3})
    {
        Pipeline::BlockCompressionOptions none;
        none.refinementRounds = 0u;
        Pipeline::BlockCompressionOptions many;
        many.refinementRounds = 8u;

        const double coarse =
            ColorMeanSquaredError(image, RoundTrip(format, image, size, size, none));
        const double fine =
            ColorMeanSquaredError(image, RoundTrip(format, image, size, size, many));
        EXPECT_LE(fine, coarse) << Pipeline::BlockCompressionFormatName(format) << ": " << fine
                                << " vs " << coarse;
    }
}

TEST(BlockCompressionTest, PartialEdgeBlocksReproduceTheImageTheyCover)
{
    // 13x7 covers 4x2 blocks, six of which are partial. The padding repeats the edge texel, so
    // it must not drag the endpoints toward a colour the image does not contain -- which a
    // smooth image makes measurable, because any pollution shows up immediately as error.
    const std::uint32_t width = 13u;
    const std::uint32_t height = 7u;
    std::vector<std::uint8_t> image(static_cast<std::size_t>(width) * height * 4u);
    for (std::uint32_t y = 0; y < height; ++y)
    {
        for (std::uint32_t x = 0; x < width; ++x)
        {
            const std::size_t offset = (static_cast<std::size_t>(y) * width + x) * 4u;
            // One colour axis, so a four-entry palette on a line can represent it well and
            // any error left over is either 565 quantization or edge-padding pollution.
            const std::uint32_t t = x + y;
            image[offset] = static_cast<std::uint8_t>(30u + t * 5u);
            image[offset + 1u] = static_cast<std::uint8_t>(60u + t * 3u);
            image[offset + 2u] = static_cast<std::uint8_t>(200u - t * 6u);
            image[offset + 3u] = 255u;
        }
    }

    const std::vector<std::uint8_t> decoded =
        RoundTrip(BlockCompressionFormat::Bc1, image, width, height);
    ASSERT_EQ(decoded.size(), image.size());
    const double psnr = ColorPsnr(image, decoded);
    EXPECT_GT(psnr, 38.0) << "PSNR " << psnr;

    // The rightmost column and bottom row live entirely inside partial blocks, so they are the
    // texels edge padding could damage.
    int worst = 0;
    for (std::uint32_t y = 0; y < height; ++y)
    {
        for (std::uint32_t x = 0; x < width; ++x)
        {
            if (x + 1u != width && y + 1u != height) { continue; }
            const std::size_t offset = (static_cast<std::size_t>(y) * width + x) * 4u;
            for (std::size_t channel = 0; channel < 3u; ++channel)
            {
                worst = std::max(worst, std::abs(static_cast<int>(decoded[offset + channel]) -
                                                 static_cast<int>(image[offset + channel])));
            }
        }
    }
    // 565 quantization alone can move a channel by four, so the bound below leaves room for
    // that and none for a block whose endpoints were fitted to padding instead of image.
    EXPECT_LE(worst, 6) << "worst edge-texel error " << worst;
}

TEST(BlockCompressionTest, ABlockOfThreeIndependentAxesStillMeetsTheFormatsOwnLimit)
{
    // This is the content BC1 is worst at: red, green and blue all varying independently inside
    // one 4x4 block, so a four-entry palette on a single line cannot represent it. The number
    // below is not a target, it is a record of what the encoder achieves on the hardest case, so
    // that a regression that halves the quality is visible.
    const std::uint32_t size = 16u;
    const std::vector<std::uint8_t> image = MakeTestImage(size, size, false);
    const double psnr = ColorPsnr(image, RoundTrip(BlockCompressionFormat::Bc1, image, size, size));
    EXPECT_GT(psnr, 24.0) << "PSNR " << psnr;
}

TEST(BlockCompressionTest, EncodingIsDeterministic)
{
    const std::vector<std::uint8_t> image = MakeTestImage(37u, 19u, true);
    for (const BlockCompressionFormat format :
         {BlockCompressionFormat::Bc1, BlockCompressionFormat::Bc2, BlockCompressionFormat::Bc3})
    {
        EXPECT_EQ(Pipeline::EncodeBlockCompressedImage(format, image, 37u, 19u),
                  Pipeline::EncodeBlockCompressedImage(format, image, 37u, 19u))
            << Pipeline::BlockCompressionFormatName(format);
    }
}

TEST(BlockCompressionTest, AlphaClassificationDrivesTheFormatChoice)
{
    const std::vector<std::uint8_t> opaque = MakeSolid(4u, 4u, 10u, 20u, 30u, 255u);
    std::vector<std::uint8_t> cutout = opaque;
    cutout[3] = 0u;
    std::vector<std::uint8_t> faded = opaque;
    faded[3] = 128u;

    EXPECT_FALSE(Pipeline::ImageHasTransparency(opaque, 4u, 4u));
    EXPECT_TRUE(Pipeline::ImageHasTransparency(cutout, 4u, 4u));
    EXPECT_FALSE(Pipeline::ImageHasPartialTransparency(cutout, 4u, 4u));
    EXPECT_TRUE(Pipeline::ImageHasPartialTransparency(faded, 4u, 4u));
}

TEST(BlockCompressionTest, MalformedInputIsRefusedRatherThanRead)
{
    const std::vector<std::uint8_t> image = MakeSolid(4u, 4u, 1u, 2u, 3u, 255u);
    EXPECT_THROW((void)Pipeline::EncodeBlockCompressedImage(BlockCompressionFormat::Bc1, image, 0u,
                                                            4u),
                 std::invalid_argument);
    EXPECT_THROW((void)Pipeline::EncodeBlockCompressedImage(BlockCompressionFormat::Bc1, image, 4u,
                                                            0u),
                 std::invalid_argument);
    EXPECT_THROW((void)Pipeline::EncodeBlockCompressedImage(BlockCompressionFormat::Bc1, image, 8u,
                                                            4u),
                 std::invalid_argument);
}
