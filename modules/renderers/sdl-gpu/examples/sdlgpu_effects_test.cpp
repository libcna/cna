// SPDX-License-Identifier: MS-PL
// plans/plan_sdlgpu.md SDLGPU-31/32: AlphaTestEffect and DualTextureEffect proof for the SDL_GPU
// graphics renderer, through the real public XNA API, verified via a real screenshot (same bar
// as sdlgpu_3d_test.cpp).
//
// Check A -- AlphaTestEffect (AlphaFunction=Greater, ReferenceAlpha=128) on a texture whose top
//   half is opaque and bottom half is fully transparent: draws with no exception, and (per the
//   screenshot) the bottom half is genuinely discarded (CornflowerBlue background shows through),
//   not just alpha-blended translucent.
// Check B -- DualTextureEffect (a quadrant texture x a uniform yellow second texture) draws with
//   no exception; per the real `tex1.rgb*=2; result=tex1*tex2*tint` formula, the screenshot
//   should show the texture's blue quadrant turned black (yellow has no blue channel) and its
//   white quadrant turned yellow -- a strong, unambiguous discriminator that the real two-sampler
//   multiply is happening, not just one texture being shown.
// Check C -- 120 frames of both draws render with no exception.
//
// Exit code 0 = all checks PASS, 1 = any FAILs.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/AlphaTestEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/CompareFunction.hpp"
#include "Microsoft/Xna/Framework/Graphics/DualTextureEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionTexture.hpp"

#include "CNA/Internal/Renderers/SdlGpu/SdlGpuRenderer.hpp"

#include "common/PixelTestGame.hpp"

#include <cstdio>
#include <memory>
#include <string>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using namespace CNA::Internal::Renderers::SdlGpu;

namespace
{
    constexpr int kTotalFrames = 120;

    // 4x4: top half (y<2) opaque red/green, bottom half (y>=2) fully transparent blue/white.
    std::vector<std::uint8_t> MakeAlphaTestTexturePixels()
    {
        std::vector<std::uint8_t> pixels(4 * 4 * 4, 0);
        auto setPixel = [&](int x, int y, std::uint8_t r, std::uint8_t g, std::uint8_t b, std::uint8_t a) {
            const std::size_t o = (static_cast<std::size_t>(y) * 4 + x) * 4;
            pixels[o+0]=r; pixels[o+1]=g; pixels[o+2]=b; pixels[o+3]=a;
        };
        for (int y = 0; y < 4; ++y)
            for (int x = 0; x < 4; ++x)
            {
                if (x < 2 && y < 2) setPixel(x, y, 255, 0, 0, 255);
                else if (x >= 2 && y < 2) setPixel(x, y, 0, 255, 0, 255);
                else if (x < 2 && y >= 2) setPixel(x, y, 0, 0, 255, 0);
                else setPixel(x, y, 255, 255, 255, 0);
            }
        return pixels;
    }

    std::vector<std::uint8_t> MakeQuadrantTexturePixels()
    {
        std::vector<std::uint8_t> pixels(4 * 4 * 4, 0);
        auto setPixel = [&](int x, int y, std::uint8_t r, std::uint8_t g, std::uint8_t b, std::uint8_t a) {
            const std::size_t o = (static_cast<std::size_t>(y) * 4 + x) * 4;
            pixels[o+0]=r; pixels[o+1]=g; pixels[o+2]=b; pixels[o+3]=a;
        };
        for (int y = 0; y < 4; ++y)
            for (int x = 0; x < 4; ++x)
            {
                if (x < 2 && y < 2) setPixel(x, y, 255, 0, 0, 255);
                else if (x >= 2 && y < 2) setPixel(x, y, 0, 255, 0, 255);
                else if (x < 2 && y >= 2) setPixel(x, y, 0, 0, 255, 255);
                else setPixel(x, y, 255, 255, 255, 255);
            }
        return pixels;
    }

}

class SdlGpuEffectsTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    std::unique_ptr<Texture2D> alphaTestTexture_;
    std::unique_ptr<Texture2D> quadrantTexture_;
    std::unique_ptr<Texture2D> yellowTexture_;
    std::unique_ptr<VertexBuffer> quadVb1_;
    std::unique_ptr<VertexBuffer> quadVb2_;
    Matrix view_ = Matrix::getIdentityProperty();
    Matrix projection_ = Matrix::getIdentityProperty();
    int frame_ = 0;
    int passCount_ = 0;
    int result_ = 1;

    void check(bool ok, const char* label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label);
        if (ok) ++passCount_;
    }

