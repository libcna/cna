// SPDX-License-Identifier: MS-PL
//
// plan_vulkan.md VULKAN-216 -- a RenderTarget2D's MultiSampleCount is its OWN, not the device's.
//
// The device here is created WITHOUT multisampling, and deliberately so: this test never calls
// GraphicsDevice::RecreateRendererForMultiSampleCount(), the CNAEXT scaffolding hook that
// vulkan_rendertarget2d_msaa_test.cpp needs because Task 878/879 tied a render target's sample
// count to the renderer's own. XNA's RenderTarget2D takes preferredMultiSampleCount per instance
// and FNA honours it per instance, so a target asking for 4x on a single-sampled device must get
// 4x -- which is exactly what the old behaviour could not do: it reported an applied count of 0
// and rendered one sample. EasyGL reported 4 for the same construction.
//
// Two legs, and the pixel one is the leg that matters. A count-only assertion cannot tell "4x
// engaged" from "4 reported while rendering one sample" -- so the discriminating check renders a
// diagonal-edged triangle and looks for partially-covered pixels along the edge, the signature a
// multisample resolve produces and a single-sampled rasterizer cannot. The established
// binary-vs-blended methodology of easygl_rendertarget2d_msaa_test.cpp (Task 337) and its Vulkan
// port, with one difference: the readback comes straight from the render target rather than via
// a sprite bounce off the backbuffer, so nothing between the resolve and the assertion can
// introduce a blend of its own.
//
// Leg C is the reason PipelineKey gained a sample count: two targets with DIFFERENT counts, alive
// and drawn into in the same frame. Sharing one cache key would hand the second target the first
// one's pipeline, which is not merely wrong output -- it is a pipeline whose rasterizationSamples
// disagree with the render pass it is bound in, which the validation layer reports and which the
// VULKAN-408 gate turns into a test failure.

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
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"

#include <cstdio>
#include <memory>
#include <string>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace {
constexpr int kRTSize = 32;
}

class RenderTargetOwnMsaaTest final : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    int pass_ = 0;
    int fail_ = 0;

    void check(bool ok, const std::string& label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label.c_str());
        std::fflush(stdout);
        ok ? ++pass_ : ++fail_;
    }

    /// Draws the diagonal-edged triangle into `rt` and returns its centre row, read back from the
    /// target itself.
    std::vector<Color> RenderAndReadRow(GraphicsDevice& device, RenderTarget2D& rt)
    {
        device.setBlendStateProperty(BlendState::Opaque);
        device.SetRenderTarget(&rt);
        device.Clear(Color(0, 0, 0, 255));

        BasicEffect fx(device);
        fx.setWorldProperty(Matrix::getIdentityProperty());
        fx.setViewProperty(Matrix::getIdentityProperty());
        fx.setProjectionProperty(Matrix::getIdentityProperty());
        fx.VertexColorEnabled = true;
        fx.Apply();

        const Color white(255, 255, 255, 255);
        const VertexPositionColor tri[3] = {
            { Vector3(-1.0f,  1.0f, 0.0f), white },
            { Vector3( 1.0f,  1.0f, 0.0f), white },
            { Vector3(-1.0f, -1.0f, 0.0f), white },
        };
        device.DrawUserPrimitives(PrimitiveType::TriangleList, tri, 0, 1);
        device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

        std::vector<Color> row(kRTSize, Color(0, 0, 0, 0));
        const Rectangle centreRow(0, kRTSize / 2, kRTSize, 1);
        rt.GetData(0, &centreRow, row.data(), 0, kRTSize);
        return row;
    }

    /// True when no pixel of the row is partially covered -- the signature of a single-sampled
    /// rasterizer, which produces only fully-inside and fully-outside texels.
    static bool IsBinary(const std::vector<Color>& row)
    {
        for (const Color& c : row)
        {
            const int v = c.getRProperty();
            if (v > 40 && v < 215) return false;
        }
        return true;
    }

    static int BlendedCount(const std::vector<Color>& row)
    {
        int n = 0;
        for (const Color& c : row)
        {
            const int v = c.getRProperty();
            if (v > 40 && v < 215) ++n;
        }
        return n;
    }

