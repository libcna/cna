// SPDX-License-Identifier: MS-PL
// Task 256: EasyGL pixel-readback test for DrawUserPrimitives with explicit VertexDeclaration.
//
// Uses a custom packed vertex struct (12-byte position + 4-byte RGBA color = 16 bytes)
// and supplies its layout via VertexDeclaration to the raw-pointer overload:
//   DrawUserPrimitives(PrimitiveType, const void*, vertexOffset, primitiveCount, VertexDeclaration)
//
// Two sub-tests:
//   (1) vertexOffset=0, 2 triangles — full-NDC red quad.
//   (2) vertexOffset=1, real quad starts after a dummy green vertex.
//
// Exit code 0 = all PASS, 1 = any FAIL.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexDeclaration.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElement.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementUsage.hpp"

#include <cstdint>
#include <cstdio>
#include <memory>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

// Custom vertex: packed to 16 bytes (same layout as VertexPositionColor GpuVertex).
struct MyVertex
{
    float x, y, z;
    std::uint8_t r, g, b, a;
};
static_assert(sizeof(MyVertex) == 16, "MyVertex must be 16 bytes");

static const Color kGreen(0, 255, 0, 255);
static const Color kRed  (255, 0, 0, 255);

class DrawUserPrimitivesCustomTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    bool done_ = false;
    int  pass_ = 0;
    int  fail_ = 0;

    Color readCenter(GraphicsDevice& dev)
    {
        const auto& vp = dev.getViewportProperty();
        const Rectangle reg(vp.getWidthProperty() / 2, vp.getHeightProperty() / 2, 1, 1);
        Color px(0, 0, 0, 0);
        dev.GetBackBufferData(&reg, &px, 0, 1);
        return px;
    }

    bool isRed(Color c)
    {
        return c.getRProperty() >= 200 && c.getGProperty() <= 50 && c.getBProperty() <= 50;
    }

    void check(bool ok, const char* label, Color got)
    {
        if (ok)
        {
            std::printf("[PASS] %s: centre=(%d,%d,%d)\n", label,
                got.getRProperty(), got.getGProperty(), got.getBProperty());
            ++pass_;
        }
        else
        {
            std::printf("[FAIL] %s: centre=(%d,%d,%d), expected R>=200 G<=50 B<=50\n", label,
                got.getRProperty(), got.getGProperty(), got.getBProperty());
            ++fail_;
        }
    }

    VertexDeclaration makeVD()
    {
        return VertexDeclaration {
            VertexElement(0,  VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
            VertexElement(12, VertexElementFormat::Color,   VertexElementUsage::Color,    0)
        };
    }

    // Sub-test 1: vertexOffset=0, 6 vertices, 2 triangles.
    void testBasic(GraphicsDevice& dev)
    {
        const MyVertex verts[6] = {
            { -1.f,  1.f, 0.f, 255,   0, 0, 255 },
            { -1.f, -1.f, 0.f, 255,   0, 0, 255 },
            {  1.f, -1.f, 0.f, 255,   0, 0, 255 },
            { -1.f,  1.f, 0.f, 255,   0, 0, 255 },
            {  1.f, -1.f, 0.f, 255,   0, 0, 255 },
            {  1.f,  1.f, 0.f, 255,   0, 0, 255 },
        };

        dev.Clear(kGreen);
        dev.SetDepthTestEnabled(false);
        dev.setBlendStateProperty(BlendState::Opaque);

        BasicEffect fx(dev);
        fx.VertexColorEnabled = true;
        fx.Apply();

        auto vd = makeVD();
        // Task 896 finding: this quad's winding is CCW/back-facing under CNA's real default RasterizerState — needs CullNone.
        dev.setRasterizerStateProperty(RasterizerState::CullNone);
        dev.DrawUserPrimitives(PrimitiveType::TriangleList, verts, 0, 2, vd);

        const Color got = readCenter(dev);
        check(isRed(got), "DrawUserPrimitives VD offset=0", got);
    }

    // Sub-test 2: vertexOffset=1 — dummy green vertex at slot 0, real quad at slots 1-6.
    void testVertexOffset(GraphicsDevice& dev)
    {
        const MyVertex verts[7] = {
            { -1.f,  1.f, 0.f,   0, 255, 0, 255 },  // dummy green
            { -1.f,  1.f, 0.f, 255,   0, 0, 255 },
            { -1.f, -1.f, 0.f, 255,   0, 0, 255 },
            {  1.f, -1.f, 0.f, 255,   0, 0, 255 },
            { -1.f,  1.f, 0.f, 255,   0, 0, 255 },
            {  1.f, -1.f, 0.f, 255,   0, 0, 255 },
            {  1.f,  1.f, 0.f, 255,   0, 0, 255 },
        };

        dev.Clear(kGreen);
        dev.SetDepthTestEnabled(false);
        dev.setBlendStateProperty(BlendState::Opaque);

        BasicEffect fx(dev);
        fx.VertexColorEnabled = true;
        fx.Apply();

        auto vd = makeVD();
        // Task 896 finding: this quad's winding is CCW/back-facing under CNA's real default RasterizerState — needs CullNone.
        dev.setRasterizerStateProperty(RasterizerState::CullNone);
        dev.DrawUserPrimitives(PrimitiveType::TriangleList, verts, 1, 2, vd);

        const Color got = readCenter(dev);
        check(isRed(got), "DrawUserPrimitives VD offset=1", got);
    }

protected:
    void Initialize() override { Game::Initialize(); }

    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;

        auto& dev = getGraphicsDeviceProperty();
        testBasic(dev);
        testVertexOffset(dev);

        std::printf("=== %d/%d PASS ===\n", pass_, pass_ + fail_);
        Exit();
    }

public:
    DrawUserPrimitivesCustomTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
    }

    int getResult() const { return fail_ > 0 ? 1 : 0; }
};

int main()
{
    DrawUserPrimitivesCustomTest game;
    game.Run();
    return game.getResult();
}
