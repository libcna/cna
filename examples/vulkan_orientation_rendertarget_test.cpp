// SPDX-License-Identifier: MS-PL
// REMED-GFX-011: render-target orientation parity for the four Vulkan effect families that were
// missing the NDC Y-flip.
//
// vulkan_orientation_effects_test.cpp proves the flip is correct when drawing to the BACKBUFFER.
// That alone does not prove it for render targets: a renderer can legitimately use a different
// clip-space or row convention when rendering to an offscreen image, and a fix that is right on
// the backbuffer can be double-flipped on an RT. This test covers that second path.
//
// Rather than hard-code an absolute RT row convention, it calibrates: colored3d (a family whose
// flip has always been correct) draws the same asymmetric quadrant quad into the RT first, and
// whichever quadrant lights up becomes the reference. Each of the four fixed families must then
// light the SAME quadrant. This stays valid whether or not RT readback rows run top-first, and it
// is exactly the property that matters — every effect family must agree with every other one.
//
// Readback blits the RT onto the backbuffer with SpriteBatch (the Vulkan renderer implements no
// GetTextureData, so Texture2D::GetData on an RT reads an unpopulated CPU shadow). SpriteBatch has
// its own NDC convention, but it applies identically to the reference and to every family under
// test, so it cancels out of the comparison — see LitQuadrant().
//
// Exit code 0 = PASS, 1 = FAIL.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/CubeMapFace.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/EnvironmentMapEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/PbrEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SkinnedPbrEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteSortMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureCube.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBufferBinding.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionNormalTexture.hpp"

#include <cstdint>
#include <cstdio>
#include <memory>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int   kRT = 64;
    constexpr float kLo = 0.2f;
    constexpr float kHi = 0.9f;

    struct PbrGpuVertex
    {
        float px, py, pz;  float nx, ny, nz;
        float tx, ty, tz, tw;  float u, v;
    };
    static_assert(sizeof(PbrGpuVertex) == 48);

    struct SkinnedPbrGpuVertex
    {
        float px, py, pz;  float nx, ny, nz;
        float tx, ty, tz, tw;  float u, v;
        float w0, w1, w2, w3;  std::uint8_t i0, i1, i2, i3;
    };
    static_assert(sizeof(SkinnedPbrGpuVertex) == 68);

    struct GpuVPC { float x, y, z; std::uint8_t r, g, b, a; };
    static_assert(sizeof(GpuVPC) == 16);

    struct InstMat4 { float m[16]; };
    static_assert(sizeof(InstMat4) == 64);

    template <typename V>
    std::vector<V> MakeQuadIn(float x0, float y0, float x1, float y1)
    {
        auto fill = [](V& v, float x, float y, float u, float vv) {
            v.px = x; v.py = y; v.pz = 0.0f;
            v.nx = 0.0f; v.ny = 0.0f; v.nz = 1.0f;
            v.tx = 1.0f; v.ty = 0.0f; v.tz = 0.0f; v.tw = 1.0f;
            v.u = u; v.v = vv;
        };
        V tl{}, bl{}, br{}, tr{};
        fill(tl, x0, y1, 0.0f, 0.0f);
        fill(bl, x0, y0, 0.0f, 1.0f);
        fill(br, x1, y0, 1.0f, 1.0f);
        fill(tr, x1, y1, 1.0f, 0.0f);
        return { tl, bl, br, tl, br, tr };
    }

    const char* QuadrantName(int q)
    {
        switch (q)
        {
            case 0:  return "TOP-LEFT";
            case 1:  return "TOP-RIGHT";
            case 2:  return "BOTTOM-LEFT";
            case 3:  return "BOTTOM-RIGHT";
            default: return "NONE/AMBIGUOUS";
        }
    }
}

class VulkanOrientationRenderTargetTest : public Game
{
    std::unique_ptr<SpriteBatch> sb_;
    bool done_ = false;
    int  pass_ = 0;
    int  fail_ = 0;

    static int Luma(const Color& c)
    {
        return (c.getRProperty() * 30 + c.getGProperty() * 59 + c.getBProperty() * 11) / 100;
    }

