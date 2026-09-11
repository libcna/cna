// SPDX-License-Identifier: MS-PL
//
// plan_vulkan.md VULKAN-190 -- `GetData` on a multisampled render target that is STILL BOUND.
//
// `REMED-GFX-164` is EasyGL's version of this: an active multisampled `RenderTarget2D` must expose
// what has been drawn into it without the caller unbinding first, and before that fix EasyGL read
// the single-sample resolve texture whose only writer lived in the unbind path -- so an active
// target returned `(0,0,0,0)` while its multisample attachment held the drawn pixels.
//
// This renderer has a large MSAA family, and **every one of its members reads after the unbind**,
// which is exactly the case that cannot fail this way. It is also the case a deferred renderer
// finds easy: on this one, "still bound" additionally means "not yet replayed", so a bound read has
// to flush the pending work for that target before it can resolve anything at all.
//
// EasyGL's own source is not portable here -- one of its legs asserts that OpenGL attachment
// diagnostics are available -- so this is the same contract written for this renderer.
//
//   A  Control: the target really is multisampled. `GetMultiSampleCountProperty` reports what was
//      applied; if that is 1 the rest of the test is about an ordinary target and proves nothing.
//   B  A read while the target is STILL BOUND returns what was drawn, not the clear colour and not
//      transparent black.
//   C  The same read after unbinding agrees with B. Two different code paths reach the same texels;
//      a renderer that resolved only on unbind would pass C and fail B, which is REMED-GFX-164's
//      exact shape.
//   D  No validation message -- a resolve issued inside a live render pass is the way this would
//      most plausibly go wrong on Vulkan.
//
// Exit code 0 = all PASS, 1 = any FAIL.

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
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

#include "CNA/Internal/Renderers/Vulkan/VulkanRenderer.hpp"

#include <cstdio>
#include <memory>
#include <string>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using CNA::Internal::Renderers::Vulkan::VulkanRenderer;

namespace
{
constexpr int kN = 8;
const Color kClear(0, 0, 255, 255);
const Color kDrawn(255, 0, 0, 255);
}  // namespace

class VulkanBoundMsaaReadbackTest final : public Game
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

    VulkanRenderer& Renderer()
    {
        return *dynamic_cast<VulkanRenderer*>(&getGraphicsDeviceProperty().GetRenderer());
    }

    static bool Is(const Color& got, const Color& want)
    {
        return got.getRProperty() == want.getRProperty() &&
               got.getGProperty() == want.getGProperty() &&
               got.getBProperty() == want.getBProperty();
    }

    static std::string Text(const Color& c)
    {
        return "(" + std::to_string(c.getRProperty()) + "," + std::to_string(c.getGProperty()) +
               "," + std::to_string(c.getBProperty()) + "," + std::to_string(c.getAProperty()) + ")";
    }

protected:
    void Draw(const GameTime&) override
    {
        static bool done = false;
        if (done) return;
        done = true;

        auto& dev = getGraphicsDeviceProperty();
        const std::size_t messagesBefore = Renderer().GetValidationMessagesEXT().size();

        RenderTarget2D rt(dev, kN, kN, false, SurfaceFormat::Color, DepthFormat::Depth24Stencil8,
                          4, RenderTargetUsage::PreserveContents);
        check(rt.getMultiSampleCountProperty() > 1,
              "A control: the target really is multisampled, applied=" +
                  std::to_string(rt.getMultiSampleCountProperty()) +
                  " (1 would make the rest of this test about an ordinary target)");

        dev.setBlendStateProperty(BlendState::Opaque);
        dev.SetRenderTarget(&rt);
        dev.Clear(kClear);
        {
            BasicEffect fx(dev);
            fx.VertexColorEnabled = true;
            fx.setLightingEnabledProperty(false);
            fx.setTextureEnabledProperty(false);
            fx.setFogEnabledProperty(false);
            fx.setWorldProperty(Matrix::getIdentityProperty());
            fx.setViewProperty(Matrix::getIdentityProperty());
            fx.setProjectionProperty(Matrix::getIdentityProperty());
            fx.Apply();
            // The left half only, so the clear colour is still visible on the right and a read
            // that returned the clear everywhere could not be mistaken for success.
            const VertexPositionColor tri[6] = {
                { Vector3(-1.f,  1.f, 0.f), kDrawn }, { Vector3(0.f,  1.f, 0.f), kDrawn },
                { Vector3(-1.f, -1.f, 0.f), kDrawn }, { Vector3(0.f,  1.f, 0.f), kDrawn },
                { Vector3( 0.f, -1.f, 0.f), kDrawn }, { Vector3(-1.f, -1.f, 0.f), kDrawn } };
            dev.DrawUserPrimitives(PrimitiveType::TriangleList, tri, 0, 2);
        }

        // B. STILL BOUND.
        std::vector<Color> bound(static_cast<std::size_t>(kN * kN), Color(0, 0, 0, 0));
        rt.GetData(bound.data(), 0, kN * kN);
        const Color boundLeft  = bound[kN * (kN / 2) + 1];
        const Color boundRight = bound[kN * (kN / 2) + kN - 2];
        check(Is(boundLeft, kDrawn) && Is(boundRight, kClear),
              "B a read while the target is STILL BOUND returns what was drawn: left=" +
                  Text(boundLeft) + " right=" + Text(boundRight) + " (want " + Text(kDrawn) +
                  " and " + Text(kClear) + "; (0,0,0,0) is the resolve-storage answer "
                  "REMED-GFX-164 describes)");

        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

        // C. The same read after the unbind.
        std::vector<Color> after(static_cast<std::size_t>(kN * kN), Color(0, 0, 0, 0));
        rt.GetData(after.data(), 0, kN * kN);
        const Color afterLeft  = after[kN * (kN / 2) + 1];
        const Color afterRight = after[kN * (kN / 2) + kN - 2];
        check(Is(afterLeft, boundLeft) && Is(afterRight, boundRight),
              "C the unbound read agrees with the bound one: left=" + Text(afterLeft) +
                  " right=" + Text(afterRight) + " (a renderer that resolved only on unbind would "
                  "pass this leg and fail B)");

        const std::size_t messagesAfter = Renderer().GetValidationMessagesEXT().size();
        check(!VulkanRenderer::IsValidationActiveEXT() || messagesAfter == messagesBefore,
              "D no validation message: " + std::to_string(messagesBefore) + " -> " +
                  std::to_string(messagesAfter) +
                  " (a resolve issued inside a live render pass lands here)");

        std::printf("=== %d/%d PASS ===\n", pass_, pass_ + fail_);
        std::fflush(stdout);
        Exit();
    }

public:
    VulkanBoundMsaaReadbackTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(64);
        gdm_->setPreferredBackBufferHeightProperty(64);
    }

    [[nodiscard]] int getResult() const { return fail_ == 0 ? 0 : 1; }
};

int main()
{
    VulkanBoundMsaaReadbackTest game;
    game.Run();
    return game.getResult();
}
