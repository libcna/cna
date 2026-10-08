// SPDX-License-Identifier: MS-PL
// cna-cs CSX-136: a compiled effect samples a float texture at full 32-bit precision, as
// Direct3D 9 did. GLSL ES 3.00's default sampler2D precision is lowp, and a lookup returns its
// sampler's precision, so MojoShader's translation let Mesa read a Vector2 texture at fp16. A
// variance shadow map's moments, depth and depth squared, then no longer cancel, and
// willcraftia's LiSPSM demo drew its shadow as noise.

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <vector>

#include "CNA/GraphicsCapability.hpp"
#include "CNA/TestSupport/TestPaths.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/Effect.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectParameter.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectParameterCollection.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsProfile.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteSortMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"

namespace
{
    using Microsoft::Xna::Framework::Color;
    using Microsoft::Xna::Framework::Rectangle;
    using Microsoft::Xna::Framework::Vector2;
    using namespace Microsoft::Xna::Framework::Graphics;

    std::vector<SharpRuntime::bytecs> LoadVarianceEffect()
    {
        const std::filesystem::path path =
            CNA::TestSupport::CompiledEffectFixtureDirectory() / "variance-moments-xna4.fxb";
        std::ifstream input(path, std::ios::binary);
        return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    }
}

// The route willcraftia's demo blurs its moments through: a SpriteBatch with a compiled pixel
// shader, drawing a Vector2 render target into another.
TEST(CompiledEffectSamplerPrecisionTest, ASpriteEffectBlursVector2MomentsAtFullPrecision)
{
    GraphicsDevice device;
    device.SetGraphicsProfileEXT(GraphicsProfile::HiDef);
    if (!device.SupportsCapability(CNA::GraphicsCapability::CompiledEffects))
        GTEST_SKIP() << "this renderer does not execute compiled effects";
    // plans/plan_apple_m4.md AM4-189: the precision measured is the Vector2 target's; a renderer
    // that substitutes Color for it (FNA3D promotes no float render target) has nothing to measure.
    if (!device.SupportsSurfaceFormatAsRenderTargetEXT(SurfaceFormat::Vector2))
        GTEST_SKIP() << "this renderer has no Vector2 render target";
    const std::vector<SharpRuntime::bytecs> bytes = LoadVarianceEffect();
    ASSERT_FALSE(bytes.empty()) << "variance-moments-xna4.fxb is missing";

    // Depths near 1, where fp16 can tell values apart only every 0.00049.
    constexpr int kWidth = 4;
    const std::array<float, kWidth> depths{{0.9991f, 0.9993f, 0.9995f, 0.9997f}};
    std::array<Vector2, kWidth> moments{};
    for (int i = 0; i < kWidth; ++i)
        moments[static_cast<std::size_t>(i)] = Vector2(depths[static_cast<std::size_t>(i)],
                                                       depths[static_cast<std::size_t>(i)] *
                                                           depths[static_cast<std::size_t>(i)]);
    RenderTarget2D source(device, kWidth, 1, false, SurfaceFormat::Vector2, DepthFormat::None);
    source.SetData(moments.data(), kWidth);
    RenderTarget2D target(device, kWidth, 1, false, SurfaceFormat::Vector2, DepthFormat::None);

    Effect effect(device, bytes);
    effect.getParametersProperty()["Weight"]->SetValue(0.5f);
    effect.getParametersProperty()["Offset"]->SetValue(Vector2(1.0f / kWidth, 0.0f));
    SpriteBatch batch(device);

    device.SetRenderTarget(&target);
    batch.Begin(SpriteSortMode::Immediate, &BlendState::Opaque, &SamplerState::PointClamp,
                &DepthStencilState::None, &RasterizerState::CullNone, &effect);
    batch.Draw(source, Rectangle(0, 0, kWidth, 1), Color::White);
    batch.End();
    device.SetRenderTarget(nullptr);

    std::array<Vector2, kWidth> blurred{};
    target.GetData(blurred.data(), kWidth);
    for (int i = 0; i < kWidth; ++i)
    {
        const Vector2 next = moments[static_cast<std::size_t>(std::min(i + 1, kWidth - 1))];
        const Vector2 expected = (moments[static_cast<std::size_t>(i)] + next) * 0.5f;
        EXPECT_NEAR(blurred[static_cast<std::size_t>(i)].X, expected.X, 1e-6f) << "texel " << i;
        EXPECT_NEAR(blurred[static_cast<std::size_t>(i)].Y, expected.Y, 1e-6f) << "texel " << i;
    }
}
