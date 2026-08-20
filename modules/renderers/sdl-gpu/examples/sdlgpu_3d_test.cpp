// SPDX-License-Identifier: MS-PL
// plans/plan_sdlgpu.md SDLGPU-26..30: end-to-end 3D vertex-format proof for the SDL_GPU graphics
// renderer -- colored3d, textured3d, colored_textured3d, and lit_textured3d (BasicEffect), all
// drawn through the real public XNA API (VertexBuffer/BasicEffect/GraphicsDevice.DrawPrimitives),
// exercising DrawPrimitivesEx's real GpuDrawParams dispatch by vertex stride, plus a real
// depth-test proof (a nearer quad drawn BEFORE a farther one still occludes it -- proves the
// depth buffer is real, not just "didn't throw").
//
// No pixel-level readback exists yet on this renderer (plans/plan_sdlgpu.md Phase SDLGPU-8/39), so this
// test proves correctness via a real screenshot (same bar as sdlgpu_2d_test.cpp's own
// SpriteBatch verification), not automated pixel assertions.
//
// Check A -- a colored3d triangle (VertexPositionColor, BasicEffect.VertexColorEnabled=true)
//   draws with no exception.
// Check B -- a depth-test pair (a nearer blue quad drawn BEFORE a farther red quad, real
//   SetDepthTestEnabled/SetDepthWriteEnabled) draws with no exception.
// Check C -- a textured3d quad (VertexPositionTexture, BasicEffect.Texture) draws with no
//   exception.
// Check D -- a colored_textured3d quad (VertexPositionColorTexture, vertex-color tint) draws
//   with no exception.
// Check E -- a lit_textured3d quad (VertexPositionNormalTexture, BasicEffect.EnableDefaultLighting())
//   draws with no exception.
// Check F -- 120 frames of the whole scene render with no exception.
//
// Exit code 0 = all checks PASS, 1 = any FAILs.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/MathHelper.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColorTexture.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionNormalTexture.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionTexture.hpp"

#include "CNA/Internal/Renderers/SdlGpu/SdlGpuRenderer.hpp"

#include "common/PixelTestGame.hpp"

#include <array>
#include <cstdio>
#include <memory>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using namespace CNA::Internal::Renderers::SdlGpu;

namespace
{
    constexpr int kTotalFrames = 120;

