// SPDX-License-Identifier: MS-PL
//
// plans/plan_diligent.md DILIGENT-25: real-device proof that MSAA on the Diligent renderer performs a
// genuine multisample resolve, both for the back buffer and for RenderTarget2D, through the
// public XNA API only.
//
// Methodology (matching examples/vulkan_msaa_test.cpp and
// examples/vulkan_rendertarget2d_msaa_test.cpp): a solid-fill draw can't distinguish "MSAA resolve
// doesn't corrupt colours" from "MSAA never happened at all" -- both produce an identical solid
// result. Instead each check renders a diagonal-edged triangle and reads back a centre row: with
// MSAA off every pixel is a hard binary transition between the two colours (point-sampled
// rasterization has no partial coverage); with MSAA genuinely engaged and resolved, some pixels
// along the edge are a blended intermediate value -- a signature only real sub-pixel coverage
// averaging can produce.
//
// Check A/B -- back buffer: MultiSampleCount=0 is a binary edge, then
//   GraphicsDeviceManager.PreferMultiSampling + ApplyChanges() (the real, non-test-only path --
//   GraphicsDevice::Reset() already forwards to IGraphicsRenderer::ApplyMultiSampleCount()
//   generically, so this renderer needs no CNAEXT RecreateRendererForMultiSampleCount() escape hatch,
//   unlike the older Vulkan/EasyGL sibling tests that had to work around a plumbing gap since
//   fixed) makes the edge blended.
// Check C/D -- RenderTarget2D: the SAME differential, this time through RenderTarget2D's own
//   multiSampleCount constructor parameter. This renderer gives each render target its own
//   independent multisampled texture and resolve target, so it does not depend on check A/B's
//   back buffer state at all (unlike Vulkan's coupled implementation). Check D also (already) uses
//   DepthFormat::None -- DILIGENT-62's own "never use swap-chain depth support to decide a
//   no-depth target" case, since a depth-less render target's applied sample count must not be
//   limited by a depth format it doesn't even have.
// Check E -- ApplyMultiSampleCount() clamps an unreasonable request (1024) to something the
//   device actually supports rather than accepting it verbatim or throwing.
// Check F -- DILIGENT-62's depth-having counterpart to check D: a RenderTarget2D requesting
//   DepthFormat::Depth24Stencil8 (real D24_UNORM_S8_UINT storage this time) at the same high
//   MultiSampleCount still resolves with genuinely blended pixels -- proving the fix's
//   depth-format-intersection branch works, not just its "skip the depth format entirely" one.
//
// Exit code 0 = all checks PASS, 1 = any FAIL, 77 = no usable device.

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PresentationParameters.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteSortMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"

#include <cstdio>
#include <cstdlib>
#include <exception>
#include <memory>
#include <string>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kSize = 32;
    constexpr int kChecks = 6;

    bool IsBinary(const std::vector<Color>& row)
    {
        for (const Color& c : row)
        {
            const int v = c.getRProperty();
            if (v > 40 && v < 215)
                return false;
        }
        return true;
    }

    bool HasIntermediate(const std::vector<Color>& row)
    {
        for (const Color& c : row)
        {
            const int v = c.getRProperty();
            if (v > 40 && v < 215)
                return true;
        }
        return false;
    }

    void DrawDiagonalTriangle(GraphicsDevice& device)
    {
        BasicEffect fx(device);
        fx.setWorldProperty(Matrix::getIdentityProperty());
        fx.setViewProperty(Matrix::getIdentityProperty());
        fx.setProjectionProperty(Matrix::getIdentityProperty());
        fx.VertexColorEnabled = true;
        fx.Apply();

        const Color white(255, 255, 255, 255);
        const VertexPositionColor tri[3] = {
            {Vector3(-1.0f, 1.0f, 0.0f), white},
            {Vector3(1.0f, 1.0f, 0.0f), white},
            {Vector3(-1.0f, -1.0f, 0.0f), white},
        };
        device.DrawUserPrimitives(PrimitiveType::TriangleList, tri, 0, 1);
    }
}

class DiligentMsaaTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> graphicsDeviceManager_;
    std::unique_ptr<SpriteBatch> spriteBatch_;
    bool done_ = false;
    int passCount_ = 0;
    int result_ = 1;

    void Check(bool ok, const char* label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label);
        if (ok)
            ++passCount_;
    }

    // Renders the diagonal triangle directly to the back buffer and returns the centre row.
    std::vector<Color> RenderBackBufferRow(GraphicsDevice& device)
    {
        device.setBlendStateProperty(BlendState::Opaque);
        device.Clear(Color(0, 0, 0, 255));
        DrawDiagonalTriangle(device);

        const auto& viewport = device.getViewportProperty();
        const int w = viewport.getWidthProperty();
        const int h = viewport.getHeightProperty();
        std::vector<Color> row(w, Color(0, 0, 0, 0));
        const Rectangle region(0, h / 2, w, 1);
        device.GetBackBufferData(&region, row.data(), 0, w);
        return row;
    }

    // Renders the diagonal triangle into a RenderTarget2D with the given MultiSampleCount, samples
    // it back onto the back buffer 1:1, and returns the centre row. DILIGENT-62: the render target
    // clamps its own applied MultiSampleCount against ITS OWN RGBA8_UNORM colour format and, only
    // when @p depthFormat requests one, its own D24_UNORM_S8_UINT depth format -- never the swap
    // chain's granted (possibly substituted, e.g. BGRA8_UNORM) formats.
    std::vector<Color> RenderRenderTargetRow(GraphicsDevice& device, int multiSampleCount,
                                             DepthFormat depthFormat = DepthFormat::None)
    {
        RenderTarget2D rt(device, kSize, kSize, false, SurfaceFormat::Color, depthFormat,
                          multiSampleCount, RenderTargetUsage::DiscardContents);
        std::printf("    (RenderTarget2D requested MultiSampleCount=%d, applied %d)\n",
                    multiSampleCount, rt.getMultiSampleCountProperty());

        device.setBlendStateProperty(BlendState::Opaque);
        device.SetRenderTarget(&rt);
        device.Clear(Color(0, 0, 0, 255));
        DrawDiagonalTriangle(device);
        device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

        device.Clear(Color(0, 0, 0, 255));
        SamplerState point = SamplerState::PointClamp;
        spriteBatch_->Begin(SpriteSortMode::Deferred, BlendState::Opaque, &point, nullptr, nullptr);
        spriteBatch_->Draw(rt, Rectangle(0, 0, kSize, kSize), Rectangle(0, 0, kSize, kSize),
                           Color::White);
        spriteBatch_->End();
        // SpriteBatch leaves RasterizerState::CullCounterClockwise bound after End() (XNA
        // semantics; see diligent_rendertarget_test.cpp's identical note), which would cull the
        // next call's diagonal triangle -- reset before any further 3D draw.
        device.setRasterizerStateProperty(RasterizerState::CullNone);

        std::vector<Color> row(kSize, Color(0, 0, 0, 0));
        const Rectangle region(0, kSize / 2, kSize, 1);
        device.GetBackBufferData(&region, row.data(), 0, kSize);
        return row;
    }

protected:
    void Initialize() override
    {
        Game::Initialize();
        spriteBatch_ = std::make_unique<SpriteBatch>(getGraphicsDeviceProperty());
    }

    void Draw(const GameTime&) override
    {
        if (done_)
            return;
        done_ = true;

        auto& device = getGraphicsDeviceProperty();
        device.setRasterizerStateProperty(RasterizerState::CullNone);

        const std::vector<Color> backBufferNoMsaa = RenderBackBufferRow(device);
        Check(IsBinary(backBufferNoMsaa),
              "back buffer MultiSampleCount=0: diagonal edge is a hard binary transition (no AA)");

        graphicsDeviceManager_->setPreferMultiSamplingProperty(true);
        graphicsDeviceManager_->ApplyChanges();
        device.setRasterizerStateProperty(RasterizerState::CullNone);
        const int appliedBackBufferCount =
            device.getPresentationParametersProperty().getMultiSampleCountProperty();
        const std::vector<Color> backBufferMsaa = RenderBackBufferRow(device);
        std::printf("    (PreferMultiSampling applied MultiSampleCount=%d via "
                    "GraphicsDeviceManager.ApplyChanges())\n",
                    appliedBackBufferCount);
        Check(appliedBackBufferCount > 1 && HasIntermediate(backBufferMsaa),
              "back buffer MultiSampleCount>1 via the real GraphicsDeviceManager path: diagonal "
              "edge has genuinely blended pixels (real AA)");

        graphicsDeviceManager_->setPreferMultiSamplingProperty(false);
        graphicsDeviceManager_->ApplyChanges();
        device.setRasterizerStateProperty(RasterizerState::CullNone);

        const std::vector<Color> rtNoMsaa = RenderRenderTargetRow(device, 0);
        Check(IsBinary(rtNoMsaa),
              "RenderTarget2D MultiSampleCount=0: diagonal edge is a hard binary transition");

        const std::vector<Color> rtMsaa = RenderRenderTargetRow(device, 8);
        Check(HasIntermediate(rtMsaa),
              "RenderTarget2D MultiSampleCount=8: diagonal edge has genuinely blended pixels");

        const std::vector<Color> rtMsaaWithDepth =
            RenderRenderTargetRow(device, 8, DepthFormat::Depth24Stencil8);
        Check(HasIntermediate(rtMsaaWithDepth),
              "RenderTarget2D MultiSampleCount=8 with DepthFormat::Depth24Stencil8: diagonal edge "
              "has genuinely blended pixels (own D24_UNORM_S8_UINT format, not the swap chain's)");

        PresentationParameters extremeRequest = device.getPresentationParametersProperty();
        extremeRequest.setMultiSampleCountProperty(1024);
        device.Reset(extremeRequest);
        device.setRasterizerStateProperty(RasterizerState::CullNone);
        const int clamped = device.getPresentationParametersProperty().getMultiSampleCountProperty();
        std::printf("    (requested MultiSampleCount=1024, renderer applied %d)\n", clamped);
        Check(clamped > 0 && clamped < 1024,
              "an unreasonable MultiSampleCount request is clamped, not accepted verbatim");

        std::printf("=== %d/%d PASS ===\n", passCount_, kChecks);
        result_ = passCount_ == kChecks ? 0 : 1;
        Exit();
    }

public:
    DiligentMsaaTest()
    {
        graphicsDeviceManager_ = std::make_unique<GraphicsDeviceManager>(this);
        graphicsDeviceManager_->setPreferredBackBufferWidthProperty(kSize * 2);
        graphicsDeviceManager_->setPreferredBackBufferHeightProperty(kSize * 2);
        graphicsDeviceManager_->ApplyChanges();
    }

    [[nodiscard]] int GetResult() const { return result_; }
};

int main()
{
    try
    {
        DiligentMsaaTest game;
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
