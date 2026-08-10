// SPDX-License-Identifier: MS-PL
// CNB-67 (Phase 13C), Bgfx port: pixel test for SkinnedEffect's NOXNA VertexColorEnabled property
// (see SkinnedEffect.hpp) -- proves the stride-56 skinned+Color vertex layout's a_color0
// attribute (BgfxRenderer's MakeBgfxLayout stride==56 case) is actually read by both
// vs_skinned3d.sc/fs_skinned3d.sc (per-pixel-lit) and vs_skinned3d_vertexlit.sc/
// fs_skinned3d_vertexlit.sc (vertex-lit, real XNA's own PreferPerPixelLighting=false default) and
// correctly gated by the shared u_vertexColorEnabled3D uniform.
//
// See examples/easygl_skinnedeffect_vertexcolor_test.cpp for the EasyGL-side sibling and its full
// rationale for why a pure-black per-vertex color is used instead of trying to analytically
// reproduce EnableDefaultLighting()'s Phong math: litRGB*texColor.rgb*vc.rgb with vc.rgb=(0,0,0)
// must zero out to (0,0,0,alpha) regardless of what the lit/textured color would otherwise be,
// independent of the lighting math -- an unambiguous, lighting-independent check that
// VertexColorEnabled actually gates the shader path, not just that the field exists.
//
// Per Task 364/884's finding: Bgfx's default RasterizerState cull state culls the standard NDC
// quad winding used throughout this pixel-test family unless RasterizerState::CullNone is set
// explicitly.
//
// Exit code 0 = PASS, 1 = FAIL.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SkinnedEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

static constexpr int kSize = 64;

// Stride-56 GPU-compact skinned+Color vertex: matches BgfxRenderer's MakeBgfxLayout
// stride==56 case (Color appended after BlendIndices, CNB-67).
struct SkinnedColorGpuVertex
{
    float px, py, pz;
    float nx, ny, nz;
    float u, v;
    float w0, w1, w2, w3;
    std::uint8_t i0, i1, i2, i3;
    std::uint8_t r, g, b, a;
};
static_assert(sizeof(SkinnedColorGpuVertex) == 56, "skinned+color vertex must be 56 bytes");

class BgfxSkinnedEffectVertexColorTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    Texture2D tex_;
    int pass_ = 0, fail_ = 0;

    void check(bool ok, const char* label, const Color& got, const char* expected)
    {
        if (ok)
        {
            std::printf("[PASS] %s: got=(%d,%d,%d)\n", label,
                got.getRProperty(), got.getGProperty(), got.getBProperty());
            ++pass_;
        }
        else
        {
            std::printf("[FAIL] %s: got=(%d,%d,%d) expected %s\n", label,
                got.getRProperty(), got.getGProperty(), got.getBProperty(), expected);
            ++fail_;
        }
    }

    static bool closeTo(int a, int b, int tol) { return std::abs(a - b) <= tol; }

    static bool matches(const Color& c, const Color& expected, int tol)
    {
        return closeTo(c.getRProperty(), expected.getRProperty(), tol)
            && closeTo(c.getGProperty(), expected.getGProperty(), tol)
            && closeTo(c.getBProperty(), expected.getBProperty(), tol);
    }

    // Bgfx's readback only reliably reflects a single fresh GetBackBufferData() call per
    // rendered frame (Task 406 finding) -- each check point gets its own full clear+draw+read
    // pass rather than sharing one frame across 2 reads.
    Color renderAndRead(GraphicsDevice& device, SkinnedEffect& fx, VertexBuffer& vb,
                         int startVertex, const Rectangle& reg)
    {
        Color got(0, 0, 0, 0);
        for (int i = 0; i < 20; ++i)
        {
            device.Clear(Color(0, 255, 0, 255));
            device.setBlendStateProperty(BlendState::Opaque);
            // See Task 364/884 finding: Bgfx's default cull state culls this quad's winding.
            device.setRasterizerStateProperty(RasterizerState::CullNone);
            fx.Apply();
            device.SetVertexBuffer(&vb);
            device.DrawPrimitives(PrimitiveType::TriangleList, startVertex, 2);
            device.GetBackBufferData(&reg, &got, 0, 1);
            if (got.getRProperty() != 0 || got.getGProperty() != 0 || got.getBProperty() != 0)
                break; // skip blank/black frames
        }
        return got;
    }

