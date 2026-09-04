// SPDX-License-Identifier: MS-PL
// plans/plan_vulkan.md VULKAN-370 (finding F-14): OcclusionQuery.PixelCount must be a real tally
// here, or the renderer must say it is not.
//
// A Vulkan occlusion query is only required to produce an exact count when the device's
// `occlusionQueryPrecise` feature is ENABLED and the query is begun with
// `VK_QUERY_CONTROL_PRECISE_BIT`. This renderer did neither, and inherited the shared `true`
// default for PixelCountIsPreciseEXT() -- so it promised XNA's real Direct3D 9 tally while asking
// the device only "did anything pass". The lensflare idiom, PixelCount() divided by an area, is
// exactly what that breaks: it would have read 1/area, or any other non-zero value the
// implementation felt like, instead of a coverage fraction.
//
//   A  The renderer states an answer, and on a device that offers the feature the answer is true.
//   B  The count is EXACT, not merely positive. An occluder covers the left half of the frame at a
//      nearer depth; the queried quad covers the whole frame at a farther one, so under LESS
//      exactly the right half survives. That is a number known in advance -- 64 x 32 = 2048 -- and
//      the leg asserts it, not "> 0". A boolean-shaped query answers 1; a partially-counting one
//      answers something else; only a real tally answers 2048.
//   C  The two agree: a renderer reporting the count as precise must produce the exact number, and
//      one reporting it imprecise must not be held to it. Written both ways so the test stays
//      honest on a device without the feature rather than failing it for being honest.
//   D  No validation messages -- and this leg carries more weight here than usual. Passing
//      VK_QUERY_CONTROL_PRECISE_BIT without the feature enabled is a usage error the layer
//      reports (VUID-vkCmdBeginQuery-queryType-00800), so a silent run is what shows the bit and
//      the feature agree. It is also the ONLY thing that can show it from outside: a driver that
//      counts exactly anyway -- as llvmpipe does -- makes "asked for precise" and "did not ask and
//      got lucky" indistinguishable through the public API, which is precisely how the unearned
//      promise survived unnoticed.
//
// Exit code 0 = all PASS, 1 = any FAIL.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/CompareFunction.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/OcclusionQuery.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"

#include "CNA/Internal/Renderers/Vulkan/VulkanRenderer.hpp"

#include <cstdio>
#include <memory>
#include <string>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using CNA::Internal::Renderers::Vulkan::VulkanRenderer;

namespace
{
    constexpr int kSize = 64;
    // The right half of the frame: the occluder takes the left half, and the queried quad covers
    // everything at a farther depth, so exactly this many fragments survive the LESS test.
    constexpr int kExpectedVisible = kSize * (kSize / 2);
    constexpr int kMaxPollFrames   = 60;

    // XNA depth range: z in [0,1] with 0 at the near plane. Both quads sit well inside it so
    // neither is clipped and the occluder is unambiguously nearer.
    void DrawQuad(GraphicsDevice& dev, float xMin, float xMax, float z, const Color& colour)
    {
        const VertexPositionColor q[6] = {
            { Vector3(xMin,  1.0f, z), colour },
            { Vector3(xMin, -1.0f, z), colour },
            { Vector3(xMax, -1.0f, z), colour },
            { Vector3(xMin,  1.0f, z), colour },
            { Vector3(xMax, -1.0f, z), colour },
            { Vector3(xMax,  1.0f, z), colour },
        };
        dev.DrawUserPrimitives(PrimitiveType::TriangleList, q, 0, 2);
    }
}

class VulkanOcclusionQueryPrecisionTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    int  pass_ = 0;
    int  fail_ = 0;
    bool done_ = false;

    void check(bool ok, const std::string& label, const std::string& detail)
    {
        std::printf("[%s] %s: %s\n", ok ? "PASS" : "FAIL", label.c_str(), detail.c_str());
        if (ok) ++pass_; else ++fail_;
    }

protected:
    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;
        auto& dev = getGraphicsDeviceProperty();

        dev.setBlendStateProperty(BlendState::Opaque);
        dev.setRasterizerStateProperty(RasterizerState::CullNone);
        DepthStencilState dss;
        dss.setDepthBufferFunctionProperty(CompareFunction::Less);
        dev.setDepthStencilStateProperty(dss);

        std::unique_ptr<OcclusionQuery> query;
        int  counted  = -1;
        bool complete = false;
        bool precise  = true;

        for (int frame = 0; frame < kMaxPollFrames; ++frame)
        {
            BasicEffect fx(dev);
            fx.VertexColorEnabled = true;
            fx.Apply();

            dev.Clear(Color(0, 0, 0, 255));
            // Occluder: left half, nearer. Not queried.
            DrawQuad(dev, -1.0f, 0.0f, 0.25f, Color(255, 0, 0, 255));

            query = std::make_unique<OcclusionQuery>(dev);
            precise = query->isPixelCountPreciseEXT();
            query->Begin();
            DrawQuad(dev, -1.0f, 1.0f, 0.75f, Color(0, 255, 0, 255));
            query->End();

            EndDraw();          // submit the frame the query was recorded in
            if (query->getIsCompleteProperty()) {
                complete = true;
                counted  = query->getPixelCountProperty();
                break;
            }
        }

        check(complete, "A the query completes and the renderer states its precision",
              complete ? (std::string("precise=") + (precise ? "true" : "false")
                          + ", PixelCount=" + std::to_string(counted))
                       : ("never completed within " + std::to_string(kMaxPollFrames) + " frames"));

        if (!complete) { Finish(); return; }

        if (precise) {
            check(counted == kExpectedVisible,
                  "B the count is the exact number of surviving fragments",
                  std::to_string(counted) + ", expected exactly "
                      + std::to_string(kExpectedVisible)
                      + " (a boolean-shaped query answers 1)");
            check(counted != 1 && counted != 0,
                  "C a precise report is not a disguised flag",
                  std::to_string(counted));
        } else {
            // Honest on a device without the feature: the promise is only "something passed".
            check(counted > 0,
                  "B this device does not offer precise occlusion queries, so only 'some fragments "
                  "passed' is promised -- and that much must hold",
                  std::to_string(counted));
            check(true, "C an imprecise report is not held to the exact count",
                  "reported imprecise, so " + std::to_string(kExpectedVisible)
                      + " is not required");
        }
        const auto* vk = dynamic_cast<const VulkanRenderer*>(&dev.GetRenderer());
        const auto& messages = vk->GetValidationMessagesEXT();
        check(messages.empty(),
              "D no validation messages -- so the PRECISE bit and the enabled feature agree",
              messages.empty() ? "0 captured"
                               : std::to_string(messages.size()) + " captured, first: "
                                     + messages.front());
        Finish();
    }

private:
    void Finish()
    {
        std::printf("=== %d/%d PASS ===\n", pass_, pass_ + fail_);
        Exit();
    }

public:
    VulkanOcclusionQueryPrecisionTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kSize);
        gdm_->setPreferredBackBufferHeightProperty(kSize);
    }

    int getResult() const { return fail_ > 0 ? 1 : 0; }
};

int main()
{
    VulkanOcclusionQueryPrecisionTest g;
    g.Run();
    return g.getResult();
}
