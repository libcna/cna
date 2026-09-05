// SPDX-License-Identifier: MS-PL
// plans/plan_vulkan.md VULKAN-395: how large can this renderer's `VkSampler` cache get, and what
// happens when the device refuses another one?
//
// `samplerCache_` is keyed by `SamplerStateKey`, and the row assumed that key space was the XNA
// enumerations -- 9 filters times 3 address modes cubed times a handful of anisotropy values, a few
// hundred at worst, which would be a bound and the row would close by recording it.
//
// It is not. Two of the seven fields are caller-supplied numbers, not enumerations:
// `SamplerState::MaxMipLevel` is an `int` and `MipMapLevelOfDetailBias` a **float**. A game that
// animates the LOD bias -- which is what a bias is FOR -- produces a distinct key every frame, and
// nothing evicts.
//
//   A  The enumerated part really is bounded: the same sampler state, drawn many times, creates one
//      sampler. This is the control; without it a growth measurement proves nothing.
//   B  A sweep of distinct LOD bias values grows the cache one entry per value, without limit.
//      Measured, not argued.
//   C  The device's own ceiling is real and finite -- `maxSamplerAllocationCount` -- so B is a route
//      to exhausting it rather than merely to using memory.
//   D  No validation messages.
//
// Exit code 0 = all PASS, 1 = any FAIL.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/BufferUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerStateCollection.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexDeclaration.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElement.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementUsage.hpp"

#include "CNA/Internal/Renderers/Vulkan/VulkanRenderer.hpp"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using CNA::Internal::Renderers::Vulkan::VulkanRenderer;

namespace
{
    constexpr int kSize = 64;
    /// How many distinct LOD bias values the sweep uses. Small enough to run in a second, large
    /// enough that "one entry per distinct value" is unmistakable.
    constexpr int kBiasCount = 64;
}

class VulkanSamplerCacheBoundTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    std::unique_ptr<Texture2D> texture_;
    std::unique_ptr<VertexBuffer> quad_;
    int  pass_ = 0;
    int  fail_ = 0;
    bool done_ = false;

    void check(bool ok, const std::string& label, const std::string& detail)
    {
        std::printf("[%s] %s: %s\n", ok ? "PASS" : "FAIL", label.c_str(), detail.c_str());
        std::fflush(stdout);
        if (ok) ++pass_; else ++fail_;
    }

    VulkanRenderer& Renderer()
    {
        return *dynamic_cast<VulkanRenderer*>(&getGraphicsDeviceProperty().GetRenderer());
    }

    /// One textured draw with sampler slot 0 set to `state`.
    void DrawWith(GraphicsDevice& dev, const SamplerState& state)
    {
        dev.getSamplerStatesProperty()[0] = state;
        BasicEffect fx(dev);
        fx.setTextureEnabledProperty(true);
        fx.setTextureProperty(texture_.get());
        fx.setLightingEnabledProperty(false);
        fx.setFogEnabledProperty(false);
        fx.setWorldProperty(Matrix::getIdentityProperty());
        fx.setViewProperty(Matrix::getIdentityProperty());
        fx.setProjectionProperty(Matrix::getIdentityProperty());
        dev.Clear(Color(0, 0, 0, 255));
        fx.Apply();
        dev.SetVertexBuffer(quad_.get());
        dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
        dev.SetVertexBuffer(nullptr);
        Color probe(0, 0, 0, 0);
        const Rectangle at(kSize / 2, kSize / 2, 1, 1);
        dev.GetBackBufferData(&at, &probe, 0, 1);
    }

