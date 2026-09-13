// SPDX-License-Identifier: MS-PL
// plans/plan_vulkan.md VULKAN-392 (finding F-13): disposing a vertex or index buffer must not
// stall the whole device.
//
// The defect
// ----------
// `VulkanVertexBufferRenderer::ReleaseVulkanResources` and its index-buffer twin each opened with
// `vkDeviceWaitIdle`. `DrawUserPrimitives` / `DrawUserIndexedPrimitives` allocate a throwaway
// `VertexBuffer` per call (`GraphicsDevice.cpp:1753`+), so on Vulkan every such draw ended in a
// full-device stall -- the pathology §13 names, on a route a game may use every frame.
//
// The fix retires the handles into the same fence-gated queue textures and render targets already
// use. That is safer than the stall it replaces, not merely faster: the deferred replay binds only
// renderer-owned per-frame buffers and never a user buffer handle, because a queued draw carries
// COPIED vertex and index bytes; the one command that names the buffer is the staging copy inside
// SetData, which `EndOneTimeCommands` has already waited on.
//
// Why the counter, and why it is trustworthy
// ------------------------------------------
// "We removed the stall" is a claim a test must be able to check. Every `vkDeviceWaitIdle` in this
// renderer now goes through one funnel that increments `GetDeviceWaitIdleCountEXT()`, so the count
// is complete by construction and a stall reintroduced ANYWHERE -- not only in the two functions
// this row touched -- fails leg B.
//
// Four legs:
//   A  the scene renders correctly. Without it, "zero stalls" is satisfied by drawing nothing.
//   B  the draw path performs no device stall at all across many buffer create/destroy cycles.
//   C  the buffers really were created and destroyed, so leg B is not measuring an empty loop.
//   D  no validation messages -- retiring a live handle would be a use-after-free the layer sees.
//
// Exit code 0 = PASS, 1 = FAIL.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"
#include "CNA/Internal/Renderers/Vulkan/VulkanRenderer.hpp"

#include <cstdio>
#include <memory>
#include <string>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using CNA::Internal::Renderers::Vulkan::VulkanRenderer;

namespace {

constexpr int kDrawsPerFrame = 40;

class BufferDisposeStallTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    int  frame_    = 0;
    int  failures_ = 0;
    std::uint64_t stallsBeforeDraws_ = 0;

    void check(bool ok, const std::string& what)
    {
        std::printf("%s %s\n", ok ? "[ok]  " : "[FAIL]", what.c_str());
        if (!ok) ++failures_;
    }

protected:
    void Initialize() override
    {
        Game::Initialize();
    }

    void Draw(const GameTime&) override
    {
        auto& device = getGraphicsDeviceProperty();
        auto* vk = dynamic_cast<VulkanRenderer*>(&device.GetRenderer());
        if (vk == nullptr)
        {
            std::printf("[FAIL] renderer is not the Vulkan renderer\n");
            ++failures_;
            Exit();
            return;
        }

        if (frame_ == 0)
        {
            // Warm-up frame: pipelines and per-frame buffers are created here, and creating them
            // is allowed to stall. Counting from AFTER it is what makes leg B about the draw path
            // rather than about one-time setup.
            device.Clear(Color(0, 255, 0, 255));
            ++frame_;
            return;
        }

        if (frame_ == 1)
            stallsBeforeDraws_ = vk->GetDeviceWaitIdleCountEXT();

        device.Clear(Color(0, 255, 0, 255));
        device.SetDepthTestEnabled(false);
        device.setBlendStateProperty(BlendState::Opaque);
        // The quad below is CCW/back-facing under CNA's real default RasterizerState.
        device.setRasterizerStateProperty(RasterizerState::CullNone);

        BasicEffect fx(device);
        fx.VertexColorEnabled = true;
        fx.Apply();

        // Each DrawUserPrimitives call creates and destroys a throwaway VertexBuffer, so this
        // frame performs kDrawsPerFrame create/destroy cycles.
        const VertexPositionColor quad[6] = {
            { Vector3(-1.0f, -1.0f, 0.0f), Color(255, 0, 0, 255) },
            { Vector3( 1.0f, -1.0f, 0.0f), Color(255, 0, 0, 255) },
            { Vector3(-1.0f,  1.0f, 0.0f), Color(255, 0, 0, 255) },
            { Vector3( 1.0f, -1.0f, 0.0f), Color(255, 0, 0, 255) },
            { Vector3( 1.0f,  1.0f, 0.0f), Color(255, 0, 0, 255) },
            { Vector3(-1.0f,  1.0f, 0.0f), Color(255, 0, 0, 255) },
        };
        for (int i = 0; i < kDrawsPerFrame; ++i)
        {
            device.DrawUserPrimitives(PrimitiveType::TriangleList, quad, 0, 2);
        }

        if (frame_ >= 3)
        {
            // ---- leg A: the control ------------------------------------------------
            const auto& vp = device.getViewportProperty();
            const Rectangle centre(vp.getWidthProperty() / 2, vp.getHeightProperty() / 2, 1, 1);
            Color px(0, 0, 0, 0);
            device.GetBackBufferData(&centre, &px, 0, 1);
            check(px.getRProperty() >= 200 && px.getGProperty() <= 60,
                  "A the scene renders: centre=(" + std::to_string(px.getRProperty()) + "," +
                      std::to_string(px.getGProperty()) + "," + std::to_string(px.getBProperty()) +
                      ") -- without this, zero stalls is satisfied by drawing nothing");

            // ---- leg B: no device stall on the draw path ---------------------------
            const std::uint64_t stalls = vk->GetDeviceWaitIdleCountEXT() - stallsBeforeDraws_;
            check(stalls == 0,
                  "B " + std::to_string(kDrawsPerFrame * 3) + " buffer create/destroy cycles cost " +
                      std::to_string(stalls) + " full-device stalls");

            // ---- leg C: the cycles really happened ---------------------------------
            // Measured, not inferred from leg A's pixel plus the documented behaviour of
            // DrawUserPrimitives: every throwaway buffer that was destroyed passed through the
            // retirement queue and was counted there.
            const std::uint64_t retired = vk->GetRetiredBufferCountEXT();
            check(retired >= static_cast<std::uint64_t>(kDrawsPerFrame * 3),
                  "C the buffer create/destroy cycles leg B priced really happened: " +
                      std::to_string(retired) + " buffers retired");

            // ---- leg D: the layer saw no use-after-free ----------------------------
            const auto& messages = vk->GetValidationMessagesEXT();
            check(messages.empty(),
                  "D no validation messages (" + std::to_string(messages.size()) + " captured)");

            Exit();
            return;
        }

        ++frame_;
    }

public:
    BufferDisposeStallTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
    }

    int getResult() const { return failures_ == 0 ? 0 : 1; }
};

} // namespace

int main()
{
    BufferDisposeStallTest game;
    game.Run();
    return game.getResult();
}