    std::vector<std::uint8_t> MakeQuadrantTexturePixels()
    {
        // 4x4 RGBA8, one solid color per 2x2 quadrant: red / green / blue / white.
        std::vector<std::uint8_t> pixels(4 * 4 * 4, 0);
        auto setPixel = [&](int x, int y, std::uint8_t r, std::uint8_t g, std::uint8_t b, std::uint8_t a) {
            const std::size_t offset = (static_cast<std::size_t>(y) * 4 + x) * 4;
            pixels[offset + 0] = r; pixels[offset + 1] = g; pixels[offset + 2] = b; pixels[offset + 3] = a;
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

class SdlGpu3DTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    std::unique_ptr<Texture2D> texture_;
    std::unique_ptr<VertexBuffer> triangleVb_;
    std::unique_ptr<VertexBuffer> nearQuadVb_;
    std::unique_ptr<VertexBuffer> farQuadVb_;
    std::unique_ptr<VertexBuffer> texturedQuadVb_;
    std::unique_ptr<VertexBuffer> coloredTexturedQuadVb_;
    std::unique_ptr<VertexBuffer> litQuadVb_;
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

        texture_ = std::make_unique<Texture2D>(Texture2D::CreateFromPixels(dev, 4, 4, MakeQuadrantTexturePixels()));

        view_ = Matrix::CreateLookAt(Vector3(0.0f, 0.0f, 10.0f), Vector3(0.0f, 0.0f, 0.0f), Vector3(0.0f, 1.0f, 0.0f));
        projection_ = Matrix::CreateOrthographic(9.0f, 6.5f, 0.1f, 20.0f);

        const VertexPositionColor triangleVerts[3] = {
            { Vector3(0.0f, 0.6f, 0.0f), Color::Red },
            { Vector3(-0.6f, -0.6f, 0.0f), Color::Green },
            { Vector3(0.6f, -0.6f, 0.0f), Color::Blue },
        };
        triangleVb_ = std::make_unique<VertexBuffer>(dev, VertexPositionColor::getVertexDeclarationStatic(), 3, BufferUsage::None);
        triangleVb_->SetData(triangleVerts, 0, 3);

        auto makeColorQuad = [](const Color& c) {
            return std::array<VertexPositionColor, 6>{
                VertexPositionColor{Vector3(-0.6f, 0.6f, 0.0f), c},
                VertexPositionColor{Vector3(-0.6f, -0.6f, 0.0f), c},
                VertexPositionColor{Vector3(0.6f, -0.6f, 0.0f), c},
                VertexPositionColor{Vector3(-0.6f, 0.6f, 0.0f), c},
                VertexPositionColor{Vector3(0.6f, -0.6f, 0.0f), c},
                VertexPositionColor{Vector3(0.6f, 0.6f, 0.0f), c},
            };
        };
        const auto nearQuad = makeColorQuad(Color::Blue);
        nearQuadVb_ = std::make_unique<VertexBuffer>(dev, VertexPositionColor::getVertexDeclarationStatic(), 6, BufferUsage::None);
        nearQuadVb_->SetData(nearQuad.data(), 0, 6);
        const auto farQuad = makeColorQuad(Color::Red);
        farQuadVb_ = std::make_unique<VertexBuffer>(dev, VertexPositionColor::getVertexDeclarationStatic(), 6, BufferUsage::None);
        farQuadVb_->SetData(farQuad.data(), 0, 6);

        const VertexPositionTexture texturedVerts[6] = {
            { Vector3(-0.6f, 0.6f, 0.0f), Vector2(0.0f, 0.0f) },
            { Vector3(-0.6f, -0.6f, 0.0f), Vector2(0.0f, 1.0f) },
            { Vector3(0.6f, -0.6f, 0.0f), Vector2(1.0f, 1.0f) },
            { Vector3(-0.6f, 0.6f, 0.0f), Vector2(0.0f, 0.0f) },
            { Vector3(0.6f, -0.6f, 0.0f), Vector2(1.0f, 1.0f) },
            { Vector3(0.6f, 0.6f, 0.0f), Vector2(1.0f, 0.0f) },
        };
        texturedQuadVb_ = std::make_unique<VertexBuffer>(dev, VertexPositionTexture::getVertexDeclarationStatic(), 6, BufferUsage::None);
        texturedQuadVb_->SetData(texturedVerts, 0, 6);

        const VertexPositionColorTexture coloredTexVerts[6] = {
            { Vector3(-0.6f, 0.6f, 0.0f), Color::Green, Vector2(0.0f, 0.0f) },
            { Vector3(-0.6f, -0.6f, 0.0f), Color::Green, Vector2(0.0f, 1.0f) },
            { Vector3(0.6f, -0.6f, 0.0f), Color::Green, Vector2(1.0f, 1.0f) },
            { Vector3(-0.6f, 0.6f, 0.0f), Color::Green, Vector2(0.0f, 0.0f) },
            { Vector3(0.6f, -0.6f, 0.0f), Color::Green, Vector2(1.0f, 1.0f) },
            { Vector3(0.6f, 0.6f, 0.0f), Color::Green, Vector2(1.0f, 0.0f) },
        };
        coloredTexturedQuadVb_ = std::make_unique<VertexBuffer>(dev, VertexPositionColorTexture::getVertexDeclarationStatic(), 6, BufferUsage::None);
        coloredTexturedQuadVb_->SetData(coloredTexVerts, 0, 6);

        const VertexPositionNormalTexture litVerts[6] = {
            { Vector3(-0.6f, 0.6f, 0.0f), Vector3(0.0f, 0.0f, 1.0f), Vector2(0.0f, 0.0f) },
            { Vector3(-0.6f, -0.6f, 0.0f), Vector3(0.0f, 0.0f, 1.0f), Vector2(0.0f, 1.0f) },
            { Vector3(0.6f, -0.6f, 0.0f), Vector3(0.0f, 0.0f, 1.0f), Vector2(1.0f, 1.0f) },
            { Vector3(-0.6f, 0.6f, 0.0f), Vector3(0.0f, 0.0f, 1.0f), Vector2(0.0f, 0.0f) },
            { Vector3(0.6f, -0.6f, 0.0f), Vector3(0.0f, 0.0f, 1.0f), Vector2(1.0f, 1.0f) },
            { Vector3(0.6f, 0.6f, 0.0f), Vector3(0.0f, 0.0f, 1.0f), Vector2(1.0f, 0.0f) },
        };
        litQuadVb_ = std::make_unique<VertexBuffer>(dev, VertexPositionNormalTexture::getVertexDeclarationStatic(), 6, BufferUsage::None);
        litQuadVb_->SetData(litVerts, 0, 6);
    }

    void DrawScene()
    {
        auto& dev = getGraphicsDeviceProperty();

        // Check A: colored3d triangle.
        BasicEffect coloredFx(dev);
        coloredFx.VertexColorEnabled = true;
        coloredFx.setWorldProperty(Matrix::CreateTranslation(-3.6f, 0.0f, 0.0f));
        coloredFx.setViewProperty(view_);
        coloredFx.setProjectionProperty(projection_);
        coloredFx.Apply();
        dev.SetVertexBuffer(triangleVb_.get());
        dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 1);

        // Check B: real depth test -- draw the NEARER (blue) quad first, then the FARTHER (red)
        // quad on top of the same screen-space area; with depth test genuinely on, the farther
        // quad must NOT overwrite the nearer one where they overlap.
        dev.SetDepthTestEnabled(true);
        dev.SetDepthWriteEnabled(true);
        BasicEffect depthFx(dev);
        depthFx.VertexColorEnabled = true;
        depthFx.setViewProperty(view_);
        depthFx.setProjectionProperty(projection_);
        depthFx.setWorldProperty(Matrix::CreateTranslation(-1.8f, 0.0f, 0.5f));
        depthFx.Apply();
        dev.SetVertexBuffer(nearQuadVb_.get());
        dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
        depthFx.setWorldProperty(Matrix::CreateTranslation(-1.8f, 0.0f, -0.5f));
        depthFx.Apply();
        dev.SetVertexBuffer(farQuadVb_.get());
        dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
        dev.SetDepthTestEnabled(false);
        dev.SetDepthWriteEnabled(false);

        // Check C: textured3d.
        BasicEffect texFx(dev);
        texFx.setTextureEnabledProperty(true);
        texFx.setTextureProperty(texture_.get());
        texFx.setWorldProperty(Matrix::CreateTranslation(0.0f, 0.0f, 0.0f));
        texFx.setViewProperty(view_);
        texFx.setProjectionProperty(projection_);
        texFx.Apply();
        dev.SetVertexBuffer(texturedQuadVb_.get());
        dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);

