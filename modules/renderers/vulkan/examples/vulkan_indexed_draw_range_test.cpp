// SPDX-License-Identifier: MS-PL
// plans/plan_vulkan.md VULKAN-141: `baseVertex`, `startIndex`, `minVertexIndex` and `numVertices`
// mean what XNA says they mean.
//
// XNA adds `baseVertex` to every index it reads; `startIndex` says where in the INDEX buffer to
// begin. Vulkan's `vkCmdDrawIndexed` takes `firstIndex` and `vertexOffset` with exactly those two
// meanings, so this is a mapping check -- and a mapping check is only worth anything with a scene
// that tells the two apart.
//
// The scene is four quads, one per screen quadrant, each a different colour, sharing one vertex
// buffer (16 vertices) and one index buffer (24 indices). Quad k's indices are 4k+{0,1,2, 0,2,3}.
// That layout is what makes each parameter separable:
//
//   A  baseVertex=0,  startIndex=0  -> the FIRST quad. The control.
//   B  baseVertex=4,  startIndex=0  -> the same six indices, +4 on each -> the SECOND quad.
//      Only `baseVertex` moved, so only `baseVertex` can explain the change.
//   C  baseVertex=0,  startIndex=6  -> different six indices, unshifted -> also the second quad.
//      Only `startIndex` moved. B and C landing on the same quad by different routes is what
//      proves the two are not being confused for one another.
//   D  baseVertex=8,  startIndex=6  -> both at once -> the FOURTH quad.
//   E  `minVertexIndex`/`numVertices` are XNA hints that Direct3D 9 needed and Vulkan does not.
//      A tight, correct range and a deliberately over-wide one must render the SAME picture -- if
//      either changed it, they would not be hints.
//   F  No validation messages.
//
// Every leg asserts all four quadrants, not just the one it expects to light: a draw that lit two
// quads, or the right quad plus a stray triangle, would pass a single-probe check.
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

#include <array>
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
    const Color kClear(0, 0, 0, 255);
    /// One colour per quad, far enough apart that "which quad is this" is exact.
    const Color kQuadColor[4] = {
        Color(230,  30,  30, 255),   // 0: top-left
        Color( 30, 230,  30, 255),   // 1: top-right
        Color( 30,  60, 230, 255),   // 2: bottom-left
        Color(230, 230,  30, 255),   // 3: bottom-right
    };
    /// The NDC corner of each quad's quadrant: {x0, y0, x1, y1}.
    constexpr float kQuadRect[4][4] = {
        { -1.f,  0.f,  0.f,  1.f },
        {  0.f,  0.f,  1.f,  1.f },
        { -1.f, -1.f,  0.f,  0.f },
        {  0.f, -1.f,  1.f,  0.f },
    };
    /// Where to probe each quadrant on a 64x64 backbuffer.
    constexpr int kProbe[4][2] = { { 16, 16 }, { 48, 16 }, { 16, 48 }, { 48, 48 } };
}

class VulkanIndexedDrawRangeTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    std::unique_ptr<VertexBuffer> vb_;
    std::unique_ptr<IndexBuffer>  ib_;
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

    /// Draws one indexed range and returns the four quadrant probes.
    std::array<Color, 4> DrawRange(GraphicsDevice& dev, BasicEffect& fx, int baseVertex,
                                   int minVertexIndex, int numVertices, int startIndex,
                                   int primitiveCount)
    {
        dev.Clear(kClear);
        fx.Apply();
        dev.SetVertexBuffer(vb_.get());
        dev.SetIndexBuffer(ib_.get());
        dev.DrawIndexedPrimitives(PrimitiveType::TriangleList, baseVertex, minVertexIndex,
                                  numVertices, startIndex, primitiveCount);
        dev.SetVertexBuffer(nullptr);
        std::array<Color, 4> got{};
        for (int q = 0; q < 4; ++q) {
            Color c(0, 0, 0, 0);
            const Rectangle at(kProbe[q][0], kProbe[q][1], 1, 1);
            dev.GetBackBufferData(&at, &c, 0, 1);
            got[q] = c;
        }
        return got;
    }

    /// Asserts quadrant `expected` carries its own colour and the other three are still cleared.
    void CheckOnly(const std::array<Color, 4>& got, int expected, const std::string& label)
    {
        std::string detail;
        bool ok = true;
        for (int q = 0; q < 4; ++q) {
            const Color& want = (q == expected) ? kQuadColor[q] : kClear;
            const bool hit = Near(got[q], want);
            ok = ok && hit;
            detail += (q ? " " : "") + std::string("q") + std::to_string(q) + "=" + Show(got[q]);
        }
        check(ok, label, detail + "; expected only q" + std::to_string(expected) + " = "
                             + Show(kQuadColor[expected]));
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

        // 16 vertices: four per quad, each carrying its quad's colour.
        std::uint8_t bytes[16 * 16]{};
        for (int q = 0; q < 4; ++q) {
            const float x0 = kQuadRect[q][0], y0 = kQuadRect[q][1];
            const float x1 = kQuadRect[q][2], y1 = kQuadRect[q][3];
            const float xs[4] = { x0, x0, x1, x1 };
            const float ys[4] = { y1, y0, y0, y1 };
            for (int v = 0; v < 4; ++v) {
                std::uint8_t* p = bytes + (q * 4 + v) * 16;
                const float pos[3] = { xs[v], ys[v], 0.0f };
                std::memcpy(p, pos, sizeof(pos));
                p[12] = kQuadColor[q].getRProperty(); p[13] = kQuadColor[q].getGProperty();
                p[14] = kQuadColor[q].getBProperty(); p[15] = kQuadColor[q].getAProperty();
            }
        }
        VertexDeclaration decl(16, {
            VertexElement(0,  VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
            VertexElement(12, VertexElementFormat::Color,   VertexElementUsage::Color,    0)});
        vb_ = std::make_unique<VertexBuffer>(dev, decl, 16, BufferUsage::None);
        vb_->SetDataRaw(bytes, 16, 16);

        std::uint16_t indices[24]{};
        for (int q = 0; q < 4; ++q) {
            const std::uint16_t b = static_cast<std::uint16_t>(q * 4);
            const std::uint16_t quad[6] = { static_cast<std::uint16_t>(b + 0),
                                            static_cast<std::uint16_t>(b + 1),
                                            static_cast<std::uint16_t>(b + 2),
                                            static_cast<std::uint16_t>(b + 0),
                                            static_cast<std::uint16_t>(b + 2),
                                            static_cast<std::uint16_t>(b + 3) };
            std::memcpy(indices + q * 6, quad, sizeof(quad));
        }
        ib_ = std::make_unique<IndexBuffer>(dev, IndexElementSize::SixteenBits, 24,
                                            BufferUsage::None);
        ib_->SetData(indices, 24);

        BasicEffect fx(dev);
        fx.VertexColorEnabled = true;
        fx.setLightingEnabledProperty(false);
        fx.setTextureEnabledProperty(false);
        fx.setFogEnabledProperty(false);
        fx.setWorldProperty(Matrix::getIdentityProperty());
        fx.setViewProperty(Matrix::getIdentityProperty());
        fx.setProjectionProperty(Matrix::getIdentityProperty());

        // `numVertices` counts from `baseVertex`, so each leg passes what is actually left of the
        // buffer -- see leg G, which is where that contract is asserted rather than assumed.
        CheckOnly(DrawRange(dev, fx, 0, 0, 16, 0, 2), 0,
                  "A baseVertex=0 startIndex=0 draws the first quad");
        CheckOnly(DrawRange(dev, fx, 4, 0, 12, 0, 2), 1,
                  "B baseVertex=4 alone moves the SAME six indices to the second quad");
        CheckOnly(DrawRange(dev, fx, 0, 0, 16, 6, 2), 1,
                  "C startIndex=6 alone reaches the second quad by a different route");
        CheckOnly(DrawRange(dev, fx, 8, 0, 8, 6, 2), 3,
                  "D both together reach the fourth quad");

        // ---- E: the two hints ---------------------------------------------------------------------
        {
            const auto tight = DrawRange(dev, fx, 4, 0, 4,  0, 2);   // exactly the vertices used
            const auto wide  = DrawRange(dev, fx, 4, 0, 12, 0, 2);   // everything left of the buffer
            bool same = true;
            for (int q = 0; q < 4; ++q) same = same && Near(tight[q], wide[q]);
            check(same && Near(tight[1], kQuadColor[1]),
                  "E minVertexIndex/numVertices are hints: the tightest legal range and the widest "
                  "render the same picture",
                  "tight q1=" + Show(tight[1]) + " wide q1=" + Show(wide[1]));
        }

        // ---- G: and the range is CHECKED, which is how the leg above learned its limits ------------
        // XNA counts `numVertices` from `baseVertex`, so `baseVertex=4, numVertices=16` names
        // vertex 19 of a 16-vertex buffer. CNA refuses it by name rather than reading past the end.
        // The first version of this file passed exactly that and was told so; the check is here now
        // so the contract is evidence instead of a thing the test happens to respect.
        {
            std::string how = "accepted";
            try {
                (void)DrawRange(dev, fx, 4, 0, 16, 0, 2);
            } catch (const std::exception& e) {
                how = e.what();
            }
            check(how.find("exceeds the bound vertex buffer") != std::string::npos,
                  "G a numVertices that runs past the end of the buffer is refused by name, "
                  "counted from baseVertex", how);
        }

        const auto& messages = Renderer().GetValidationMessagesEXT();
        check(messages.empty(), "F no validation messages",
              messages.empty() ? "0 captured"
                               : std::to_string(messages.size()) + " captured, first: "
                                     + messages.front());

        std::printf("=== %d/%d PASS ===\n", pass_, pass_ + fail_);
        std::fflush(stdout);
        Exit();
    }

public:
    VulkanIndexedDrawRangeTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kSize);
        gdm_->setPreferredBackBufferHeightProperty(kSize);
    }

    int getResult() const { return fail_ > 0 ? 1 : 0; }
};

int main()
{
    VulkanIndexedDrawRangeTest g;
    g.Run();
    return g.getResult();
}
