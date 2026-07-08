// SPDX-License-Identifier: MS-PL
// Task 702: Verify default sampler state matches FNA's LinearClamp on SDL_Renderer.
//
// SpriteBatch::Begin's own source (SpriteBatch.cpp) resolves a null/omitted samplerState to
// SamplerState::LinearClamp ("Matches FNA: a null samplerState defaults to
// SamplerState.LinearClamp"), which is Filter=Linear, AddressU=AddressV=Clamp. Rather than
// re-deriving from first principles what the exact expected pixel values should be at some
// hand-picked sample point (fragile at this backbuffer's low resolution -- an earlier version of
// this test tried exactly that with a combined boundary+clamp sample and got ambiguous partial-
// blend results that were hard to interpret), this test instead directly proves the actual claim:
// that calling SpriteBatch::Begin() with NO arguments produces byte-identical rendered output to
// calling SpriteBatch::Begin() with an EXPLICIT SamplerState::LinearClamp, for the same draw. This
// sidesteps needing to know what "correct" looks like in absolute terms and only checks that the
// two code paths -- default-resolution and explicit-LinearClamp -- are indistinguishable, which is
// exactly what "the default is LinearClamp" means operationally.
//
// Samples multiple points across the draw (including the Red/Blue texel boundary and a point past
// the texture's own bounds via an oversized sourceRectangle, Task 685's methodology) so that if the
// default resolved to some OTHER SamplerState (e.g. PointClamp, LinearWrap), at least one of these
// sample points would very likely diverge between the two renders.
//
// Requires PresentationMode::NativeBackBuffer (Task 915 finding): SDL_RenderReadPixels operates
// in physical output coordinates, while this backend's default presentation mode
// (FixedHeightDynamicWidth) does not map logical pixels 1:1 to physical ones.
//
// Exit code 0 = all sample points match between default Begin() and explicit LinearClamp,
// 1 = at least one diverges.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PresentationParameters.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"

#include <cstdio>
#include <memory>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

class SdlSamplerStateDefaultTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    std::unique_ptr<SpriteBatch>           sb_;
    std::unique_ptr<Texture2D>             tex_; // 2x1: [Red, Blue]

    bool done_   = false;
    int  result_ = 0;

    // Renders the same draw with the given samplerState (nullptr = the no-arg Begin() overload,
    // exercising the full default chain) and returns a handful of sample points across it.
    std::vector<Color> RenderAndSample(SamplerState* samplerState)
    {
        auto& device = getGraphicsDeviceProperty();
        const auto& vp = device.getViewportProperty();
        const int W = vp.getWidthProperty();
        const int H = vp.getHeightProperty();

        device.Clear(Color(0, 0, 0, 255));
        if (samplerState)
            sb_->Begin(SpriteSortMode::Deferred, BlendState::Opaque, samplerState, nullptr, nullptr);
        else
            sb_->Begin();
        // Oversized sourceRectangle (2x the texture's own width, Task 685's methodology) so the
        // sampled source spans texel range [0,4] on a 2-texel-wide texture -- exercises both the
        // Linear-filter texel-boundary blend AND the Clamp-address out-of-bounds behavior in one
        // draw.
        sb_->Draw(*tex_, Rectangle(0, 0, W, H), Rectangle(0, 0, 4, 1), Color::White);
        sb_->End();

        std::vector<Color> samples;
        const int xs[] = {W / 8, W / 4, 3 * W / 8, W / 2, 5 * W / 8, 3 * W / 4, 7 * W / 8, W - 1};
        for (int x : xs)
        {
            Color pixel(0, 0, 0, 0);
            const Rectangle region(x, H / 2, 1, 1);
            device.GetBackBufferData(&region, &pixel, 0, 1);
            samples.push_back(pixel);
        }
        return samples;
    }

protected:
    void Initialize() override
    {
        Game::Initialize();
        auto& dev = getGraphicsDeviceProperty();
        sb_ = std::make_unique<SpriteBatch>(dev);

        const std::vector<std::uint8_t> px = {
            255,   0,   0, 255,   // texel 0: red
              0,   0, 255, 255    // texel 1: blue
        };
        tex_ = std::make_unique<Texture2D>(Texture2D::CreateFromPixels(dev, 2, 1, px));
    }

    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;

        const std::vector<Color> defaultSamples = RenderAndSample(nullptr);
        const std::vector<Color> explicitSamples =
            RenderAndSample(const_cast<SamplerState*>(&SamplerState::LinearClamp));

        bool allMatch = true;
        for (std::size_t i = 0; i < defaultSamples.size(); ++i)
        {
            const Color& a = defaultSamples[i];
            const Color& b = explicitSamples[i];
            const bool match = a.getRProperty() == b.getRProperty()
                             && a.getGProperty() == b.getGProperty()
                             && a.getBProperty() == b.getBProperty();
            if (!match) allMatch = false;
            std::printf("[%s] sample %zu: default=(%d,%d,%d) explicit-LinearClamp=(%d,%d,%d)\n",
                        match ? "PASS" : "FAIL", i,
                        a.getRProperty(), a.getGProperty(), a.getBProperty(),
                        b.getRProperty(), b.getGProperty(), b.getBProperty());
        }

        std::printf("[%s] Default SamplerState (no Begin() arg) matches explicit SamplerState::LinearClamp across all sample points\n",
                    allMatch ? "PASS" : "FAIL");
        if (!allMatch) result_ = 1;

        Exit();
    }

public:
    SdlSamplerStateDefaultTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(64);
        gdm_->setPreferredBackBufferHeightProperty(8);
        gdm_->setPreferredPresentationModeProperty(PresentationMode::NativeBackBuffer);
    }

    int getResult() const { return result_; }
};

int main()
{
    SdlSamplerStateDefaultTest game;
    game.Run();
    return game.getResult();
}
