// SPDX-License-Identifier: MS-PL
// Task 257: EasyGL pixel-readback test for DrawUserIndexedPrimitives<VertexPositionColor>
// (16-bit index overload).
//
// Draws a full-NDC red quad via DrawUserIndexedPrimitives (typed VPC + uint16_t overload)
// and reads back the centre pixel. Two sub-tests:
//   (1) vertexOffset=0, indexOffset=0: 4 vertices + 6 indices, 2 triangles.
//   (2) vertexOffset=1 and indexOffset=1 non-zero: a dummy vertex/index precede the real
//       geometry to verify offset arithmetic for both arrays.
//
// Exit code 0 = all PASS, 1 = any FAIL.

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

#include <cstdint>
#include <cstdio>
#include <memory>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

static const Color kGreen(0, 255, 0, 255);
static const Color kRed  (255, 0, 0, 255);

// Full-NDC quad corners.
static const Vector3 kTL(-1.0f,  1.0f, 0.0f);
static const Vector3 kBL(-1.0f, -1.0f, 0.0f);
static const Vector3 kBR( 1.0f, -1.0f, 0.0f);
static const Vector3 kTR( 1.0f,  1.0f, 0.0f);

class DrawUserIndexedPrimitivesVPCTest : public Game
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

    // Sub-test 1: vertexOffset=0, indexOffset=0. 4 vertices, 6 indices, 2 triangles.
    void testBasic(GraphicsDevice& dev)
    {
        const VertexPositionColor verts[4] = {
            { kTL, kRed }, { kBL, kRed }, { kBR, kRed }, { kTR, kRed },
        };
        const std::uint16_t indices[6] = { 0, 1, 2, 0, 2, 3 };

        dev.Clear(kGreen);
        dev.SetDepthTestEnabled(false);
        dev.setBlendStateProperty(BlendState::Opaque);

        BasicEffect fx(dev);
        fx.VertexColorEnabled = true;
        fx.Apply();

        // Task 896 finding: this quad's winding is CCW/back-facing under CNA's real default RasterizerState — needs CullNone.
        dev.setRasterizerStateProperty(RasterizerState::CullNone);
        dev.DrawUserIndexedPrimitives(PrimitiveType::TriangleList, verts, 0, 4, indices, 0, 2);

        const Color got = readCenter(dev);
        check(isRed(got), "DrawUserIndexedPrimitives VPC offset=0", got);
    }

    // Sub-test 2: vertexOffset=1 and indexOffset=1 — a dummy vertex/index precede the real
    // geometry in both arrays, verifying offset arithmetic independently for each.
    void testOffsets(GraphicsDevice& dev)
    {
        const VertexPositionColor verts[5] = {
            { kTL, kGreen },                                       // dummy at index 0
            { kTL, kRed }, { kBL, kRed }, { kBR, kRed }, { kTR, kRed },
        };
        // Index values are relative to the vertexOffset-shifted window (0 == verts[vertexOffset]).
        const std::uint16_t indices[7] = { 0 /* dummy */, 0, 1, 2, 0, 2, 3 };

        dev.Clear(kGreen);
        dev.SetDepthTestEnabled(false);
        dev.setBlendStateProperty(BlendState::Opaque);

        BasicEffect fx(dev);
        fx.VertexColorEnabled = true;
        fx.Apply();

        // Task 896 finding: this quad's winding is CCW/back-facing under CNA's real default RasterizerState — needs CullNone.
        dev.setRasterizerStateProperty(RasterizerState::CullNone);
        dev.DrawUserIndexedPrimitives(PrimitiveType::TriangleList, verts, 1, 4, indices, 1, 2);

        const Color got = readCenter(dev);
        check(isRed(got), "DrawUserIndexedPrimitives VPC offset=1", got);
    }

protected:
    void Initialize() override { Game::Initialize(); }

    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;

        auto& dev = getGraphicsDeviceProperty();
        testBasic(dev);
        testOffsets(dev);

        std::printf("=== %d/%d PASS ===\n", pass_, pass_ + fail_);
        Exit();
    }

public:
    DrawUserIndexedPrimitivesVPCTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
    }

    int getResult() const { return fail_ > 0 ? 1 : 0; }
};

int main()
{
    DrawUserIndexedPrimitivesVPCTest game;
    game.Run();
    return game.getResult();
}