    // Returns the index of the single lit quadrant, or -1 if not exactly one is lit.
    //
    // The Vulkan renderer implements no GetTextureData, so Texture2D::GetData on a render target
    // reads an unpopulated CPU shadow. Readback therefore goes the same way every other Vulkan RT
    // test goes: blit the RT 1:1 onto the backbuffer with SpriteBatch and sample that. SpriteBatch
    // carries its own NDC convention, but every family here — including the colored3d reference —
    // passes through the identical blit, so any constant SpriteBatch flip cancels out of the
    // comparison. What survives is exactly the per-family disagreement this test is looking for.
    int LitQuadrant(GraphicsDevice& dev, RenderTarget2D& rt)
    {
        dev.Clear(Color(0, 0, 0, 255));
        SamplerState point = SamplerState::PointClamp;
        sb_->Begin(SpriteSortMode::Deferred, BlendState::Opaque, &point, nullptr, nullptr);
        sb_->Draw(rt, Rectangle(0, 0, kRT, kRT), Rectangle(0, 0, kRT, kRT), Color::White);
        sb_->End();

        const int q = kRT / 4;
        const int xs[4] = { q, 3 * q, q, 3 * q };
        const int ys[4] = { q, q, 3 * q, 3 * q };
        int lit = -1, count = 0;
        for (int i = 0; i < 4; ++i)
        {
            const Rectangle reg(xs[i], ys[i], 1, 1);
            Color px(0, 0, 0, 0);
            dev.GetBackBufferData(&reg, &px, 0, 1);
            if (Luma(px) >= 40) { lit = i; ++count; }
        }
        return count == 1 ? lit : -1;
    }

    void BeginRT(GraphicsDevice& dev, RenderTarget2D& rt)
    {
        dev.SetRenderTarget(&rt);
        dev.Clear(Color(0, 0, 0, 255));
        dev.SetDepthTestEnabled(false);
        dev.setBlendStateProperty(BlendState::Opaque);
        // REMED-GFX-011/052: with the flip in place this winding is back-facing under the real
        // default state; without CullNone the RT would read all-black and prove nothing.
        dev.setRasterizerStateProperty(RasterizerState::CullNone);
    }

    std::unique_ptr<TextureCube> MakeSolidCube(GraphicsDevice& dev, Color col)
    {
        auto cube = std::make_unique<TextureCube>(dev, 1, false, SurfaceFormat::Color);
        const CubeMapFace faces[6] = {
            CubeMapFace::PositiveX, CubeMapFace::NegativeX,
            CubeMapFace::PositiveY, CubeMapFace::NegativeY,
            CubeMapFace::PositiveZ, CubeMapFace::NegativeZ,
        };
        for (CubeMapFace f : faces) cube->SetData(f, &col, 1);
        return cube;
    }

    template <typename FxT>
    void ConfigurePbrLike(FxT& fx, Texture2D& tex)
    {
        fx.setTextureProperty(&tex);
        fx.setNormalMapProperty(nullptr);
        fx.setMetallicFactorProperty(0.0f);
        fx.setRoughnessFactorProperty(1.0f);
        fx.setAmbientLightColorProperty(Vector3(0.6f, 0.6f, 0.6f));
        fx.DirectionalLight0.setEnabledProperty(true);
        fx.DirectionalLight0.setDirectionProperty(Vector3(0.0f, 0.0f, -1.0f));
        fx.DirectionalLight0.setDiffuseColorProperty(Vector3(1.0f, 1.0f, 1.0f));
        fx.DirectionalLight1.setEnabledProperty(false);
        fx.DirectionalLight2.setEnabledProperty(false);
        fx.setWorldProperty(Matrix::getIdentityProperty());
        fx.setViewProperty(Matrix::getIdentityProperty());
        fx.setProjectionProperty(Matrix::getIdentityProperty());
    }

    // --- reference: colored3d, a family whose flip was never in question -----
    int RenderReference(GraphicsDevice& dev, RenderTarget2D& rt)
    {
        BeginRT(dev, rt);
        const GpuVPC verts[6] = {
            { kLo, kHi, 0.0f, 255, 255, 255, 255 },
            { kLo, kLo, 0.0f, 255, 255, 255, 255 },
            { kHi, kLo, 0.0f, 255, 255, 255, 255 },
            { kLo, kHi, 0.0f, 255, 255, 255, 255 },
            { kHi, kLo, 0.0f, 255, 255, 255, 255 },
            { kHi, kHi, 0.0f, 255, 255, 255, 255 },
        };
        VertexBuffer vb(dev, 6);
        vb.SetDataRaw(verts, 6, static_cast<int>(sizeof(GpuVPC)));

        BasicEffect fx(dev);
        fx.setWorldProperty(Matrix::getIdentityProperty());
        fx.setViewProperty(Matrix::getIdentityProperty());
        fx.setProjectionProperty(Matrix::getIdentityProperty());
        fx.VertexColorEnabled = true;
        fx.Apply();

        dev.SetVertexBuffer(&vb);
        dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        return LitQuadrant(dev, rt);
    }

