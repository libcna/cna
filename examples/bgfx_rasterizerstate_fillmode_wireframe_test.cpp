// SPDX-License-Identifier: MS-PL
// Task 766: verify RasterizerState.FillMode = FillMode::WireFrame actually suppresses triangle
// interior rasterization on Bgfx (bgfx has no native polygon-fill-mode toggle, unlike D3D9/Vulkan
// -- emulated by re-expanding triangle indices into a line list, see
// BgfxGraphicsBackend::ExpandWireframeIndices).
//
// Adapted from examples/vulkan_fill_mode_test.cpp (Task 327) -- no EasyGL/shared source exists for
// this property (confirmed via search; EasyGLGraphicsBackend::DrawWireframe has no dedicated pixel
// test anywhere in this project prior to this task). Restructured into per-check RunCheck() passes
// (own Clear+Draw+Read retry loop) since Bgfx's own GetBackBufferData only reliably reflects the
// first read per rendered frame (Task 406 finding).
//
// Triangle geometry (input NDC, Y is flipped by CNA's own vertex shader convention, matching
// vulkan_fill_mode_test.cpp's reference triangle): top (0, 0.8), BR (1,-0.8), BL (-1,-0.8). The
// centre pixel at NDC (0,0) is well inside the triangle interior for the solid case.
//
// Check A — Solid baseline: centre must be RED (interior filled) -- confirms the reference
//   geometry/effect setup itself is correct before testing the property under investigation.
// Check B — WireFrame: centre must be BACKGROUND (interior not rasterized, only the 3 edges are
//   drawn as thin lines that don't cover the centre of this triangle).
// Check C — Reset to Solid: centre must be RED again -- confirms FillMode genuinely reverts
//   (rules out "WireFrame gets stuck permanently once set").
//
// Exit code 0 = all 3 checks PASS, 1 = any FAIL.

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

static constexpr int kSize = 64;

struct GpuVPC { float x, y, z; uint8_t r, g, b, a; };
static_assert(sizeof(GpuVPC) == 16, "GpuVPC must be 16 bytes");

namespace
{
    const Color kBackground(0, 0, 0, 255);
}

class BgfxRasterizerStateFillModeWireframeTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    std::unique_ptr<VertexBuffer> vb_;
    bool done_   = false;
    int  result_ = 1;

    static bool IsRed(const Color& c)
    {
        return c.getRProperty() >= 200 && c.getGProperty() <= 30 && c.getBProperty() <= 30;
    }
    static bool IsBackground(const Color& c)
    {
        return c.getRProperty() <= 30 && c.getGProperty() <= 30 && c.getBProperty() <= 30;
    }

    Color RunCheck(GraphicsDevice& dev, FillMode fillMode)
    {
        Color got(0, 0, 0, 0);
        for (int i = 0; i < 20; ++i)
        {
            dev.Clear(kBackground);
            dev.setBlendStateProperty(BlendState::Opaque);

            RasterizerState rs;
            rs.setFillModeProperty(fillMode);
            dev.setRasterizerStateProperty(rs);

            BasicEffect fx(dev);
            fx.VertexColorEnabled = true;
            fx.Apply();

            dev.SetVertexBuffer(vb_.get());
            dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 1);
            dev.SetVertexBuffer(nullptr);

            const Rectangle reg(kSize / 2, kSize / 2, 1, 1);
            dev.GetBackBufferData(&reg, &got, 0, 1);
            if (got.getRProperty() != 0 || got.getGProperty() != 0 || got.getBProperty() != 0)
                break;  // Bgfx's GetBackBufferData only reliably reflects the first read per
                        // rendered frame (Task 406 finding); this test reads once per frame.
        }
        return got;
    }

protected:
    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;

        auto& dev = getGraphicsDeviceProperty();

        // Triangle covering most of NDC space, matching vulkan_fill_mode_test.cpp's reference
        // geometry: centre (NDC 0,0) is well inside the interior for the solid case.
        GpuVPC verts[3] = {
            {  0.0f,  0.8f, 0.f, 255, 0, 0, 255 },
            {  1.0f, -0.8f, 0.f, 255, 0, 0, 255 },
            { -1.0f, -0.8f, 0.f, 255, 0, 0, 255 },
        };
        VertexDeclaration decl(16, {
            VertexElement( 0, VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
            VertexElement(12, VertexElementFormat::Color,   VertexElementUsage::Color,    0),
        });
        vb_ = std::make_unique<VertexBuffer>(dev, decl, 3, BufferUsage::None);
        vb_->SetDataRaw(verts, 3, 16);

        struct Check { const char* name; FillMode mode; bool expectRed; };
        const Check checks[3] = {
            { "Solid baseline",     FillMode::Solid,     true  },
            { "WireFrame",          FillMode::WireFrame, false },
            { "Solid (reset)",      FillMode::Solid,     true  },
        };

        int passCount = 0;
        for (const auto& check : checks)
        {
            const Color c = RunCheck(dev, check.mode);
            const bool ok = check.expectRed ? IsRed(c) : IsBackground(c);
            std::printf("[%s] %s: centre=(%d,%d,%d) expected %s\n",
                        ok ? "PASS" : "FAIL", check.name,
                        c.getRProperty(), c.getGProperty(), c.getBProperty(),
                        check.expectRed ? "RED (interior filled)"
                                        : "BACKGROUND (interior not rasterized)");
            if (ok) ++passCount;
        }

        std::printf("=== %d/3 PASS ===\n", passCount);
        result_ = (passCount == 3) ? 0 : 1;
        Exit();
    }

public:
    BgfxRasterizerStateFillModeWireframeTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kSize);
        gdm_->setPreferredBackBufferHeightProperty(kSize);
    }

    int getResult() const { return result_; }
};

int main()
{
    BgfxRasterizerStateFillModeWireframeTest game;
    game.Run();
    return game.getResult();
}
