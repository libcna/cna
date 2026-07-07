// SPDX-License-Identifier: MS-PL
// Task 907: verify RenderTargetCube's mip chain is genuinely generated on Bgfx (not just
// present-but-undefined storage), split out of Task 878's original combined RenderTarget2D/
// RenderTargetCube scope. Also the first-ever Bgfx test to sample a RenderTargetCube via
// EnvironmentMapEffect at all, closing Task 874 as a hard prerequisite (see below).
//
// Direct port of vulkan_rendertargetcube_mip_test.cpp (itself a port of Task 334's Vulkan-only
// sample test, with mipMap=true) -- same 6-face solid-blue-fill + EnvironmentMapEffect-sample
// sequence. Mirrors that file's own scope decision: this does NOT assert on coarser mip levels'
// content specifically (see that file's header comment for why a differential attempt was tried
// and abandoned as non-discriminating) -- it confirms the mip-generation *mechanism*
// (BgfxRenderTargetCubeBackend's hasMips=true, Task 906's identical bgfx::createTextureCube fix
// applied per-face) doesn't corrupt level 0 or crash.
//
// **Closes Task 874 as a hard prerequisite**: BgfxGraphicsBackend's EnvironmentMapEffect dispatch
// previously did an unsafe static_cast<const BgfxTextureCubeBackend&> on whatever
// ITextureCubeBackend it was handed -- reading BgfxRenderTargetCubeBackend::fbo (a framebuffer-
// pool handle) where BgfxTextureCubeBackend::handle (a texture-pool handle) was expected whenever
// the argument was actually a RenderTargetCube, the identical bug shape Task 873 already fixed
// for RenderTarget2D via IBgfxSamplable. Fixed via a new IBgfxCubeSamplable interface + a safe
// dynamic_cast, mirroring that exact precedent -- this test is Bgfx's first-ever RenderTargetCube-
// via-EnvironmentMapEffect test, so it could not have been written (would have sampled garbage
// data) without this fix landing first.
//
// **Found and fixed a second real, previously-unreported prerequisite bug**: the shared
// IGraphicsBackend::SetRenderTargetCubeFace default only calls BindAsRenderTargetFace -- it never
// updates currentRtWidth_/currentRtHeight_ (the state EnsureViewState() uses to size the 2D
// ortho/viewport for SpriteBatch draws), the identical bug shape Task 901 already fixed for 2D
// RenderTarget2D. Without a Bgfx-specific override, every SpriteBatch draw into a cube face
// rasterized into a viewport sized to the full window instead of the face's own size. Fixed by
// overriding SetRenderTargetCubeFace to also set these, mirroring SetRenderTarget2D's pattern.
//
// **Found a third, deeper, genuinely architectural bug -- root cause isolated, NOT fixed here**
// (tracked as Task 910; mirrors Task 876's "found, not fixed" precedent for an analogous Vulkan
// RenderTargetCube gap): rendering into all 6 cube faces within a single un-advanced bgfx frame
// (i.e. before any bgfx::frame() boundary, exactly how this test's Phase 1 loop -- and any real
// game refreshing a reflection probe once per game-frame -- would naturally do it) only actually
// renders into whichever face's framebuffer was bound *last*. bgfx::setViewFrameBuffer(viewId, fbo)
// is a per-view, per-frame setting, not a per-submit-call one -- all 6 faces share view id 1
// (BgfxRenderTargetCubeBackend::BindAsRenderTargetFace's hardcoded view index), so every SpriteBatch
// submit queued against view 1 across all 6 iterations ends up rendering into the LAST-bound
// framebuffer when bgfx::frame() actually processes the view, silently leaving the other 5 faces'
// real GPU memory untouched (garbage, not a crash). Confirmed directly: forcing a bgfx::frame()
// boundary between each face's fill (this test's own per-iteration dummy GetBackBufferData call
// below) makes every face render correctly; removing it reproduces the original all-faces-but-
// the-last-are-garbage symptom. A real fix needs each cube face to get its own distinct bgfx view
// id (or another mechanism to force a frame boundary between per-face renders) -- a larger,
// separate architectural change, deliberately not attempted in this task.
//
// Exit code 0 = PASS, 1 = FAIL.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
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

