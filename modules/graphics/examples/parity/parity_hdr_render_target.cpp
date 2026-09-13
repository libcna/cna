// SPDX-License-Identifier: MS-PL
// plans/plan_webgpu.md WEBGPU-199 (harness: WEBGPU-207): a half-float render target really holds
// values above 1.0, and sampling one gives the same picture on both renderers.
//
// THE PROBLEM WITH MEASURING THIS IN 8-BIT OUTPUT. A target holding 2.0 and a target that clamped
// it to 1.0 both read back as 255 once they reach an 8-bit backbuffer, so the obvious fixture --
// "render 2.0, sample it, look" -- cannot tell a real float target from a substituted `Color` one.
// That is exactly the substitution `MOD-115`/`MOD-107` exist to catch, so a fixture that cannot see
// it is worse than none.
//
// The tint is what makes it visible. `SpriteBatch` multiplies the sampled texel by the draw colour
// BEFORE the 8-bit write, so a tint of 0.5 halves whatever the target actually held:
//
//   0  target holds 2.0, drawn at tint 0.5  -> 2.0 * 0.5 = 1.0 -> 255
//   1  target holds 1.0, drawn at tint 0.5  -> 1.0 * 0.5 = 0.5 -> 128
//
// A target that clamped its own storage to 1.0 renders BOTH columns at 128, and the "materially
// different" assertion fails. A target that dropped the tint renders both at 255, and the second
// column's absolute check fails. Neither defect can produce this pair.
//
// The two columns also have to agree between renderers, which is the parity half: `HdrBlendable` is
// RGBA16F on both, so the sampled result is byte-comparable. (The single- and dual-channel float
// formats are deliberately NOT here: EasyGL broadcasts a one-channel texture to (r,r,r,1) where
// WGSL's `texture_2d<f32>` gives (r,0,0,1), which is a real divergence this fixture must not
// pretend away. `RenderTargetFormatAgreementTests` records it; `WEBGPU-199`/`200` own settling it.)

#include "parity/ParityFixture.hpp"

#include "CNA/GraphicsCapability.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"

#include <array>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kWidth = 128;
    constexpr int kHeight = 64;
    constexpr int kColumns = 2;
    constexpr int kTargetSize = 32;

    const Color kClearColor(9, 13, 17, 255);
    /// Half brightness, as exact in 8-bit as a half is: 128/255 is not 0.5, so the expected values
    /// below are derived from 128 rather than from 0.5, and the tolerance covers the difference.
    const Color kHalfTint(128, 128, 128, 255);
}

/// WEBGPU-199: an HdrBlendable target keeps values above 1.0, and both renderers agree.
class HdrRenderTargetParityFixture : public CNA::Parity::ParityFixture
{
public:
    HdrRenderTargetParityFixture() : ParityFixture(kWidth, kHeight) {}

protected:
    void RunFixture() override
    {
        auto& device = getGraphicsDeviceProperty();
        const CNA::Parity::ParityGrid grid{kWidth, kHeight, kColumns, 1};

        Require(device.SupportsCapability(CNA::GraphicsCapability::HalfFloatRenderTargets),
                "the renderer must support half-float render targets to run this fixture");

        device.setBlendStateProperty(BlendState::Opaque);
        device.setDepthStencilStateProperty(DepthStencilState::None);

        // Two targets, filled by CLEAR alone -- no shader, so nothing between the requested value
        // and the target's storage that could clamp on the way in.
        RenderTarget2D above(device, kTargetSize, kTargetSize, false, SurfaceFormat::HdrBlendable,
                             DepthFormat::None, 0, RenderTargetUsage::PreserveContents);
        RenderTarget2D unit(device, kTargetSize, kTargetSize, false, SurfaceFormat::HdrBlendable,
                            DepthFormat::None, 0, RenderTargetUsage::PreserveContents);

        device.SetRenderTarget(&above);
        device.Clear(2.0f, 0.0f, 0.0f, 1.0f);
        device.SetRenderTarget(&unit);
        device.Clear(1.0f, 0.0f, 0.0f, 1.0f);
        device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

        device.Clear(kClearColor);
        {
            SpriteBatch batch(device);
            batch.Begin();
            const auto cell = grid.getCellWidthProperty();
            batch.Draw(above, Rectangle(2, 2, cell - 4, kHeight - 4),
                       Rectangle(0, 0, kTargetSize, kTargetSize), kHalfTint);
            batch.Draw(unit, Rectangle(cell + 2, 2, cell - 4, kHeight - 4),
                       Rectangle(0, 0, kTargetSize, kTargetSize), kHalfTint);
            batch.End();
        }

        // 2.0 * (128/255) = 1.004, which saturates: 255.
        ExpectAverage("a value above 1.0 survived the target and saturates at half tint",
                      grid.Interior(0, 0), Color(255, 0, 0, 255), 3);
        // 1.0 * (128/255) = 0.502 -> 128.
        ExpectAverage("a value of exactly 1.0 halves at the same tint",
                      grid.Interior(1, 0), Color(128, 0, 0, 255), 3);
        // The assertion that catches a target which clamped its own storage: it would render both
        // columns at 128, satisfying the second check and failing this one.
        ExpectDistinct("the target's storage really is unclamped -- 2.0 and 1.0 are different there",
                       grid.Interior(0, 0), grid.Interior(1, 0), /*minDelta=*/100);
        ExpectFlat("the above-1.0 column is flat", grid.Interior(0, 0), /*maxSpread=*/3);
        ExpectFlat("the exactly-1.0 column is flat", grid.Interior(1, 0), /*maxSpread=*/3);
    }
};

CNA_PARITY_FIXTURE_MAIN(HdrRenderTargetParityFixture)
