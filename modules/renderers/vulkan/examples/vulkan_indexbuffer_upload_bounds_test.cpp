// SPDX-License-Identifier: MS-PL
// plans/plan_vulkan.md VULKAN-131 (finding F-02): an IndexBuffer upload must not write past its
// own mapping.
//
// VulkanIndexBufferRenderer::SetData16/SetData32 memcpy'd index_count * elementSize bytes into a
// mapping of capacity * elementSize with no comparison between the two. Unlike the vertex-buffer
// twin (VULKAN-130), the allocation here is exact from the start -- the element width is fixed at
// construction -- so the only ways past its end are more indices than the buffer's capacity, or a
// width that is not this buffer's. Both are caller errors that CNA's shared layer already refuses,
// which is why the renderer-level bound is defence in depth rather than a reachable crash: it
// exists so the invariant belongs to the object that owns the allocation.
//
// What this test proves, and in which layer:
//
//   A  The allocation is exactly capacity * element width, for both widths. Read from the
//      renderer's own GetLiveIndexBufferBytesEXT() as a before/after delta.
//   B  A full-capacity upload of each width round-trips every index.
//   C  An over-capacity upload is refused by name (System::ArgumentOutOfRangeException) and the
//      buffer still holds -- and still returns -- what was in it before the refused call.
//   D  A width that is not this buffer's is refused by name (System::ArgumentException).
//   E  A full-capacity 32-bit index buffer still DRAWS: the boundary case where the draw route's
//      own copy reads the very last byte of the mapping.
//   F  No validation messages.
//
// Exit code 0 = all PASS, 1 = any FAIL.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/BufferUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexElementSize.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexDeclaration.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElement.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementUsage.hpp"

#include "System/ArgumentException.hpp"
#include "System/ArgumentOutOfRangeException.hpp"

#include "CNA/Internal/Renderers/Vulkan/VulkanRenderer.hpp"

#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using CNA::Internal::Renderers::Vulkan::VulkanRenderer;

namespace
{
    // Large enough that a mis-sized allocation is pages out, not bytes.
    constexpr int kIndexCount = 3072;
    constexpr int kSize       = 64;

    struct GpuVPC { float x, y, z; std::uint8_t r, g, b, a; };
    static_assert(sizeof(GpuVPC) == 16);
}

class VulkanIndexBufferUploadBoundsTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    int pass_ = 0;
    int fail_ = 0;

    void check(bool ok, const std::string& label, const std::string& detail)
    {
        std::printf("[%s] %s: %s\n", ok ? "PASS" : "FAIL", label.c_str(), detail.c_str());
        if (ok) ++pass_; else ++fail_;
    }

    VulkanRenderer& Renderer()
    {
        return *dynamic_cast<VulkanRenderer*>(&getGraphicsDeviceProperty().GetRenderer());
    }

    // ── Legs A, B, C, D for the 16-bit width ─────────────────────────────────

    void test16Bit(GraphicsDevice& dev)
    {
        std::vector<std::uint16_t> payload(kIndexCount);
        for (int i = 0; i < kIndexCount; ++i)
            payload[static_cast<std::size_t>(i)] = static_cast<std::uint16_t>(i * 7 + 3);

        const VkDeviceSize before = Renderer().GetLiveIndexBufferBytesEXT();
        IndexBuffer ib(dev, IndexElementSize::SixteenBits, kIndexCount, BufferUsage::None);
        const VkDeviceSize mapped = Renderer().GetLiveIndexBufferBytesEXT() - before;
        const VkDeviceSize wanted = static_cast<VkDeviceSize>(kIndexCount) * 2u;
        check(mapped == wanted, "A 16-bit allocation is exactly capacity x 2",
              "mapped=" + std::to_string(mapped) + " wanted=" + std::to_string(wanted));

        ib.SetData(payload.data(), kIndexCount);
        std::vector<std::uint16_t> readBack(kIndexCount, 0u);
        ib.GetData(readBack.data(), kIndexCount);
        check(readBack == payload, "B 16-bit full-capacity upload round-trips",
              std::to_string(kIndexCount) + " indices");

        // C: one index too many. The shared layer refuses it by name; the renderer's own bound is
        // behind that and is what makes the invariant belong to the allocation's owner.
        std::vector<std::uint16_t> tooMany(static_cast<std::size_t>(kIndexCount) + 1u, 0xABCDu);
        bool refused = false;
        std::string how = "no exception";
        try {
            ib.SetData(tooMany.data(), kIndexCount + 1);
        } catch (const System::ArgumentOutOfRangeException& e) {
            refused = true; how = std::string("ArgumentOutOfRangeException: ") + e.what();
        } catch (const std::exception& e) {
            how = std::string("wrong type: ") + e.what();
        }
        check(refused, "C 16-bit over-capacity upload is refused by name", how);

        // The refusal must not have disturbed what was already there.
        std::vector<std::uint16_t> afterRefusal(kIndexCount, 0u);
        ib.GetData(afterRefusal.data(), kIndexCount);
        check(afterRefusal == payload, "C the refused upload left the contents intact",
              afterRefusal == payload ? "identical" : "contents changed");

        // D: a 32-bit source into a 16-bit buffer. Widening the allocation would let this draw
        // from misread bytes, so it must be refused rather than accommodated.
        std::vector<std::uint32_t> wrongWidth(kIndexCount, 1u);
        bool widthRefused = false;
        std::string widthHow = "no exception";
        try {
            ib.SetData(wrongWidth.data(), kIndexCount);
        } catch (const System::ArgumentException& e) {
            widthRefused = true; widthHow = std::string("ArgumentException: ") + e.what();
        } catch (const std::exception& e) {
            widthHow = std::string("wrong type: ") + e.what();
        }
        check(widthRefused, "D a 32-bit source into a 16-bit buffer is refused by name", widthHow);
    }

    // ── Legs A, B, E for the 32-bit width ────────────────────────────────────

    void test32Bit(GraphicsDevice& dev)
    {
        const VkDeviceSize before = Renderer().GetLiveIndexBufferBytesEXT();
        IndexBuffer ib(dev, IndexElementSize::ThirtyTwoBits, kIndexCount, BufferUsage::None);
        const VkDeviceSize mapped = Renderer().GetLiveIndexBufferBytesEXT() - before;
        const VkDeviceSize wanted = static_cast<VkDeviceSize>(kIndexCount) * 4u;
        check(mapped == wanted, "A 32-bit allocation is exactly capacity x 4",
              "mapped=" + std::to_string(mapped) + " wanted=" + std::to_string(wanted));

        // A full-capacity index list whose LAST six entries are the quad. Everything before them
        // is a degenerate triangle on vertex 0, so the visible result depends on the very end of
        // the mapping being both written and read.
        std::vector<std::uint32_t> indices(kIndexCount, 0u);
        const std::uint32_t quad[6] = { 0u, 1u, 2u, 0u, 2u, 3u };
        for (int i = 0; i < 6; ++i)
            indices[static_cast<std::size_t>(kIndexCount - 6 + i)] = quad[i];
        ib.SetData(indices.data(), kIndexCount);

        std::vector<std::uint32_t> readBack(kIndexCount, 0xFFFFFFFFu);
        ib.GetData(readBack.data(), kIndexCount);
        check(readBack == indices, "B 32-bit full-capacity upload round-trips",
              std::to_string(kIndexCount) + " indices");

        // E: draw the two triangles that live at the end of that buffer.
        GpuVPC verts[4] = {
            { -1.f,  1.f, 0.f, 255, 0, 0, 255 },
            { -1.f, -1.f, 0.f, 255, 0, 0, 255 },
            {  1.f, -1.f, 0.f, 255, 0, 0, 255 },
            {  1.f,  1.f, 0.f, 255, 0, 0, 255 },
        };
        VertexDeclaration decl(16, {
            VertexElement(0,  VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
            VertexElement(12, VertexElementFormat::Color,   VertexElementUsage::Color,    0),
        });
        VertexBuffer vb(dev, decl, 4, BufferUsage::None);
        vb.SetDataRaw(verts, 4, 16);

        dev.Clear(Color(0, 0, 0, 255));
        BasicEffect fx(dev);
        fx.VertexColorEnabled = true;
        fx.Apply();
        dev.SetVertexBuffer(&vb);
        dev.SetIndexBuffer(&ib);
        dev.DrawIndexedPrimitives(PrimitiveType::TriangleList, 0, 0, 4, kIndexCount - 6, 2);
        dev.SetIndexBuffer(nullptr);
        dev.SetVertexBuffer(nullptr);

        const Rectangle reg(kSize / 2, kSize / 2, 1, 1);
        Color got(0, 0, 0, 0);
        dev.GetBackBufferData(&reg, &got, 0, 1);
        const bool red = got.getRProperty() > 200 && got.getGProperty() < 60
                      && got.getBProperty() < 60;
        check(red, "E a full-capacity 32-bit index buffer draws from its last six indices",
              "centre=(" + std::to_string(got.getRProperty()) + ","
                         + std::to_string(got.getGProperty()) + ","
                         + std::to_string(got.getBProperty()) + ")");
    }

protected:
    void Initialize() override
    {
        Game::Initialize();
        auto& dev = getGraphicsDeviceProperty();
        dev.SetDepthTestEnabled(false);
        dev.setBlendStateProperty(BlendState::Opaque);
        dev.setRasterizerStateProperty(RasterizerState::CullNone);

        test16Bit(dev);
        test32Bit(dev);

        const auto& messages = Renderer().GetValidationMessagesEXT();
        check(messages.empty(), "F no validation messages",
              messages.empty() ? "0 captured"
                               : std::to_string(messages.size()) + " captured, first: "
                                     + messages.front());

        std::printf("=== %d/%d PASS ===\n", pass_, pass_ + fail_);
        Exit();
    }

    void Draw(const GameTime&) override {}

public:
    VulkanIndexBufferUploadBoundsTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kSize);
        gdm_->setPreferredBackBufferHeightProperty(kSize);
    }

    int getResult() const { return fail_ > 0 ? 1 : 0; }
};

int main()
{
    VulkanIndexBufferUploadBoundsTest g;
    g.Run();
    return g.getResult();
}
