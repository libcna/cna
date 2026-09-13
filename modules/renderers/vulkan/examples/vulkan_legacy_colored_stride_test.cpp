// SPDX-License-Identifier: MS-PL
// plans/plan_vulkan.md VULKAN-155 (finding F-24): the legacy `DrawColoredPrimitives` pair and a
// vertex stride that is not 16.
//
// `GetOrCreatePipeline3D` -- the only pipeline these two entry points reach -- declares
//
//     VkVertexInputBindingDescription bind{ 0, 16, VK_VERTEX_INPUT_RATE_VERTEX };
//     attrs[0] = { 0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0  };   // Position
//     attrs[1] = { 1, 0, VK_FORMAT_R8G8B8A8_UNORM,   12 };   // Color
//
// with nothing parameterised, while both entry points compute `stride = GetStride()` and copy
// `vertexCount * stride` bytes into the deferred arena. A 20-byte record is therefore copied
// faithfully at 20 and read back at 16: vertex 1 is fetched from byte 16, in the middle of vertex
// 0. Neither method runs the declaration-fidelity guard that every other draw entry point runs.
//
// Of the three things a renderer can do with a stride it cannot express -- bind it, refuse it, or
// mis-read it -- EasyGL binds it (`vb.BindForDraw()` uses the buffer's own declared layout) and
// D3D9 refuses it by name. This file pins which one this renderer does.
//
//   A  stride 16, the record this route was written for: renders the vertex colour. The control,
//      and the thing that must not change.
//   B  stride 20, the same colours in the same place with four bytes of padding after: must not
//      silently render something else. Refusing by name is the accepted answer here; drawing the
//      wrong picture is not.
//   C  DrawIndexedColoredPrimitives owes exactly the same answer as DrawColoredPrimitives -- it is
//      the same pipeline and the same hard-coded binding.
//   D  No validation messages.
//
// Exit code 0 = all PASS, 1 = any FAIL.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
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
    const Color kVertexColor(230, 30, 30, 255);
    const Color kClear(0, 0, 0, 255);

    /// Two triangles covering the whole viewport, in the order the indexed leg also uses.
    constexpr float kX[6] = { -1.f, -1.f,  1.f, -1.f,  1.f,  1.f };
    constexpr float kY[6] = {  1.f, -1.f, -1.f,  1.f, -1.f,  1.f };
}

