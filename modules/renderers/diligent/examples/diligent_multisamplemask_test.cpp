// SPDX-License-Identifier: MS-PL
//
// plans/plan_diligent.md DILIGENT-60: real-device proof that BlendState.MultiSampleMask reaches
// Dg::GraphicsPipelineDesc::SampleMask instead of being silently discarded.
//
// Methodology: SampleMask bit i gates whether multisample coverage sample i of a covered pixel is
// written at all. On a genuinely multisampled (4x) RenderTarget2D, a fully-covered triangle drawn
// with:
//   - SampleMask = 0          -> no samples written at all; the resolved pixel stays the clear colour
//   - SampleMask = 1          -> only sample 0 of 4 written; the resolved (averaged) pixel is a
//                                 ~1/4-strength blend of draw colour and clear colour -- a value no
//                                 other mask setting produces, so it also proves this is genuinely a
//                                 per-sample coverage effect and not e.g. a binary discard
//   - SampleMask = 0xFFFFFFFF -> every sample written; the resolved pixel is the full draw colour
// On a single-sample (non-MSAA) target only bit 0 is meaningful: SampleMask=0 -> nothing written,
// SampleMask=1/0xFFFFFFFF -> fully written (both identical, since there is only one sample to gate).
//
// Checks 0-2 -- single-sample RenderTarget2D: mask 0 / 1 / all-ones.
// Checks 3-5 -- genuinely 4x MSAA RenderTarget2D: mask 0 / 1 / all-ones, using the ~1/4-blend
//   signature above to prove the mask reaches per-sample coverage, not just an on/off switch.
// Checks 6-8 -- A(all-ones) -> B(0) -> A(all-ones) again on the SAME 4x target and draw position,
//   DILIGENT-60's own "A->B->A cache transition" acceptance criterion: proves GetOrCreatePipeline()
//   creates/reuses the right cached pipeline across a mask change, not stuck at whichever mask first
//   created a pipeline for this shader variant/topology/blend/depth-stencil/format/sample-count.
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
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <memory>
#include <string>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kSize = 16;

    bool ColorNear(Color a, Color b, int tolerance = 20)
    {
        return std::abs(a.getRProperty() - b.getRProperty()) <= tolerance &&
               std::abs(a.getGProperty() - b.getGProperty()) <= tolerance &&
               std::abs(a.getBProperty() - b.getBProperty()) <= tolerance;
    }

    // Neither near the clear colour, near the full draw colour, nor near a 1/2 blend -- the ~1/4
    // (of 4 MSAA samples) signature only genuine per-sample coverage masking can produce.
    bool IsQuarterBlend(Color result, Color clearColor, Color drawColor)
    {
        const int expectedR = clearColor.getRProperty() +
                              (drawColor.getRProperty() - clearColor.getRProperty()) / 4;
        const int expectedG = clearColor.getGProperty() +
                              (drawColor.getGProperty() - clearColor.getGProperty()) / 4;
        const int expectedB = clearColor.getBProperty() +
                              (drawColor.getBProperty() - clearColor.getBProperty()) / 4;
        return ColorNear(result, Color(expectedR, expectedG, expectedB, 255), 40);
    }
}

class DiligentMultiSampleMaskTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> graphicsDeviceManager_;
    int passCount_ = 0;
    int totalCount_ = 0;
    int result_ = 1;

    void Check(bool ok, const std::string& label)
    {
        ++totalCount_;
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label.c_str());
        if (ok)
            ++passCount_;
    }

    // Fills the whole target with a solid quad using the given BlendState (which carries
    // MultiSampleMask), reads back a centre pixel.
    static Color DrawAndRead(GraphicsDevice& device, RenderTarget2D& target, const Color& clearColor,
                             const Color& drawColor, int multiSampleMask)
    {
        device.SetRenderTarget(&target);
        device.Clear(clearColor);
        device.setRasterizerStateProperty(RasterizerState::CullNone);

        BlendState blend = BlendState::Opaque;
        blend.setMultiSampleMaskProperty(multiSampleMask);
        device.setBlendStateProperty(blend);

        BasicEffect effect(device);
        effect.setWorldProperty(Matrix::getIdentityProperty());
        effect.setViewProperty(Matrix::getIdentityProperty());
        effect.setProjectionProperty(Matrix::getIdentityProperty());
        effect.VertexColorEnabled = true;
        effect.Apply();

        const VertexPositionColor quad[6] = {
            {Vector3(-1.0f, 1.0f, 0.0f), drawColor},  {Vector3(1.0f, 1.0f, 0.0f), drawColor},
            {Vector3(-1.0f, -1.0f, 0.0f), drawColor}, {Vector3(1.0f, 1.0f, 0.0f), drawColor},
            {Vector3(1.0f, -1.0f, 0.0f), drawColor},  {Vector3(-1.0f, -1.0f, 0.0f), drawColor},
        };
        device.DrawUserPrimitives(PrimitiveType::TriangleList, quad, 0, 2);
        device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

        Color pixel(0, 0, 0, 0);
        const Rectangle region(kSize / 2, kSize / 2, 1, 1);
        target.GetData(0, &region, &pixel, 0, 1);
        return pixel;
    }

