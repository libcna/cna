// SPDX-License-Identifier: MS-PL
// plans/plan_cnj.md Phase 14J WebGPU counterpart: verify WebGPURenderer's skinned3d.wgsl family
// (GetOrCreatePipelineSkinned3D()/QueueSkinnedDraw()/DrawPrimitivesEx() dispatch) for SkinnedEffect
// on stride-52 (VertexPositionNormalTextureSkinned) and stride-56 (with a trailing per-vertex
// Color, CNB-67) draws -- closing this renderer's pre-existing "no skinning shader at all" gap.
// Ported from EasyGLRenderer::EnsureSkinnedProgram()/EnsureSkinnedVertexLitProgram().
//
// All checks use World=View=Projection=Identity (so NDC == object-space xyz) and a quad at
// z=0.5 with Normal=(0,0,-1) (facing the camera at the origin, same convention as
// webgpu_littextured3d_test.cpp's own MakeFacingQuad) unless noted.
//
// Check A -- one identity bone, AmbientLightColor=(1,1,1), all directional lights disabled, white
//   base color: renders white -- proves the stride-52 dispatch reaches a real pipeline (not
//   silently falling back to colored3d.wgsl's stride-16 layout on a 52-byte buffer).
// Check B -- AmbientLightColor=black, DirectionalLight0 facing the visible side of the quad:
//   renders clearly non-black -- proves a light reaching the shader.
// Check C -- same as B but DirectionalLight0.Direction flipped (hits the back face): renders
//   black -- proves the N-dot-L gate really excludes light from the wrong side.
// Check D -- bone-palette + WeightsPerVertex gating (Task 895): two bones, bone0=Identity
//   (index0/weight0=1), bone1=Translate(+2,0,0) (index1/weight1=1). A quad with local x in
//   [-1,-0.5] (left strip, NDC centre x=-0.75) is drawn twice:
//     - WeightsPerVertex=1: skinMat=bone0*1=Identity (bone1 must NOT contribute) -- position
//       unchanged, quad stays in the left strip (left sample coloured, right sample background).
//     - WeightsPerVertex=2: skinMat=bone0*1+bone1*1. Summing two M44=1 affine matrices whose
//       rotation/scale parts are both Identity gives a matrix with M44=2 and translation column
//       (2,0,0,2); after the GPU's perspective divide by w=2 this is EXACTLY equivalent to
//       Translate(+1,0,0) in NDC (skinnedPos=(2x+2,2y,2z,2) -> NDC=((2x+2)/2,y,z)=(x+1,y,z)) --
//       hand-derived, not measured -- so the quad's local x in [-1,-0.5] lands at NDC x in
//       [0,0.5] (right strip, centre x=+0.25): left sample background, right sample coloured.
//   This simultaneously proves the bone-palette translation genuinely reaches the vertex shader
//   AND that WeightsPerVertex correctly gates which bones contribute.
// Check E -- stride-56 VertexColorEnabled (CNB-67), mirrors
//   examples/easygl_skinnedeffect_vertexcolor_test.cpp's own convention: a pure-black per-vertex
//   colour must zero the final combined diffuse+specular output when VertexColorEnabled=true, and
//   be ignored when false -- independent of the exact lit/textured value (no Phong hand-derivation
//   needed for this check to be unambiguous).
// Check F -- PreferPerPixelLighting dispatch: a quad with ONE shared normal (0,0,1), viewed via a
//   real perspective projection so the view vector varies across the quad (Blinn-Phong specular
//   is then genuinely position-dependent even though diffuse is spatially constant), sampled at
//   the seam between its two triangles. PreferPerPixelLighting=false (XNA's real default) must
//   render the Gouraud-*interpolated average* of the two seam vertices' own independently
//   evaluated specular terms; PreferPerPixelLighting=true must render a *fresh per-fragment*
//   evaluation at that exact point instead -- these are provably different code paths for a
//   non-symmetric light direction, checked here as "both non-black" (light reaches the shader)
//   plus "the two results genuinely differ" (proves GetOrCreatePipelineSkinned3D() actually
//   dispatches two distinct live shaders, not one path silently always winning).
//
// Exit code 0 = all checks PASS, 1 = any FAILs.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/MathHelper.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/DirectionalLight.hpp"
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
    constexpr int kSize = 64;

    // Matches ApplyLayout's/QueueSkinnedDraw's stride==52 case (VertexPositionNormalTextureSkinned).
    struct SkinnedGpuVertex
    {
        float px, py, pz;
        float nx, ny, nz;
        float u, v;
        float w0, w1, w2, w3;
        std::uint8_t i0, i1, i2, i3;
    };
    static_assert(sizeof(SkinnedGpuVertex) == 52, "skinned vertex must be 52 bytes");

    // Matches stride==56 (CNB-67: trailing normalized ubyte4 Color at offset 52).
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

    Color readPixel(GraphicsDevice& dev, int x, int y)
    {
        const Rectangle region(x, y, 1, 1);
        Color pixel(0, 0, 0, 0);
        dev.GetBackBufferData(&region, &pixel, 0, 1);
        return pixel;
    }

    bool isNearBlack(Color c, int tol = 24)
    {
        return c.getRProperty() <= tol && c.getGProperty() <= tol && c.getBProperty() <= tol;
    }

    bool isNearWhite(Color c, int tol = 24)
    {
        return c.getRProperty() >= 255 - tol && c.getGProperty() >= 255 - tol && c.getBProperty() >= 255 - tol;
    }

    bool colorDiffers(Color a, Color b, int tol = 6)
    {
        return std::abs(a.getRProperty() - b.getRProperty()) > tol ||
               std::abs(a.getGProperty() - b.getGProperty()) > tol ||
               std::abs(a.getBProperty() - b.getBProperty()) > tol;
    }

    VertexBuffer MakeFacingQuad(GraphicsDevice& dev, float xMin, float xMax)
    {
        // Normal=(0,0,-1) faces the camera at the origin (quad sits at z=0.5), single bone
        // (index 0, weight 1) unless overwritten by the caller.
        const SkinnedGpuVertex tl{ xMin,  1, 0.5f,  0,0,-1,  0,0,  1,0,0,0,  0,0,0,0 };
        const SkinnedGpuVertex bl{ xMin, -1, 0.5f,  0,0,-1,  0,1,  1,0,0,0,  0,0,0,0 };
        const SkinnedGpuVertex br{ xMax, -1, 0.5f,  0,0,-1,  1,1,  1,0,0,0,  0,0,0,0 };
        const SkinnedGpuVertex tr{ xMax,  1, 0.5f,  0,0,-1,  1,0,  1,0,0,0,  0,0,0,0 };
        const std::vector<SkinnedGpuVertex> verts = { tl, bl, br, tl, br, tr };

        VertexBuffer vb(dev, static_cast<int>(verts.size()));
        vb.SetDataRaw(verts.data(), static_cast<int>(verts.size()), static_cast<int>(sizeof(SkinnedGpuVertex)));
        return vb;
    }

    // Check D's two-bone quad: local x in [xMin,xMax], both bone slots populated
    // (index0/weight0=1 -> bone0; index1/weight1=1 -> bone1) so WeightsPerVertex alone decides
    // whether bone1 contributes.
    VertexBuffer MakeTwoBoneQuad(GraphicsDevice& dev, float xMin, float xMax)
    {
        const SkinnedGpuVertex tl{ xMin,  1, 0.5f,  0,0,-1,  0,0,  1,1,0,0,  0,1,0,0 };
        const SkinnedGpuVertex bl{ xMin, -1, 0.5f,  0,0,-1,  0,1,  1,1,0,0,  0,1,0,0 };
        const SkinnedGpuVertex br{ xMax, -1, 0.5f,  0,0,-1,  1,1,  1,1,0,0,  0,1,0,0 };
        const SkinnedGpuVertex tr{ xMax,  1, 0.5f,  0,0,-1,  1,0,  1,1,0,0,  0,1,0,0 };
        const std::vector<SkinnedGpuVertex> verts = { tl, bl, br, tl, br, tr };

        VertexBuffer vb(dev, static_cast<int>(verts.size()));
        vb.SetDataRaw(verts.data(), static_cast<int>(verts.size()), static_cast<int>(sizeof(SkinnedGpuVertex)));
        return vb;
    }

    void AppendColorQuad(std::vector<SkinnedColorGpuVertex>& out, float xMin, float xMax,
                          std::uint8_t r, std::uint8_t g, std::uint8_t b, std::uint8_t a)
    {
        const SkinnedColorGpuVertex tl{ xMin,  1, 0,  0,0,1,  0,0,  1,0,0,0,  0,0,0,0,  r,g,b,a };
        const SkinnedColorGpuVertex bl{ xMin, -1, 0,  0,0,1,  0,1,  1,0,0,0,  0,0,0,0,  r,g,b,a };
        const SkinnedColorGpuVertex br{ xMax, -1, 0,  0,0,1,  1,1,  1,0,0,0,  0,0,0,0,  r,g,b,a };
        const SkinnedColorGpuVertex tr{ xMax,  1, 0,  0,0,1,  1,0,  1,0,0,0,  0,0,0,0,  r,g,b,a };
        out.push_back(tl); out.push_back(bl); out.push_back(br);
        out.push_back(tl); out.push_back(br); out.push_back(tr);
    }
}

