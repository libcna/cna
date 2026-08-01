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
//   - Every face fill must be a real draw call (SpriteBatch), not Clear()-only (Task 875-class
//     finding, applies here too).
// Task 910 and REMED-GFX-155 subsequently fixed render-target view stability and ordered view
// segmentation. REMED-GFX-138 removes the obsolete per-face dummy reads and result retry: the six
// producers must succeed in public order with one direct resolved observation.
//
// Exit code 0 = all checks PASS, 1 = any FAILs.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
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

class BgfxRenderTargetCubeMsaaTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    std::unique_ptr<SpriteBatch>           sb_;
    std::unique_ptr<Texture2D>             blueTex_;
    bool done_   = false;
    int  result_ = 1;

protected:
    void Initialize() override
    {
        Game::Initialize();
        auto& device = getGraphicsDeviceProperty();
        sb_ = std::make_unique<SpriteBatch>(device);
        const std::vector<uint8_t> blue = { 0, 0, 255, 255 };
        blueTex_ = std::make_unique<Texture2D>(Texture2D::CreateFromPixels(device, 1, 1, blue));
    }

    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;

        auto& device = getGraphicsDeviceProperty();
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

        Color resolvedPixel(7, 11, 19, 197);
        const Rectangle oneTexel(0, 0, 1, 1);
        bool returned = false;
        try
        {
            rtcMsaa.GetData(CubeMapFace::PositiveZ, 0, &oneTexel, &resolvedPixel, 0, 1);
            returned = true;
        }
        catch (const std::exception&)
        {
        }
        const bool renderOk = returned && resolvedPixel.getRProperty() <= 50 &&
                              resolvedPixel.getGProperty() <= 50 &&
                              resolvedPixel.getBProperty() >= 200;
        std::printf("[%s] MSAA cube resolved read: returned=%d pixel=(%d,%d,%d) (expected blue)\n",
                    renderOk ? "PASS" : "FAIL",
                    returned ? 1 : 0, resolvedPixel.getRProperty(),
                    resolvedPixel.getGProperty(), resolvedPixel.getBProperty());

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
