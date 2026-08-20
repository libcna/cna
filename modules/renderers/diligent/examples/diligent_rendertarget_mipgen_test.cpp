// SPDX-License-Identifier: MS-PL
//
// plans/plan_diligent.md DILIGENT-26: dedicated real-device pixel test for RenderTarget2D mip generation
// content, closing the gap that row's own notes left open (mip regeneration was implemented and
// reachable, but nothing asserted on what the generated levels actually contain).
//
// DiligentRenderTargetRenderer::UnbindAsRenderTarget() (DiligentRenderer.cpp) calls
// IDeviceContext::GenerateMips(srv_) whenever a mipMap render target stops being the bound draw
// target -- the same point FNA3D itself regenerates mips. This test proves that call performs a
// real box-filter downsample, not a nearest-pixel copy or a silent no-op:
//
//   A 4x4 mipMap RenderTarget2D (levels: 4x4, 2x2, 1x1) has an exact checkerboard of Red/Blue
//   pixel-copied into level 0 (a SpriteBatch draw of a matching 4x4 checkerboard source texture,
//   PointClamp-sampled 1:1 so level 0's content is byte-exact, not itself a filtering artifact).
//   Checkerboard means every aligned 2x2 block in level 0 contains exactly 2 Red + 2 Blue texels,
//   so a CORRECT box-filter downsample must read back level 1 as a blended ~(128,0,128) at every
//   texel -- neither pure Red nor pure Blue. A renderer that silently used nearest-pixel sampling
//   instead of averaging (or that left level 1 as stale/garbage/zeroed data) would read back
//   something else: pure Red, pure Blue, or black. Level 2 (1x1) averages all 16 checkerboard
//   texels (8 Red + 8 Blue), landing at the same ~(128,0,128) by the same reasoning.
//
// Check A -- level 0 (4x4) round-trips the checkerboard pattern exactly BEFORE unbinding.
// Check B -- level 1 (2x2), read back AFTER unbinding (which triggers GenerateMips): every texel
//   is a genuine Red/Blue blend, not pure Red, pure Blue, or black.
// Check C -- level 2 (1x1): the same blend.
// Check D -- level 0's own content is unaffected by mip generation (still the exact checkerboard).
//
// Exit code 0 = all checks PASS, 1 = any FAIL, 77 = no usable device.

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetBinding.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteSortMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"

#include <cstdint>
#include <cstdio>
#include <exception>
#include <memory>
#include <string>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kSize = 4;

    const Color kRed(255, 0, 0, 255);
    const Color kBlue(0, 0, 255, 255);
    const Color kBlack(0, 0, 0, 255);

    bool ColorEqual(const Color& a, const Color& b)
    {
        return a.getRProperty() == b.getRProperty() && a.getGProperty() == b.getGProperty() &&
               a.getBProperty() == b.getBProperty();
    }

    bool ColorNear(const Color& a, const Color& b, int tolerance)
    {
        return std::abs(a.getRProperty() - b.getRProperty()) <= tolerance &&
               std::abs(a.getGProperty() - b.getGProperty()) <= tolerance &&
               std::abs(a.getBProperty() - b.getBProperty()) <= tolerance;
    }

    // A real box-filter blend of 2 Red + 2 Blue texels: neither channel saturated, both meaningfully
    // above zero. Distinguishes "averaged" from "copied one input texel verbatim" or "zeroed".
    bool IsRedBlueBlend(const Color& c)
    {
        return c.getRProperty() >= 90 && c.getRProperty() <= 165 && c.getGProperty() <= 20 &&
               c.getBProperty() >= 90 && c.getBProperty() <= 165;
    }
}

class DiligentRenderTargetMipGenTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> graphicsDeviceManager_;
    SpriteBatch* spriteBatch_ = nullptr;
    Texture2D checkerboard_;
    bool done_ = false;
    int passCount_ = 0;
    int checkCount_ = 0;
    int result_ = 1;

    void Check(bool ok, const char* label)
    {
        ++checkCount_;
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label);
        if (ok)
            ++passCount_;
    }