class BgfxRenderTargetCubeMipTest : public Game
{
    std::unique_ptr<RenderTargetCube> rtc_;
    std::unique_ptr<Texture2D>        whiteTex_;
    std::unique_ptr<SpriteBatch>      sb_;
    std::unique_ptr<Texture2D>        blueTex_;
    bool done_   = false;
    int  result_ = 1;

protected:
    void Initialize() override
    {
        Game::Initialize();
        auto& device = getGraphicsDeviceProperty();

        rtc_ = std::make_unique<RenderTargetCube>(
                   device, kCubeSize, /*mipMap=*/true, SurfaceFormat::Color, DepthFormat::None);

        const std::vector<uint8_t> white = { 255, 255, 255, 255 };
        whiteTex_ = std::make_unique<Texture2D>(Texture2D::CreateFromPixels(device, 1, 1, white));

        sb_ = std::make_unique<SpriteBatch>(device);
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

        // Note: SetDepthTestEnabled is omitted -- not yet wired into Bgfx state flags (Task 375's
        // known, documented gap), matching this test family's established workaround.
        device.setBlendStateProperty(BlendState::Opaque);
        device.setRasterizerStateProperty(RasterizerState::CullNone);

        // --- Phase 1: render solid blue into every face (triggers bgfx's own per-face
        // auto-mip-regeneration on unbind -- the Task 906/907 fix). ---
        const CubeMapFace faces[6] = {
            CubeMapFace::PositiveX, CubeMapFace::NegativeX,
            CubeMapFace::PositiveY, CubeMapFace::NegativeY,
            CubeMapFace::PositiveZ, CubeMapFace::NegativeZ,
        };
        for (CubeMapFace face : faces)
        {
            device.SetRenderTarget(rtc_.get(), face);
            sb_->Begin();
            sb_->Draw(*blueTex_,
                      Rectangle(0, 0, kCubeSize, kCubeSize),
                      Rectangle(0, 0, 1, 1),
                      Color::White);
            sb_->End();
            // Work around the Task 910 finding documented above: force a bgfx::frame() boundary
            // between each face's fill (a throwaway 1x1 backbuffer read) so this face's own
            // framebuffer binding is actually the one bgfx processes for this face's submit,
            // instead of being silently overwritten by the next iteration's setViewFrameBuffer
            // call before any of the 6 faces' draws are actually processed.
            Color dummy(0, 0, 0, 0);
            const Rectangle dummyReg(0, 0, 1, 1);
            device.GetBackBufferData(&dummyReg, &dummy, 0, 1);
        }
        device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

        // --- Phase 2: sample the unbound, now-mip-complete RenderTargetCube via
        // EnvironmentMapEffect (Task 874's dynamic_cast fix is required for this to read
        // anything meaningful at all). Retried per this test family's established Bgfx
        // convention (GetBackBufferData only reliably reflects the first read per frame). ---
        Color centPx(0, 0, 0, 0);
        for (int i = 0; i < 20; ++i)
        {
            device.Clear(Color(0, 0, 0, 255));

            EnvironmentMapEffect fx(device);
            fx.setDiffuseColorProperty(Vector3(1.0f, 1.0f, 1.0f));
            fx.setEmissiveColorProperty(Vector3(0.0f, 0.0f, 0.0f));
            fx.setEnvironmentMapAmountProperty(1.0f);
            fx.setEnvironmentMapSpecularProperty(Vector3(0.0f, 0.0f, 0.0f));
            fx.setTextureProperty(whiteTex_.get());
            fx.setEnvironmentMapProperty(rtc_.get());
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

        const bool pass = (centPx.getRProperty() <= 50  &&
                           centPx.getGProperty() <= 50  &&
                           centPx.getBProperty() >= 200);

        if (pass)
        {
            std::printf("[PASS] BgfxRenderTargetCubeMip: centre=(%d,%d,%d)\n",
                        centPx.getRProperty(), centPx.getGProperty(), centPx.getBProperty());
            result_ = 0;
        }
        else
        {
            std::printf("[FAIL] BgfxRenderTargetCubeMip: centre=(%d,%d,%d), "
                        "expected blue (R<=50, G<=50, B>=200)\n",
                        centPx.getRProperty(), centPx.getGProperty(), centPx.getBProperty());
        }
        Exit();
    }

public:
    int getResult() const { return result_; }
};

int main()
{
    BgfxRenderTargetCubeMipTest game;
    game.Run();
    return game.getResult();
}
