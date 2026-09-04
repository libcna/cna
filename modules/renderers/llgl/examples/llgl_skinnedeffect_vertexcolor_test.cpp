// SPDX-License-Identifier: MS-PL
// plans/plan_llgl.md LLGL-37: SkinnedEffect.VertexColorEnabled (CNAEXT extension property, CNB-66/67) --
// proves the stride-56 skinned+Color vertex layout's colour attribute (skinned3d_color.vert/frag.glsl,
// selected instead of skinned3d.vert/frag.glsl only when the bound layout carries one -- see
// AcquirePrimitiveSkinnedVertexShader()'s own doc comment) is actually read and correctly gated by
// the emissiveColorPad.w uniform, multiplied into the FINAL combined diffuse+specular output (not
// just diffuse alone -- see skinned3d_color.frag.glsl's own header comment for why the multiply's
// position matters). Adapted from examples/vulkan_skinnedeffect_vertexcolor_test.cpp's own
// analytically-derived technique (checks (a)/(b)/(c) only; that file's own (d)/(e) are a Vulkan-
// specific dynamic-blend-factor finding unrelated to VertexColorEnabled itself).
//
// Uses a straight-on camera (eye=(0,0,3) looking at the origin, flat quad normal (0,0,1)) with
// DirectionalLight0's direction set to (0,0,-1) so, at the exact backbuffer centre pixel, N=L=V=
// (0,0,1) and NdotL0=1 -- an analytically exact case. SpecularColor=(0,0,0) and
// AmbientLightColor=(0,0,0) zero out every other term, isolating VertexColorEnabled's own multiply.
// weightsPerVertex=1 with a single identity bone isolates skinning from the check.
//
// Exit code 0 = PASS, 1 = FAIL.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/MathHelper.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
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

namespace
{
    // Stride-56: matches skinned3d_color.vert.glsl's attribute layout exactly (the stride-52
    // SkinnedGpuVertex layout with a per-vertex Color appended at offset 52, CNB-66/67).
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

    constexpr int kSize = 64;

    bool closeTo(int a, int b, int tol) { return std::abs(a - b) <= tol; }
    bool matches(const Color& c, const Color& expected, int tol = 8)
    {
        return closeTo(c.getRProperty(), expected.getRProperty(), tol)
            && closeTo(c.getGProperty(), expected.getGProperty(), tol)
            && closeTo(c.getBProperty(), expected.getBProperty(), tol);
    }
}

class LlglSkinnedEffectVertexColorTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    int  pass_ = 0;
    int  fail_ = 0;

    void check(bool ok, const char* label, const Color& got, const char* expected)
    {
        if (ok) { std::printf("[PASS] %s: got=(%d,%d,%d)\n", label,
                got.getRProperty(), got.getGProperty(), got.getBProperty()); ++pass_; }
        else { std::printf("[FAIL] %s: got=(%d,%d,%d) expected %s\n", label,
                got.getRProperty(), got.getGProperty(), got.getBProperty(), expected); ++fail_; }
    }

    Color readCenter(GraphicsDevice& dev)
    {
        const Rectangle reg(kSize / 2, kSize / 2, 1, 1);
        Color px(0, 0, 0, 0);
        dev.GetBackBufferData(&reg, &px, 0, 1);
        return px;
    }

    Color renderWith(GraphicsDevice& dev, Texture2D& tex, bool vertexColorEnabled,
                      std::uint8_t vr, std::uint8_t vg, std::uint8_t vb, std::uint8_t va)
    {
        SkinnedEffect fx(dev);
        fx.setTextureProperty(&tex);
        fx.setAmbientLightColorProperty(Vector3::Zero);
        fx.setDiffuseColorProperty(Vector3(0.8f, 0.6f, 0.4f));
        fx.setEmissiveColorProperty(Vector3::Zero);
        fx.setSpecularColorProperty(Vector3::Zero);
        fx.setSpecularPowerProperty(32.0f);
        fx.VertexColorEnabled = vertexColorEnabled;

        fx.DirectionalLight0.setEnabledProperty(true);
        fx.DirectionalLight0.setDirectionProperty(Vector3(0.0f, 0.0f, -1.0f));
        fx.DirectionalLight0.setDiffuseColorProperty(Vector3(0.5f, 0.5f, 0.5f));
        fx.DirectionalLight0.setSpecularColorProperty(Vector3::Zero);
        fx.DirectionalLight1.setEnabledProperty(false);
        fx.DirectionalLight2.setEnabledProperty(false);

        fx.setWorldProperty(Matrix::getIdentityProperty());
        fx.setViewProperty(Matrix::CreateLookAt(Vector3(0.0f, 0.0f, 3.0f), Vector3::Zero, Vector3(0.0f, 1.0f, 0.0f)));
        fx.setProjectionProperty(Matrix::CreatePerspectiveFieldOfView(MathHelper::PiOver4, 1.0f, 0.1f, 100.0f));

        std::vector<Matrix> bones = { Matrix::getIdentityProperty() };
        fx.SetBoneTransforms(bones);
        fx.setWeightsPerVertexProperty(1);

        // Single large quad (covers the backbuffer centre regardless of FOV/aspect rounding).
        const SkinnedColorGpuVertex verts[6] = {
            { -4,  4, 0, 0,0,1, 0,0, 1,0,0,0, 0,0,0,0, vr,vg,vb,va },
            { -4, -4, 0, 0,0,1, 0,1, 1,0,0,0, 0,0,0,0, vr,vg,vb,va },
            {  4, -4, 0, 0,0,1, 1,1, 1,0,0,0, 0,0,0,0, vr,vg,vb,va },
            { -4,  4, 0, 0,0,1, 0,0, 1,0,0,0, 0,0,0,0, vr,vg,vb,va },
            {  4, -4, 0, 0,0,1, 1,1, 1,0,0,0, 0,0,0,0, vr,vg,vb,va },
            {  4,  4, 0, 0,0,1, 1,0, 1,0,0,0, 0,0,0,0, vr,vg,vb,va },
        };
        VertexBuffer vb2(dev, 6);
        vb2.SetDataRaw(verts, 6, static_cast<int>(sizeof(SkinnedColorGpuVertex)));

        dev.Clear(Color(0, 0, 0, 255));
        dev.setBlendStateProperty(BlendState::Opaque);
        dev.setRasterizerStateProperty(RasterizerState::CullNone);
        dev.SetVertexBuffer(&vb2);
        fx.Apply();
        dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
        return readCenter(dev);
    }

