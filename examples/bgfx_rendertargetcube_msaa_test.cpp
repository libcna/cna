// SPDX-License-Identifier: MS-PL
// Task 903: verify RenderTargetCube MSAA support on Bgfx (split out of Task 878/879, which only
// covered RenderTarget2D). Direct port of vulkan_rendertargetcube_msaa_test.cpp -- see that
// file's header comment for the full rationale behind this test's scope (property fidelity +
// no-corruption sanity check, not a genuine sub-pixel AA differential test -- Task 907 already
// found and documented why that technique can't discriminate cube-face content, independently
// re-confirmed while investigating this exact task).
//
// Unlike Vulkan, Bgfx's per-RT MSAA (BGFX_TEXTURE_RT_MSAA_Xn) is fully independent of any
// backbuffer MSAA state (same as bgfx_rendertarget2d_msaa_test.cpp) -- no
// GraphicsDeviceManager.PreferMultiSampling or RecreateBackendForMultiSampleCount workaround is
// needed here.
//
// Bgfx-only conventions mirrored from this test family (Task 364/896/907 findings):
//   - RasterizerState::CullNone is required.
//   - GetBackBufferData() only reliably reflects the *first* read call per rendered frame, so the
//     final centre-pixel sample retries (<=20 attempts) until a genuinely fresh (non-all-black)
//     result is captured.
//   - Every face fill must be a real draw call (SpriteBatch), not Clear()-only (Task 875-class
//     finding, applies here too).
//   - Task 910's already-documented view-id-per-frame limitation applies here too (first
//     encountered by this test as a real, reproducing failure -- centre read back a wrong,
//     non-blue value): a dummy backbuffer read between each face's fill forces the bgfx::frame()
//     boundary Task 910's own fix would otherwise require.
//
// Exit code 0 = all checks PASS, 1 = any FAILs.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/CubeMapFace.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/EnvironmentMapEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetCube.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionNormalTexture.hpp"

#include <cstdio>
#include <memory>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

static constexpr int kCubeSize = 32;

class BgfxRenderTargetCubeMsaaTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    std::unique_ptr<SpriteBatch>           sb_;
    std::unique_ptr<Texture2D>             whiteTex_;
    std::unique_ptr<Texture2D>             blueTex_;
    bool done_   = false;
    int  result_ = 1;

