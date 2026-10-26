// SPDX-License-Identifier: MS-PL
// A RenderTarget2D's MultiSampleCount is its own, not the device's: on a single-sampled device,
// targets with different requested counts -- interleaved in one frame -- each rasterize with the
// count they report.
//
// Moved from the retired VULKAN suite (VULKAN-216). XNA's RenderTarget2D takes
// preferredMultiSampleCount per instance and reports, through MultiSampleCount, the count it
// actually applied (never more than was asked for). A count alone cannot tell "4x engaged" from
// "4 reported while one sample is rasterized", so every leg also draws a triangle whose vertical
// edge bisects a texel on the target's centre row and reads that row back from the target itself: a
// multisample resolve leaves partially covered texels on the edge, a single-sampled rasterizer
// leaves none. The assertion is the invariant, not a number -- the edge is anti-aliased exactly
// when the applied count exceeds one -- because which counts a device offers is its own business.
//
//   0  the device itself is single-sampled, so nothing below can be inherited from it
//   A  a 4x request reports a count in [0, 4], and its edge is anti-aliased exactly when that
//      count exceeds one
//   B  a target that asked for no multisampling reports 0 and its edge is binary
//   C  4x / none / 4x interleaved in one frame: each target keeps its own count and its own edge
//   D  a 2x request reports at most 2, anti-aliased exactly when the count exceeds one
//
// Exit code 0 = PASS, 1 = FAIL, 77 = skipped (no display, or the renderer has no MSAA).

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsProfile.hpp"
#include "Microsoft/Xna/Framework/Graphics/PresentationParameters.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"
#include "CNA/GraphicsCapability.hpp"

#include "common/PixelTestGame.hpp"

#include <cstdio>
#include <memory>
#include <string>
#include <vector>
#include "ConformanceSupport.hpp"

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kSize = 32;

    // A texel strictly between the clear colour and the triangle's colour can only come from
    // partial coverage.
    bool IsPartial(const Color& c) { return c.getRProperty() > 40 && c.getRProperty() < 215; }

    int PartialCount(const std::vector<Color>& row)
    {
        int n = 0;
        for (const Color& c : row) n += IsPartial(c) ? 1 : 0;
        return n;
    }
}

class RenderTargetOwnMsaaTest final : public CNA::Examples::PixelTestGame
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    bool skipped_ = false;

    std::unique_ptr<RenderTarget2D> MakeTarget(int requestedCount)
    {
        return std::make_unique<RenderTarget2D>(getGraphicsDeviceProperty(), kSize, kSize, false,
                                                SurfaceFormat::Color, DepthFormat::None,
                                                requestedCount, RenderTargetUsage::DiscardContents);
    }

    // Draws a half-texel vertical edge into `rt` and returns its centre row from the target.
    std::vector<Color> RenderAndReadRow(RenderTarget2D& rt)
    {
        auto& device = getGraphicsDeviceProperty();
        device.setBlendStateProperty(BlendState::Opaque);
        device.SetRenderTarget(&rt);
        device.Clear(Color(0, 0, 0, 255));

        BasicEffect fx(device);
        fx.setWorldProperty(Matrix::getIdentityProperty());
        fx.setViewProperty(Matrix::getIdentityProperty());
        fx.setProjectionProperty(Matrix::getIdentityProperty());
        fx.setVertexColorEnabledProperty(true);
        fx.Apply();

        const Color white(255, 255, 255, 255);
        // The vertical edge runs through the centre of texel kSize/2, between the left and the
        // right samples of every pattern -- 2x's (0.25,0.25)/(0.75,0.75) included, which an exact
        // y=x diagonal cannot separate. XNA puts a texel's centre on its integer coordinate,
        // also on a multisampled target (spikes/xna-msaa-pixel-center-spike/), so that centre
        // is NDC x=0; the half-integer one, NDC x=1/kSize, lies beyond both 2x samples there.
        constexpr float edgeX = 0.0f;
        const VertexPositionColor triangle[3] = {
            { Vector3(-1.0f,  1.0f, 0.0f), white },
            { Vector3(edgeX,  1.0f, 0.0f), white },
            { Vector3(edgeX, -1.0f, 0.0f), white },
        };
        device.DrawUserPrimitives(PrimitiveType::TriangleList, triangle, 0, 1);
        device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

        std::vector<Color> row(kSize, Color(0, 0, 0, 0));
        const Rectangle centreRow(0, kSize / 2, kSize, 1);
        rt.GetData(0, &centreRow, row.data(), 0, kSize);
        return row;
    }

    // The invariant every leg asserts: anti-aliased exactly when the applied count exceeds one.
    void CheckTarget(const std::string& label, int requested, int applied,
                     const std::vector<Color>& row)
    {
        const int partial = PartialCount(row);
        const bool consistent = (applied > 1) == (partial > 0);
        const bool inRange = applied >= 0 && applied <= requested;
        Check(consistent && inRange,
              (label + ": requested " + std::to_string(requested) + ", applied " +
               std::to_string(applied) + ", " + std::to_string(partial) +
               " partially covered edge texels (want >0 exactly when applied > 1, and applied in [0, " +
               std::to_string(requested) + "])").c_str());
    }

