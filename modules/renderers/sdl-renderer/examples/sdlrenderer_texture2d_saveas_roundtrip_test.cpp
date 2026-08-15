// SPDX-License-Identifier: MS-PL
// Task 683: Verify SaveAsPng/SaveAsJpeg round-trip when the source texture was
// created/updated through SDL_Renderer.
//
// SaveAsPng/SaveAsJpeg (shared, renderer-agnostic Texture2D.cpp) operate ENTIRELY on the
// CPU-side pixel shadow cache (cpuPixels_) via the shared vendored-stb image backend -- neither
// ever touches renderer_ or the real GPU
// texture at all. cpuPixels_ is populated identically regardless of which renderer created or
// updated the texture (confirmed: Texture2D::MaybeFreeCpuPixels() only frees it when
// GraphicsDevice::contextRecoveryEnabled_ is false, and that defaults to true -- a
// GraphicsDevice-level flag, not a renderer-specific one). So the CPU-side save path is
// guaranteed correct by construction once SetData has correctly populated cpuPixels_, which
// Tasks 678-680 already proved true for SDL_Renderer's real renderer texture.
//
// This test verifies that guarantee holds end-to-end for a texture that was actually
// SetData'd through the real SDL_Renderer renderer: SetData -> SaveAsPng/SaveAsJpeg -> a
// fresh FromStream decode -> drawn via SpriteBatch and read back from the real framebuffer
// (reusing Task 682's already-proven "draw + real readback" method, chained onto the save
// path this task is actually about).
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

static bool colourMatch(Color got, Color want, int tol)
{
    return std::abs((int)got.getRProperty() - (int)want.getRProperty()) <= tol
        && std::abs((int)got.getGProperty() - (int)want.getGProperty()) <= tol
        && std::abs((int)got.getBProperty() - (int)want.getBProperty()) <= tol;
}

static const Color kRed   (255,   0,   0, 255);
static const Color kGreen (  0, 255,   0, 255);
static const Color kBlue  (  0,   0, 255, 255);
static const Color kYellow(255, 255,   0, 255);

class SdlTexture2DSaveAsRoundTripTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    std::unique_ptr<SpriteBatch>           sb_;
    std::unique_ptr<Texture2D>             pngTex_;
    std::unique_ptr<Texture2D>             jpegTex_;

    bool done_   = false;
    int  result_ = 0;

    void check(bool ok, const char* label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label);
        if (!ok) result_ = 1;
    }

protected:
    void Initialize() override
    {
        Game::Initialize();
        auto& dev = getGraphicsDeviceProperty();
        sb_ = std::make_unique<SpriteBatch>(dev);

        // 2x2 source, updated through the real SDL_Renderer renderer (full-array SetData
        // reaches SdlTextureRenderer's constructor, per Task 678's already-verified path).
        Texture2D src(dev, 2, 2);
        const Color pixels[4] = { kRed, kGreen, kBlue, kYellow };
        src.SetData(pixels, 4);

        System::IO::MemoryStream pngStream;
        src.SaveAsPng(&pngStream, 2, 2);
        auto pngBytes = pngStream.GetBuffer();
        check(!pngBytes.empty(), "SaveAsPng produced non-empty output");

        System::IO::MemoryStream jpegStream;
        src.SaveAsJpeg(&jpegStream, 2, 2);
        auto jpegBytes = jpegStream.GetBuffer();
        check(!jpegBytes.empty(), "SaveAsJpeg produced non-empty output");

        System::IO::MemoryStream pngRead(pngBytes.data(), static_cast<System::IO::intcs>(pngBytes.size()));
        pngTex_ = std::make_unique<Texture2D>(Texture2D::FromStream(dev, pngRead));

        System::IO::MemoryStream jpegRead(jpegBytes.data(), static_cast<System::IO::intcs>(jpegBytes.size()));
        jpegTex_ = std::make_unique<Texture2D>(Texture2D::FromStream(dev, jpegRead));
    }

    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;

        auto& dev = getGraphicsDeviceProperty();
        dev.Clear(Color(0, 0, 0, 255));
        dev.setBlendStateProperty(BlendState::Opaque);

        check(pngTex_->getWidthProperty() == 2 && pngTex_->getHeightProperty() == 2,
              "PNG-decoded texture dimensions == 2x2");
        check(jpegTex_->getWidthProperty() == 2 && jpegTex_->getHeightProperty() == 2,
              "JPEG-decoded texture dimensions == 2x2");

        sb_->Begin(SpriteSortMode::Deferred,
                   BlendState::Opaque,
                   const_cast<SamplerState*>(&SamplerState::PointClamp),
                   nullptr, nullptr, nullptr, Matrix::getIdentityProperty());
        // PNG round trip drawn scaled up into (0,0)-(16,16).
        sb_->Draw(*pngTex_, Rectangle(0, 0, 16, 16), Rectangle(0, 0, 2, 2), Color::White);
        // JPEG round trip drawn scaled up into (16,0)-(32,16).
        sb_->Draw(*jpegTex_, Rectangle(16, 0, 16, 16), Rectangle(0, 0, 2, 2), Color::White);
        sb_->End();

        struct Check { int x; int y; Color want; const char* label; int tol; };
        const Check checks[] = {
            // PNG is lossless -- tight tolerance.
            {  4,  4, kRed,    "PNG (0,0) -> Red",     10 },
            { 12,  4, kGreen,  "PNG (1,0) -> Green",   10 },
            {  4, 12, kBlue,   "PNG (0,1) -> Blue",    10 },
            { 12, 12, kYellow, "PNG (1,1) -> Yellow",  10 },
            // JPEG is lossy -- wider tolerance, matching Texture2DTests.cpp's own convention.
            { 20,  4, kRed,    "JPEG (0,0) -> Red",    40 },
            { 28,  4, kGreen,  "JPEG (1,0) -> Green",  40 },
            { 20, 12, kBlue,   "JPEG (0,1) -> Blue",   40 },
            { 28, 12, kYellow, "JPEG (1,1) -> Yellow", 40 },
        };

        for (const auto& c : checks)
        {
            Color px(0, 0, 0, 0);
            Rectangle reg(c.x, c.y, 1, 1);
            dev.GetBackBufferData(&reg, &px, 0, 1);
            const bool ok = colourMatch(px, c.want, c.tol);
            std::printf("[%s] %s: got=(%d,%d,%d)\n",
                ok ? "PASS" : "FAIL", c.label,
                px.getRProperty(), px.getGProperty(), px.getBProperty());
            if (!ok) result_ = 1;
        }

        Exit();
    }

public:
    SdlTexture2DSaveAsRoundTripTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(32);
        gdm_->setPreferredBackBufferHeightProperty(16);
        gdm_->setPreferredPresentationModeProperty(PresentationMode::NativeBackBuffer);
    }

    int getResult() const { return result_; }
};

int main()
{
    SdlTexture2DSaveAsRoundTripTest game;
    game.Run();
    return game.getResult();
}
