// SPDX-License-Identifier: MS-PL
// plans/plan_sdlgpu.md SDLGPU-21: real dynamic SamplerState proof for direct 3D draws on the SDL_GPU
// graphics renderer -- before this task, every 3D draw (BasicEffect/AlphaTestEffect/
// DualTextureEffect/EnvironmentMapEffect/SkinnedEffect) hardcoded Linear+Wrap-then-Clamp
// regardless of what GraphicsDevice.SamplerStates[slot] was assigned; only SpriteBatch's own
// per-draw sampler selection was wired up.
//
// Every check asserts exact RenderTarget2D::GetData() pixel values (not "didn't throw"), each
// chosen so a stuck-hardcoded sampler would read back a clearly different, wrong pixel.
//
// Check A -- TextureAddressMode (Wrap vs Clamp), slot 0: a quad UV-mapped 0..2 (so U=1.25 lands
//   past the texture's own [0,1) range) over a texture whose left half is Red and right half is
//   Green. SamplerState.PointWrap must read back Red (the pattern repeats, U=1.25 wraps to 0.25 --
//   inside the left/Red half); SamplerState.PointClamp must read back Green (U clamps to 1.0,
//   holding the rightmost/Green texel).
// Check B -- TextureFilter (Point vs Linear), slot 0: the same Red/Green texture sampled near the
//   colour boundary (U=0.45, between the two texel centres). SamplerState.PointClamp must read
//   back pure Red (nearest-texel snap); SamplerState.LinearClamp must read back a real blend
//   (green channel clearly elevated above zero, not a sharp snap).
// Check C -- DualTextureEffect's two texture units are independent per-slot samplers
//   (GraphicsDevice.SamplerStates[0]/[1]), not one shared sampler: texture0 (White|Black halves)
//   set to Wrap, texture1 (Black|White halves) set to Clamp, both sampled at the SAME shared
//   UV=1.25. If both slots genuinely apply their own address mode, texture0 wraps to White and
//   texture1 clamps to White, multiplying to White. Using either address mode for BOTH slots
//   (the old shared-sampler behaviour) always multiplies to Black instead -- a strong, self-
//   contained differential.
//
// Exit code 0 = all checks PASS, 1 = any FAILs.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BufferUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/DualTextureEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionTexture.hpp"

#include "CNA/Internal/Renderers/SdlGpu/SdlGpuRenderer.hpp"

#include "common/PixelTestGame.hpp"

#include <cstdio>
#include <cstdlib>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using namespace CNA::Internal::Renderers::SdlGpu;

namespace
{
    constexpr int kRTSize = 32;
    constexpr int kTotalFrames = 120;

    bool CloseTo(int a, int b, int tol) { return std::abs(a - b) <= tol; }
    bool Matches(const Color& c, const Color& expected, int tol = 12)
    {
        return CloseTo(c.getRProperty(), expected.getRProperty(), tol)
            && CloseTo(c.getGProperty(), expected.getGProperty(), tol)
            && CloseTo(c.getBProperty(), expected.getBProperty(), tol);
    }

    Color ReadPixel(RenderTarget2D& rt, int x, int y)
    {
        Color px(0, 0, 0, 0);
        const Rectangle rect(x, y, 1, 1);
        rt.GetData(0, &rect, &px, 0, 1);
        return px;
    }

    std::string ColorStr(const Color& c)
    {
        return "(" + std::to_string(c.getRProperty()) + "," + std::to_string(c.getGProperty())
             + "," + std::to_string(c.getBProperty()) + ")";
    }

    // 4x4 RGBA8: left half (columns 0-1) leftColor, right half (columns 2-3) rightColor.
    std::vector<std::uint8_t> MakeHalvesTexturePixels(const Color& leftColor, const Color& rightColor)
    {
        std::vector<std::uint8_t> pixels(4 * 4 * 4, 0);
        for (int y = 0; y < 4; ++y)
            for (int x = 0; x < 4; ++x)
            {
                const Color& c = (x < 2) ? leftColor : rightColor;
                const std::size_t off = (static_cast<std::size_t>(y) * 4 + x) * 4;
                pixels[off + 0] = c.getRProperty(); pixels[off + 1] = c.getGProperty();
                pixels[off + 2] = c.getBProperty(); pixels[off + 3] = c.getAProperty();
            }
        return pixels;
    }
}

class SdlGpuSamplerStateTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;

    std::unique_ptr<RenderTarget2D> rtAddressMode_;
    std::unique_ptr<RenderTarget2D> rtFilter_;
    std::unique_ptr<RenderTarget2D> rtDualTexture_;

    std::unique_ptr<Texture2D> texRedGreen_;
    std::unique_ptr<Texture2D> texWhiteBlack_;
    std::unique_ptr<Texture2D> texBlackWhite_;

    std::unique_ptr<VertexBuffer> quadU2Vb_;

    int frame_ = 0;
    int passCount_ = 0;
    int result_ = 1;

    void Check(bool ok, const char* label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label);
        if (ok) ++passCount_;
    }

    void DrawTexturedQuad(GraphicsDevice& dev, Texture2D& tex)
    {
        BasicEffect fx(dev);
        fx.setTextureEnabledProperty(true);
        fx.setTextureProperty(&tex);
        fx.setWorldProperty(Matrix::getIdentityProperty());
        fx.setViewProperty(Matrix::getIdentityProperty());
        fx.setProjectionProperty(Matrix::getIdentityProperty());
        fx.Apply();
        dev.SetVertexBuffer(quadU2Vb_.get());
        dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
        dev.SetVertexBuffer(nullptr);
    }

    // Check A: AddressMode (Wrap vs Clamp), slot 0.
    void RunAddressModeCheck(GraphicsDevice& dev)
    {
        dev.SetRenderTarget(rtAddressMode_.get());
        dev.Clear(Color::Magenta);
        dev.getSamplerStatesProperty()[0] = SamplerState::PointWrap;
        DrawTexturedQuad(dev, *texRedGreen_);
        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        addressModeWrap_ = ReadPixel(*rtAddressMode_, 20, kRTSize / 2);

        dev.SetRenderTarget(rtAddressMode_.get());
        dev.Clear(Color::Magenta);
        dev.getSamplerStatesProperty()[0] = SamplerState::PointClamp;
        DrawTexturedQuad(dev, *texRedGreen_);
        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        addressModeClamp_ = ReadPixel(*rtAddressMode_, 20, kRTSize / 2);

        dev.getSamplerStatesProperty()[0] = SamplerState::LinearWrap;
    }

    // Check B: Filter (Point vs Linear), slot 0.
    void RunFilterCheck(GraphicsDevice& dev)
    {
        dev.SetRenderTarget(rtFilter_.get());
        dev.Clear(Color::Magenta);
        dev.getSamplerStatesProperty()[0] = SamplerState::PointClamp;
        DrawTexturedQuad(dev, *texRedGreen_);
        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        filterPoint_ = ReadPixel(*rtFilter_, 7, kRTSize / 2);

        dev.SetRenderTarget(rtFilter_.get());
        dev.Clear(Color::Magenta);
        dev.getSamplerStatesProperty()[0] = SamplerState::LinearClamp;
        DrawTexturedQuad(dev, *texRedGreen_);
        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        filterLinear_ = ReadPixel(*rtFilter_, 7, kRTSize / 2);

        dev.getSamplerStatesProperty()[0] = SamplerState::LinearWrap;
    }

    // Check C: DualTextureEffect's two texture units are independent samplers (slots 0/1).
    void RunDualTextureCheck(GraphicsDevice& dev)
    {
        dev.SetRenderTarget(rtDualTexture_.get());
        dev.Clear(Color::Magenta);
        dev.getSamplerStatesProperty()[0] = SamplerState::PointWrap;
        dev.getSamplerStatesProperty()[1] = SamplerState::PointClamp;
        DualTextureEffect fx(dev);
        fx.setTextureProperty(texWhiteBlack_.get());
        fx.setTexture2Property(texBlackWhite_.get());
        fx.setWorldProperty(Matrix::getIdentityProperty());
        fx.setViewProperty(Matrix::getIdentityProperty());
        fx.setProjectionProperty(Matrix::getIdentityProperty());
        fx.Apply();
        dev.SetVertexBuffer(quadU2Vb_.get());
        dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
        dev.SetVertexBuffer(nullptr);
        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        dualTextureResult_ = ReadPixel(*rtDualTexture_, 20, kRTSize / 2);

        dev.getSamplerStatesProperty()[0] = SamplerState::LinearWrap;
        dev.getSamplerStatesProperty()[1] = SamplerState::LinearWrap;
    }

    Color addressModeWrap_{0, 0, 0, 0};
    Color addressModeClamp_{0, 0, 0, 0};
    Color filterPoint_{0, 0, 0, 0};
    Color filterLinear_{0, 0, 0, 0};
    Color dualTextureResult_{0, 0, 0, 0};

    void RunAll(GraphicsDevice& dev)
    {
        RunAddressModeCheck(dev);
        RunFilterCheck(dev);
        RunDualTextureCheck(dev);
    }

