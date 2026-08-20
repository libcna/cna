// SPDX-License-Identifier: MS-PL
// plans/plan_opengl2.md: pixel-exact proof for AlphaTestEffect, DualTextureEffect, BasicEffect
// lighting, and BasicEffect fog on the native OpenGL 2.1 graphics renderer. All draws use
// World=View=Projection=Identity (vertex XYZ is clip-space XYZ directly), matching this
// project's own established convention for this kind of test (see opengl2_3d_test.cpp).
//
// Check A -- AlphaTestEffect(CompareFunction::Greater, ReferenceAlpha=128) over a texture whose
//   alpha is 255 (opaque): the fragment survives and shows the texture color. Over a Black
//   background, a Green|alpha=32 texture: the fragment is discarded (alpha 32 <= 128), so the
//   background shows through unchanged -- proves the discard formula (GpuDrawParams::alphaTest)
//   actually reaches GL, not just "didn't throw".
// Check B -- DualTextureEffect's texture0*texture2*2 lightmap formula: a half-intensity
//   (128,0,0) base texture combined with a full-white texture2 must read back FULL red
//   (255,0,0), not half red -- only possible if the shader actually doubles base before
//   modulating (a naive base*tex2 would read back half red instead).
// Check C -- BasicEffect lighting: two identical unlit-texture quads (VertexPositionNormalTexture,
//   normal +Z, TextureEnabled=false so the shader's white fallback isolates the light response),
//   one lit by DirectionalLight0 pointing straight into the surface (N.L=1, near-white) and one
//   lit by a light pointing away from it (N.L<=0, ambient-only, dim) -- proves per-fragment
//   Lambertian lighting is real, not a pass-through.
// Check C2 -- the lighting normal matrix is a real inverse-transpose of World's upper-3x3, not
//   the raw upper-3x3: under World=Scale(3,1,1) with a 45-degree-tilted normal, the correct
//   transform gives N.(-L)~=0.95 (near-white); a raw-3x3 bug would give ~=0.32 (dim gray).
// Check D -- BasicEffect fog (object-space vertex Z, this renderer's documented convention --
//   see plans/plan_opengl2.md): FogStart=-0.8, FogEnd=0.8 (kept within the Identity-projection clip
//   range [-1,1] so the quad itself is not near/far-clipped). A quad at local Z=-0.8 reads back the fog color
//   exactly (fully fogged); a quad at local Z=0.8 reads back its own unfogged color exactly (fog
//   factor clamps to "no fog" there) -- a differential only a working per-fragment fog blend
//   produces.
//
// Exit code 0 = all checks PASS, 1 = any FAILs.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/AlphaTestEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BufferUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/CompareFunction.hpp"
#include "Microsoft/Xna/Framework/Graphics/DualTextureEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionNormalTexture.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionTexture.hpp"

#include "CNA/Internal/Renderers/OpenGL2/OpenGL2Renderer.hpp"

#include "common/PixelTestGame.hpp"

#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using namespace CNA::Internal::Renderers::OpenGL2;

namespace
{
    constexpr int kTotalFrames = 60;

    bool CloseTo(int a, int b, int tol) { return std::abs(a - b) <= tol; }
    bool Matches(const Color& c, const Color& expected, int tol)
    {
        return CloseTo(c.getRProperty(), expected.getRProperty(), tol)
            && CloseTo(c.getGProperty(), expected.getGProperty(), tol)
            && CloseTo(c.getBProperty(), expected.getBProperty(), tol);
    }

    std::string ColorStr(const Color& c)
    {
        return "(" + std::to_string(c.getRProperty()) + "," + std::to_string(c.getGProperty())
             + "," + std::to_string(c.getBProperty()) + ")";
    }
}

