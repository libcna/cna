// SPDX-License-Identifier: MS-PL
// REMED-GFX-011: per-family NDC orientation regression for the four Vulkan effect families that
// were missing the Y-flip (env_map3d, pbr3d, pbr3d_skinned, instanced3d).
//
// Methodology — see vulkan_orientation_calibration_test.cpp, which pins the convention this test
// asserts against a family whose flip is already correct:
//   * XNA clip space is +Y up, so geometry at y>0 belongs in the UPPER half of the image.
//   * GetBackBufferData returns rows top-first, so the upper half is the LOW pixel-row range.
//
// Each family draws one bright quad confined to the +X/+Y quadrant of clip space against a black
// background, then samples all four quadrant centres. The scene has no symmetry on either axis,
// which is what makes a mirror detectable at all: every pre-existing Vulkan test for these
// families draws a centred, symmetric, full-screen quad and samples only the centre pixel, so a
// vertical mirror moves nothing it looks at. That blind spot is why this bug survived.
//
// Expected for every family:
//   TR bright, TL/BR/BL background.
//
// Diagnostic value of each failure mode:
//   BR bright -> vertical mirror   (the missing Y-flip, and equally a double flip)
//   TL bright -> horizontal mirror
//   BL bright -> both
//
// Exit code 0 = PASS, 1 = FAIL.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/MathHelper.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/CubeMapFace.hpp"
#include "Microsoft/Xna/Framework/Graphics/EnvironmentMapEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/PbrEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SkinnedPbrEffect.hpp"
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
    // Clip-space extent of the test quad: the +X/+Y quadrant, asymmetric on both axes.
    constexpr float kLo = 0.2f;
    constexpr float kHi = 0.9f;

    struct PbrGpuVertex
    {
        float px, py, pz;
        float nx, ny, nz;
        float tx, ty, tz, tw;
        float u, v;
    };
    static_assert(sizeof(PbrGpuVertex) == 48, "PBR vertex must be 48 bytes");

    struct SkinnedPbrGpuVertex
    {
        float px, py, pz;
        float nx, ny, nz;
        float tx, ty, tz, tw;
        float u, v;
        float w0, w1, w2, w3;
        std::uint8_t i0, i1, i2, i3;
    };
    static_assert(sizeof(SkinnedPbrGpuVertex) == 68, "skinned PBR vertex must be 68 bytes");

    struct GpuVPC { float x, y, z; std::uint8_t r, g, b, a; };
    static_assert(sizeof(GpuVPC) == 16);

    struct InstMat4 { float m[16]; };
    static_assert(sizeof(InstMat4) == 64);

    // Six vertices (two triangles) spanning [x0,x1] x [y0,y1] with a flat +Z normal.
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
}

class VulkanOrientationEffectsTest : public Game
{
    bool done_ = false;
    int  pass_ = 0;
    int  fail_ = 0;

    // Perceptual-ish luminance; the test only needs "lit object" vs "black background".
    static int Luma(const Color& c)
    {
        return (c.getRProperty() * 30 + c.getGProperty() * 59 + c.getBProperty() * 11) / 100;
    }

    Color ReadAt(GraphicsDevice& dev, int x, int y)
    {
        const Rectangle reg(x, y, 1, 1);
        Color px(0, 0, 0, 0);
        dev.GetBackBufferData(&reg, &px, 0, 1);
        return px;
    }