protected:
    void Draw(const GameTime&) override
    {
        auto& dev = getGraphicsDeviceProperty();

        Texture2D whiteTex(dev, 1, 1);
        const Color white(255, 255, 255, 255);
        whiteTex.SetData(&white, 1);

        // litRGB = (ambient(0) + light0Diffuse*NdotL0(1)) * diffuseColor = (0.5,0.5,0.5)*(0.8,0.6,0.4)
        //        = (0.4, 0.3, 0.2); no specular (SpecularColor=0); tex=(1,1,1,1).

        // (a) VertexColorEnabled=false: per-vertex (200,100,50) must be ignored entirely.
        //   FragColor.rgb = litRGB*tex.rgb = (0.4,0.3,0.2) -> round(*255) = (102,77,51)
        const Color a = renderWith(dev, whiteTex, false, 200, 100, 50, 255);
        check(matches(a, Color(102, 77, 51, 255)),
              "(a) VertexColorEnabled=false: vertex color ignored, litRGB*tex only", a, "(102,77,51)");

        // (b) VertexColorEnabled=true, distinctive vertex color (200,100,50,255):
        //   FragColor.rgb = litRGB*tex.rgb*vc.rgb = (0.4,0.3,0.2)*(200,100,50)/255
        //                  = (0.313725, 0.117647, 0.039216) -> round(*255) = (80,30,10)
        const Color b = renderWith(dev, whiteTex, true, 200, 100, 50, 255);
        check(matches(b, Color(80, 30, 10, 255)),
              "(b) VertexColorEnabled=true: pixel == litRGB*tex*VertexColor (component-wise)", b, "(80,30,10)");
        check(!matches(b, a), "(b) differs from (a) -- VertexColorEnabled genuinely gates the multiply",
              b, "!= (102,77,51)");

        // (c) VertexColorEnabled=true, pure black vertex color: must zero the FINAL combined
        // output exactly, independent of the lighting math.
        const Color c = renderWith(dev, whiteTex, true, 0, 0, 0, 255);
        check(matches(c, Color(0, 0, 0, 255), 6),
              "(c) VertexColorEnabled=true, black vertex color: pixel == (0,0,0)", c, "(0,0,0)");

        std::printf("\nResult: %d/%d PASS\n", pass_, pass_ + fail_);
        Exit();
    }

public:
    LlglSkinnedEffectVertexColorTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kSize);
        gdm_->setPreferredBackBufferHeightProperty(kSize);
    }

    int getResult() const { return fail_ == 0 ? 0 : 1; }
};

int main()
{
    LlglSkinnedEffectVertexColorTest game;
    game.Run();
    return game.getResult();
}
