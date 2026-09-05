// SPDX-License-Identifier: MS-PL
// plans/plan_webgpu.md WEBGPU-184: `NormalizedByte2` and `NormalizedByte4` are stored natively.
//
// Renderer-neutral: the two formats are `GraphicsProfile.Reach` formats a real XNA game may use,
// EasyGL has stored them since REMED-GFX-244, and WebGPU gained them here by mapping onto core
// WebGPU's `rg8snorm`/`rgba8snorm`. A renderer that does not support them is skipped rather than
// failed -- what is asserted is that a renderer claiming them keeps its promise.
//
// TWO CLAIMS, and the second is the one a construction test cannot make.
//
//   1. The bytes round-trip EXACTLY. `NormalizedByte` is signed, so the values chosen below include
//      both signs and both extremes: a format quietly stored as an UNSIGNED eight-bit texture --
//      the substitution WebGPU's classifier would have made before this row, since its default arm
//      returns RGBA8Unorm -- turns every negative component into a large positive one and fails on
//      the first negative value.
//
//   2. The value the SHADER sees is signed. Sampling is what separates "the bytes came back"
//      (which the framework's own CPU shadow can satisfy without the GPU being involved at all --
//      see WEBGPU-187) from "the GPU holds a signed-normalized texture". A `NormalizedByte4` texel
//      of (-1, -1, -1, +1) samples as a colour whose RGB is negative and therefore clamps to BLACK
//      on write, while (+1, +1, +1, +1) samples as white. An unsigned store maps the same two byte
//      patterns to 0x81818181 and 0x7F7F7F7F -- two mid-greys, indistinguishable from each other
//      and from neither black nor white.

#include <gtest/gtest.h>

#include "CNA/RendererTestGate.hpp"

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector4.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PackedVector/NormalizedByte2.hpp"
#include "Microsoft/Xna/Framework/Graphics/PackedVector/NormalizedByte4.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteSortMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "System/NotSupportedException.hpp"

#include <array>
#include <memory>
#include <stdexcept>
#include <vector>

using Microsoft::Xna::Framework::Vector2;
using Microsoft::Xna::Framework::Vector4;
using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
using Microsoft::Xna::Framework::Graphics::SurfaceFormat;
using Microsoft::Xna::Framework::Graphics::Texture2D;
using Microsoft::Xna::Framework::Graphics::PackedVector::NormalizedByte2;
using Microsoft::Xna::Framework::Graphics::PackedVector::NormalizedByte4;

namespace
{
    /// Constructs a texture in @p format, or reports that this renderer refuses it by name.
    template <typename Fn>
    [[nodiscard]] bool RendererStores(GraphicsDevice& device, SurfaceFormat format, Fn&& body)
    {
        try
        {
            body();
        }
        catch (const System::NotSupportedException&)
        {
            return false;
        }
        catch (const std::runtime_error&)
        {
            return false;
        }
        (void)device;
        (void)format;
        return true;
    }
}

TEST(NormalizedByteFormat, NormalizedByte4RoundTripsEveryComponentExactly)
{
    GraphicsDevice device;
    // Both extremes and both signs, plus an asymmetric middle so a channel swap is visible.
    const std::array<NormalizedByte4, 4> written{
        NormalizedByte4(-1.0f, -1.0f, -1.0f, 1.0f),
        NormalizedByte4(1.0f, 1.0f, 1.0f, 1.0f),
        NormalizedByte4(-1.0f, 0.0f, 1.0f, -1.0f),
        NormalizedByte4(0.25f, -0.5f, 0.75f, -0.25f),
    };

    std::array<NormalizedByte4, 4> read{};
    const bool stored = RendererStores(device, SurfaceFormat::NormalizedByte4, [&]() {
        Texture2D texture(device, 2, 2, false, SurfaceFormat::NormalizedByte4);
        texture.SetData(written.data(), static_cast<int>(written.size()));
        texture.GetData(read.data(), static_cast<int>(read.size()));
    });
    if (!stored) GTEST_SKIP() << "this renderer refuses NormalizedByte4 by name";

    for (std::size_t i = 0; i < written.size(); ++i)
    {
        EXPECT_EQ(read[i].getPackedValueProperty(), written[i].getPackedValueProperty())
            << "texel " << i << " -- a signed-normalized format stored as UNSIGNED turns every "
               "negative component into a large positive one, and these texels carry both signs";
    }
}

TEST(NormalizedByteFormat, NormalizedByte2RoundTripsEveryComponentExactly)
{
    GraphicsDevice device;
    const std::array<NormalizedByte2, 4> written{
        NormalizedByte2(-1.0f, 1.0f),
        NormalizedByte2(1.0f, -1.0f),
        NormalizedByte2(0.0f, 0.5f),
        NormalizedByte2(-0.75f, 0.25f),
    };

    std::array<NormalizedByte2, 4> read{};
    const bool stored = RendererStores(device, SurfaceFormat::NormalizedByte2, [&]() {
        Texture2D texture(device, 2, 2, false, SurfaceFormat::NormalizedByte2);
        texture.SetData(written.data(), static_cast<int>(written.size()));
        texture.GetData(read.data(), static_cast<int>(read.size()));
    });
    if (!stored) GTEST_SKIP() << "this renderer refuses NormalizedByte2 by name";

    for (std::size_t i = 0; i < written.size(); ++i)
    {
        EXPECT_EQ(read[i].getPackedValueProperty(), written[i].getPackedValueProperty())
            << "texel " << i << " -- two bytes per texel, so a path that sized the upload at four "
               "reads the next texel's bytes as this one's";
    }
}