    // Samples the four quadrant centres and asserts the object is in the top-right only.
    void CheckQuadrants(GraphicsDevice& dev, const char* family)
    {
        const auto& vp = dev.getViewportProperty();
        const int W = vp.getWidthProperty();
        const int H = vp.getHeightProperty();

        const Color tr = ReadAt(dev, 3 * W / 4,     H / 4);
        const Color tl = ReadAt(dev,     W / 4,     H / 4);
        const Color br = ReadAt(dev, 3 * W / 4, 3 * H / 4);
        const Color bl = ReadAt(dev,     W / 4, 3 * H / 4);

        constexpr int kLit = 40;   // object must be at least this bright
        constexpr int kBg  = 15;   // background must be at most this bright

        const bool ok = Luma(tr) >= kLit && Luma(tl) <= kBg
                     && Luma(br) <= kBg  && Luma(bl) <= kBg;

        std::printf("[%s] Orientation/%s: TR=(%d,%d,%d) TL=(%d,%d,%d) BR=(%d,%d,%d) BL=(%d,%d,%d)\n",
                    ok ? "PASS" : "FAIL", family,
                    tr.getRProperty(), tr.getGProperty(), tr.getBProperty(),
                    tl.getRProperty(), tl.getGProperty(), tl.getBProperty(),
                    br.getRProperty(), br.getGProperty(), br.getBProperty(),
                    bl.getRProperty(), bl.getGProperty(), bl.getBProperty());

        std::fflush(stdout);   // keep per-family evidence if a later family aborts
        if (ok) { ++pass_; return; }
        ++fail_;
        std::printf("       expected the quad at XNA +X/+Y to land TOP-RIGHT only\n");
        if (Luma(br) >= kLit)
            std::printf("       -> object is BOTTOM-RIGHT: VERTICAL MIRROR "
                        "(missing or doubled gl_Position.y flip in %s)\n", family);
        if (Luma(tl) >= kLit)
            std::printf("       -> object is TOP-LEFT: HORIZONTAL MIRROR in %s\n", family);
        if (Luma(bl) >= kLit)
            std::printf("       -> object is BOTTOM-LEFT: BOTH AXES MIRRORED in %s\n", family);
        if (Luma(tr) < kLit && Luma(br) < kLit && Luma(tl) < kLit && Luma(bl) < kLit)
            std::printf("       -> nothing drawn anywhere; orientation is undetermined, "
                        "not proven correct\n");
    }

    void ResetScene(GraphicsDevice& dev)
    {
        dev.Clear(Color(0, 0, 0, 255));
        dev.SetDepthTestEnabled(false);
        dev.setBlendStateProperty(BlendState::Opaque);
        // REMED-GFX-052: this winding is back-facing under CNA's real default RasterizerState,
        // which would render an all-black frame and make the check vacuous.
        dev.setRasterizerStateProperty(RasterizerState::CullNone);
    }

    static void Identity(Effect& fx) = delete;   // effects don't share a matrix base; set per-type

    std::unique_ptr<TextureCube> MakeSolidCube(GraphicsDevice& dev, Color col)
    {
        auto cube = std::make_unique<TextureCube>(dev, 1, false, SurfaceFormat::Color);
        const CubeMapFace faces[6] = {
            CubeMapFace::PositiveX, CubeMapFace::NegativeX,
            CubeMapFace::PositiveY, CubeMapFace::NegativeY,
            CubeMapFace::PositiveZ, CubeMapFace::NegativeZ,
        };
        for (CubeMapFace face : faces)
            cube->SetData(face, &col, 1);
        return cube;
    }

    // --- env_map3d ---------------------------------------------------------
    void TestEnvMap(GraphicsDevice& dev)
    {
        ResetScene(dev);

        const Color kWhite(255, 255, 255, 255);
        Texture2D tex(dev, 1, 1);
        tex.SetData(&kWhite, 1);
        auto cube = MakeSolidCube(dev, Color(0, 0, 0, 255));

        EnvironmentMapEffect fx(dev);
        fx.setTextureProperty(&tex);
        fx.setEnvironmentMapProperty(cube.get());
        fx.setEnvironmentMapAmountProperty(0.0f);
        fx.setEnvironmentMapSpecularProperty(Vector3::Zero);
        // No lights; emissive alone makes the quad bright, keeping the check independent of
        // the lighting semantics REMED-GFX-007/008 cover.
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

        CheckQuadrants(dev, "env_map3d/EnvironmentMapEffect");
    }