    void Compare(const char* family, int got, int reference)
    {
        const bool ok = (got >= 0 && got == reference);
        std::printf("[%s] RT-Orientation/%s: lit quadrant = %s (reference colored3d = %s)\n",
                    ok ? "PASS" : "FAIL", family, QuadrantName(got), QuadrantName(reference));
        if (!ok)
        {
            if (got < 0)
                std::printf("       no single lit quadrant — nothing drawn, or the quad "
                            "straddles quadrants; orientation undetermined\n");
            else
                std::printf("       family disagrees with the reference on a RENDER TARGET: "
                            "the RT path is flipped relative to the backbuffer path\n");
            ++fail_;
        }
        else ++pass_;
        std::fflush(stdout);
    }

    void TestEnvMap(GraphicsDevice& dev, RenderTarget2D& rt, int ref)
    {
        BeginRT(dev, rt);
        const Color kWhite(255, 255, 255, 255);
        Texture2D tex(dev, 1, 1);
        tex.SetData(&kWhite, 1);
        auto cube = MakeSolidCube(dev, Color(0, 0, 0, 255));

        EnvironmentMapEffect fx(dev);
        fx.setTextureProperty(&tex);
        fx.setEnvironmentMapProperty(cube.get());
        fx.setEnvironmentMapAmountProperty(0.0f);
        fx.setEnvironmentMapSpecularProperty(Vector3::Zero);
        fx.setEmissiveColorProperty(Vector3(1.0f, 1.0f, 1.0f));
        fx.setWorldProperty(Matrix::getIdentityProperty());
        fx.setViewProperty(Matrix::getIdentityProperty());
        fx.setProjectionProperty(Matrix::getIdentityProperty());
        fx.Apply();

        const Vector3 n(0.0f, 0.0f, 1.0f);
        const VertexPositionNormalTexture quad[6] = {
            { Vector3(kLo, kHi, 0.0f), n, Vector2(0.0f, 0.0f) },
            { Vector3(kLo, kLo, 0.0f), n, Vector2(0.0f, 1.0f) },
            { Vector3(kHi, kLo, 0.0f), n, Vector2(1.0f, 1.0f) },
            { Vector3(kLo, kHi, 0.0f), n, Vector2(0.0f, 0.0f) },
            { Vector3(kHi, kLo, 0.0f), n, Vector2(1.0f, 1.0f) },
            { Vector3(kHi, kHi, 0.0f), n, Vector2(1.0f, 0.0f) },
        };
        dev.DrawUserPrimitives(PrimitiveType::TriangleList, quad, 0, 2);
        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        Compare("env_map3d", LitQuadrant(dev, rt), ref);
    }

    void TestPbr(GraphicsDevice& dev, RenderTarget2D& rt, int ref)
    {
        BeginRT(dev, rt);
        const Color kWhite(255, 255, 255, 255);
        Texture2D tex(dev, 1, 1);
        tex.SetData(&kWhite, 1);

        PbrEffect fx(dev);
        ConfigurePbrLike(fx, tex);
        fx.Apply();

        auto verts = MakeQuadIn<PbrGpuVertex>(kLo, kLo, kHi, kHi);
        VertexBuffer vb(dev, static_cast<int>(verts.size()));
        vb.SetDataRaw(verts.data(), static_cast<int>(verts.size()),
                      static_cast<int>(sizeof(PbrGpuVertex)));
        dev.SetVertexBuffer(&vb);
        dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        Compare("pbr3d", LitQuadrant(dev, rt), ref);
    }