// A NormalizedByte2 texture is HALF the bytes of a Color one of the same size. A renderer that
// sized its upload at four bytes per texel either overruns the source buffer or lands every row on
// the wrong offset; at 2x2 the overrun is small enough to survive, which is why the size below is
// wide enough for a row stride to be wrong in a way a single row cannot show.
TEST(NormalizedByteFormat, NormalizedByte2SurvivesAWiderTextureWhereRowStrideMatters)
{
    GraphicsDevice device;
    constexpr int kWidth = 9;
    constexpr int kHeight = 5;
    std::vector<NormalizedByte2> written;
    written.reserve(static_cast<std::size_t>(kWidth) * kHeight);
    for (int y = 0; y < kHeight; ++y)
        for (int x = 0; x < kWidth; ++x)
            written.emplace_back(static_cast<float>(x) / kWidth * 2.0f - 1.0f,
                                 static_cast<float>(y) / kHeight * 2.0f - 1.0f);

    std::vector<NormalizedByte2> read(written.size());
    const bool stored = RendererStores(device, SurfaceFormat::NormalizedByte2, [&]() {
        Texture2D texture(device, kWidth, kHeight, false, SurfaceFormat::NormalizedByte2);
        texture.SetData(written.data(), static_cast<int>(written.size()));
        texture.GetData(read.data(), static_cast<int>(read.size()));
    });
    if (!stored) GTEST_SKIP() << "this renderer refuses NormalizedByte2 by name";

    for (std::size_t i = 0; i < written.size(); ++i)
    {
        EXPECT_EQ(read[i].getPackedValueProperty(), written[i].getPackedValueProperty())
            << "texel " << i << " of a 9x5 NormalizedByte2 texture";
        if (read[i].getPackedValueProperty() != written[i].getPackedValueProperty()) return;
    }
}

// The claim a round-trip cannot make: the value the SHADER sees is signed. See the header.
TEST(NormalizedByteFormat, ASampledNormalizedByte4IsSignedOnTheGpu)
{
    using Microsoft::Xna::Framework::Color;
    using Microsoft::Xna::Framework::Rectangle;
    using Microsoft::Xna::Framework::Vector2;
    using Microsoft::Xna::Framework::Graphics::BlendState;
    using Microsoft::Xna::Framework::Graphics::DepthFormat;
    using Microsoft::Xna::Framework::Graphics::RenderTarget2D;
    using Microsoft::Xna::Framework::Graphics::RenderTargetUsage;
    using Microsoft::Xna::Framework::Graphics::SamplerState;
    using Microsoft::Xna::Framework::Graphics::SpriteBatch;
    using Microsoft::Xna::Framework::Graphics::SpriteSortMode;

    GraphicsDevice device;
    // Two texels: all components at -1 except alpha, and all components at +1.
    const std::array<NormalizedByte4, 2> texels{
        NormalizedByte4(-1.0f, -1.0f, -1.0f, 1.0f),
        NormalizedByte4(1.0f, 1.0f, 1.0f, 1.0f),
    };

    std::unique_ptr<Texture2D> texture;
    const bool stored = RendererStores(device, SurfaceFormat::NormalizedByte4, [&]() {
        texture = std::make_unique<Texture2D>(device, 2, 1, false, SurfaceFormat::NormalizedByte4);
        texture->SetData(texels.data(), static_cast<int>(texels.size()));
    });
    if (!stored || !texture) GTEST_SKIP() << "this renderer refuses NormalizedByte4 by name";

    RenderTarget2D target(device, 2, 1, false, SurfaceFormat::Color, DepthFormat::None, 0,
                          RenderTargetUsage::PreserveContents);
    const SamplerState pointClamp = SamplerState::PointClamp;
    device.SetRenderTarget(&target);
    device.Clear(Color(40, 40, 40, 255));
    {
        SpriteBatch batch(device);
        batch.Begin(SpriteSortMode::Deferred, BlendState::Opaque, &pointClamp, nullptr, nullptr);
        batch.Draw(*texture, Rectangle(0, 0, 2, 1), Color::White);
        batch.End();
    }
    device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

    std::array<Color, 2> pixels{};
    target.GetData(pixels.data(), static_cast<int>(pixels.size()));

    // The negative texel clamps to black on write; the positive one is white. An UNSIGNED store
    // would put both at a mid-grey (0x81 and 0x7F) and this pair would be indistinguishable.
    EXPECT_LE(pixels[0].getRProperty(), 8)
        << "a NormalizedByte4 texel of -1 must reach the shader as -1 and clamp to black; an "
           "unsigned store makes it a mid-grey";
    EXPECT_GE(pixels[1].getRProperty(), 247)
        << "and a texel of +1 must reach the shader as +1";
    EXPECT_GT(pixels[1].getRProperty() - pixels[0].getRProperty(), 200)
        << "the two texels must be far apart -- an unsigned store puts them two units apart";
}