    // --- pbr3d -------------------------------------------------------------
    void TestPbr(GraphicsDevice& dev)
    {
        ResetScene(dev);

        const Color kWhite(255, 255, 255, 255);
        Texture2D tex(dev, 1, 1);
        tex.SetData(&kWhite, 1);

        PbrEffect fx(dev);
        fx.setTextureProperty(&tex);
        fx.setNormalMapProperty(nullptr);
        fx.setMetallicFactorProperty(0.0f);
        fx.setRoughnessFactorProperty(1.0f);
        // Head-on light plus ambient: the quad is lit regardless of the normal-transform
        // questions REMED-GFX-006 covers.
        fx.setAmbientLightColorProperty(Vector3(0.6f, 0.6f, 0.6f));
        fx.DirectionalLight0.setEnabledProperty(true);
        fx.DirectionalLight0.setDirectionProperty(Vector3(0.0f, 0.0f, -1.0f));
        fx.DirectionalLight0.setDiffuseColorProperty(Vector3(1.0f, 1.0f, 1.0f));
        fx.DirectionalLight1.setEnabledProperty(false);
        fx.DirectionalLight2.setEnabledProperty(false);
        fx.setWorldProperty(Matrix::getIdentityProperty());
        fx.setViewProperty(Matrix::getIdentityProperty());
        fx.setProjectionProperty(Matrix::getIdentityProperty());
        fx.Apply();

        auto verts = MakeQuadIn<PbrGpuVertex>(kLo, kLo, kHi, kHi);
        VertexBuffer vb(dev, static_cast<int>(verts.size()));
        vb.SetDataRaw(verts.data(), static_cast<int>(verts.size()),
                      static_cast<int>(sizeof(PbrGpuVertex)));
        dev.SetVertexBuffer(&vb);
        dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);

        CheckQuadrants(dev, "pbr3d/PbrEffect");
    }

    // --- pbr3d_skinned -----------------------------------------------------
    void TestSkinnedPbr(GraphicsDevice& dev)
    {
        ResetScene(dev);

        const Color kWhite(255, 255, 255, 255);
        Texture2D tex(dev, 1, 1);
        tex.SetData(&kWhite, 1);

        SkinnedPbrEffect fx(dev);
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

        // Identity bind pose: skinning is a no-op, so any orientation difference from the
        // unskinned PbrEffect above is the shader's own clip-space convention, nothing else.
        std::vector<Matrix> bones = { Matrix::getIdentityProperty() };
        fx.SetBoneTransforms(bones);
        fx.setWeightsPerVertexProperty(1);
        fx.Apply();

        auto verts = MakeQuadIn<SkinnedPbrGpuVertex>(kLo, kLo, kHi, kHi);
        for (auto& v : verts)
        {
            v.w0 = 1.0f; v.w1 = v.w2 = v.w3 = 0.0f;
            v.i0 = v.i1 = v.i2 = v.i3 = 0;
        }
        VertexBuffer vb(dev, static_cast<int>(verts.size()));
        vb.SetDataRaw(verts.data(), static_cast<int>(verts.size()),
                      static_cast<int>(sizeof(SkinnedPbrGpuVertex)));
        dev.SetVertexBuffer(&vb);
        dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);

        CheckQuadrants(dev, "pbr3d_skinned/SkinnedPbrEffect");
    }

    // --- instanced3d -------------------------------------------------------
    void TestInstanced(GraphicsDevice& dev)
    {
        ResetScene(dev);

        // Quad centred on the origin; the per-instance world matrix is what pushes it into the
        // +X/+Y quadrant, so this also proves VP*World composes in the right order/orientation.
        const float h = 0.35f;
        const GpuVPC quadVerts[4] = {
            { -h,  h, 0.0f, 255, 255, 255, 255 },
            { -h, -h, 0.0f, 255, 255, 255, 255 },
            {  h, -h, 0.0f, 255, 255, 255, 255 },
            {  h,  h, 0.0f, 255, 255, 255, 255 },
        };
        VertexBuffer perVertVb(dev, 4);
        perVertVb.SetDataRaw(quadVerts, 4, static_cast<int>(sizeof(GpuVPC)));

        // DrawInstancedPrimitives is indexed-only.
        const std::uint16_t indices[6] = { 0, 1, 2,  0, 2, 3 };
        IndexBuffer ib(dev, 6);
        ib.SetData(indices, 6);

        InstMat4 inst{};
        inst.m[0] = inst.m[5] = inst.m[10] = inst.m[15] = 1.0f;
        inst.m[12] = 0.55f;   // +X
        inst.m[13] = 0.55f;   // +Y  -> upper-right in XNA terms
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

        CheckQuadrants(dev, "instanced3d/DrawInstancedPrimitives");
    }

protected:
    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;

        auto& dev = getGraphicsDeviceProperty();
        TestEnvMap(dev);
        TestPbr(dev);
        TestSkinnedPbr(dev);
        TestInstanced(dev);

        std::printf("VulkanOrientationEffects: %d passed, %d failed\n", pass_, fail_);
        Exit();
    }

public:
    int getResult() const { return fail_ == 0 ? 0 : 1; }
};

int main()
{
    VulkanOrientationEffectsTest game;
    game.Run();
    return game.getResult();
}