protected:
    void LoadContent() override
    {
        auto& dev = getGraphicsDeviceProperty();

        rtAddressMode_ = std::make_unique<RenderTarget2D>(dev, kRTSize, kRTSize, false, SurfaceFormat::Color,
                                                           DepthFormat::None, 0, RenderTargetUsage::DiscardContents);
        rtFilter_ = std::make_unique<RenderTarget2D>(dev, kRTSize, kRTSize, false, SurfaceFormat::Color,
                                                      DepthFormat::None, 0, RenderTargetUsage::DiscardContents);
        rtDualTexture_ = std::make_unique<RenderTarget2D>(dev, kRTSize, kRTSize, false, SurfaceFormat::Color,
                                                           DepthFormat::None, 0, RenderTargetUsage::DiscardContents);

        texRedGreen_ = std::make_unique<Texture2D>(Texture2D::CreateFromPixels(
            dev, 4, 4, MakeHalvesTexturePixels(Color::Red, Color::Green)));
        texWhiteBlack_ = std::make_unique<Texture2D>(Texture2D::CreateFromPixels(
            dev, 4, 4, MakeHalvesTexturePixels(Color::White, Color::Black)));
        texBlackWhite_ = std::make_unique<Texture2D>(Texture2D::CreateFromPixels(
            dev, 4, 4, MakeHalvesTexturePixels(Color::Black, Color::White)));

        // CW winding (TL,BR,BL / TL,TR,BR) -- visible under this project's default
        // RasterizerState.CullCounterClockwise with the identity view/projection this test uses
        // (see sdlgpu_renderstate_test.cpp's own note on this exact convention). U spans 0..2
        // across the full quad width so a sample past screen-centre reads U>1, outside the
        // texture's own [0,1) range.
        const VertexPositionTexture verts[6] = {
            { Vector3(-1.0f, 1.0f, 0.0f), Vector2(0.0f, 0.0f) }, { Vector3(1.0f, -1.0f, 0.0f), Vector2(2.0f, 1.0f) }, { Vector3(-1.0f, -1.0f, 0.0f), Vector2(0.0f, 1.0f) },
            { Vector3(-1.0f, 1.0f, 0.0f), Vector2(0.0f, 0.0f) }, { Vector3(1.0f, 1.0f, 0.0f), Vector2(2.0f, 0.0f) }, { Vector3(1.0f, -1.0f, 0.0f), Vector2(2.0f, 1.0f) },
        };
        quadU2Vb_ = std::make_unique<VertexBuffer>(dev, VertexPositionTexture::getVertexDeclarationStatic(), 6, BufferUsage::None);
        quadU2Vb_->SetData(verts, 0, 6);
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
                stage = "RunAll";
                RunAll(dev);
                Check(true, "all SamplerState checks render with no exception");
            }
            catch (const std::exception& e)
            {
                threw = true;
                Check(false, (std::string("threw during ") + stage + ": " + e.what()).c_str());
            }
            (void)threw;

            Check(Matches(addressModeWrap_, Color::Red),
                  ("Check A: SamplerState.PointWrap (U=1.25 wraps to 0.25 -> Red half): got=" + ColorStr(addressModeWrap_)).c_str());
            Check(Matches(addressModeClamp_, Color::Green),
                  ("Check A: SamplerState.PointClamp (U=1.25 clamps to 1.0 -> Green half): got=" + ColorStr(addressModeClamp_)).c_str());

            Check(Matches(filterPoint_, Color::Red, 15),
                  ("Check B: SamplerState.PointClamp (U=0.45, nearest-texel snap) -> pure Red: got=" + ColorStr(filterPoint_)).c_str());
            Check(filterLinear_.getGProperty() > 15 && !Matches(filterLinear_, Color::Red, 15),
                  ("Check B: SamplerState.LinearClamp (U=0.45) -> real blend, not a sharp Red snap: got=" + ColorStr(filterLinear_)).c_str());

            Check(Matches(dualTextureResult_, Color::White),
                  ("Check C: DualTextureEffect independent slots (tex0=Wrap,tex1=Clamp) -> White: got=" + ColorStr(dualTextureResult_)).c_str());
        }
        else
        {
            RunAll(dev);
        }

        if (frame_ == kTotalFrames)
        {
            Check(true, "120 frames of all SamplerState checks render with no exception");
            std::printf("=== %d/7 PASS ===\n", passCount_);
            result_ = (passCount_ == 7) ? 0 : 1;
            Exit();
        }
    }

public:
    SdlGpuSamplerStateTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(300);
        gdm_->setPreferredBackBufferHeightProperty(100);
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

    SdlGpuSamplerStateTest game;
    game.Run();
    return game.getResult();
}
