// SPDX-License-Identifier: MS-PL
// Task 682: Verify Texture2D::FromStream (PNG/JPG/BMP/DDS) round-trip renders correctly
// when drawn via SDL_Renderer.
//
// The existing renderer-agnostic Texture2DTests.cpp (Task 262) already thoroughly verifies
// FromStream's DECODE correctness (PNG/JPEG/BMP byte layouts, resize/crop overload) via
// SetData/SaveAsPng/SaveAsJpeg round trips read back through Texture2D::GetData -- a pure
// CPU-side cache read (Task 678's finding), guaranteed correct on every renderer by
// construction and already exercised on SDL_Renderer whenever CnaTests runs there.
//
// The genuinely renderer-specific question here: does the REAL GPU texture SDL_Renderer
// creates from a FromStream-decoded image actually render the right pixels. Tracing the
// code: Texture2D::FromStream -> DecodeStreamToImageData (DDS via DxtUtil, everything else
// via the shared, renderer-agnostic ImageLoader::LoadFromMemory, built on vendored stb --
// independent of which CNA_GRAPHICS_RENDERER is active) -> MakeTextureFromPixels, which calls
// `device.GetRenderer().CreateTexture(img)` -- the EXACT SAME call site already used by
// Texture2D::SetData's full-array overload, which Task 678 already proved renders correctly
// on SDL_Renderer via a real SpriteBatch draw + framebuffer readback. FromStream should
// therefore be correct by construction, sharing that already-verified code path; this test
// confirms that holds for a real encoded-file round trip too, not just raw pixel arrays.
//
// Design: a 2x2 source texture with 4 distinct solid colours, encoded to PNG via the
// existing SaveAsPng (lossless, so exact colour match is expected), decoded back via
// FromStream, then drawn scaled up (16x16, PointClamp) via SpriteBatch and read back from
// the real framebuffer for all 4 quadrants.
//
// Requires PresentationMode::NativeBackBuffer (Task 915 finding): SDL_RenderReadPixels
// operates in physical output coordinates, while this renderer's default presentation mode
// (FixedHeightDynamicWidth) does not map logical pixels 1:1 to physical ones.
//
// Exit code 0 = all PASS, 1 = at least one FAIL.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PresentationParameters.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "System/IO/MemoryStream.hpp"

#include <cstdio>
#include <memory>
#include <vector>

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

class SdlTexture2DFromStreamTest : public Game
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
    SdlTexture2DFromStreamTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(16);
        gdm_->setPreferredBackBufferHeightProperty(16);
        gdm_->setPreferredPresentationModeProperty(PresentationMode::NativeBackBuffer);
    }

    int getResult() const { return result_; }
};

int main()
{
    SdlTexture2DFromStreamTest game;
    game.Run();
    return game.getResult();
}
