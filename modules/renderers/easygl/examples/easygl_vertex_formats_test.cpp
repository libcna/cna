// SPDX-License-Identifier: MS-PL
// Task 247: EasyGL integration test — draw quad with each supported VertexElementFormat.
//
// Exercises EasyGL's ApplyLayout path for each supported vertex stride using
// VertexBuffer + DrawPrimitives (the GPU buffer path, not DrawUserPrimitives):
//   (a) stride=16: Vector3 position + Color            → colored3D shader
//   (b) stride=20: Vector3 position + Vector2 texcoord → textured3D shader
//   (c) stride=24: Vector3 + Color + Vector2           → col_textured3D shader
//   (d) stride=32: Vector3 + Vector3 normal + Vector2  → lit_textured3D shader
//
// Each sub-test clears to green, draws a full-NDC red quad, reads back the centre
// pixel, and asserts R≥200, G≤50, B≤50.
//
// Exit code 0 = all PASS, 1 = any FAIL.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColorTexture.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionNormalTexture.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionTexture.hpp"

#include <cstdio>
#include <memory>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

static const Color kGreen(0, 255, 0, 255);
static const Color kRed  (255, 0, 0, 255);

// Full-screen NDC quad — 6 vertices, 2 triangles (TriangleList).
static const Vector3 kTL(-1.0f,  1.0f, 0.0f);
static const Vector3 kBL(-1.0f, -1.0f, 0.0f);
static const Vector3 kBR( 1.0f, -1.0f, 0.0f);
static const Vector3 kTR( 1.0f,  1.0f, 0.0f);

class VertexFormatsTest : public Game
{
    bool done_   = false;
    int  pass_   = 0;
    int  fail_   = 0;

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

    void check(bool cond, const char* label, Color got)
    {
        if (cond)
        {
            std::printf("[PASS] %s: centre=(%d,%d,%d)\n", label,
                got.getRProperty(), got.getGProperty(), got.getBProperty());
            ++pass_;
        }
        else
        {
            std::printf("[FAIL] %s: centre=(%d,%d,%d), expected R≥200 G≤50 B≤50\n", label,
                got.getRProperty(), got.getGProperty(), got.getBProperty());
            ++fail_;
        }
    }

    // stride=16 (colored3D): Vector3 position + Color.
    // BasicEffect.VertexColorEnabled=true; FragColor = vColor.
    void testStride16(GraphicsDevice& dev)
    {
        const VertexPositionColor verts[6] = {
            { kTL, kRed }, { kBL, kRed }, { kBR, kRed },
            { kTL, kRed }, { kBR, kRed }, { kTR, kRed },
        };
        VertexBuffer vb(dev, 6);
        vb.SetData(verts, 6);

        dev.Clear(kGreen);
        dev.SetDepthTestEnabled(false);
        dev.setBlendStateProperty(BlendState::Opaque);
        dev.SetVertexBuffer(&vb);

        BasicEffect fx(dev);
        fx.VertexColorEnabled = true;
        fx.Apply();
        // Task 896 finding: this quad's winding is CCW/back-facing under CNA's real default
        // RasterizerState — needs CullNone.
        dev.setRasterizerStateProperty(RasterizerState::CullNone);
        dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
        dev.SetVertexBuffer(nullptr);

        check(isRed(readCenter(dev)), "stride=16 (Vector3+Color)", readCenter(dev));
    }

    // stride=20 (textured3D): Vector3 position + Vector2 texcoord.
    // BasicEffect.DiffuseColor=red; no texture → EasyGL uses default white; FragColor = white * red = red.
    void testStride20(GraphicsDevice& dev)
    {
        const VertexPositionTexture verts[6] = {
            { kTL, Vector2(0.0f, 0.0f) }, { kBL, Vector2(0.0f, 1.0f) }, { kBR, Vector2(1.0f, 1.0f) },
            { kTL, Vector2(0.0f, 0.0f) }, { kBR, Vector2(1.0f, 1.0f) }, { kTR, Vector2(1.0f, 0.0f) },
        };
        VertexBuffer vb(dev, 6);
        vb.SetData(verts, 6);

        dev.Clear(kGreen);
        dev.SetDepthTestEnabled(false);
        dev.setBlendStateProperty(BlendState::Opaque);
        dev.SetVertexBuffer(&vb);

        BasicEffect fx(dev);
        fx.setDiffuseColorProperty(Vector3(1.0f, 0.0f, 0.0f));
        fx.Apply();
        // Task 896 finding: this quad's winding is CCW/back-facing under CNA's real default
        // RasterizerState — needs CullNone.
        dev.setRasterizerStateProperty(RasterizerState::CullNone);
        dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
        dev.SetVertexBuffer(nullptr);

        check(isRed(readCenter(dev)), "stride=20 (Vector3+Vector2)", readCenter(dev));
    }