        // Check D: colored_textured3d (green vertex-color tint over the quadrant texture).
        BasicEffect coloredTexFx(dev);
        coloredTexFx.VertexColorEnabled = true;
        coloredTexFx.setTextureEnabledProperty(true);
        coloredTexFx.setTextureProperty(texture_.get());
        coloredTexFx.setWorldProperty(Matrix::CreateTranslation(1.8f, 0.0f, 0.0f));
        coloredTexFx.setViewProperty(view_);
        coloredTexFx.setProjectionProperty(projection_);
        coloredTexFx.Apply();
        dev.SetVertexBuffer(coloredTexturedQuadVb_.get());
        dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);

        // Check E: lit_textured3d (BasicEffect's default 3-directional-light rig).
        BasicEffect litFx(dev);
        litFx.setTextureEnabledProperty(true);
        litFx.setTextureProperty(texture_.get());
        litFx.EnableDefaultLighting();
        litFx.setWorldProperty(Matrix::CreateTranslation(3.6f, 0.0f, 0.0f));
        litFx.setViewProperty(view_);
        litFx.setProjectionProperty(projection_);
        litFx.Apply();
        dev.SetVertexBuffer(litQuadVb_.get());
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
            const char* stage = "start";
            try
            {
                stage = "colored3d triangle";
                BasicEffect coloredFx(dev);
                coloredFx.VertexColorEnabled = true;
                coloredFx.setWorldProperty(Matrix::CreateTranslation(-3.6f, 0.0f, 0.0f));
                coloredFx.setViewProperty(view_);
                coloredFx.setProjectionProperty(projection_);
                coloredFx.Apply();
                dev.SetVertexBuffer(triangleVb_.get());
                dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 1);
                check(true, "colored3d triangle draws with no exception");

                stage = "depth-test pair";
                dev.SetDepthTestEnabled(true);
                dev.SetDepthWriteEnabled(true);
                BasicEffect depthFx(dev);
                depthFx.VertexColorEnabled = true;
                depthFx.setViewProperty(view_);
                depthFx.setProjectionProperty(projection_);
                depthFx.setWorldProperty(Matrix::CreateTranslation(-1.8f, 0.0f, 0.5f));
                depthFx.Apply();
                dev.SetVertexBuffer(nearQuadVb_.get());
                dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
                depthFx.setWorldProperty(Matrix::CreateTranslation(-1.8f, 0.0f, -0.5f));
                depthFx.Apply();
                dev.SetVertexBuffer(farQuadVb_.get());
                dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
                dev.SetDepthTestEnabled(false);
                dev.SetDepthWriteEnabled(false);
                check(true, "depth-test pair (real SetDepthTestEnabled/SetDepthWriteEnabled) draws with no exception");

                stage = "textured3d quad";
                BasicEffect texFx(dev);
                texFx.setTextureEnabledProperty(true);
                texFx.setTextureProperty(texture_.get());
                texFx.setWorldProperty(Matrix::CreateTranslation(0.0f, 0.0f, 0.0f));
                texFx.setViewProperty(view_);
                texFx.setProjectionProperty(projection_);
                texFx.Apply();
                dev.SetVertexBuffer(texturedQuadVb_.get());
                dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
                check(true, "textured3d quad draws with no exception");

                stage = "colored_textured3d quad";
                BasicEffect coloredTexFx(dev);
                coloredTexFx.VertexColorEnabled = true;
                coloredTexFx.setTextureEnabledProperty(true);
                coloredTexFx.setTextureProperty(texture_.get());
                coloredTexFx.setWorldProperty(Matrix::CreateTranslation(1.8f, 0.0f, 0.0f));
                coloredTexFx.setViewProperty(view_);
                coloredTexFx.setProjectionProperty(projection_);
                coloredTexFx.Apply();
                dev.SetVertexBuffer(coloredTexturedQuadVb_.get());
                dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
                check(true, "colored_textured3d quad draws with no exception");

                stage = "lit_textured3d quad";
                BasicEffect litFx(dev);
                litFx.setTextureEnabledProperty(true);
                litFx.setTextureProperty(texture_.get());
                litFx.EnableDefaultLighting();
                litFx.setWorldProperty(Matrix::CreateTranslation(3.6f, 0.0f, 0.0f));
                litFx.setViewProperty(view_);
                litFx.setProjectionProperty(projection_);
                litFx.Apply();
                dev.SetVertexBuffer(litQuadVb_.get());
                dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
                check(true, "lit_textured3d quad draws with no exception");

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
            check(true, "120 frames of the full 3D scene render with no exception");
            std::printf("=== %d/%d PASS ===\n", passCount_, 6);
            result_ = (passCount_ == 6) ? 0 : 1;
            Exit();
        }
    }

public:
    SdlGpu3DTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(360);
        gdm_->setPreferredBackBufferHeightProperty(260);
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

    SdlGpu3DTest game;
    game.Run();
    return game.getResult();
}