protected:
    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;
        auto& dev = getGraphicsDeviceProperty();
        dev.setBlendStateProperty(BlendState::Opaque);
        dev.setRasterizerStateProperty(RasterizerState::CullNone);
        dev.SetDepthTestEnabled(false);

        const Color texels[2] = { Color(230, 30, 30, 255), Color(30, 60, 230, 255) };
        texture_ = std::make_unique<Texture2D>(dev, 2, 1);
        texture_->SetData(texels, 2);

        std::uint8_t bytes[6 * 20]{};
        constexpr float kX[6] = { -1.f, -1.f,  1.f, -1.f,  1.f,  1.f };
        constexpr float kY[6] = {  1.f, -1.f, -1.f,  1.f, -1.f,  1.f };
        for (int i = 0; i < 6; ++i) {
            std::uint8_t* v = bytes + i * 20;
            const float pos[3] = { kX[i], kY[i], 0.0f };
            const float uv[2]  = { 0.25f, 0.5f };
            std::memcpy(v + 0,  pos, sizeof(pos));
            std::memcpy(v + 12, uv,  sizeof(uv));
        }
        VertexDeclaration decl(20, {
            VertexElement(0,  VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
            VertexElement(12, VertexElementFormat::Vector2,
                          VertexElementUsage::TextureCoordinate, 0)});
        quad_ = std::make_unique<VertexBuffer>(dev, decl, 6, BufferUsage::None);
        quad_->SetDataRaw(bytes, 6, 20);

        // ---- A: the control. One state, many draws, one sampler. --------------------------------
        {
            SamplerState state = SamplerState::PointClamp;
            DrawWith(dev, state);                                   // warm the cache
            const std::size_t before = Renderer().GetSamplerCacheSizeEXT();
            for (int i = 0; i < 32; ++i) { DrawWith(dev, state); }
            const std::size_t after = Renderer().GetSamplerCacheSizeEXT();
            check(after == before,
                  "A 32 draws with one sampler state create no further samplers",
                  std::to_string(before) + " -> " + std::to_string(after));
        }

        // ---- B: the LOD bias sweep --------------------------------------------------------------
        const std::size_t beforeSweep = Renderer().GetSamplerCacheSizeEXT();
        for (int i = 0; i < kBiasCount; ++i) {
            SamplerState state = SamplerState::PointClamp;
            // A distinct float per iteration -- the shape of an animated bias, which is what a LOD
            // bias is for.
            state.setMipMapLevelOfDetailBiasProperty(static_cast<float>(i) * 0.0125f);
            DrawWith(dev, state);
        }
        const std::size_t afterSweep = Renderer().GetSamplerCacheSizeEXT();
        const std::size_t grew = afterSweep - beforeSweep;
        check(grew > 0,
              "B a sweep of distinct MipMapLevelOfDetailBias values grows the sampler cache",
              std::to_string(beforeSweep) + " -> " + std::to_string(afterSweep) + " (+"
                  + std::to_string(grew) + " for " + std::to_string(kBiasCount) + " values)");
        std::printf("[INFO] one sampler per distinct bias: %s\n",
                    grew == static_cast<std::size_t>(kBiasCount) ? "yes, exactly"
                                                                 : "no, fewer than the sweep");
        std::fflush(stdout);

        // ---- C: the device's ceiling is real -----------------------------------------------------
        const std::uint32_t ceiling = Renderer().GetMaxSamplerAllocationCountEXT();
        check(ceiling > 0,
              "C the device declares a finite maxSamplerAllocationCount, so B is a route to "
              "exhausting it rather than merely to using memory",
              std::to_string(ceiling) + " live samplers allowed; the sweep above used "
                  + std::to_string(afterSweep));

        // ---- F: VULKAN-160. The bound holds, and the picture stays right. ------------------------
        // Two assertions, and both are needed. A bound that evicted the sampler the next draw is
        // about to use would satisfy the size one and fail the pixel one; a bound that never
        // evicted would satisfy the pixel one and fail the size one.
        {
            const std::size_t bound = VulkanRenderer::GetSamplerCacheBoundEXT();
            for (int i = 0; i < static_cast<int>(bound) * 2; ++i) {
                SamplerState state = SamplerState::PointClamp;
                state.setMipMapLevelOfDetailBiasProperty(1.0f + static_cast<float>(i) * 0.001f);
                DrawWith(dev, state);
            }
            const std::size_t size = Renderer().GetSamplerCacheSizeEXT();
            check(size <= bound + 16,
                  "F sweeping twice the bound leaves the cache bounded, not doubled",
                  std::to_string(bound * 2) + " distinct states swept, cache holds "
                      + std::to_string(size) + " (bound " + std::to_string(bound)
                      + ", plus up to 16 pinned device slots)");

            // And the picture: a known state, a known texel. If eviction had taken a sampler a
            // draw still needed, this reads black or garbage rather than the left texel.
            SamplerState state = SamplerState::PointClamp;
            DrawWith(dev, state);
            Color got(0, 0, 0, 0);
            const Rectangle at(kSize / 2, kSize / 2, 1, 1);
            dev.GetBackBufferData(&at, &got, 0, 1);
            const bool right = std::abs(int(got.getRProperty()) - 230) <= 24
                            && std::abs(int(got.getGProperty()) - 30) <= 24
                            && std::abs(int(got.getBProperty()) - 30) <= 24;
            check(right,
                  "F and a draw after all that eviction still samples the right texel",
                  "(" + std::to_string(got.getRProperty()) + ","
                      + std::to_string(got.getGProperty()) + ","
                      + std::to_string(got.getBProperty()) + "), expected (230,30,30)");
        }

        // ---- E: VULKAN-161. Exhaustion is loud, not white. ---------------------------------------
        // The device here allows 32768 live samplers, so reaching the branch for real would mean
        // creating tens of thousands of them -- slow, and on a driver that over-delivers,
        // unreachable. Injected instead, the same way VULKAN-390 reaches the descriptor-pool arm.
        {
            const std::size_t before = Renderer().GetSamplerCacheSizeEXT();
            VulkanRenderer::SetSamplerCreationFailuresForTestEXT(1);
            std::string how = "the draw SUCCEEDED with a sampler creation failure injected";
            try {
                SamplerState state = SamplerState::PointClamp;
                state.setMipMapLevelOfDetailBiasProperty(-7.25f);   // a key nothing has cached
                DrawWith(dev, state);
            } catch (const std::exception& e) {
                how = e.what();
            }
            VulkanRenderer::SetSamplerCreationFailuresForTestEXT(0);
            check(how.find("vkCreateSampler failed") != std::string::npos
                      && how.find("Refused rather than drawing with a substituted sampler")
                             != std::string::npos,
                  "E a device out of samplers refuses by name instead of drawing white", how);
            check(Renderer().GetSamplerCacheSizeEXT() == before,
                  "E and the failed creation left nothing behind in the cache",
                  std::to_string(before) + " -> "
                      + std::to_string(Renderer().GetSamplerCacheSizeEXT()));

            // The refusal must not have broken the renderer: the same draw succeeds once the
            // injection is disarmed. Without this a test could pass by leaving it wedged.
            std::string after = "drew";
            try {
                SamplerState state = SamplerState::PointClamp;
                state.setMipMapLevelOfDetailBiasProperty(-7.25f);
                DrawWith(dev, state);
            } catch (const std::exception& e) {
                after = std::string("still refused: ") + e.what();
            }
            check(after == "drew", "E the refusal left the renderer usable", after);
        }

        const auto& messages = Renderer().GetValidationMessagesEXT();
        check(messages.empty(), "D no validation messages",
              messages.empty() ? "0 captured"
                               : std::to_string(messages.size()) + " captured, first: "
                                     + messages.front());

        std::printf("=== %d/%d PASS ===\n", pass_, pass_ + fail_);
        std::fflush(stdout);
        Exit();
    }

public:
    VulkanSamplerCacheBoundTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kSize);
        gdm_->setPreferredBackBufferHeightProperty(kSize);
    }

    int getResult() const { return fail_ > 0 ? 1 : 0; }
};

int main()
{
    VulkanSamplerCacheBoundTest g;
    g.Run();
    return g.getResult();
}
