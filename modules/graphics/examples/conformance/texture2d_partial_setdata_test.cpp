// SPDX-License-Identifier: MS-PL
// A partial Texture2D.SetData reaches the GPU texture a draw samples, not only the CPU copy.
//
// Texture2DTests.cpp and PartialTextureTransferTests.cpp verify the partial overloads through
// GetData, which can be answered from a CPU-side copy. This program asks what GetData cannot: after
// SetData(level, rect, data, startIndex, elementCount), does the texture the renderer SAMPLES hold
// the merged pixels. Every result is read back from the framebuffer after a SpriteBatch draw
// (PointClamp, 4 screen pixels per texel), never from the texture.
//
//   A -- a 4x4 texture filled red with the whole-level overload, then a 2x2 blue rectangle written
//        at (1,1):                         R R R R / R B B R / R B B R / R R R R
//   B -- a 4x1 texture filled red, then a 6-element source [G G B B G G] written into rect (1,0,2,1)
//        with startIndex 2, elementCount 2, so only the two blue elements are read:  R B B R
//   C -- a further one-texel rectangle written into A AFTER A has already been drawn and read back:
//        the update reaches a texture the GPU has used, and leaves the rest of it alone.
//
// Runs with PresentationMode::NativeBackBuffer so logical and physical pixels map 1:1.
//
// First written for the retired SDL_RENDERER renderer (Tasks 679 and 680).
//
// Exit code 0 = all PASS, 1 = at least one FAIL.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsProfile.hpp"
#include "Microsoft/Xna/Framework/Graphics/PresentationParameters.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"

#include <cstdio>
#include <cstdlib>
#include <memory>
#include <vector>
#include "ConformanceSupport.hpp"

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    const Color kRed  (255,   0,   0, 255);
    const Color kGreen(  0, 255,   0, 255);
    const Color kBlue (  0,   0, 255, 255);

    bool colourMatch(const Color& got, const Color& want, int tol = 40)
    {
        return std::abs(got.getRProperty() - want.getRProperty()) <= tol
            && std::abs(got.getGProperty() - want.getGProperty()) <= tol
            && std::abs(got.getBProperty() - want.getBProperty()) <= tol;
    }

    struct Probe { int x; int y; Color want; const char* label; };
}

class Texture2DPartialSetDataTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    std::unique_ptr<SpriteBatch>           sb_;
    std::unique_ptr<Texture2D>             rectTex_;
    std::unique_ptr<Texture2D>             sliceTex_;

    bool done_   = false;
    int  result_ = 0;

    void drawBoth(GraphicsDevice& dev)
    {
        dev.Clear(Color(0, 0, 0, 255));
        sb_->Begin(SpriteSortMode::Deferred, BlendState::Opaque,
                   const_cast<SamplerState*>(&SamplerState::PointClamp),
                   nullptr, nullptr, nullptr, Matrix::getIdentityProperty());
        // A: 4x4 texels -> 16x16 pixels at the left. B: 4x1 texels -> 16x4 pixels at the right.
        sb_->Draw(*rectTex_, Rectangle(0, 0, 16, 16), Rectangle(0, 0, 4, 4), Color::White);
        sb_->Draw(*sliceTex_, Rectangle(16, 0, 16, 4), Rectangle(0, 0, 4, 1), Color::White);
        sb_->End();
    }

    void probe(GraphicsDevice& dev, const Probe& p)
    {
        Color px(0, 0, 0, 0);
        const Rectangle region(p.x, p.y, 1, 1);
        dev.GetBackBufferData(&region, &px, 0, 1);
        const bool ok = colourMatch(px, p.want);
        std::printf("[%s] %s: got=(%d,%d,%d) want=(%d,%d,%d)\n", ok ? "PASS" : "FAIL", p.label,
                    px.getRProperty(), px.getGProperty(), px.getBProperty(),
                    p.want.getRProperty(), p.want.getGProperty(), p.want.getBProperty());
        if (!ok) result_ = 1;
    }

protected:
    void Initialize() override
    {
        Game::Initialize();
        auto& dev = getGraphicsDeviceProperty();
        sb_ = std::make_unique<SpriteBatch>(dev);

        rectTex_ = std::make_unique<Texture2D>(dev, 4, 4);
        const std::vector<Color> allRed16(16, kRed);
        rectTex_->SetData(0, nullptr, allRed16.data(), 0, 16);
        const Rectangle subRect(1, 1, 2, 2);
        const Color blue4[4] = {kBlue, kBlue, kBlue, kBlue};
        rectTex_->SetData(0, &subRect, blue4, 0, 4);

        sliceTex_ = std::make_unique<Texture2D>(dev, 4, 1);
        const std::vector<Color> allRed4(4, kRed);
        sliceTex_->SetData(0, nullptr, allRed4.data(), 0, 4);
        const Rectangle middle(1, 0, 2, 1);
        const Color source[6] = {kGreen, kGreen, kBlue, kBlue, kGreen, kGreen};
        sliceTex_->SetData(0, &middle, source, 2, 2);
    }

    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;

        auto& dev = getGraphicsDeviceProperty();
        drawBoth(dev);

        const Probe first[] = {
            { 2,  2, kRed,  "A: (0,0) texel outside the rectangle stays red"},
            {14,  2, kRed,  "A: (3,0) texel outside the rectangle stays red"},
            { 2, 14, kRed,  "A: (0,3) texel outside the rectangle stays red"},
            {14, 14, kRed,  "A: (3,3) texel outside the rectangle stays red"},
            { 6,  6, kBlue, "A: (1,1) texel inside the rectangle is blue"},
            {10,  6, kBlue, "A: (2,1) texel inside the rectangle is blue"},
            { 6, 10, kBlue, "A: (1,2) texel inside the rectangle is blue"},
            {10, 10, kBlue, "A: (2,2) texel inside the rectangle is blue"},
            {18,  2, kRed,  "B: texel 0 outside the slice stays red"},
            {22,  2, kBlue, "B: texel 1 is source element 2 (blue), not element 0 (green)"},
            {26,  2, kBlue, "B: texel 2 is source element 3 (blue)"},
            {30,  2, kRed,  "B: texel 3 outside the slice stays red"},
        };
        for (const auto& p : first) probe(dev, p);

        // C: A has been sampled and read back; a later one-texel update must still reach it.
        const Rectangle corner(3, 3, 1, 1);
        const Color green1[1] = {kGreen};
        rectTex_->SetData(0, &corner, green1, 0, 1);
        drawBoth(dev);

        const Probe second[] = {
            {14, 14, kGreen, "C: a rectangle written after the texture was drawn reaches the GPU copy"},
            {10, 10, kBlue,  "C: the earlier blue rectangle survives the later update"},
            { 2,  2, kRed,   "C: texels outside both rectangles stay red"},
        };
        for (const auto& p : second) probe(dev, p);

        Exit();
    }

public:
    Texture2DPartialSetDataTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        // GetBackBufferData is HiDef-only, as in XNA 4.0.
        gdm_->setGraphicsProfileProperty(GraphicsProfile::HiDef);
        gdm_->setPreferredBackBufferWidthProperty(32);
        gdm_->setPreferredBackBufferHeightProperty(16);
        gdm_->setPreferredPresentationModeProperty(PresentationMode::NativeBackBuffer);
    }

    int getResult() const { return result_; }
};

int main()
{
    if (!CNA::Examples::Conformance::ProbePixelOutputAvailable("texture2d_partial_setdata"))
        return CNA::Examples::Conformance::kSkipExitCode;
    Texture2DPartialSetDataTest game;
    game.Run();
    return game.getResult();
}
