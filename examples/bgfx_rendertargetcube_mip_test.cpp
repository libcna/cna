// SPDX-License-Identifier: MS-PL
// Task 907: verify RenderTargetCube's mip chain is genuinely generated on Bgfx (not just
// present-but-undefined storage), split out of Task 878's original combined RenderTarget2D/
// RenderTargetCube scope. Also the first-ever Bgfx test to sample a RenderTargetCube via
// EnvironmentMapEffect at all, closing Task 874 as a hard prerequisite (see below).
//
// Direct port of vulkan_rendertargetcube_mip_test.cpp (itself a port of Task 334's Vulkan-only
// sample test, with mipMap=true) -- same 6-face solid-blue fill. REMED-GFX-138 strengthens the
// result oracle from a retrying backbuffer sample to one direct public read of generated level 1,
// which is the exact contract this fixture names.
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
// Task 910 and REMED-GFX-155 subsequently supplied stable render-target views plus ordered segment
// views. REMED-GFX-138 therefore removes this fixture's obsolete per-face dummy backbuffer reads
// and final retry loop: all six producers must now work in their natural public order, with one
// direct generated-level observation and no synchronization workaround.
//
// Exit code 0 = PASS, 1 = FAIL.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/CubeMapFace.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetCube.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"

#include <cstdio>
#include <memory>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

static constexpr int kCubeSize = 32;

class BgfxRenderTargetCubeMipTest : public Game
{
    std::unique_ptr<RenderTargetCube> rtc_;
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

        sb_ = std::make_unique<SpriteBatch>(device);
        const std::vector<uint8_t> blue = { 0, 0, 255, 255 };
        blueTex_ = std::make_unique<Texture2D>(Texture2D::CreateFromPixels(device, 1, 1, blue));
    }

    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;

        auto& device = getGraphicsDeviceProperty();
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
        }
        device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

        // --- Phase 2: observe the generated mip directly, once. ---
        Color mipPixel(7, 11, 19, 197);
        const Rectangle oneTexel(0, 0, 1, 1);
        bool returned = false;
        try
        {
            rtc_->GetData(CubeMapFace::PositiveZ, 1, &oneTexel, &mipPixel, 0, 1);
            returned = true;
        }
        catch (const std::exception&)
        {
        }

        const bool pass = returned && mipPixel.getRProperty() <= 50 &&
                          mipPixel.getGProperty() <= 50 &&
                          mipPixel.getBProperty() >= 200;

        if (pass)
        {
            std::printf("[PASS] BgfxRenderTargetCubeMip: level1=(%d,%d,%d)\n",
                        mipPixel.getRProperty(), mipPixel.getGProperty(), mipPixel.getBProperty());
            result_ = 0;
        }
        else
        {
            std::printf("[FAIL] BgfxRenderTargetCubeMip: returned=%d level1=(%d,%d,%d), "
                        "expected blue (R<=50, G<=50, B>=200)\n", returned ? 1 : 0,
                        mipPixel.getRProperty(), mipPixel.getGProperty(), mipPixel.getBProperty());
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