protected:
    void Initialize() override
    {
        Game::Initialize();
        auto& device = getGraphicsDeviceProperty();
        sb_ = std::make_unique<SpriteBatch>(device);
        const std::vector<uint8_t> white = { 255, 255, 255, 255 };
        whiteTex_ = std::make_unique<Texture2D>(Texture2D::CreateFromPixels(device, 1, 1, white));
        const std::vector<uint8_t> blue = { 0, 0, 255, 255 };
        blueTex_ = std::make_unique<Texture2D>(Texture2D::CreateFromPixels(device, 1, 1, blue));
    }

    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;

        auto& device = getGraphicsDeviceProperty();
        const auto& vp = device.getViewportProperty();
        const int W = vp.getWidthProperty();
        const int H = vp.getHeightProperty();

        device.setBlendStateProperty(BlendState::Opaque);
        device.setRasterizerStateProperty(RasterizerState::CullNone);

        // --- Check 1: MultiSampleCount=0 must report 0 ---
        RenderTargetCube rtcNoMsaa(device, kCubeSize, /*mipMap=*/false, SurfaceFormat::Color,
                                   DepthFormat::None, /*preferredMultiSampleCount=*/0,
                                   RenderTargetUsage::DiscardContents);
        const int noMsaaApplied = rtcNoMsaa.getMultiSampleCountProperty();
        const bool noMsaaOk = (noMsaaApplied == 0);
        std::printf("[%s] MultiSampleCount request 0 -> applied %d (expected 0)\n",
                    noMsaaOk ? "PASS" : "FAIL", noMsaaApplied);

        // --- Check 2: MultiSampleCount=8 must report a real, nonzero applied value ---
        RenderTargetCube rtcMsaa(device, kCubeSize, /*mipMap=*/false, SurfaceFormat::Color,
                                 DepthFormat::None, /*preferredMultiSampleCount=*/8,
                                 RenderTargetUsage::DiscardContents);
        const int msaaApplied = rtcMsaa.getMultiSampleCountProperty();
        const bool msaaPropertyOk = (msaaApplied > 1);
        std::printf("[%s] MultiSampleCount request 8 -> applied %d (expected >1)\n",
                    msaaPropertyOk ? "PASS" : "FAIL", msaaApplied);

        // --- Check 3: rendering into the MSAA cube doesn't corrupt/crash ---
        const CubeMapFace faces[6] = {
            CubeMapFace::PositiveX, CubeMapFace::NegativeX,
            CubeMapFace::PositiveY, CubeMapFace::NegativeY,
            CubeMapFace::PositiveZ, CubeMapFace::NegativeZ,
        };
        for (CubeMapFace face : faces)
        {
            device.SetRenderTarget(&rtcMsaa, face);
            sb_->Begin();
            sb_->Draw(*blueTex_,
                      Rectangle(0, 0, kCubeSize, kCubeSize),
                      Rectangle(0, 0, 1, 1),
                      Color::White);
            sb_->End();
            // Task 910 workaround (established by bgfx_rendertargetcube_mip_test.cpp): force a
            // bgfx::frame() boundary between each face's fill via a throwaway backbuffer read --
            // bgfx::setViewFrameBuffer() is a per-view, per-*frame* setting, and all 6 faces share
            // one hardcoded view id, so without this every face but the last stays garbage.
            Color dummy(0, 0, 0, 0);
            const Rectangle dummyReg(0, 0, 1, 1);
            device.GetBackBufferData(&dummyReg, &dummy, 0, 1);
        }
        device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

        Color centPx(0, 0, 0, 0);
        bool renderOk = false;
        for (int i = 0; i < 20; ++i)
        {
            device.Clear(Color(0, 0, 0, 255));
            EnvironmentMapEffect fx(device);
            fx.setDiffuseColorProperty(Vector3(1.0f, 1.0f, 1.0f));
            fx.setEmissiveColorProperty(Vector3(0.0f, 0.0f, 0.0f));
            fx.setEnvironmentMapAmountProperty(1.0f);
            fx.setEnvironmentMapSpecularProperty(Vector3(0.0f, 0.0f, 0.0f));
            fx.setTextureProperty(whiteTex_.get());
            fx.setEnvironmentMapProperty(&rtcMsaa);
            fx.setWorldProperty(Matrix::getIdentityProperty());
            fx.setViewProperty(Matrix::getIdentityProperty());
            fx.setProjectionProperty(Matrix::getIdentityProperty());
            fx.Apply();

            const Vector3 n(0.0f, 0.0f, 1.0f);
            const VertexPositionNormalTexture verts[6] = {
                { Vector3(-1.0f,  1.0f, 0.0f), n, Vector2(0.0f, 1.0f) },
                { Vector3(-1.0f, -1.0f, 0.0f), n, Vector2(0.0f, 0.0f) },
                { Vector3( 1.0f, -1.0f, 0.0f), n, Vector2(1.0f, 0.0f) },
                { Vector3(-1.0f,  1.0f, 0.0f), n, Vector2(0.0f, 1.0f) },
                { Vector3( 1.0f, -1.0f, 0.0f), n, Vector2(1.0f, 0.0f) },
                { Vector3( 1.0f,  1.0f, 0.0f), n, Vector2(1.0f, 1.0f) },
            };
            device.DrawUserPrimitives(PrimitiveType::TriangleList, verts, 0, 2);

            const Rectangle centReg(W / 2, H / 2, 1, 1);
            device.GetBackBufferData(&centReg, &centPx, 0, 1);
            if (centPx.getRProperty() > 10 || centPx.getGProperty() > 10 || centPx.getBProperty() > 10)
                break;
        }
        renderOk = (centPx.getRProperty() <= 50  &&
                    centPx.getGProperty() <= 50  &&
                    centPx.getBProperty() >= 200);
        std::printf("[%s] MSAA cube sample after unbind: centre=(%d,%d,%d) (expected blue)\n",
                    renderOk ? "PASS" : "FAIL",
                    centPx.getRProperty(), centPx.getGProperty(), centPx.getBProperty());

        result_ = (noMsaaOk && msaaPropertyOk && renderOk) ? 0 : 1;
        Exit();
    }

public:
    BgfxRenderTargetCubeMsaaTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(320);
        gdm_->setPreferredBackBufferHeightProperty(240);
    }

    int getResult() const { return result_; }
};

int main()
{
    BgfxRenderTargetCubeMsaaTest game;
    game.Run();
    return game.getResult();
}