    // stride=24 (col_textured3D): Vector3 position + Color + Vector2 texcoord.
    // BasicEffect.VertexColorEnabled=true; no texture → default white; FragColor = white * vColor = red.
    void testStride24(GraphicsDevice& dev)
    {
        const VertexPositionColorTexture verts[6] = {
            { kTL, kRed, Vector2(0.0f, 0.0f) }, { kBL, kRed, Vector2(0.0f, 1.0f) }, { kBR, kRed, Vector2(1.0f, 1.0f) },
            { kTL, kRed, Vector2(0.0f, 0.0f) }, { kBR, kRed, Vector2(1.0f, 1.0f) }, { kTR, kRed, Vector2(1.0f, 0.0f) },
        };
        VertexBuffer vb(dev, 6);
        vb.SetData(verts, 6);

        dev.Clear(kGreen);
        dev.SetDepthTestEnabled(false);
        dev.setBlendStateProperty(BlendState::Opaque);
        dev.SetVertexBuffer(&vb);

        BasicEffect fx(dev);
        fx.VertexColorEnabled = true;
        fx.Apply();
        // Task 896 finding: this quad's winding is CCW/back-facing under CNA's real default
        // RasterizerState — needs CullNone.
        dev.setRasterizerStateProperty(RasterizerState::CullNone);
        dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
        dev.SetVertexBuffer(nullptr);

        check(isRed(readCenter(dev)), "stride=24 (Vector3+Color+Vector2)", readCenter(dev));
    }

    // stride=32 (lit_textured3D): Vector3 position + Vector3 normal + Vector2 texcoord.
    // LightingEnabled=false → EasyGL sets ambient=(1,1,1), light=black.
    // DiffuseColor=red; no texture → default white; litRGB=(1,1,1)*red=red; FragColor=white*red=red.
    void testStride32(GraphicsDevice& dev)
    {
        const Vector3 kNorm(0.0f, 0.0f, 1.0f);
        const VertexPositionNormalTexture verts[6] = {
            { kTL, kNorm, Vector2(0.0f, 0.0f) }, { kBL, kNorm, Vector2(0.0f, 1.0f) }, { kBR, kNorm, Vector2(1.0f, 1.0f) },
            { kTL, kNorm, Vector2(0.0f, 0.0f) }, { kBR, kNorm, Vector2(1.0f, 1.0f) }, { kTR, kNorm, Vector2(1.0f, 0.0f) },
        };
        VertexBuffer vb(dev, 6);
        vb.SetData(verts, 6);

        dev.Clear(kGreen);
        dev.SetDepthTestEnabled(false);
        dev.setBlendStateProperty(BlendState::Opaque);
        dev.SetVertexBuffer(&vb);

        BasicEffect fx(dev);
        fx.setLightingEnabledProperty(false);
        fx.setDiffuseColorProperty(Vector3(1.0f, 0.0f, 0.0f));
        fx.Apply();
        // Task 896 finding: this quad's winding is CCW/back-facing under CNA's real default
        // RasterizerState — needs CullNone.
        dev.setRasterizerStateProperty(RasterizerState::CullNone);
        dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
        dev.SetVertexBuffer(nullptr);

        check(isRed(readCenter(dev)), "stride=32 (Vector3+Vector3+Vector2)", readCenter(dev));
    }

    // plans/plan_gltf.md GLTF-337: EasyGL reuses its lit stride-32 program for BasicEffect's unlit
    // variant. Keep a vertex exactly at the shader's default eye position. Before the explicit
    // uLightingEnabled branch, the supposedly-disabled specular calculation evaluated
    // normalize(0), and NaN*zero contaminated the whole primitive into black. XNA's separate
    // unlit shader never evaluates that lighting math.
    void testStride32UnlitVertexAtEye(GraphicsDevice& dev)
    {
        const Vector3 centre(0.0f, 0.0f, 0.0f);
        const Vector3 normal(0.0f, 0.0f, 1.0f);
        const Vector2 uv(0.0f, 0.0f);
        const VertexPositionNormalTexture verts[12] = {
            { centre, normal, uv }, { kTL, normal, uv }, { kBL, normal, uv },
            { centre, normal, uv }, { kBL, normal, uv }, { kBR, normal, uv },
            { centre, normal, uv }, { kBR, normal, uv }, { kTR, normal, uv },
            { centre, normal, uv }, { kTR, normal, uv }, { kTL, normal, uv },
        };
        VertexBuffer vb(dev, 12);
        vb.SetData(verts, 12);

        dev.Clear(kGreen);
        dev.SetDepthTestEnabled(false);
        dev.setBlendStateProperty(BlendState::Opaque);
        dev.SetVertexBuffer(&vb);

        BasicEffect fx(dev);
        fx.setLightingEnabledProperty(false);
        fx.setDiffuseColorProperty(Vector3(1.0f, 0.0f, 0.0f));
        fx.Apply();
        dev.setRasterizerStateProperty(RasterizerState::CullNone);
        dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 4);
        dev.SetVertexBuffer(nullptr);

        check(isRed(readCenter(dev)), "stride=32 unlit vertex at eye stays finite",
              readCenter(dev));
    }

protected:
    void Initialize() override { Game::Initialize(); }

    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;

        auto& dev = getGraphicsDeviceProperty();
        testStride16(dev);
        testStride20(dev);
        testStride24(dev);
        testStride32(dev);
        testStride32UnlitVertexAtEye(dev);

        std::printf("=== %d/%d PASS ===\n", pass_, pass_ + fail_);
        Exit();
    }

public:
    int getResult() const { return fail_ > 0 ? 1 : 0; }
};

int main()
{
    VertexFormatsTest game;
    game.Run();
    return game.getResult();
}