protected:
    void Draw(const GameTime&) override
    {
        static bool done = false;
        if (done) return;
        done = true;

        auto& device = getGraphicsDeviceProperty();

        // The premise the whole row rests on: this device has no multisampling of its own.
        const int devicePP = device.getPresentationParametersProperty().getMultiSampleCountProperty();
        check(devicePP == 0,
              "0 device: the back buffer is single-sampled, so nothing here can be inherited "
              "(PresentationParameters.MultiSampleCount=" + std::to_string(devicePP) + ")");

        // A. The cheap half: a target that asked for 4x reports what it actually got, and on a
        //    device that supports 4x that is 4. VULKAN-347's rule -- report what you got.
        RenderTarget2D msaaRT(device, kRTSize, kRTSize, false, SurfaceFormat::Color,
                              DepthFormat::None, 4, RenderTargetUsage::DiscardContents);
        const int applied = msaaRT.getMultiSampleCountProperty();
        check(applied == 4,
              "A applied count: a RenderTarget2D asking for 4x on a single-sampled device reports "
              + std::to_string(applied) + " (want 4)");

        // B. The discriminating half. Reporting 4 while rasterizing one sample passes leg A and
        //    fails here: a single-sampled diagonal edge is binary, a resolved 4x one is not.
        const std::vector<Color> msaaRow = RenderAndReadRow(device, msaaRT);
        const int blended = BlendedCount(msaaRow);
        check(!IsBinary(msaaRow),
              "B real coverage: the resolved diagonal edge has " + std::to_string(blended) +
                  " partially-covered texels (want > 0; a single-sampled edge has none)");

        // C. The control, and the reason leg B means anything: the SAME geometry into a target
        //    that asked for nothing must still come out binary. If this row blends too, the test
        //    is measuring something other than multisampling -- a filtered readback, say.
        {
            RenderTarget2D plainRT(device, kRTSize, kRTSize, false, SurfaceFormat::Color,
                                   DepthFormat::None, 0, RenderTargetUsage::DiscardContents);
            const std::vector<Color> plainRow = RenderAndReadRow(device, plainRT);
            check(plainRT.getMultiSampleCountProperty() == 0 && IsBinary(plainRow),
                  "C control: a target that asked for no MSAA reports " +
                      std::to_string(plainRT.getMultiSampleCountProperty()) +
                      " and its edge is binary (" + std::to_string(BlendedCount(plainRow)) +
                      " partially-covered texels, want 0)");
        }

        // D. Two different counts INTERLEAVED in one frame, which is what the pipeline cache
        //    key has to survive. Before this row a pipeline's identity carried a single `msaa`
        //    bit and the count it implied was always the device's, so a multisampled target and a
        //    single-sampled one drawn in the same frame could be handed the same VkPipeline. That
        //    is not merely wrong output: rasterizationSamples would then disagree with the render
        //    pass the pipeline is bound in, which the validation layer reports and the
        //    VULKAN-408 gate turns into a failure -- so this leg has teeth on both counts.
        //
        //    The order matters: MSAA, then single-sampled, then MSAA again. A cache that ignores
        //    the count gives the third draw the second's pipeline.
        {
            RenderTarget2D firstMsaa(device, kRTSize, kRTSize, false, SurfaceFormat::Color,
                                     DepthFormat::None, 4, RenderTargetUsage::DiscardContents);
            RenderTarget2D plain(device, kRTSize, kRTSize, false, SurfaceFormat::Color,
                                 DepthFormat::None, 0, RenderTargetUsage::DiscardContents);
            RenderTarget2D secondMsaa(device, kRTSize, kRTSize, false, SurfaceFormat::Color,
                                      DepthFormat::None, 4, RenderTargetUsage::DiscardContents);
            const std::vector<Color> rowFirst  = RenderAndReadRow(device, firstMsaa);
            const std::vector<Color> rowPlain  = RenderAndReadRow(device, plain);
            const std::vector<Color> rowSecond = RenderAndReadRow(device, secondMsaa);
            check(!IsBinary(rowFirst) && IsBinary(rowPlain) && !IsBinary(rowSecond),
                  "D interleaved counts: 4x/none/4x in one frame give " +
                      std::to_string(BlendedCount(rowFirst)) + "/" +
                      std::to_string(BlendedCount(rowPlain)) + "/" +
                      std::to_string(BlendedCount(rowSecond)) +
                      " blended texels (want >0 / 0 / >0)");
        }

        // E. A count the device may not offer. XNA clamps preferredMultiSampleCount to what the
        //    adapter supports, and VULKAN-347's rule is to report what was actually applied -- so
        //    the assertion here is the INVARIANT, not a number: whatever count comes back, the
        //    edge is anti-aliased exactly when that count is greater than one. On llvmpipe, which
        //    offers 1x and 4x and nothing between, a 2x request lands on 0 and this leg proves
        //    the honest fallback rather than a silent 4x substitution.
        {
            RenderTarget2D twoX(device, kRTSize, kRTSize, false, SurfaceFormat::Color,
                                DepthFormat::None, 2, RenderTargetUsage::DiscardContents);
            const int applied2 = twoX.getMultiSampleCountProperty();
            const std::vector<Color> row2 = RenderAndReadRow(device, twoX);
            check((applied2 > 1) == !IsBinary(row2) && applied2 <= 2,
                  "E honest fallback: a 2x request applied as " + std::to_string(applied2) +
                      " and produced " + std::to_string(BlendedCount(row2)) +
                      " blended texels -- anti-aliased exactly when the applied count exceeds 1, "
                      "and never more than was asked for");
        }

        std::printf("=== %d/%d PASS ===\n", pass_, pass_ + fail_);
        std::fflush(stdout);
        Exit();
    }

public:
    RenderTargetOwnMsaaTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kRTSize);
        gdm_->setPreferredBackBufferHeightProperty(kRTSize);
        // Explicitly NOT PreferMultiSampling: the device must stay single-sampled.
    }

    [[nodiscard]] int getResult() const { return fail_ == 0 ? 0 : 1; }
};

int main()
{
    RenderTargetOwnMsaaTest game;
    game.Run();
    return game.getResult();
}
