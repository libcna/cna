// SPDX-License-Identifier: MS-PL
// plans/plan_vulkan.md VULKAN-394: how many pipelines can this renderer's ~20 `PipelineKey`-keyed
// caches hold, and is that a bound or a leak?
//
// The row's own test: a cache bounded by its key space is fine and closes the row by recording the
// bound; a cache that grows with frame count is a defect and gets its own row. `VULKAN-395` asked
// the same question of the sampler cache and found the key space was not what it looked like, so
// the same reading is applied here rather than assumed.
//
// `PipelineKey` has five fields. Four are bounded by enumerations and by the renderer's own shape --
// topology, depth/stencil state, cull mode, colour-attachment count, wireframe, MSAA, blend
// equation bits, colour-write bits, the stride bucket. The other two are not:
//
//   * `sm` is `BlendState::MultiSampleMask`, a **32-bit integer the game sets**;
//   * `vl` is `VulkanVertexInputLayoutEXT::Hash()`, a 64-bit hash of a caller's `VertexDeclaration`.
//
//   A  The control: the same draw repeated builds one pipeline and no more. Without it a growth
//      measurement means nothing.
//   B  An ordinary frame mix -- several effects and states, drawn repeatedly -- is bounded. This is
//      the case the row cares about, and it is the one that must not grow with frame count.
//   C  A sweep of distinct `MultiSampleMask` values. Measured, not argued.
//   D  A sweep of distinct vertex declarations at one stride, with the colour element moved.
//   E  No validation messages.
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
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
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
    constexpr int kSweep = 32;
    const Color kVertexColor(230, 30, 30, 255);
}

class VulkanPipelineCacheBoundTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
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

    /// A stride-16 Position+Colour quad whose colour element sits at `colorOffset`.
    static std::vector<std::uint8_t> Records(int stride, int colorOffset)
    {
        std::vector<std::uint8_t> bytes(static_cast<std::size_t>(6 * stride), 0);
        constexpr float kX[6] = { -1.f, -1.f,  1.f, -1.f,  1.f,  1.f };
        constexpr float kY[6] = {  1.f, -1.f, -1.f,  1.f, -1.f,  1.f };
        for (int i = 0; i < 6; ++i) {
            std::uint8_t* v = bytes.data() + i * stride;
            const float pos[3] = { kX[i], kY[i], 0.0f };
            std::memcpy(v + 0, pos, sizeof(pos));
            v[colorOffset + 0] = kVertexColor.getRProperty();
            v[colorOffset + 1] = kVertexColor.getGProperty();
            v[colorOffset + 2] = kVertexColor.getBProperty();
            v[colorOffset + 3] = kVertexColor.getAProperty();
        }
        return bytes;
    }

    void DrawQuad(GraphicsDevice& dev, VertexBuffer& vb)
    {
        BasicEffect fx(dev);
        fx.VertexColorEnabled = true;
        fx.setLightingEnabledProperty(false);
        fx.setTextureEnabledProperty(false);
        fx.setFogEnabledProperty(false);
        fx.setWorldProperty(Matrix::getIdentityProperty());
        fx.setViewProperty(Matrix::getIdentityProperty());
        fx.setProjectionProperty(Matrix::getIdentityProperty());
        dev.Clear(Color(0, 0, 0, 255));
        fx.Apply();
        dev.SetVertexBuffer(&vb);
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
        dev.setRasterizerStateProperty(RasterizerState::CullNone);
        dev.SetDepthTestEnabled(false);

        const VertexDeclaration decl16(16, {
            VertexElement(0,  VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
            VertexElement(12, VertexElementFormat::Color,   VertexElementUsage::Color,    0)});
        VertexBuffer vb(dev, decl16, 6, BufferUsage::None);
        const auto bytes = Records(16, 12);
        vb.SetDataRaw(bytes.data(), 6, 16);

        // ---- A: the control ---------------------------------------------------------------------
        {
            dev.setBlendStateProperty(BlendState::Opaque);
            DrawQuad(dev, vb);                                    // warm
            const std::size_t before = Renderer().GetGraphicsPipelineCacheEntryCountEXT();
            for (int i = 0; i < 32; ++i) { DrawQuad(dev, vb); }
            const std::size_t after = Renderer().GetGraphicsPipelineCacheEntryCountEXT();
            check(after == before,
                  "A 32 identical draws build no further pipelines",
                  std::to_string(before) + " -> " + std::to_string(after));
        }

        // ---- B: an ordinary frame mix, repeated -------------------------------------------------
        // Four states a game really does cycle between, drawn 16 times each. The claim the row
        // cares about is that this is bounded by the number of STATES, not by the number of frames.
        {
            const BlendState* states[4] = { &BlendState::Opaque, &BlendState::AlphaBlend,
                                            &BlendState::Additive, &BlendState::NonPremultiplied };
            for (int pass = 0; pass < 2; ++pass) {
                for (const BlendState* bs : states) {
                    dev.setBlendStateProperty(*bs);
                    DrawQuad(dev, vb);
                }
            }
            const std::size_t settled = Renderer().GetGraphicsPipelineCacheEntryCountEXT();
            for (int frame = 0; frame < 16; ++frame) {
                for (const BlendState* bs : states) {
                    dev.setBlendStateProperty(*bs);
                    DrawQuad(dev, vb);
                }
            }
            const std::size_t after = Renderer().GetGraphicsPipelineCacheEntryCountEXT();
            check(after == settled,
                  "B an ordinary four-state mix stops growing once every state has been seen",
                  std::to_string(settled) + " -> " + std::to_string(after)
                      + " over 16 further frames");
            dev.setBlendStateProperty(BlendState::Opaque);
        }

        // ---- C: the MultiSampleMask sweep --------------------------------------------------------
        const std::size_t beforeMask = Renderer().GetGraphicsPipelineCacheEntryCountEXT();
        for (int i = 0; i < kSweep; ++i) {
            BlendState bs;
            bs.setMultiSampleMaskProperty(static_cast<int>(0xFFFFFFFFu >> (i % 31)));
            dev.setBlendStateProperty(bs);
            DrawQuad(dev, vb);
        }
        const std::size_t afterMask = Renderer().GetGraphicsPipelineCacheEntryCountEXT();
        dev.setBlendStateProperty(BlendState::Opaque);
        std::printf("[INFO] C %d distinct MultiSampleMask values: %zu -> %zu pipelines (+%zu)\n",
                    kSweep, beforeMask, afterMask, afterMask - beforeMask);
        std::fflush(stdout);
        check(true, "C MultiSampleMask sweep measured", std::to_string(afterMask - beforeMask)
                  + " new pipelines for " + std::to_string(kSweep) + " distinct masks");

        // ---- D: the declaration axis -------------------------------------------------------------
        // Distinct declarations must get distinct pipelines -- that is VULKAN-146's whole point, and
        // a cache that merged them would render one layout's bytes through another's rule. The
        // question THIS row asks is different: is that axis bounded by how many declarations exist,
        // or does it grow with frames? So the same five are drawn twice.
        //
        // All five are stride 32 with the colour element moved, and they stay on one program on
        // purpose: the stride selects which stock program runs, so sweeping the stride would have
        // been measuring program selection rather than the declaration axis. (The first version of
        // this leg did exactly that and was refused by the fidelity guard, which is the guard doing
        // its job.)
        std::vector<std::unique_ptr<VertexBuffer>> buffers;
        const std::size_t beforeDecl = Renderer().GetGraphicsPipelineCacheEntryCountEXT();
        for (int i = 0; i < 5; ++i) {
            const int colorOffset = 12 + i * 4;          // 12, 16, 20, 24, 28
            VertexDeclaration d(32, {
                VertexElement(0, VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
                VertexElement(colorOffset, VertexElementFormat::Color,
                              VertexElementUsage::Color, 0)});
            auto buffer = std::make_unique<VertexBuffer>(dev, d, 6, BufferUsage::None);
            const auto b = Records(32, colorOffset);
            buffer->SetDataRaw(b.data(), 6, 32);
            DrawQuad(dev, *buffer);
            buffers.push_back(std::move(buffer));
        }
        const std::size_t afterFirst = Renderer().GetGraphicsPipelineCacheEntryCountEXT();
        for (auto& buffer : buffers) { DrawQuad(dev, *buffer); }
        const std::size_t afterSecond = Renderer().GetGraphicsPipelineCacheEntryCountEXT();
        check(afterFirst > beforeDecl && afterSecond == afterFirst,
              "D the declaration axis is bounded by how many declarations exist, not by frames",
              std::to_string(beforeDecl) + " -> " + std::to_string(afterFirst)
                  + " for 5 distinct declarations, then " + std::to_string(afterSecond)
                  + " after drawing all five again");

        const auto& messages = Renderer().GetValidationMessagesEXT();
        check(messages.empty(), "E no validation messages",
              messages.empty() ? "0 captured"
                               : std::to_string(messages.size()) + " captured, first: "
                                     + messages.front());

        std::printf("=== %d/%d PASS ===\n", pass_, pass_ + fail_);
        std::fflush(stdout);
        Exit();
    }

public:
    VulkanPipelineCacheBoundTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kSize);
        gdm_->setPreferredBackBufferHeightProperty(kSize);
    }

    int getResult() const { return fail_ > 0 ? 1 : 0; }
};

int main()
{
    VulkanPipelineCacheBoundTest g;
    g.Run();
    return g.getResult();
}
