// SPDX-License-Identifier: MS-PL
// Task 327: Vulkan FillMode::WireFrame integration test.
//
// Verifies that setting RasterizerState.FillMode = FillMode::WireFrame causes the
// Vulkan renderer to use VK_POLYGON_MODE_LINE, so triangle interiors are not
// rasterized.
//
// Triangle geometry (input NDC, Y is flipped by the vertex shader):
//   top (0, 0.8), BR (1,-0.8), BL (-1,-0.8).
// After the shader's Y-flip these become CW in Vulkan NDC, making them a front
// face under the default CullCounterClockwiseFace rasterizer state.
// At y=0 (screen centre) the left/right edges cross at x=-0.5 and x=+0.5, so
// the centre pixel at NDC (0,0) is well inside the triangle interior.
//
// Sub-test 1 — Solid baseline:  centre must be RED  (interior filled).
// Sub-test 2 — WireFrame:       centre must be BLACK (interior not rasterized).
// Sub-test 3 — Reset to Solid:  centre must be RED  again (FillMode reverts).
//
// Requires VkPhysicalDeviceFeatures.fillModeNonSolid (AMD Radeon 780M and all
// modern desktop Vulkan GPUs support this).
//
// Exit code 0 = all PASS, 1 = any FAIL.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/FillMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexDeclaration.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElement.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementUsage.hpp"

#include <cstdint>
#include <cstdio>
#include <memory>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

struct GpuVPC { float x, y, z; uint8_t r, g, b, a; };
static_assert(sizeof(GpuVPC) == 16, "GpuVPC must be 16 bytes");

class VulkanFillModeTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    int pass_ = 0;
    int fail_ = 0;

    void check(bool ok, const char* label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label);
        if (ok) ++pass_; else ++fail_;
    }

    Color readCenter(GraphicsDevice& dev)
    {
        const auto& vp = dev.getViewportProperty();
        Rectangle reg(vp.getWidthProperty() / 2, vp.getHeightProperty() / 2, 1, 1);
        Color px(0, 0, 0, 0);
        dev.GetBackBufferData(&reg, &px, 0, 1);
        return px;
    }

    Color drawAndRead(GraphicsDevice& dev, VertexBuffer& vb, BasicEffect& fx)
    {
        dev.Clear(Color(0, 0, 0, 255));
        dev.setBlendStateProperty(BlendState::Opaque);
        fx.Apply();
        dev.SetVertexBuffer(&vb);
        dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 1);
        dev.SetVertexBuffer(nullptr);
        return readCenter(dev);
    }

protected:
    void Initialize() override { Game::Initialize(); }

    void Draw(const GameTime&) override
    {
        auto& dev = getGraphicsDeviceProperty();

        // Triangle with CW winding in actual Vulkan NDC (after shader Y-flip):
        //   input top (0, 0.8) → clip (0,-0.8); input BR (1,-0.8) → clip (1, 0.8);
        //   input BL (-1,-0.8) → clip (-1, 0.8).
        // CW in Vulkan NDC → front face under CullCounterClockwiseFace → visible.
        GpuVPC verts[3] = {
            {  0.0f,  0.8f, 0.f, 255, 0, 0, 255 },
            {  1.0f, -0.8f, 0.f, 255, 0, 0, 255 },
            { -1.0f, -0.8f, 0.f, 255, 0, 0, 255 },
        };
        VertexDeclaration decl(16, {
            VertexElement( 0, VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
            VertexElement(12, VertexElementFormat::Color,   VertexElementUsage::Color,    0),
        });
        VertexBuffer vb(dev, decl, 3, BufferUsage::None);
        vb.SetDataRaw(verts, 3, 16);

        BasicEffect fx(dev);
        fx.VertexColorEnabled = true;

        Color px(0, 0, 0, 0);
        char buf[160];

        // Sub-test 1: Solid fill (default RasterizerState) — interior must be rasterized.
        px = drawAndRead(dev, vb, fx);
        std::snprintf(buf, sizeof(buf),
            "Solid: centre=(%d,%d,%d) expected red",
            px.getRProperty(), px.getGProperty(), px.getBProperty());
        check(px.getRProperty() >= 200 && px.getGProperty() <= 30 && px.getBProperty() <= 30, buf);

        // Sub-test 2: WireFrame — interior must NOT be rasterized.
        {
            RasterizerState rs;
            rs.setFillModeProperty(FillMode::WireFrame);
            dev.setRasterizerStateProperty(rs);
        }
        px = drawAndRead(dev, vb, fx);
        std::snprintf(buf, sizeof(buf),
            "WireFrame: centre=(%d,%d,%d) expected black (interior not rasterized)",
            px.getRProperty(), px.getGProperty(), px.getBProperty());
        check(px.getRProperty() <= 30 && px.getGProperty() <= 30 && px.getBProperty() <= 30, buf);

        // Sub-test 3: Reset to Solid — verify FillMode change takes effect.
        {
            RasterizerState rs;
            rs.setFillModeProperty(FillMode::Solid);
            dev.setRasterizerStateProperty(rs);
        }
        px = drawAndRead(dev, vb, fx);
        std::snprintf(buf, sizeof(buf),
            "Solid (reset): centre=(%d,%d,%d) expected red again",
            px.getRProperty(), px.getGProperty(), px.getBProperty());
        check(px.getRProperty() >= 200 && px.getGProperty() <= 30 && px.getBProperty() <= 30, buf);

        std::printf("\nResult: %d/%d PASS\n", pass_, pass_ + fail_);
        Exit();
    }

public:
    VulkanFillModeTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(320);
        gdm_->setPreferredBackBufferHeightProperty(240);
    }

    int getResult() const { return fail_ == 0 ? 0 : 1; }
};

int main()
{
    VulkanFillModeTest game;
    game.Run();
    return game.getResult();
}
