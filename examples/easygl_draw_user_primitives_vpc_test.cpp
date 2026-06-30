// SPDX-License-Identifier: MS-PL
// Task 255: EasyGL pixel-readback test for DrawUserPrimitives<VertexPositionColor>.
//
// Draws a full-NDC red quad via DrawUserPrimitives (typed VPC overload) and reads
// back the centre pixel. Two sub-tests:
//   (1) 16-bit VertexPositionColor array, 2 triangles.
//   (2) VertexOffset non-zero: first vertex in array is a dummy, real geometry at offset 1.
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
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"

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

class DrawUserPrimitivesVPCTest : public Game
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

    // Sub-test 1: vertexOffset=0, 6 vertices, 2 triangles.
    void testBasic(GraphicsDevice& dev)
    {
        const VertexPositionColor verts[6] = {
            { kTL, kRed }, { kBL, kRed }, { kBR, kRed },
            { kTL, kRed }, { kBR, kRed }, { kTR, kRed },
        };

        dev.Clear(kGreen);
        dev.SetDepthTestEnabled(false);
        dev.setBlendStateProperty(BlendState::Opaque);

        BasicEffect fx(dev);
        fx.VertexColorEnabled = true;
        fx.Apply();

        dev.DrawUserPrimitives(PrimitiveType::TriangleList, verts, 0, 2);

        const Color got = readCenter(dev);
        check(isRed(got), "DrawUserPrimitives VPC offset=0", got);
    }

    // Sub-test 2: vertexOffset=1 — first slot is a dummy green vertex, real quad starts at index 1.
    void testVertexOffset(GraphicsDevice& dev)
    {
        const VertexPositionColor verts[7] = {
            { kTL, kGreen },                                       // dummy at index 0
            { kTL, kRed }, { kBL, kRed }, { kBR, kRed },
            { kTL, kRed }, { kBR, kRed }, { kTR, kRed },
        };

        dev.Clear(kGreen);
        dev.SetDepthTestEnabled(false);
        dev.setBlendStateProperty(BlendState::Opaque);

        BasicEffect fx(dev);
        fx.VertexColorEnabled = true;
        fx.Apply();

        // offset=1 skips the dummy green vertex; 2 triangles from the 6 real vertices.
        dev.DrawUserPrimitives(PrimitiveType::TriangleList, verts, 1, 2);

        const Color got = readCenter(dev);
        check(isRed(got), "DrawUserPrimitives VPC offset=1", got);
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
    DrawUserPrimitivesVPCTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
    }

    int getResult() const { return fail_ > 0 ? 1 : 0; }
};

int main()
{
    DrawUserPrimitivesVPCTest game;
    game.Run();
    return game.getResult();
}