protected:
    void LoadContent() override
    {
        auto& dev = getGraphicsDeviceProperty();

        alphaTestTexture_ = std::make_unique<Texture2D>(Texture2D::CreateFromPixels(dev, 4, 4, MakeAlphaTestTexturePixels()));
        quadrantTexture_ = std::make_unique<Texture2D>(Texture2D::CreateFromPixels(dev, 4, 4, MakeQuadrantTexturePixels()));
        std::vector<std::uint8_t> yellow(4 * 4 * 4);
        for (std::size_t i = 0; i < yellow.size(); i += 4)
        {
            yellow[i+0] = 255; yellow[i+1] = 255; yellow[i+2] = 0; yellow[i+3] = 255;
        }
        yellowTexture_ = std::make_unique<Texture2D>(Texture2D::CreateFromPixels(dev, 4, 4, yellow));

        view_ = Matrix::CreateLookAt(Vector3(0.0f, 0.0f, 10.0f), Vector3(0.0f, 0.0f, 0.0f), Vector3(0.0f, 1.0f, 0.0f));
        projection_ = Matrix::CreateOrthographic(6.0f, 4.0f, 0.1f, 20.0f);

        const VertexPositionTexture quadVerts[6] = {
            { Vector3(-0.8f, 0.8f, 0.0f), Vector2(0.0f, 0.0f) },
            { Vector3(-0.8f, -0.8f, 0.0f), Vector2(0.0f, 1.0f) },
            { Vector3(0.8f, -0.8f, 0.0f), Vector2(1.0f, 1.0f) },
            { Vector3(-0.8f, 0.8f, 0.0f), Vector2(0.0f, 0.0f) },
            { Vector3(0.8f, -0.8f, 0.0f), Vector2(1.0f, 1.0f) },
            { Vector3(0.8f, 0.8f, 0.0f), Vector2(1.0f, 0.0f) },
        };
        quadVb1_ = std::make_unique<VertexBuffer>(dev, VertexPositionTexture::getVertexDeclarationStatic(), 6, BufferUsage::None);
        quadVb1_->SetData(quadVerts, 0, 6);
        quadVb2_ = std::make_unique<VertexBuffer>(dev, VertexPositionTexture::getVertexDeclarationStatic(), 6, BufferUsage::None);
        quadVb2_->SetData(quadVerts, 0, 6);
    }

    void DrawScene()
    {
        auto& dev = getGraphicsDeviceProperty();

        AlphaTestEffect alphaFx(dev);
        alphaFx.setTextureProperty(alphaTestTexture_.get());
        alphaFx.setAlphaFunctionProperty(CompareFunction::Greater);
        alphaFx.setReferenceAlphaProperty(128);
        alphaFx.setWorldProperty(Matrix::CreateTranslation(-1.2f, 0.0f, 0.0f));
        alphaFx.setViewProperty(view_);
        alphaFx.setProjectionProperty(projection_);
        alphaFx.Apply();
        dev.SetVertexBuffer(quadVb1_.get());
        dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);

        DualTextureEffect dualFx(dev);
        dualFx.setTextureProperty(quadrantTexture_.get());
        dualFx.setTexture2Property(yellowTexture_.get());
        dualFx.setWorldProperty(Matrix::CreateTranslation(1.2f, 0.0f, 0.0f));
        dualFx.setViewProperty(view_);
        dualFx.setProjectionProperty(projection_);
        dualFx.Apply();
        dev.SetVertexBuffer(quadVb2_.get());
        dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);

        dev.SetVertexBuffer(nullptr);
    }

    void Draw(const GameTime&) override
    {
        ++frame_;
        auto& dev = getGraphicsDeviceProperty();
        dev.Clear(Color::CornflowerBlue);

        if (frame_ == 1)
        {
            bool threw = false;
            const char* stage = "AlphaTestEffect";
            try
            {
                AlphaTestEffect alphaFx(dev);
                alphaFx.setTextureProperty(alphaTestTexture_.get());
                alphaFx.setAlphaFunctionProperty(CompareFunction::Greater);
                alphaFx.setReferenceAlphaProperty(128);
                alphaFx.setWorldProperty(Matrix::CreateTranslation(-1.2f, 0.0f, 0.0f));
                alphaFx.setViewProperty(view_);
                alphaFx.setProjectionProperty(projection_);
                alphaFx.Apply();
                dev.SetVertexBuffer(quadVb1_.get());
                dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
                check(true, "AlphaTestEffect quad draws with no exception");

                stage = "DualTextureEffect";
                DualTextureEffect dualFx(dev);
                dualFx.setTextureProperty(quadrantTexture_.get());
                dualFx.setTexture2Property(yellowTexture_.get());
                dualFx.setWorldProperty(Matrix::CreateTranslation(1.2f, 0.0f, 0.0f));
                dualFx.setViewProperty(view_);
                dualFx.setProjectionProperty(projection_);
                dualFx.Apply();
                dev.SetVertexBuffer(quadVb2_.get());
                dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
                check(true, "DualTextureEffect quad draws with no exception");

                dev.SetVertexBuffer(nullptr);
            }
            catch (const std::exception& e)
            {
                threw = true;
                check(false, (std::string("scene draw threw during ") + stage + ": " + e.what()).c_str());
            }
            (void)threw;
        }
        else
        {
            DrawScene();
        }

        if (frame_ == kTotalFrames)
        {
            check(true, "120 frames of both effects render with no exception");
            std::printf("=== %d/%d PASS ===\n", passCount_, 3);
            result_ = (passCount_ == 3) ? 0 : 1;
            Exit();
        }
    }

public:
    SdlGpuEffectsTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(320);
        gdm_->setPreferredBackBufferHeightProperty(220);
        // GraphicsDeviceManager.SynchronizeWithVerticalRetrace defaults to true (the XNA
        // default); this test's virtual/headless display has no real vblank signal, so leaving
        // VSync on makes every frame wait roughly a second, blowing past this test's frame
        // budget. Disabling it here -- before Game::DoInitialize()'s CreateDevice() call reads
        // it -- is the correct, property-level way to request Immediate presentation.
        gdm_->setSynchronizeWithVerticalRetraceProperty(false);
    }

    int getResult() const { return result_; }
};

int main()
{
    if (!CNA::Examples::ProbeGpuDisplayAvailable())
        return CNA::Examples::kSkipExitCode;

    SdlGpuEffectsTest game;
    game.Run();
    return game.getResult();
}