protected:
    void LoadContent() override
    {
        auto& device = getGraphicsDeviceProperty();

        // Standard (x+y)%2 checkerboard: every aligned 2x2 block (anywhere in the grid) contains
        // exactly 2 Red + 2 Blue texels, so the whole-image average and every 2x2 block's average
        // are the identical ~50/50 blend -- level 1 AND level 2 both land on the same prediction.
        std::vector<std::uint8_t> pixels(kSize * kSize * 4);
        for (int y = 0; y < kSize; ++y)
        {
            for (int x = 0; x < kSize; ++x)
            {
                const bool isRed = ((x + y) % 2) == 0;
                std::uint8_t* p = &pixels[(y * kSize + x) * 4];
                p[0] = isRed ? 255 : 0;
                p[1] = 0;
                p[2] = isRed ? 0 : 255;
                p[3] = 255;
            }
        }
        checkerboard_ = Texture2D::CreateFromPixels(device, kSize, kSize, pixels);
        spriteBatch_ = new SpriteBatch(device);
    }

    void Draw(const GameTime&) override
    {
        if (done_)
            return;
        done_ = true;

        auto& device = getGraphicsDeviceProperty();

        RenderTarget2D target(device, kSize, kSize, /*mipMap=*/true, SurfaceFormat::Color,
                              DepthFormat::None);

        device.SetRenderTargets({RenderTargetBinding(&target)});
        device.Clear(kBlack);
        SamplerState pointClamp = SamplerState::PointClamp;
        spriteBatch_->Begin(SpriteSortMode::Deferred, BlendState::Opaque, &pointClamp, nullptr,
                            nullptr);
        spriteBatch_->Draw(checkerboard_, Rectangle(0, 0, kSize, kSize), Color::White);
        spriteBatch_->End();

        // Check A -- level 0 is the exact checkerboard BEFORE unbinding triggers GenerateMips.
        {
            std::vector<Color> level0(kSize * kSize, kBlack);
            const Rectangle whole(0, 0, kSize, kSize);
            target.GetData(0, &whole, level0.data(), 0, static_cast<int>(level0.size()));
            bool exact = true;
            for (int y = 0; y < kSize && exact; ++y)
                for (int x = 0; x < kSize && exact; ++x)
                {
                    const Color& want = ((x + y) % 2 == 0) ? kRed : kBlue;
                    if (!ColorEqual(level0[y * kSize + x], want))
                        exact = false;
                }
            Check(exact, "level 0 is the exact checkerboard before unbinding");
        }

        // Unbind: DiligentRenderTargetRenderer::UnbindAsRenderTarget() regenerates mips here.
        device.SetRenderTargets({});

        // Check B -- level 1 (2x2): every texel is a genuine Red/Blue blend.
        {
            std::vector<Color> level1(4, kBlack);
            const Rectangle whole(0, 0, 2, 2);
            target.GetData(1, &whole, level1.data(), 0, 4);
            for (int i = 0; i < 4; ++i)
            {
                char label[96];
                std::snprintf(label, sizeof(label),
                             "level 1 texel(%d,%d) is a real box-filter blend, not pure Red/Blue: "
                             "(%d,%d,%d)",
                             i % 2, i / 2, level1[i].getRProperty(), level1[i].getGProperty(),
                             level1[i].getBProperty());
                Check(IsRedBlueBlend(level1[i]), label);
            }
        }

        // Check C -- level 2 (1x1): the same 50/50 blend, averaging all 16 checkerboard texels.
        {
            std::vector<Color> level2(1, kBlack);
            const Rectangle whole(0, 0, 1, 1);
            target.GetData(2, &whole, level2.data(), 0, 1);
            char label[96];
            std::snprintf(label, sizeof(label), "level 2 (1x1) is a real whole-image blend: (%d,%d,%d)",
                         level2[0].getRProperty(), level2[0].getGProperty(),
                         level2[0].getBProperty());
            Check(IsRedBlueBlend(level2[0]), label);
        }

        // Check D -- level 0's own content is unaffected by mip generation.
        {
            std::vector<Color> level0(kSize * kSize, kBlack);
            const Rectangle whole(0, 0, kSize, kSize);
            target.GetData(0, &whole, level0.data(), 0, static_cast<int>(level0.size()));
            bool exact = true;
            for (int y = 0; y < kSize && exact; ++y)
                for (int x = 0; x < kSize && exact; ++x)
                {
                    const Color& want = ((x + y) % 2 == 0) ? kRed : kBlue;
                    if (!ColorNear(level0[y * kSize + x], want, 4))
                        exact = false;
                }
            Check(exact, "level 0 content unaffected by GenerateMips");
        }

        std::printf("=== %d/%d PASS ===\n", passCount_, checkCount_);
        result_ = passCount_ == checkCount_ ? 0 : 1;
        Exit();
    }

public:
    DiligentRenderTargetMipGenTest()
    {
        graphicsDeviceManager_ = std::make_unique<GraphicsDeviceManager>(this);
        graphicsDeviceManager_->setPreferredBackBufferWidthProperty(1);
        graphicsDeviceManager_->setPreferredBackBufferHeightProperty(1);
    }

    [[nodiscard]] int GetResult() const { return result_; }
};

int main()
{
    try
    {
        DiligentRenderTargetMipGenTest game;
        game.Run();
        return game.GetResult();
    }
    catch (const std::exception& error)
    {
        const std::string message = error.what();
        const bool noDevice = message.find("no device type could be created") != std::string::npos ||
                              message.find("unsupported SDL video driver") != std::string::npos ||
                              message.find("SDL_Init") != std::string::npos ||
                              message.find("live SDL window") != std::string::npos;
        if (noDevice)
        {
            std::printf("[SKIP] CNA Diligent smoke: no usable device (%s)\n", error.what());
            return 77;
        }
        std::printf("[FAIL] unexpected exception: %s\n", error.what());
        return 1;
    }
}