public:
    RenderTargetOwnMsaaTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setGraphicsProfileProperty(GraphicsProfile::HiDef);
        gdm_->setPreferredBackBufferWidthProperty(kSize);
        gdm_->setPreferredBackBufferHeightProperty(kSize);
        // Deliberately NOT PreferMultiSampling: the device must stay single-sampled.
    }

    [[nodiscard]] int ExitCode() const
    {
        return skipped_ ? CNA::Examples::kSkipExitCode : getResultProperty();
    }

    void RunTest() override
    {
        auto& device = getGraphicsDeviceProperty();
        if (!device.SupportsCapability(CNA::GraphicsCapability::MultiSampleAntiAliasing))
        {
            std::printf("[SKIP] this renderer reports no multisample anti-aliasing\n");
            skipped_ = true;
            return;
        }

        const int deviceCount =
            device.getPresentationParametersProperty().getMultiSampleCountProperty();
        Check(deviceCount == 0,
              ("0 the back buffer is single-sampled (PresentationParameters.MultiSampleCount=" +
               std::to_string(deviceCount) + ")").c_str());

        int fourXApplied = 0;
        {
            auto rt = MakeTarget(4);
            fourXApplied = rt->getMultiSampleCountProperty();
            CheckTarget("A a 4x target on a single-sampled device", 4, fourXApplied,
                        RenderAndReadRow(*rt));
        }
        {
            auto rt = MakeTarget(0);
            const int applied = rt->getMultiSampleCountProperty();
            const auto row = RenderAndReadRow(*rt);
            Check(applied == 0 && PartialCount(row) == 0,
                  ("B a target that asked for no multisampling reports " + std::to_string(applied) +
                   " and has " + std::to_string(PartialCount(row)) +
                   " partially covered edge texels (want 0 and 0)").c_str());
        }
        {
            auto first = MakeTarget(4);
            auto plain = MakeTarget(0);
            auto second = MakeTarget(4);
            const auto rowFirst = RenderAndReadRow(*first);
            const auto rowPlain = RenderAndReadRow(*plain);
            const auto rowSecond = RenderAndReadRow(*second);
            CheckTarget("C1 interleaved: the first 4x target", 4,
                        first->getMultiSampleCountProperty(), rowFirst);
            CheckTarget("C2 interleaved: the single-sampled target between them", 0,
                        plain->getMultiSampleCountProperty(), rowPlain);
            CheckTarget("C3 interleaved: the second 4x target", 4,
                        second->getMultiSampleCountProperty(), rowSecond);
        }
        {
            auto rt = MakeTarget(2);
            CheckTarget("D a 2x request", 2, rt->getMultiSampleCountProperty(),
                        RenderAndReadRow(*rt));
        }

        if (fourXApplied <= 1)
            std::printf("[NOTE] this device applied no multisampling to a 4x request, so every "
                        "leg could only prove the binary half of the invariant\n");
    }
};

int main()
{
    if (!CNA::Examples::Conformance::ProbePixelOutputAvailable("render_target_own_msaa"))
        return CNA::Examples::Conformance::kSkipExitCode;
    if (!CNA::Examples::ProbeGpuDisplayAvailable())
        return CNA::Examples::kSkipExitCode;
    RenderTargetOwnMsaaTest game;
    game.Run();
    return game.ExitCode();
}