class VulkanLegacyColoredStrideTest : public Game
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

    static bool Near(const Color& got, const Color& want)
    {
        const int tol = 24;
        return std::abs(int(got.getRProperty()) - int(want.getRProperty())) <= tol
            && std::abs(int(got.getGProperty()) - int(want.getGProperty())) <= tol
            && std::abs(int(got.getBProperty()) - int(want.getBProperty())) <= tol;
    }
    static std::string Show(const Color& c)
    {
        return "(" + std::to_string(c.getRProperty()) + "," + std::to_string(c.getGProperty())
             + "," + std::to_string(c.getBProperty()) + ")";
    }

    Color ReadCentre(GraphicsDevice& dev)
    {
        Color got(0, 0, 0, 0);
        const Rectangle probe(kSize / 2, kSize / 2, 1, 1);
        dev.GetBackBufferData(&probe, &got, 0, 1);
        return got;
    }

    /// `stride`-byte records: position at 0, the colour at 12, the rest zero padding.
    static std::vector<std::uint8_t> BuildRecords(int stride)
    {
        std::vector<std::uint8_t> bytes(static_cast<std::size_t>(6 * stride), 0);
        for (int i = 0; i < 6; ++i) {
            std::uint8_t* v = bytes.data() + i * stride;
            const float pos[3] = { kX[i], kY[i], 0.0f };
            std::memcpy(v + 0, pos, sizeof(pos));
            v[12] = kVertexColor.getRProperty(); v[13] = kVertexColor.getGProperty();
            v[14] = kVertexColor.getBProperty(); v[15] = kVertexColor.getAProperty();
        }
        return bytes;
    }

    static VertexDeclaration PositionColorDeclaration(int stride)
    {
        return VertexDeclaration(
            stride,
            {
                VertexElement(0,  VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
                VertexElement(12, VertexElementFormat::Color,   VertexElementUsage::Color,    0),
            });
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
        auto& renderer = Renderer();
        const Matrix id = Matrix::getIdentityProperty();

        const std::uint16_t indices[6] = { 0, 1, 2, 3, 4, 5 };
        IndexBuffer ib(dev, IndexElementSize::SixteenBits, 6, BufferUsage::None);
        ib.SetData(indices, 6);

        /// One legacy draw at `stride`, returning the centre pixel or the refusal text.
        const auto legacyDraw = [&](int stride, bool indexed, std::string& refusal) {
            Color got(0, 0, 0, 0);
            refusal.clear();
            VertexBuffer vb(dev, PositionColorDeclaration(stride), 6, BufferUsage::None);
            const auto bytes = BuildRecords(stride);
            vb.SetDataRaw(bytes.data(), 6, stride);
            try {
                dev.Clear(kClear);
                if (indexed)
                    renderer.DrawIndexedColoredPrimitives(vb.GetRenderer(), ib.GetRenderer(),
                                                          id, id, id, PrimitiveType::TriangleList, 2);
                else
                    renderer.DrawColoredPrimitives(vb.GetRenderer(), id, id, id,
                                                   PrimitiveType::TriangleList, 2);
                got = ReadCentre(dev);
            } catch (const std::exception& e) {
                refusal = e.what();
            }
            return got;
        };

        // ---- A: stride 16, the record this route was written for --------------------------------
        {
            std::string refusal;
            const Color got = legacyDraw(16, /*indexed=*/false, refusal);
            check(refusal.empty() && Near(got, kVertexColor),
                  "A DrawColoredPrimitives at stride 16 renders the vertex colour",
                  refusal.empty() ? Show(got) + " (expected " + Show(kVertexColor) + ")"
                                  : "refused: " + refusal);
        }

        // ---- B: stride 20 -----------------------------------------------------------------------
        {
            std::string refusal;
            const Color got = legacyDraw(20, /*indexed=*/false, refusal);
            const bool refused  = !refusal.empty();
            const bool rendered = refusal.empty() && Near(got, kVertexColor);
            check(refused || rendered,
                  "B DrawColoredPrimitives at stride 20 refuses by name, or renders the same "
                  "colour -- never a different picture",
                  refused ? "refused: " + refusal
                          : "drew " + Show(got) + " from a 20-byte record read at 16 bytes");
        }

        // ---- C: the indexed twin owes the same answer -------------------------------------------
        {
            std::string refusal16;
            const Color got16 = legacyDraw(16, /*indexed=*/true, refusal16);
            check(refusal16.empty() && Near(got16, kVertexColor),
                  "C DrawIndexedColoredPrimitives at stride 16 renders the vertex colour",
                  refusal16.empty() ? Show(got16) + " (expected " + Show(kVertexColor) + ")"
                                    : "refused: " + refusal16);

            std::string refusal20;
            const Color got20 = legacyDraw(20, /*indexed=*/true, refusal20);
            const bool refused  = !refusal20.empty();
            const bool rendered = refusal20.empty() && Near(got20, kVertexColor);
            check(refused || rendered,
                  "C DrawIndexedColoredPrimitives at stride 20 answers the same way as its "
                  "non-indexed twin",
                  refused ? "refused: " + refusal20
                          : "drew " + Show(got20) + " from a 20-byte record read at 16 bytes");
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
    VulkanLegacyColoredStrideTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kSize);
        gdm_->setPreferredBackBufferHeightProperty(kSize);
    }

    int getResult() const { return fail_ > 0 ? 1 : 0; }
};

int main()
{
    VulkanLegacyColoredStrideTest g;
    g.Run();
    return g.getResult();
}