class OpenGL2EffectsTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    std::unique_ptr<Texture2D> texOpaque_;
    std::unique_ptr<Texture2D> texTransparent_;
    std::unique_ptr<Texture2D> texHalfRed_;
    std::unique_ptr<Texture2D> texWhite_;

    int frame_ = 0;
    int passCount_ = 0;
    int result_ = 1;

    void Check(bool ok, const std::string& label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label.c_str());
        if (ok) ++passCount_;
    }

    Color ReadPixel(int x, int y)
    {
        Color px(0, 0, 0, 0);
        const Rectangle rect(x, y, 1, 1);
        getGraphicsDeviceProperty().GetBackBufferData(&rect, &px, 0, 1);
        return px;
    }

    static void FullScreenQuad(VertexPositionTexture out[6], float z = 0.0f)
    {
        out[0] = {Vector3(-1, 1, z), Vector2(0, 0)}; out[1] = {Vector3(1, -1, z), Vector2(1, 1)}; out[2] = {Vector3(-1, -1, z), Vector2(0, 1)};
        out[3] = {Vector3(-1, 1, z), Vector2(0, 0)}; out[4] = {Vector3(1, 1, z), Vector2(1, 0)};  out[5] = {Vector3(1, -1, z), Vector2(1, 1)};
    }

    static void FullScreenQuadNormal(VertexPositionNormalTexture out[6], Vector3 normal)
    {
        out[0] = {Vector3(-1, 1, 0), normal, Vector2(0, 0)}; out[1] = {Vector3(1, -1, 0), normal, Vector2(1, 1)}; out[2] = {Vector3(-1, -1, 0), normal, Vector2(0, 1)};
        out[3] = {Vector3(-1, 1, 0), normal, Vector2(0, 0)}; out[4] = {Vector3(1, 1, 0), normal, Vector2(1, 0)};  out[5] = {Vector3(1, -1, 0), normal, Vector2(1, 1)};
    }

    void DrawAlphaTest(GraphicsDevice& dev, Texture2D& tex, CompareFunction func, int refAlpha)
    {
        AlphaTestEffect fx(dev);
        fx.setTextureProperty(&tex);
        fx.setAlphaFunctionProperty(func);
        fx.setReferenceAlphaProperty(refAlpha);
        fx.setWorldProperty(Matrix::getIdentityProperty());
        fx.setViewProperty(Matrix::getIdentityProperty());
        fx.setProjectionProperty(Matrix::getIdentityProperty());
        fx.Apply();

        VertexPositionTexture verts[6];
        FullScreenQuad(verts);
        VertexBuffer vb(dev, VertexPositionTexture::getVertexDeclarationStatic(), 6, BufferUsage::None);
        vb.SetData(verts, 0, 6);
        dev.SetVertexBuffer(&vb);
        dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
        dev.SetVertexBuffer(nullptr);
    }

    void DrawDualTexture(GraphicsDevice& dev, Texture2D& tex0, Texture2D& tex2)
    {
        DualTextureEffect fx(dev);
        fx.setTextureProperty(&tex0);
        fx.setTexture2Property(&tex2);
        fx.setWorldProperty(Matrix::getIdentityProperty());
        fx.setViewProperty(Matrix::getIdentityProperty());
        fx.setProjectionProperty(Matrix::getIdentityProperty());
        fx.Apply();

        VertexPositionTexture verts[6];
        FullScreenQuad(verts);
        VertexBuffer vb(dev, VertexPositionTexture::getVertexDeclarationStatic(), 6, BufferUsage::None);
        vb.SetData(verts, 0, 6);
        dev.SetVertexBuffer(&vb);
        dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
        dev.SetVertexBuffer(nullptr);
    }

    void DrawLit(GraphicsDevice& dev, const Vector3& lightDir)
    {
        BasicEffect fx(dev);
        fx.setLightingEnabledProperty(true);
        fx.setAmbientLightColorProperty(Vector3(0.1f, 0.1f, 0.1f));
        fx.DirectionalLight0.setEnabledProperty(true);
        fx.DirectionalLight0.setDirectionProperty(lightDir);
        fx.DirectionalLight0.setDiffuseColorProperty(Vector3(1.0f, 1.0f, 1.0f));
        fx.DirectionalLight0.setSpecularColorProperty(Vector3(0.0f, 0.0f, 0.0f));
        fx.setTextureEnabledProperty(false);
        fx.setWorldProperty(Matrix::getIdentityProperty());
        fx.setViewProperty(Matrix::getIdentityProperty());
        fx.setProjectionProperty(Matrix::getIdentityProperty());
        fx.Apply();

        VertexPositionNormalTexture verts[6];
        FullScreenQuadNormal(verts, Vector3(0.0f, 0.0f, 1.0f));
        VertexBuffer vb(dev, VertexPositionNormalTexture::getVertexDeclarationStatic(), 6, BufferUsage::None);
        vb.SetData(verts, 0, 6);
        dev.SetVertexBuffer(&vb);
        dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
        dev.SetVertexBuffer(nullptr);
    }

    // Non-uniform-scale World (X*3, Y/Z unchanged) + a 45-degree-tilted normal (1,0,1)/sqrt(2).
    // The CORRECT inverse-transpose normal matrix shrinks the transformed normal's X component
    // (undoing the X stretch), tilting it toward +Z and giving N.(-L)=0.9487 for
    // L=(0,0,-1) -- near-full lit. Using the raw World upper-3x3 instead (the old,
    // pre-inverse-transpose behavior) tilts the wrong way, toward +X, giving N.(-L)=0.3162 --
    // a much dimmer, clearly distinguishable result.
    void DrawLitNonUniformScale(GraphicsDevice& dev)
    {
        BasicEffect fx(dev);
        fx.setLightingEnabledProperty(true);
        fx.setAmbientLightColorProperty(Vector3(0.0f, 0.0f, 0.0f));
        fx.DirectionalLight0.setEnabledProperty(true);
        fx.DirectionalLight0.setDirectionProperty(Vector3(0.0f, 0.0f, -1.0f));
        fx.DirectionalLight0.setDiffuseColorProperty(Vector3(1.0f, 1.0f, 1.0f));
        fx.DirectionalLight0.setSpecularColorProperty(Vector3(0.0f, 0.0f, 0.0f));
        fx.setTextureEnabledProperty(false);
        fx.setWorldProperty(Matrix::CreateScale(3.0f, 1.0f, 1.0f));
        fx.setViewProperty(Matrix::getIdentityProperty());
        fx.setProjectionProperty(Matrix::getIdentityProperty());
        fx.Apply();

        const float s = 0.70710678f; // 1/sqrt(2)
        VertexPositionNormalTexture verts[6];
        FullScreenQuadNormal(verts, Vector3(s, 0.0f, s));
        VertexBuffer vb(dev, VertexPositionNormalTexture::getVertexDeclarationStatic(), 6, BufferUsage::None);
        vb.SetData(verts, 0, 6);
        dev.SetVertexBuffer(&vb);
        dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
        dev.SetVertexBuffer(nullptr);
    }

    void DrawFog(GraphicsDevice& dev, Texture2D& tex, float z)
    {
        BasicEffect fx(dev);
        fx.setTextureEnabledProperty(true);
        fx.setTextureProperty(&tex);
        fx.setFogEnabledProperty(true);
        fx.setFogColorProperty(Vector3(1.0f, 1.0f, 0.0f)); // yellow
        fx.setFogStartProperty(-0.8f);
        fx.setFogEndProperty(0.8f);
        fx.setWorldProperty(Matrix::getIdentityProperty());
        fx.setViewProperty(Matrix::getIdentityProperty());
        fx.setProjectionProperty(Matrix::getIdentityProperty());
        fx.Apply();

        VertexPositionTexture verts[6];
        FullScreenQuad(verts, z);
        VertexBuffer vb(dev, VertexPositionTexture::getVertexDeclarationStatic(), 6, BufferUsage::None);
        vb.SetData(verts, 0, 6);
        dev.SetVertexBuffer(&vb);
        dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
        dev.SetVertexBuffer(nullptr);
    }

