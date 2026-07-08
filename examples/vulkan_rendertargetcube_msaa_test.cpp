// SPDX-License-Identifier: MS-PL
// Task 903: verify RenderTargetCube MSAA support on Vulkan (split out of Task 878/879, which
// only covered RenderTarget2D).
//
// Two checks:
//   1. GetMultiSampleCount() property fidelity: a RenderTargetCube constructed with
//      preferredMultiSampleCount=0 must report 0; one constructed with 8 (while the backend
//      itself has real backbuffer MSAA engaged, see below) must report a real, nonzero,
//      device-clamped value -- proving the request actually reached the backend instead of being
//      silently dropped (as it was before this task: VulkanRenderTargetCubeBackend never
//      overrode GetMultiSampleCount() at all, so it always returned the IRenderTargetCubeBackend
//      base class's default of 0 regardless of what was requested).
//   2. No corruption/crash: fill all 6 faces of the MSAA-enabled cube with solid blue (a real
//      draw call per face, not Clear()-only -- see Task 875's finding), unbind, and sample the
//      centre back via EnvironmentMapEffect (Task 334/907's established, proven-reliable
//      technique). Must read back blue, not black/garbage/corrupted.
//
// **Deliberately does NOT attempt a genuine sub-pixel anti-aliasing differential test** (the
// diagonal-edge methodology `{vulkan,bgfx}_rendertarget2d_msaa_test.cpp` use for RenderTarget2D).
// Task 907's own mip-chain test already discovered and documented why: EnvironmentMapEffect's
// reflection-vector sampling of a flat, fixed-normal quad has no equivalent of "shrink the
// destination rectangle to force a specific LOD/UV point" the way direct 2D texture sampling
// does, and an attempt at an asymmetric per-face split was found non-discriminating (git-stash
// reverting the fix still passed identically). Independently re-confirmed while investigating
// this exact task: with World=View=Projection=Identity and a per-vertex normal fixed at
// (0,0,1), the actual reflection direction reduces to normalize(vertexPosition.xy, 0) -- it has
// a zero Z component almost everywhere on the quad (only the exact, numerically-degenerate
// centre vertex could ever reach a +-Z face at all), meaning this technique overwhelmingly
// samples the cube's four side faces, not the one face a diagonal-split test would need to
// sweep across. This is why every existing test using this technique (Task 148/334/907) fills
// ALL SIX faces identically and only ever checks the exact centre pixel -- it cannot
// discriminate between different faces' content, only confirm real (non-garbage) content came
// back from *some* face. This test's scope is therefore: property fidelity (the real, provable
// bug this task's title describes) + a no-corruption sanity check, matching Task 907's own
// "confirms the mechanism doesn't corrupt or crash" scope decision for the identical class of
// verification limitation.
//
// Vulkan-specific scaffolding requirement (mirrors vulkan_rendertarget2d_msaa_test.cpp exactly):
// per-RT MSAA piggybacks on the backend's own already-picked sampleCount_, which only becomes >1
// when the game requested backbuffer multisampling BEFORE the backend was constructed --
// GraphicsDeviceManager.PreferMultiSampling doesn't actually reach the Vulkan backend at all
// (Task 902 finding), so this test uses the same narrow NOXNA test-only
// GraphicsDevice::RecreateBackendForMultiSampleCount() hook Task 878/879 added to unblock this.
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

class VulkanRenderTargetCubeMsaaTest : public Game
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
        // See this file's header comment: force real backbuffer MSAA directly, before any GPU
        // resources exist, since GraphicsDeviceManager.PreferMultiSampling never reaches Vulkan.
        device.RecreateBackendForMultiSampleCount(8);

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

        device.SetDepthTestEnabled(false);
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
        }
        device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

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
        Color centPx(0, 0, 0, 0);
        device.GetBackBufferData(&centReg, &centPx, 0, 1);
        const bool renderOk = (centPx.getRProperty() <= 50  &&
                               centPx.getGProperty() <= 50  &&
                               centPx.getBProperty() >= 200);
        std::printf("[%s] MSAA cube sample after unbind: centre=(%d,%d,%d) (expected blue)\n",
                    renderOk ? "PASS" : "FAIL",
                    centPx.getRProperty(), centPx.getGProperty(), centPx.getBProperty());

        result_ = (noMsaaOk && msaaPropertyOk && renderOk) ? 0 : 1;
        Exit();
    }

public:
    VulkanRenderTargetCubeMsaaTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(320);
        gdm_->setPreferredBackBufferHeightProperty(240);
    }

    int getResult() const { return result_; }
};

int main()
{
    VulkanRenderTargetCubeMsaaTest game;
    game.Run();
    return game.getResult();
}
