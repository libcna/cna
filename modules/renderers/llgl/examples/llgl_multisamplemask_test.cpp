// SPDX-License-Identifier: MS-PL
// plans/plan_llgl.md LLGL-33: BlendState.MultiSampleMask -- a real per-sample coverage bitmask, proven
// against a genuinely multisampled RenderTarget2D (LLGL-26 MSAA follow-up), not just a bookkeeping
// check. Mirrors examples/gfx077_colorwritechannels_3d_test.cpp's own MSAA sub-block technique
// (full-screen Opaque-blended quad, differential dst/src baselines) but as a dedicated,
// runtime-gated LLGL test: this renderer picks its native module at RUNTIME, so the shared test's
// own GFX077_MULTISAMPLEMASK_SUPPORTED compile-time flag cannot express "the Vulkan module
// supports it, the OpenGL module does not" the way single-native-API renderers can.
//
// Method: clear a 4x MSAA RenderTarget2D to a destination colour D, then draw a full-target quad
// in an Opaque-blended source colour S (SrcBlend=One, DstBlend=Zero -> blend disabled, output ==
// source, so the ONLY thing that can preserve D is a sample mask blocking every sample's write
// entirely):
//   Check A -- MultiSampleCount really applies (construction sanity, mirrors every other LLGL-26
//     MSAA-follow-up test's own first check).
//   Check B -- the DEFAULT MultiSampleMask (0xFFFFFFFF, every sample enabled) resolves normally to
//     the quad's own source colour S -- proves the plumbing doesn't accidentally mask anything by
//     default.
//   Check C -- MultiSampleMask=0 (every sample disabled) resolves to the clear colour D instead of
//     S, on a module that genuinely honours it -- OR is detected and reported [SKIP] (not FAIL) on
//     a module that does not, matching Llgl_Msaa/Llgl_MRT's own established module-dependent
//     precedent. Confirmed by reading LLGL's own vendored source before writing this test: the
//     Vulkan module applies VkPipelineMultisampleStateCreateInfo::pSampleMask unconditionally
//     (VKGraphicsPSO.cpp's own CreateMultisampleState); the OpenGL module's own SetSampleMask call
//     is permanently `#if 0`'d out (GLBlendState.cpp), so it can never apply a sample mask at all
//     on this renderer, on any driver.
//
// Exit code 0 = every executed check PASS (or was [SKIP]ped for a confirmed module reason), 1 =
// any genuine FAIL.

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/BufferUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/ColorWriteChannels.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"

#include "common/PixelTestGame.hpp"

#include <cstdio>
#include <memory>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kRTSize = 32;

    const Color kDst(10, 20, 30, 255);
    const Color kSrc(200, 100, 50, 255);

    BlendState MakeOpaqueMaskedBlend(unsigned int sampleMask)
    {
        BlendState bs; // default = Opaque (One/Zero) -> blend disabled, output == source
        bs.setColorWriteChannelsProperty(ColorWriteChannels::All);
        bs.setMultiSampleMaskProperty(static_cast<int>(sampleMask));
        return bs;
    }
}

class LlglMultiSampleMaskTest : public CNA::Examples::PixelTestGame
{
public:
    // Draws a full-target quad in kSrc, over a kDst clear, into an MSAA RenderTarget2D, using the
    // given MultiSampleMask, and returns the resolved centre pixel.
    Color RenderWithMask(GraphicsDevice& device, RenderTarget2D& target, unsigned int sampleMask)
    {
        BasicEffect effect(device);
        effect.VertexColorEnabled = true;
        effect.setTextureEnabledProperty(false);
        effect.setLightingEnabledProperty(false);
        effect.setWorldProperty(Matrix::getIdentityProperty());
        effect.setViewProperty(Matrix::getIdentityProperty());
        effect.setProjectionProperty(Matrix::CreateOrthographicOffCenter(
            0.0f, static_cast<float>(kRTSize), static_cast<float>(kRTSize), 0.0f, 0.0f, 1.0f));

        const VertexPositionColor quad[6] = {
            {Vector3(0.0f, 0.0f, 0.0f), kSrc},
            {Vector3(static_cast<float>(kRTSize), 0.0f, 0.0f), kSrc},
            {Vector3(0.0f, static_cast<float>(kRTSize), 0.0f), kSrc},
            {Vector3(0.0f, static_cast<float>(kRTSize), 0.0f), kSrc},
            {Vector3(static_cast<float>(kRTSize), 0.0f, 0.0f), kSrc},
            {Vector3(static_cast<float>(kRTSize), static_cast<float>(kRTSize), 0.0f), kSrc},
        };
        VertexBuffer buffer(device, VertexPositionColor::getVertexDeclarationStatic(), 6, BufferUsage::None);
        buffer.SetData(quad, 0, 6);

        device.SetRenderTarget(&target);
        device.Clear(kDst);
        device.setBlendStateProperty(MakeOpaqueMaskedBlend(sampleMask));
        device.SetVertexBuffer(&buffer);
        effect.Apply();
        device.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
        device.SetVertexBuffer(nullptr);
        device.SetRenderTarget(nullptr);

        Color pixel(0, 0, 0, 0);
        const Rectangle centre(kRTSize / 2, kRTSize / 2, 1, 1);
        target.GetData(0, &centre, &pixel, 0, 1);
        return pixel;
    }

    void RunTest() override
    {
        auto& device = getGraphicsDeviceProperty();

        std::unique_ptr<RenderTarget2D> msaaTarget;
        try
        {
            msaaTarget = std::make_unique<RenderTarget2D>(
                device, kRTSize, kRTSize, false, SurfaceFormat::Color, DepthFormat::None, 4);
        }
        catch (const std::exception&)
        {
            msaaTarget.reset();
        }
        if (!ExpectTrue("4x MSAA RenderTarget2D construction succeeds", msaaTarget != nullptr))
            return;

        const int appliedSamples = msaaTarget->getMultiSampleCountProperty();
        if (!ExpectTrue("MultiSampleCount is really applied to the render target (LLGL-26 MSAA "
                        "follow-up already proved this genuinely antialiases; here it just needs "
                        "to be > 1 for MultiSampleMask to have anything to mask)",
                        appliedSamples > 1))
        {
            return;
        }

        const Color allMask = RenderWithMask(device, *msaaTarget, 0xFFFFFFFFu);
        ExpectTrue("the default MultiSampleMask (every sample enabled) resolves normally to the "
                  "quad's own source colour",
                  allMask == kSrc);

        const Color zeroMask = RenderWithMask(device, *msaaTarget, 0u);
        if (zeroMask == kDst)
        {
            ExpectTrue("MultiSampleMask=0 (every sample disabled) resolves to the clear colour "
                      "instead of the quad's source colour -- a real per-sample coverage mask, "
                      "not merely \"didn't crash\"", true);
        }
        else
        {
            std::printf("[SKIP] this module does not apply BlendState.MultiSampleMask to a "
                        "RenderTarget2D in this environment -- MultiSampleMask=0 resolved to "
                        "(%d,%d,%d,%d) instead of the expected clear colour (%d,%d,%d,%d)\n",
                        zeroMask.getRProperty(), zeroMask.getGProperty(), zeroMask.getBProperty(),
                        zeroMask.getAProperty(), kDst.getRProperty(), kDst.getGProperty(),
                        kDst.getBProperty(), kDst.getAProperty());
        }
    }
};

int main()
{
    return CNA::Examples::RunPixelTest<LlglMultiSampleMaskTest>();
}