protected:
    void Initialize() override
    {
        Game::Initialize();
        auto& device = getGraphicsDeviceProperty();
        const std::vector<uint8_t> px = { 255, 0, 0, 255 };
        tex_ = Texture2D::CreateFromPixels(device, 1, 1, px);
    }

    void Draw(const GameTime&) override
    {
        auto& device = getGraphicsDeviceProperty();
        const auto& vp = device.getViewportProperty();
        const int W = vp.getWidthProperty();
        const int H = vp.getHeightProperty();

        SkinnedEffect fx(device);
        fx.setTextureProperty(&tex_);
        fx.setWorldProperty(Matrix::getIdentityProperty());
        fx.setViewProperty(Matrix::getIdentityProperty());
        fx.setProjectionProperty(Matrix::getIdentityProperty());
        std::vector<Matrix> bones = { Matrix::getIdentityProperty() };
        fx.SetBoneTransforms(bones);
        fx.setWeightsPerVertexProperty(1);
        fx.EnableDefaultLighting();

        std::vector<SkinnedColorGpuVertex> verts;
        // Quad A (left, x in [-1,-0.5]): VertexColorEnabled=false -- per-vertex black must be
        // ignored, rendering the same red-dominant lit/textured result as
        // easygl_skinnedeffect_golden_test.cpp's / bgfx_skinnedeffect_identity_bones_test.cpp's
        // own identity-bone quad.
        auto appendQuad = [&](float xMin, float xMax, std::uint8_t r, std::uint8_t g,
                               std::uint8_t b, std::uint8_t a)
        {
            const SkinnedColorGpuVertex tl{ xMin,  1, 0,  0,0,1,  0,0,  1,0,0,0,  0,0,0,0,  r,g,b,a };
            const SkinnedColorGpuVertex bl{ xMin, -1, 0,  0,0,1,  0,1,  1,0,0,0,  0,0,0,0,  r,g,b,a };
            const SkinnedColorGpuVertex br{ xMax, -1, 0,  0,0,1,  1,1,  1,0,0,0,  0,0,0,0,  r,g,b,a };
            const SkinnedColorGpuVertex tr{ xMax,  1, 0,  0,0,1,  1,0,  1,0,0,0,  0,0,0,0,  r,g,b,a };
            verts.push_back(tl); verts.push_back(bl); verts.push_back(br);
            verts.push_back(tl); verts.push_back(br); verts.push_back(tr);
        };
        appendQuad(-1.0f, -0.5f, 0, 0, 0, 255);
        // Quad B (right, x in [0.5,1]): VertexColorEnabled=true -- per-vertex black must zero the
        // lit/textured result to pure black, regardless of the lighting math.
        appendQuad(0.5f, 1.0f, 0, 0, 0, 255);
        // Quads C/D reuse A/B's exact geometry+color at a different Y band, distinguished only by
        // PreferPerPixelLighting -- see below.
        appendQuad(-1.0f, -0.5f, 0, 0, 0, 255);
        appendQuad(0.5f, 1.0f, 0, 0, 0, 255);

        VertexBuffer vb(device, static_cast<int>(verts.size()));
        vb.SetDataRaw(verts.data(), static_cast<int>(verts.size()),
                       static_cast<int>(sizeof(SkinnedColorGpuVertex)));

        const Rectangle regA(W / 8,       H / 2, 1, 1); // quad A: NDC x ~ -0.75
        const Rectangle regB((W * 7) / 8, H / 2, 1, 1); // quad B: NDC x ~ +0.75

        // Quads A/B: real XNA's SkinnedEffect default (PreferPerPixelLighting=false) --
        // dispatches to vs_skinned3d_vertexlit.sc/fs_skinned3d_vertexlit.sc.
        fx.VertexColorEnabled = false;
        const Color quadA = renderAndRead(device, fx, vb, 0, regA);

        fx.VertexColorEnabled = true;
        const Color quadB = renderAndRead(device, fx, vb, 6, regB);

        // Cross-check against bgfx_skinnedeffect_identity_bones_test.cpp's own live-observed
        // "red > green, red > 50" shape for the identical identity-bone/red-texture/lit scenario
        // -- VertexColorEnabled=false must reproduce a red-dominant lit result unchanged.
        check(quadA.getRProperty() > quadA.getGProperty() && quadA.getRProperty() > 50,
              "(A) VertexColorEnabled=false -- black vertex color ignored, red-dominant lit result",
              quadA, "R > G, R > 50");
        // VertexColorEnabled=true with a pure-black per-vertex color must zero the result --
        // lighting-independent by construction (0 * anything = 0).
        check(matches(quadB, Color(0, 0, 0, 255), 10),
              "(B) VertexColorEnabled=true -- black vertex color zeroes the result", quadB, "(0,0,0)");

        // Quads C/D: PreferPerPixelLighting=true -- dispatches to vs_skinned3d.sc/
        // fs_skinned3d.sc instead, exercising that shader pair's own aColor/uVertexColorEnabled
        // wiring independently of the vertex-lit sibling above.
        fx.setPreferPerPixelLightingProperty(true);
        fx.VertexColorEnabled = false;
        const Color quadC = renderAndRead(device, fx, vb, 12, regA);

        fx.VertexColorEnabled = true;
        const Color quadD = renderAndRead(device, fx, vb, 18, regB);

        check(quadC.getRProperty() > quadC.getGProperty() && quadC.getRProperty() > 50,
              "(C) PreferPerPixelLighting=true, VertexColorEnabled=false -- black vertex color ignored",
              quadC, "R > G, R > 50");
        check(matches(quadD, Color(0, 0, 0, 255), 10),
              "(D) PreferPerPixelLighting=true, VertexColorEnabled=true -- black vertex color zeroes the result",
              quadD, "(0,0,0)");

        std::printf("\nResult: %d/%d PASS\n", pass_, pass_ + fail_);
        Exit();
    }

public:
    BgfxSkinnedEffectVertexColorTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kSize);
        gdm_->setPreferredBackBufferHeightProperty(kSize);
    }

    int getResult() const { return fail_ == 0 ? 0 : 1; }
};

int main()
{
    BgfxSkinnedEffectVertexColorTest game;
    game.Run();
    return game.getResult();
}