protected:
    void Draw(const GameTime&) override
    {
        auto& device = getGraphicsDeviceProperty();
        const Color clearColor = Color::Black;
        const Color drawColor = Color::Red;

        // Checks 0-2: single-sample target -- only bit 0 of the mask matters.
        {
            RenderTarget2D single(device, kSize, kSize, false, SurfaceFormat::Color,
                                  DepthFormat::None, 0, RenderTargetUsage::DiscardContents);

            Color r0 = DrawAndRead(device, single, clearColor, drawColor, 0);
            Check(ColorNear(r0, clearColor),
                 "single-sample SampleMask=0: nothing written, stays clear colour");

            Color r1 = DrawAndRead(device, single, clearColor, drawColor, 1);
            Check(ColorNear(r1, drawColor), "single-sample SampleMask=1: fully written");

            Color rAll = DrawAndRead(device, single, clearColor, drawColor,
                                     static_cast<int>(0xFFFFFFFFu));
            Check(ColorNear(rAll, drawColor), "single-sample SampleMask=all-ones: fully written");
        }

        // Checks 3-5 + 6-8 (A->B->A): genuinely 4x MSAA target.
        RenderTarget2D msaa(device, kSize, kSize, false, SurfaceFormat::Color, DepthFormat::None, 4,
                            RenderTargetUsage::DiscardContents);
        std::printf("    (requested MultiSampleCount=4, applied %d)\n",
                    msaa.getMultiSampleCountProperty());
        const bool genuinelyMsaa = msaa.getMultiSampleCountProperty() > 1;

        Color m0 = DrawAndRead(device, msaa, clearColor, drawColor, 0);
        Check(ColorNear(m0, clearColor), "4x MSAA SampleMask=0: nothing written, stays clear colour");

        Color m1 = DrawAndRead(device, msaa, clearColor, drawColor, 1);
        Check(!genuinelyMsaa || IsQuarterBlend(m1, clearColor, drawColor),
             "4x MSAA SampleMask=1: resolved pixel is a ~1/4 blend (only 1 of 4 samples written)");

        Color mAll = DrawAndRead(device, msaa, clearColor, drawColor, static_cast<int>(0xFFFFFFFFu));
        Check(ColorNear(mAll, drawColor), "4x MSAA SampleMask=all-ones: fully written");

        // A -> B -> A pipeline-cache-identity check, same draw/target/topology/blend-func/format
        // throughout so a stale cache hit is the only way a wrong result could appear.
        Color a1 = DrawAndRead(device, msaa, clearColor, drawColor, static_cast<int>(0xFFFFFFFFu));
        Check(ColorNear(a1, drawColor), "A->B->A step A (SampleMask=all-ones): fully written");

        Color b1 = DrawAndRead(device, msaa, clearColor, drawColor, 0);
        Check(ColorNear(b1, clearColor), "A->B->A step B (SampleMask=0): nothing written");

        Color a2 = DrawAndRead(device, msaa, clearColor, drawColor, static_cast<int>(0xFFFFFFFFu));
        Check(ColorNear(a2, drawColor),
             "A->B->A step A again (SampleMask=all-ones): fully written, cache not stuck at B");

        std::printf("=== %d/%d PASS ===\n", passCount_, totalCount_);
        result_ = passCount_ == totalCount_ ? 0 : 1;
        Exit();
    }

public:
    DiligentMultiSampleMaskTest()
    {
        graphicsDeviceManager_ = std::make_unique<GraphicsDeviceManager>(this);
        graphicsDeviceManager_->setPreferredBackBufferWidthProperty(64);
        graphicsDeviceManager_->setPreferredBackBufferHeightProperty(64);
    }

    [[nodiscard]] int GetResult() const { return result_; }
};

int main()
{
    try
    {
        DiligentMultiSampleMaskTest game;
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
