// SPDX-License-Identifier: MS-PL
// Task 682: a Texture2D::FromStream PNG round trip renders the right pixels through the GPU.
//
// Texture2DTests.cpp (Task 262) already verifies FromStream's decoding through Texture2D::GetData,
// which reads the CPU-side copy. This program asks the question GetData cannot answer: does the
// texture the renderer creates from a decoded file draw the right pixels.
//
// Design: a 2x2 source texture with four distinct solid colours is encoded to PNG with SaveAsPng
// (lossless, so an exact colour match is expected), decoded back with FromStream, drawn scaled
// up (16x16, PointClamp) through SpriteBatch, and read back from the framebuffer per quadrant.
// Runs with PresentationMode::NativeBackBuffer so logical and physical pixels map 1:1.
//
// First written for the retired SDL_RENDERER renderer.
//
// Exit code 0 = all PASS, 1 = at least one FAIL.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsProfile.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PresentationParameters.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "System/IO/MemoryStream.hpp"

#include <cstdio>
#include <memory>
#include <vector>
#include "ConformanceSupport.hpp"

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

static bool colourMatch(Color got, Color want, int tol = 40)
{
    return std::abs((int)got.getRProperty() - (int)want.getRProperty()) <= tol
        && std::abs((int)got.getGProperty() - (int)want.getGProperty()) <= tol
        && std::abs((int)got.getBProperty() - (int)want.getBProperty()) <= tol;
}

static const Color kRed   (255,   0,   0, 255);
static const Color kGreen (  0, 255,   0, 255);
static const Color kBlue  (  0,   0, 255, 255);
static const Color kYellow(255, 255,   0, 255);

class Texture2DFromStreamTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    std::unique_ptr<SpriteBatch>           sb_;
    std::unique_ptr<Texture2D>             tex_;

    bool done_   = false;
    int  result_ = 0;

protected:
    void Initialize() override
    {
        Game::Initialize();
        auto& dev = getGraphicsDeviceProperty();
        sb_ = std::make_unique<SpriteBatch>(dev);

        // 2x2 source: Red, Green, Blue, Yellow (row-major).
        Texture2D src(dev, 2, 2);
        const Color pixels[4] = { kRed, kGreen, kBlue, kYellow };
        src.SetData(pixels, 4);

        System::IO::MemoryStream writeStream;
        src.SaveAsPng(&writeStream, 2, 2);
        auto bytes = writeStream.GetBuffer();

        System::IO::MemoryStream readStream(bytes.data(), static_cast<System::IO::intcs>(bytes.size()));
        tex_ = std::make_unique<Texture2D>(Texture2D::FromStream(dev, readStream));
    }

    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;

        auto& dev = getGraphicsDeviceProperty();
        dev.Clear(Color(0, 0, 0, 255));
        dev.setBlendStateProperty(BlendState::Opaque);

        if (tex_->getWidthProperty() != 2 || tex_->getHeightProperty() != 2)
        {
            std::printf("[FAIL] FromStream decoded dimensions: got=%dx%d want=2x2\n",
                tex_->getWidthProperty(), tex_->getHeightProperty());
            result_ = 1;
        }
        else
        {
            std::printf("[PASS] FromStream decoded dimensions == 2x2\n");
        }

        // 2x2 source scaled 8x per texel -> 16x16 destination.
        sb_->Begin(SpriteSortMode::Deferred,
                   BlendState::Opaque,
                   const_cast<SamplerState*>(&SamplerState::PointClamp),
                   nullptr, nullptr, nullptr, Matrix::getIdentityProperty());
        sb_->Draw(*tex_, Rectangle(0, 0, 16, 16), Rectangle(0, 0, 2, 2), Color::White);
        sb_->End();

        struct Check { int x; int y; Color want; const char* label; };
        const Check checks[] = {
            {  4,  4, kRed,    "(0,0) texel -> Red"    },
            { 12,  4, kGreen,  "(1,0) texel -> Green"  },
            {  4, 12, kBlue,   "(0,1) texel -> Blue"   },
            { 12, 12, kYellow, "(1,1) texel -> Yellow" },
        };

        for (const auto& c : checks)
        {
            Color px(0, 0, 0, 0);
            Rectangle reg(c.x, c.y, 1, 1);
            dev.GetBackBufferData(&reg, &px, 0, 1);
            const bool ok = colourMatch(px, c.want);
            std::printf("[%s] %s: got=(%d,%d,%d)\n",
                ok ? "PASS" : "FAIL", c.label,
                px.getRProperty(), px.getGProperty(), px.getBProperty());
            if (!ok) result_ = 1;
        }

        Exit();
    }

public:
    Texture2DFromStreamTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        // Vulkan parity VKPAR-0018: this test uses a HiDef-only feature
        // (GetBackBufferData, volume textures, multiple render targets, separate alpha blending,
        // mipmapped non-power-of-two surfaces, occlusion queries or float targets), and CNA
        // enforces XNA's Reach profile, which GraphicsDeviceManager defaults to -- so under Reach
        // it failed before reaching its subject.
        gdm_->setGraphicsProfileProperty(Microsoft::Xna::Framework::Graphics::GraphicsProfile::HiDef);
        gdm_->setPreferredBackBufferWidthProperty(16);
        gdm_->setPreferredBackBufferHeightProperty(16);
        gdm_->setPreferredPresentationModeProperty(PresentationMode::NativeBackBuffer);
    }

    int getResult() const { return result_; }
};

int main()
{
    if (!CNA::Examples::Conformance::ProbePixelOutputAvailable("texture2d_fromstream"))
        return CNA::Examples::Conformance::kSkipExitCode;
    Texture2DFromStreamTest game;
    game.Run();
    return game.getResult();
}