    void TestSkinnedPbr(GraphicsDevice& dev, RenderTarget2D& rt, int ref)
    {
        BeginRT(dev, rt);
        const Color kWhite(255, 255, 255, 255);
        Texture2D tex(dev, 1, 1);
        tex.SetData(&kWhite, 1);

        SkinnedPbrEffect fx(dev);
        ConfigurePbrLike(fx, tex);
        std::vector<Matrix> bones = { Matrix::getIdentityProperty() };
        fx.SetBoneTransforms(bones);
        fx.setWeightsPerVertexProperty(1);
        fx.Apply();

        auto verts = MakeQuadIn<SkinnedPbrGpuVertex>(kLo, kLo, kHi, kHi);
        for (auto& v : verts) { v.w0 = 1.0f; v.w1 = v.w2 = v.w3 = 0.0f; v.i0 = v.i1 = v.i2 = v.i3 = 0; }
        VertexBuffer vb(dev, static_cast<int>(verts.size()));
        vb.SetDataRaw(verts.data(), static_cast<int>(verts.size()),
                      static_cast<int>(sizeof(SkinnedPbrGpuVertex)));
        dev.SetVertexBuffer(&vb);
        dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        Compare("pbr3d_skinned", LitQuadrant(dev, rt), ref);
    }

    void TestInstanced(GraphicsDevice& dev, RenderTarget2D& rt, int ref)
    {
        BeginRT(dev, rt);
        const float h = 0.35f;
        const GpuVPC quadVerts[4] = {
            { -h,  h, 0.0f, 255, 255, 255, 255 },
            { -h, -h, 0.0f, 255, 255, 255, 255 },
            {  h, -h, 0.0f, 255, 255, 255, 255 },
            {  h,  h, 0.0f, 255, 255, 255, 255 },
        };
        VertexBuffer perVertVb(dev, 4);
        perVertVb.SetDataRaw(quadVerts, 4, static_cast<int>(sizeof(GpuVPC)));

        const std::uint16_t indices[6] = { 0, 1, 2,  0, 2, 3 };
        IndexBuffer ib(dev, 6);
        ib.SetData(indices, 6);

        InstMat4 inst{};
        inst.m[0] = inst.m[5] = inst.m[10] = inst.m[15] = 1.0f;
        inst.m[12] = 0.55f;
        inst.m[13] = 0.55f;
        VertexBuffer instVb(dev, 1);
        instVb.SetDataRaw(&inst, 1, static_cast<int>(sizeof(InstMat4)));

        BasicEffect fx(dev);
        fx.setWorldProperty(Matrix::getIdentityProperty());
        fx.setViewProperty(Matrix::getIdentityProperty());
        fx.setProjectionProperty(Matrix::getIdentityProperty());
        fx.setDiffuseColorProperty(Vector3(1.0f, 1.0f, 1.0f));
        fx.Apply();

        dev.SetVertexBuffer(&perVertVb);
        std::vector<VertexBufferBinding> bindings = {
            VertexBufferBinding(&perVertVb, 0, 0),
            VertexBufferBinding(&instVb,    0, 1),
        };
        dev.SetVertexBuffers(bindings);
        dev.SetIndexBuffer(&ib);
        dev.DrawInstancedPrimitives(PrimitiveType::TriangleList, 0, 0, 4, 0, 2, 1);
        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        Compare("instanced3d", LitQuadrant(dev, rt), ref);
    }

protected:
    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;

        auto& dev = getGraphicsDeviceProperty();
        sb_ = std::make_unique<SpriteBatch>(dev);
        RenderTarget2D rt(dev, kRT, kRT, false, SurfaceFormat::Color,
                          DepthFormat::None, 0, RenderTargetUsage::DiscardContents);

        const int ref = RenderReference(dev, rt);
        if (ref < 0)
        {
            std::printf("[FAIL] RT-Orientation: colored3d reference produced no single lit "
                        "quadrant — the RT harness itself is broken, results below are void\n");
            ++fail_;
        }
        else
        {
            std::printf("[INFO] RT-Orientation: colored3d reference lights %s on a render target\n",
                        QuadrantName(ref));
        }

        TestEnvMap(dev, rt, ref);
        TestPbr(dev, rt, ref);
        TestSkinnedPbr(dev, rt, ref);
        TestInstanced(dev, rt, ref);

        std::printf("VulkanOrientationRenderTarget: %d passed, %d failed\n", pass_, fail_);
        Exit();
    }

public:
    int getResult() const { return fail_ == 0 ? 0 : 1; }
};

int main()
{
    VulkanOrientationRenderTargetTest game;
    game.Run();
    return game.getResult();
}