protected:
    void LoadContent() override
    {
        auto& dev = getGraphicsDeviceProperty();
        texOpaque_ = std::make_unique<Texture2D>(
            Texture2D::CreateFromPixels(dev, 1, 1, std::vector<std::uint8_t>{0, 255, 0, 255}));
        texTransparent_ = std::make_unique<Texture2D>(
            Texture2D::CreateFromPixels(dev, 1, 1, std::vector<std::uint8_t>{0, 255, 0, 32}));
        texHalfRed_ = std::make_unique<Texture2D>(
            Texture2D::CreateFromPixels(dev, 1, 1, std::vector<std::uint8_t>{128, 0, 0, 255}));
        texWhite_ = std::make_unique<Texture2D>(
            Texture2D::CreateFromPixels(dev, 1, 1, std::vector<std::uint8_t>{255, 255, 255, 255}));
    }

    void RunScene(bool runChecks)
    {
        auto& dev = getGraphicsDeviceProperty();

        // Check A: AlphaTestEffect.
        dev.Clear(Color::Black);
        DrawAlphaTest(dev, *texOpaque_, CompareFunction::Greater, 128);
        if (runChecks)
        {
            const Color opaquePass = ReadPixel(160, 120);
            Check(Matches(opaquePass, Color(0, 255, 0, 255), 10),
                  "AlphaTestEffect: alpha=255 > ref=128 survives: got=" + ColorStr(opaquePass));
        }

        dev.Clear(Color::Black);
        DrawAlphaTest(dev, *texTransparent_, CompareFunction::Greater, 128);
        if (runChecks)
        {
            const Color discarded = ReadPixel(160, 120);
            Check(Matches(discarded, Color::Black, 10),
                  "AlphaTestEffect: alpha=32 <= ref=128 is discarded (background shows): got=" + ColorStr(discarded));
        }

        // Check B: DualTextureEffect's base*tex2*2 formula.
        dev.Clear(Color::Black);
        DrawDualTexture(dev, *texHalfRed_, *texWhite_);
        if (runChecks)
        {
            const Color got = ReadPixel(160, 120);
            Check(Matches(got, Color(255, 0, 0, 255), 10),
                  "DualTextureEffect: half-intensity base * white tex2 * 2 reads back full red: got=" + ColorStr(got));
        }

        // Check C: BasicEffect lighting.
        dev.Clear(Color::Black);
        DrawLit(dev, Vector3(0.0f, 0.0f, -1.0f)); // travels toward -Z -> hits the +Z-facing surface
        const Color litResult = ReadPixel(160, 120);

        dev.Clear(Color::Black);
        DrawLit(dev, Vector3(0.0f, 0.0f, 1.0f)); // travels toward +Z -> away from the +Z-facing surface
        const Color unlitResult = ReadPixel(160, 120);
        if (runChecks)
        {
            Check(Matches(litResult, Color::White, 15),
                  "BasicEffect lighting: light facing the surface -> near-white: got=" + ColorStr(litResult));
            Check(litResult.getRProperty() > unlitResult.getRProperty() + 30,
                  "BasicEffect lighting: light facing away is dimmer (ambient only): lit=" + ColorStr(litResult) +
                  " unlit=" + ColorStr(unlitResult));
        }

        // Check C2: normal matrix is a real inverse-transpose, not a raw World upper-3x3 --
        // see DrawLitNonUniformScale's own comment for the expected math.
        dev.Clear(Color::Black);
        DrawLitNonUniformScale(dev);
        const Color nonUniformResult = ReadPixel(160, 120);
        if (runChecks)
        {
            Check(nonUniformResult.getRProperty() > 210,
                  "BasicEffect lighting under non-uniform World scale uses a real inverse-transpose normal matrix "
                  "(expected near-white ~242, a raw-3x3 bug would read back ~81): got=" + ColorStr(nonUniformResult));
        }

        // Check D: BasicEffect fog.
        dev.Clear(Color::Black);
        DrawFog(dev, *texOpaque_, -0.8f);
        const Color foggedResult = ReadPixel(160, 120);

        dev.Clear(Color::Black);
        DrawFog(dev, *texOpaque_, 0.8f);
        const Color unfoggedResult = ReadPixel(160, 120);
        if (runChecks)
        {
            Check(Matches(foggedResult, Color(255, 255, 0, 255), 15),
                  "BasicEffect fog: z=-0.8 (FogStart) reads back the fog color: got=" + ColorStr(foggedResult));
            Check(Matches(unfoggedResult, Color(0, 255, 0, 255), 10),
                  "BasicEffect fog: z=0.8 (FogEnd) reads back the unfogged color: got=" + ColorStr(unfoggedResult));
        }
    }

    void Draw(const GameTime&) override
    {
        ++frame_;
        RunScene(/*runChecks=*/frame_ == 1);

        if (frame_ == kTotalFrames)
        {
            Check(true, "60 frames of the effects scene render with no exception");
            std::printf("=== %d/%d PASS ===\n", passCount_, 9);
            result_ = (passCount_ == 9) ? 0 : 1;
            Exit();
        }
    }

public:
    OpenGL2EffectsTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(320);
        gdm_->setPreferredBackBufferHeightProperty(240);
        gdm_->setSynchronizeWithVerticalRetraceProperty(false);
    }

    int getResult() const { return result_; }
};

int main()
{
    if (!CNA::Examples::ProbeGpuDisplayAvailable())
        return CNA::Examples::kSkipExitCode;

    OpenGL2EffectsTest game;
    game.Run();
    return game.getResult();
}
