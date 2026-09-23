// SPDX-License-Identifier: MS-PL
//
// plans/plan_sdlgpu_modern_graphics.md SMG-0038: SDL_gpu's Vulkan memory defragmenter, driven on
// purpose rather than by allocator luck.
//
// SDL 3.5.0 at the vendored pin crashed the first time it defragmented a texture allocation: it
// barriered the replacement image before giving it a container, and the barrier read the container
// (SMG-0034; upstream f286e420). CNAEXT_PointShadow reached it by accident. This program reaches it
// every time, through the public API alone, by building the one state that makes SDL defragment:
//
//   1. small textures (under SDL's 2 MiB threshold) share one 16 MiB block;
//   2. disposing one of them leaves that block with a hole as well as its tail -- more than one
//      free region, which is SDL's definition of fragmented;
//   3. a texture over 2 MiB fits in no existing region, and on that miss SDL marks every
//      fragmented block for defragmentation;
//   4. the next presenting submit defragments one: every surviving image is copied to a new one.
//
// After it, the surviving textures must still hold exactly what was uploaded into them -- the
// defragmenter's copy is the other thing this proves.
//
// Exit code 0 = all checks PASS, 1 = any FAIL, 77 = no GPU display.

#include "common/PixelTestGame.hpp"

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"

#include <cstdio>
#include <memory>
#include <string>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kSmall = 256;        // 256 KiB of RGBA8: well under SDL's small threshold
    constexpr int kLarge = 1024;       // 4 MiB: over it, so no small block can take it
    constexpr int kSmallCount = 8;
    constexpr int kRounds = 3;         // three separate defragmentations, not one lucky one

    int checks = 0;
    int failures = 0;
    int roundsRun = 0;

    void Check(const bool ok, const std::string& label)
    {
        ++checks;
        if (!ok) ++failures;
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label.c_str());
        std::fflush(stdout);
    }

    Color Pattern(const int texture, const int texel)
    {
        return Color(static_cast<int>((texture * 37 + texel) & 0xFF),
                     static_cast<int>((texel >> 8) & 0xFF),
                     static_cast<int>((texture * 91) & 0xFF), 255);
    }
}

class TextureDefragmentTest final : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    std::vector<std::unique_ptr<Texture2D>> small_;
    std::vector<std::unique_ptr<Texture2D>> large_;
    int frame_ = 0;
    int round_ = 0;

    void Fill(Texture2D& texture, const int index, const int size)
    {
        std::vector<Color> texels(static_cast<std::size_t>(size) * size);
        for (std::size_t i = 0; i < texels.size(); ++i)
            texels[i] = Pattern(index, static_cast<int>(i));
        texture.SetData(texels.data(), static_cast<int>(texels.size()));
    }

    bool Intact(const Texture2D& texture, const int index)
    {
        std::vector<Color> texels(static_cast<std::size_t>(kSmall) * kSmall);
        texture.GetData(texels.data(), static_cast<int>(texels.size()));
        for (std::size_t i = 0; i < texels.size(); ++i)
            if (texels[i] != Pattern(index, static_cast<int>(i))) return false;
        return true;
    }

protected:
    void Draw(const GameTime&) override
    {
        GraphicsDevice& device = getGraphicsDeviceProperty();
        device.Clear(Color::Black);
        // One presented frame between steps, so each release SDL defers is processed by the
        // submit that follows it before the next step depends on it.
        const int step = frame_++ % 4;
        if (step == 0)
        {
            small_.clear();
            for (int i = 0; i < kSmallCount; ++i)
            {
                small_.push_back(std::make_unique<Texture2D>(device, kSmall, kSmall));
                Fill(*small_.back(), i, kSmall);
            }
        }
        else if (step == 1)
        {
            // Holes between survivors: the block now has several free regions.
            for (int i = 1; i < kSmallCount; i += 2) small_[static_cast<std::size_t>(i)].reset();
        }
        else if (step == 2)
        {
            // Fits nowhere that exists, so SDL marks the fragmented block; the Present() that
            // ends this frame is the submit that defragments it.
            large_.push_back(std::make_unique<Texture2D>(device, kLarge, kLarge));
            Fill(*large_.back(), 100 + round_, kLarge);
        }
        else
        {
            bool intact = true;
            for (int i = 0; i < kSmallCount; i += 2)
                intact = intact && Intact(*small_[static_cast<std::size_t>(i)], i);
            Check(intact, "round " + std::to_string(round_ + 1) +
                              ": every surviving texture reads back exactly after defragmentation");
            roundsRun = ++round_;
            if (round_ == kRounds) Exit();
        }
    }

public:
    TextureDefragmentTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(64);
        gdm_->setPreferredBackBufferHeightProperty(64);
    }
};

int main()
{
    if (!CNA::Examples::ProbeGpuDisplayAvailable())
        return CNA::Examples::kSkipExitCode;
    {
        TextureDefragmentTest game;
        game.Run();
    }
    Check(roundsRun == kRounds, "all " + std::to_string(kRounds) + " rounds ran to completion");
    std::printf("=== %d/%d PASS ===\n", checks - failures, checks);
    return failures == 0 ? 0 : 1;
}