class WebGpuSkinned3DTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    Texture2D whiteTex_;
    Texture2D redTex_;
    bool done_ = false;
    int passCount_ = 0;
    int checkCount_ = 0;
    int result_ = 1;

    void check(bool ok, const char* label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label);
        ++checkCount_;
        if (ok) ++passCount_;
    }

protected:
    void LoadContent() override
    {
        whiteTex_ = Texture2D::CreateFromPixels(getGraphicsDeviceProperty(), 1, 1,
                                                 std::vector<std::uint8_t>{255, 255, 255, 255});
        redTex_ = Texture2D::CreateFromPixels(getGraphicsDeviceProperty(), 1, 1,
                                               std::vector<std::uint8_t>{255, 0, 0, 255});
    }

    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;

        auto& dev = getGraphicsDeviceProperty();
        dev.setRasterizerStateProperty(RasterizerState::CullNone);
        dev.setDepthStencilStateProperty(DepthStencilState::None);
        dev.setBlendStateProperty(BlendState::Opaque);

        // Check A: ambient-only, one identity bone.
        {
            dev.Clear(Color::Black);
            VertexBuffer vb = MakeFacingQuad(dev, -1.0f, 1.0f);
            SkinnedEffect fx(dev);
            fx.setWorldProperty(Matrix::getIdentityProperty());
            fx.setViewProperty(Matrix::getIdentityProperty());
            fx.setProjectionProperty(Matrix::getIdentityProperty());
            fx.setTextureProperty(&whiteTex_);
            std::vector<Matrix> bones = { Matrix::getIdentityProperty() };
            fx.SetBoneTransforms(bones);
            fx.setWeightsPerVertexProperty(1);
            fx.setAmbientLightColorProperty(Vector3(1.0f, 1.0f, 1.0f));
            fx.DirectionalLight0.setEnabledProperty(false);
            fx.DirectionalLight1.setEnabledProperty(false);
            fx.DirectionalLight2.setEnabledProperty(false);
            fx.Apply();
            dev.SetVertexBuffer(&vb);
            dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
            dev.SetVertexBuffer(nullptr);
            check(isNearWhite(readPixel(dev, kSize / 2, kSize / 2)),
                  "(A) ambient alone (identity bone) renders white");
        }

        // Check B: one light facing the visible side.
        {
            dev.Clear(Color::Black);
            VertexBuffer vb = MakeFacingQuad(dev, -1.0f, 1.0f);
            SkinnedEffect fx(dev);
            fx.setWorldProperty(Matrix::getIdentityProperty());
            fx.setViewProperty(Matrix::getIdentityProperty());
            fx.setProjectionProperty(Matrix::getIdentityProperty());
            fx.setTextureProperty(&whiteTex_);
            std::vector<Matrix> bones = { Matrix::getIdentityProperty() };
            fx.SetBoneTransforms(bones);
            fx.setWeightsPerVertexProperty(1);
            fx.setAmbientLightColorProperty(Vector3(0.0f, 0.0f, 0.0f));
            fx.DirectionalLight0.setEnabledProperty(true);
            fx.DirectionalLight0.setDirectionProperty(Vector3(0.0f, 0.0f, 1.0f));
            fx.DirectionalLight0.setDiffuseColorProperty(Vector3(1.0f, 1.0f, 1.0f));
            fx.DirectionalLight1.setEnabledProperty(false);
            fx.DirectionalLight2.setEnabledProperty(false);
            fx.Apply();
            dev.SetVertexBuffer(&vb);
            dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
            dev.SetVertexBuffer(nullptr);
            check(!isNearBlack(readPixel(dev, kSize / 2, kSize / 2)),
                  "(B) a light facing the surface produces real (non-black) lit output");
        }

        // Check C: the same light behind the quad.
        {
            dev.Clear(Color::Black);
            VertexBuffer vb = MakeFacingQuad(dev, -1.0f, 1.0f);
            SkinnedEffect fx(dev);
            fx.setWorldProperty(Matrix::getIdentityProperty());
            fx.setViewProperty(Matrix::getIdentityProperty());
            fx.setProjectionProperty(Matrix::getIdentityProperty());
            fx.setTextureProperty(&whiteTex_);
            std::vector<Matrix> bones = { Matrix::getIdentityProperty() };
            fx.SetBoneTransforms(bones);
            fx.setWeightsPerVertexProperty(1);
            fx.setAmbientLightColorProperty(Vector3(0.0f, 0.0f, 0.0f));
            fx.DirectionalLight0.setEnabledProperty(true);
            fx.DirectionalLight0.setDirectionProperty(Vector3(0.0f, 0.0f, -1.0f));
            fx.DirectionalLight0.setDiffuseColorProperty(Vector3(1.0f, 1.0f, 1.0f));
            fx.DirectionalLight1.setEnabledProperty(false);
            fx.DirectionalLight2.setEnabledProperty(false);
            fx.Apply();
            dev.SetVertexBuffer(&vb);
            dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
            dev.SetVertexBuffer(nullptr);
            check(isNearBlack(readPixel(dev, kSize / 2, kSize / 2)),
                  "(C) a light behind the surface contributes nothing (black)");
        }

        // Check D: bone-palette translation + WeightsPerVertex gating.
        {
            const int leftX = kSize / 8;         // NDC ~ -0.75
            const int rightX = (kSize * 5) / 8;   // NDC ~ +0.25
            const int y = kSize / 2;

            std::vector<Matrix> bones = { Matrix::getIdentityProperty(),
                                          Matrix::CreateTranslation(Vector3(2.0f, 0.0f, 0.0f)) };

            for (int wpv = 1; wpv <= 2; wpv += 1)
            {
                dev.Clear(Color::Black);
                VertexBuffer vb = MakeTwoBoneQuad(dev, -1.0f, -0.5f);
                SkinnedEffect fx(dev);
                fx.setWorldProperty(Matrix::getIdentityProperty());
                fx.setViewProperty(Matrix::getIdentityProperty());
                fx.setProjectionProperty(Matrix::getIdentityProperty());
                fx.setTextureProperty(&whiteTex_);
                fx.SetBoneTransforms(bones);
                fx.setWeightsPerVertexProperty(wpv);
                fx.setAmbientLightColorProperty(Vector3(1.0f, 1.0f, 1.0f));
                fx.DirectionalLight0.setEnabledProperty(false);
                fx.DirectionalLight1.setEnabledProperty(false);
                fx.DirectionalLight2.setEnabledProperty(false);
                fx.Apply();
                dev.SetVertexBuffer(&vb);
                dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
                dev.SetVertexBuffer(nullptr);

                const Color left = readPixel(dev, leftX, y);
                const Color right = readPixel(dev, rightX, y);
                if (wpv == 1)
                {
                    check(isNearWhite(left) && isNearBlack(right),
                          "(D1) WeightsPerVertex=1 ignores bone1 -- quad stays at its original position");
                }
                else
                {
                    check(isNearBlack(left) && isNearWhite(right),
                          "(D2) WeightsPerVertex=2 blends bone1's translation -- quad shifts right");
                }
            }
        }

        // Check E: stride-56 VertexColorEnabled (CNB-67).
        {
            dev.Clear(Color(0, 255, 0, 255));
            SkinnedEffect fx(dev);
            fx.setTextureProperty(&redTex_);
            fx.setWorldProperty(Matrix::getIdentityProperty());
            fx.setViewProperty(Matrix::getIdentityProperty());
            fx.setProjectionProperty(Matrix::getIdentityProperty());
            std::vector<Matrix> bones = { Matrix::getIdentityProperty() };
            fx.SetBoneTransforms(bones);
            fx.setWeightsPerVertexProperty(1);
            fx.EnableDefaultLighting();

            std::vector<SkinnedColorGpuVertex> verts;
            AppendColorQuad(verts, -1.0f, -0.5f, 0, 0, 0, 255);  // quad A (left)
            AppendColorQuad(verts, 0.5f, 1.0f, 0, 0, 0, 255);    // quad B (right)

            VertexBuffer vb(dev, static_cast<int>(verts.size()));
            vb.SetDataRaw(verts.data(), static_cast<int>(verts.size()), static_cast<int>(sizeof(SkinnedColorGpuVertex)));
            dev.SetVertexBuffer(&vb);

            fx.VertexColorEnabled = false;
            fx.Apply();
            dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);

            fx.VertexColorEnabled = true;
            fx.Apply();
            dev.DrawPrimitives(PrimitiveType::TriangleList, 6, 2);
            dev.SetVertexBuffer(nullptr);

            const int y = kSize / 2;
            const int leftX = kSize / 8;
            const int rightX = (kSize * 7) / 8;
            const Color quadA = readPixel(dev, leftX, y);
            const Color quadB = readPixel(dev, rightX, y);
            check(!isNearBlack(quadA),
                  "(E1) VertexColorEnabled=false ignores the per-vertex black colour (lit/textured result)");
            check(isNearBlack(quadB),
                  "(E2) VertexColorEnabled=true with a black per-vertex colour zeroes the result");
        }

        // Check F: PreferPerPixelLighting dispatch (vertex-lit vs pixel-lit are genuinely
        // different shaders/code paths).
        {
            auto renderWith = [&](bool preferPerPixel) -> Color
            {
                dev.Clear(Color(0, 0, 0, 255));
                const SkinnedGpuVertex tl{ -1,  1, 0,  0,0,1,  0,0,  1,0,0,0,  0,0,0,0 };
                const SkinnedGpuVertex bl{ -1, -1, 0,  0,0,1,  0,1,  1,0,0,0,  0,0,0,0 };
                const SkinnedGpuVertex br{  1, -1, 0,  0,0,1,  1,1,  1,0,0,0,  0,0,0,0 };
                const SkinnedGpuVertex tr{  1,  1, 0,  0,0,1,  1,0,  1,0,0,0,  0,0,0,0 };
                const std::vector<SkinnedGpuVertex> verts = { tl, bl, br, tl, br, tr };
                VertexBuffer vb(dev, static_cast<int>(verts.size()));
                vb.SetDataRaw(verts.data(), static_cast<int>(verts.size()), static_cast<int>(sizeof(SkinnedGpuVertex)));

                SkinnedEffect fx(dev);
                fx.setTextureProperty(&whiteTex_);
                std::vector<Matrix> bones = { Matrix::getIdentityProperty() };
                fx.SetBoneTransforms(bones);
                fx.setWeightsPerVertexProperty(1);
                fx.setPreferPerPixelLightingProperty(preferPerPixel);
                fx.setAmbientLightColorProperty(Vector3(0.02f, 0.02f, 0.02f));
                fx.setDiffuseColorProperty(Vector3(0.4f, 0.4f, 0.4f));
                fx.setSpecularColorProperty(Vector3(1.0f, 1.0f, 1.0f));
                fx.setSpecularPowerProperty(32.0f);

                Vector3 lightDir(0.5f, 0.0f, -1.0f);
                lightDir.Normalize();
                fx.DirectionalLight0.setEnabledProperty(true);
                fx.DirectionalLight0.setDirectionProperty(lightDir);
                fx.DirectionalLight0.setDiffuseColorProperty(Vector3(0.5f, 0.5f, 0.5f));
                fx.DirectionalLight0.setSpecularColorProperty(Vector3(1.0f, 1.0f, 1.0f));
                fx.DirectionalLight1.setEnabledProperty(false);
                fx.DirectionalLight2.setEnabledProperty(false);

                fx.setWorldProperty(Matrix::getIdentityProperty());
                fx.setViewProperty(Matrix::CreateLookAt(Vector3(0.0f, 0.0f, 3.0f), Vector3::Zero, Vector3(0.0f, 1.0f, 0.0f)));
                fx.setProjectionProperty(Matrix::CreatePerspectiveFieldOfView(MathHelper::PiOver4, 1.0f, 0.1f, 100.0f));
                fx.Apply();

                dev.SetVertexBuffer(&vb);
                dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
                dev.SetVertexBuffer(nullptr);
                return readPixel(dev, kSize / 2, kSize / 2);
            };

            const Color vertexLit = renderWith(false);
            const Color pixelLit = renderWith(true);
            std::printf("[INFO] skinned vertex-lit centre = (%d,%d,%d), pixel-lit centre = (%d,%d,%d)\n",
                        vertexLit.getRProperty(), vertexLit.getGProperty(), vertexLit.getBProperty(),
                        pixelLit.getRProperty(), pixelLit.getGProperty(), pixelLit.getBProperty());
            check(!isNearBlack(vertexLit) && !isNearBlack(pixelLit),
                  "(F1) both PreferPerPixelLighting variants produce real lit output");
            check(colorDiffers(vertexLit, pixelLit),
                  "(F2) vertex-lit and pixel-lit results genuinely differ (two distinct shaders)");
        }

        std::printf("=== %d/%d PASS ===\n", passCount_, checkCount_);
        result_ = (passCount_ == checkCount_) ? 0 : 1;
        Exit();
    }

public:
    WebGpuSkinned3DTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kSize);
        gdm_->setPreferredBackBufferHeightProperty(kSize);
    }

    int getResult() const { return result_; }
};

int main()
{
    WebGpuSkinned3DTest game;
    game.Run();
    return game.getResult();
}
