// SPDX-License-Identifier: MS-PL
// Task 774 / Task 842: verify MRT with mixed formats is rejected or handled per XNA constraints.
//
// FNA's GraphicsDevice.SetRenderTargets performs no surface-format validation at all -- it forwards
// every RenderTargetBinding to FNA3D -- and neither does CNA's, so there is no "mixed-format MRT"
// rule at the bind site to match. What decides the formats is construction.
//
// This test's first version asserted that RenderTarget2D(..., SurfaceFormat::Bgr565, ...) THROWS,
// because at the time every non-Color format did, and concluded mixed-format MRT was unreachable.
// Both halves stopped being true: XNA's constructor treats the format as a preference and falls
// back to Color (plans/plan_software.md SOFTWARE-216), and this renderer now stores float targets.
// plans/plan_vulkan_parity.md VKPAR-0020 replaced the premise with the two things that are true:
//
//   * a Bgr565 request becomes a Color target and binds beside another Color target; and
//   * a genuinely mixed pair -- Color beside Single, two different VkFormats in one render pass --
//     binds, clears, and each target reads back its own representation of the clear colour.
//
// Exit code 0 = PASS, 1 = FAIL.

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsProfile.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetBinding.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"

#include <cmath>
#include <cstdio>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

class MrtMixedFormatsTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    int result_ = 0;
    int pass_   = 0;
    int fail_   = 0;

    void check(bool ok, const std::string& label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label.c_str());
        ok ? ++pass_ : ++fail_;
    }

    void Draw(const GameTime&) override {}

protected:
    void Initialize() override
    {
        Game::Initialize();
        GraphicsDevice& dev = getGraphicsDeviceProperty();

        // Baseline: two Color-format RTs construct fine and CAN be bound together.
        bool sameFormatOk = true;
        try
        {
            RenderTarget2D rtA(dev, 4, 4, false, SurfaceFormat::Color, DepthFormat::None);
            RenderTarget2D rtB(dev, 4, 4, false, SurfaceFormat::Color, DepthFormat::None);
            dev.SetRenderTargets({ RenderTargetBinding(&rtA), RenderTargetBinding(&rtB) });
            dev.SetRenderTargets({});
        }
        catch (const std::exception& e)
        {
            sameFormatOk = false;
            std::printf("       (same-format MRT bind threw unexpectedly: %s)\n", e.what());
        }
        check(sameFormatOk, "two SurfaceFormat::Color RenderTarget2D instances bind together via SetRenderTargets without throwing");

        // A Bgr565 request is a preference: this renderer stores no packed 16-bit target, so it
        // is built as Color and binds beside a Color target like any other pair.
        {
            bool fellBack = false;
            bool bound = false;
            std::string error;
            try
            {
                RenderTarget2D rtColor(dev, 4, 4, false, SurfaceFormat::Color, DepthFormat::None);
                RenderTarget2D rtPreferred(dev, 4, 4, false, SurfaceFormat::Bgr565, DepthFormat::None);
                fellBack = rtPreferred.getFormatProperty() == SurfaceFormat::Color;
                dev.SetRenderTargets({ RenderTargetBinding(&rtColor), RenderTargetBinding(&rtPreferred) });
                dev.SetRenderTargets({});
                bound = true;
            }
            catch (const std::exception& e) { error = e.what(); }
            check(fellBack && bound,
                  "a Bgr565 request is built as a Color target and binds beside one" +
                      (error.empty() ? std::string{} : " -- threw: " + error));
        }

        // A genuinely mixed pair. Clear writes the colour into every bound target, each in its own
        // representation: bytes in the Color target, R as a float in the Single one.
        if (!dev.SupportsSurfaceFormatAsRenderTargetEXT(SurfaceFormat::Single))
        {
            std::printf("[SKIP] this device has no Single render target, so no mixed pair exists\n");
        }
        else
        {
            const Color clearColor(128, 64, 32, 255);
            bool ok = false;
            std::string detail;
            try
            {
                RenderTarget2D rtColor(dev, 4, 4, false, SurfaceFormat::Color, DepthFormat::None);
                RenderTarget2D rtFloat(dev, 4, 4, false, SurfaceFormat::Single, DepthFormat::None);
                dev.SetRenderTargets({ RenderTargetBinding(&rtColor), RenderTargetBinding(&rtFloat) });
                dev.Clear(clearColor);
                dev.SetRenderTargets({});
                std::vector<Color> bytes(16);
                std::vector<float> floats(16);
                rtColor.GetData(bytes.data(), static_cast<int>(bytes.size()));
                rtFloat.GetData(floats.data(), static_cast<int>(floats.size()));
                const float wantR = 128.0f / 255.0f;
                const bool colorOk = bytes[5] == clearColor;
                const bool floatOk = std::fabs(floats[5] - wantR) < 1.0e-3f;
                ok = rtFloat.getFormatProperty() == SurfaceFormat::Single && colorOk && floatOk;
                detail = " (Color target " + std::to_string(bytes[5].getRProperty()) + "," +
                         std::to_string(bytes[5].getGProperty()) + "," +
                         std::to_string(bytes[5].getBProperty()) + "; Single target " +
                         std::to_string(floats[5]) + ", want " + std::to_string(wantR) + ")";
            }
            catch (const std::exception& e) { detail = " -- threw: " + std::string(e.what()); }
            check(ok, "Color and Single bind together and each reads back its own clear" + detail);
        }

        std::printf("=== %d/%d PASS ===\n", pass_, pass_ + fail_);
        result_ = (fail_ == 0) ? 0 : 1;
        Exit();
    }

public:
    MrtMixedFormatsTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        // plans/plan_vulkan_parity.md VKPAR-0018: this test uses a HiDef-only feature
        // (GetBackBufferData, volume textures, multiple render targets, separate alpha blending,
        // mipmapped non-power-of-two surfaces, occlusion queries or float targets), and CNA
        // enforces XNA's Reach profile, which GraphicsDeviceManager defaults to -- so under Reach
        // it failed before reaching its subject.
        gdm_->setGraphicsProfileProperty(Microsoft::Xna::Framework::Graphics::GraphicsProfile::HiDef);
        gdm_->setPreferredBackBufferWidthProperty(1);
        gdm_->setPreferredBackBufferHeightProperty(1);
    }

    int getResult() const { return result_; }
};

int main()
{
    MrtMixedFormatsTest game;
    game.Run();
    return game.getResult();
}
