// SPDX-License-Identifier: MS-PL
// plans/plan_street_sdlgpu.md STREETS-0003: an engine-layer pass samples a half-float render
// target through the linear filter it asked for.
//
// The SDL GPU renderer answered `HalfFloatTextureLinearFiltering` with a flat `false`, although
// every backend of SDL's GPU API filters RGBA16F. `CNA::Graphics::FullscreenPass` trusts that
// answer and degrades a filtered read of a half-float source to point sampling, so on that
// renderer every pass reading the HDR scene -- FXAA's sub-texel taps, bloom's downsample --
// sampled nearest texels. cna-street's anti-aliasing did visibly less than on Vulkan on the same
// GPU, and nothing reported why.
//
// The witness is the degrade decision itself: a 2x1 HdrBlendable target, black then white,
// stretched across the back buffer through `FullscreenPass` with `SamplerState::LinearClamp`. A
// linear read ramps from black to white across the middle; a point read has no value in between.
//
// Exit code 0 = all checks PASS, 1 = any FAILs.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsProfile.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteSortMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"

#include "CNA/GraphicsCapability.hpp"
#include "CNA/Graphics/FullscreenPass.hpp"

#include <cstdio>
#include <memory>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kWidth = 64;
    constexpr int kHeight = 8;

    int passCount = 0;
    int totalCount = 0;

    void check(bool ok, const char* label)
    {
        ++totalCount;
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label);
        if (ok) ++passCount;
    }
}

class SdlGpuHalfFloatFilteringTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    int frame_ = 0;

    void Draw(const GameTime&) override
    {
        if (frame_++ < 1) return;
        GraphicsDevice& device = getGraphicsDeviceProperty();

        check(device.SupportsCapability(CNA::GraphicsCapability::HalfFloatTextureLinearFiltering),
              "the SDL GPU renderer reports linear filtering of half-float textures");

        // Black texel on the left, white on the right.
        RenderTarget2D source(device, 2, 1, false, SurfaceFormat::HdrBlendable, DepthFormat::None);
        Texture2D white(device, 1, 1);
        const Color whitePixel = Color::White;
        white.SetData(&whitePixel, 1);
        SpriteBatch batch(device);
        device.SetRenderTarget(&source);
        device.Clear(Color::Black);
        batch.Begin(SpriteSortMode::Deferred, BlendState::Opaque,
                    const_cast<SamplerState*>(&SamplerState::PointClamp), nullptr, nullptr);
        batch.Draw(white, Rectangle(1, 0, 1, 1), Color::White);
        batch.End();
        device.SetRenderTarget(nullptr);

        device.Clear(Color(255, 0, 255, 255));
        CNA::Graphics::FullscreenPass pass(device);
        pass.draw(&source, nullptr, nullptr, kWidth, kHeight,
                  const_cast<SamplerState*>(&SamplerState::LinearClamp));

        std::vector<Color> row(kWidth);
        const Rectangle region(0, kHeight / 2, kWidth, 1);
        device.GetBackBufferData(&region, row.data(), 0, kWidth);

        int between = 0;
        bool monotonic = true;
        for (int x = 0; x < kWidth; ++x)
        {
            const int value = row[static_cast<std::size_t>(x)].getRProperty();
            if (value > 24 && value < 231) ++between;
            if (x > 0 && value + 2 < row[static_cast<std::size_t>(x - 1)].getRProperty())
                monotonic = false;
        }
        std::printf("  row: R[0]=%d R[16]=%d R[31]=%d R[32]=%d R[47]=%d R[63]=%d, %d in between\n",
                    row[0].getRProperty(), row[16].getRProperty(), row[31].getRProperty(),
                    row[32].getRProperty(), row[47].getRProperty(), row[63].getRProperty(),
                    between);
        check(row[0].getRProperty() <= 24 && row[kWidth - 1].getRProperty() >= 231,
              "the two ends keep the two texels' own values");
        check(between >= kWidth / 4,
              "the middle ramps between them -- the half-float source was read with the linear "
              "filter the pass asked for, not degraded to point sampling");
        check(monotonic, "the ramp rises from the black texel to the white one");

        std::printf("=== %d/%d PASS ===\n", passCount, totalCount);
        Exit();
    }

public:
    SdlGpuHalfFloatFilteringTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        // HdrBlendable render targets and GetBackBufferData are both HiDef-only.
        gdm_->setGraphicsProfileProperty(GraphicsProfile::HiDef);
        gdm_->setPreferredBackBufferWidthProperty(kWidth);
        gdm_->setPreferredBackBufferHeightProperty(kHeight);
    }
};

int main()
{
    SdlGpuHalfFloatFilteringTest game;
    game.Run();

    std::printf("=== %d/%d PASS (total) ===\n", passCount, totalCount);
    return (passCount == totalCount) ? 0 : 1;
}
