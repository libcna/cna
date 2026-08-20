// SPDX-License-Identifier: MS-PL
// plans/plan_cnj.md Phase 14J WebGPU counterpart: verify WebGPURenderer's skinned_pbr3d.wgsl
// (GetOrCreatePipelineSkinnedPbr3D()/QueueSkinnedPbrDraw()/DrawPrimitivesEx() dispatch) for
// SkinnedPbrEffect on stride-68 (VertexPositionNormalTangentTextureSkinned) draws -- the PBR +
// skinning combo, closing the remaining half of this renderer's pre-existing "no skinning shader
// at all" gap (webgpu_skinned3d_test.cpp covers plain SkinnedEffect). Ported from
// EasyGLRenderer::EnsurePbrSkinnedProgram(), which feeds pbr3d.wgsl's own glTF 2.0
// metallic-roughness BRDF (GGX + Smith-Schlick-GGX + Schlick Fresnel) with a skinned
// position/normal/tangent instead of an unskinned one.
//
// All checks use World=View=Projection=Identity and a quad at z=0.5 with Normal=(0,0,-1)
// (facing the camera at the origin) and Tangent=(1,0,0,1), matching webgpu_pbr3d_test.cpp's own
// MakeFacingQuad convention.
//
// Check A -- one identity bone, AmbientLightColor=(1,1,1), all directional lights disabled, white
//   base color, no optional maps bound: renders white -- proves the stride-68 dispatch reaches a
//   real pipeline (not silently falling back to colored3d.wgsl's stride-16 layout, nor to the
//   unskinned pbr3d.wgsl on a 68-byte buffer).
// Check B -- AmbientLightColor=black, DirectionalLight0 facing the visible side: renders clearly
//   non-black -- proves a light reaching the shader and the BRDF producing real energy.
// Check C -- same as B but DirectionalLight0.Direction flipped (hits the back face): NdotL clamps
//   to 0, renders black.
// Check D -- bone-palette translation + WeightsPerVertex gating (Task 895), identical
//   hand-derived NDC-shift technique to webgpu_skinned3d_test.cpp's own Check D: two bones,
//   bone0=Identity, bone1=Translate(+2,0,0), both weighted 1 on a quad with local x in
//   [-1,-0.5]. WeightsPerVertex=1 sums only bone0 (skinMat=Identity, quad stays in the left NDC
//   strip); WeightsPerVertex=2 sums bone0+bone1 (skinMat has M44=2, translation column (2,0,0,2)
//   -- after the GPU's perspective divide this is exactly Translate(+1,0,0) in NDC, so the quad's
//   local x in [-1,-0.5] lands at NDC x in [0,0.5], the right strip). Verified independently for
//   THIS shader (not assumed identical to plain SkinnedEffect's skinning) since it is a genuinely
//   separate WGSL shader module.
//
// Exit code 0 = all checks PASS, 1 = any FAILs.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
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
#include "Microsoft/Xna/Framework/Graphics/SkinnedPbrEffect.hpp"
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

    // Matches ApplyLayout's/QueueSkinnedPbrDraw's stride==68 case
    // (VertexPositionNormalTangentTextureSkinned).
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

    VertexBuffer MakeFacingQuad(GraphicsDevice& dev, float xMin, float xMax)
    {
        const SkinnedPbrGpuVertex tl{ xMin,  1, 0.5f,  0,0,-1,  1,0,0,1,  0,0,  1,0,0,0,  0,0,0,0 };
        const SkinnedPbrGpuVertex bl{ xMin, -1, 0.5f,  0,0,-1,  1,0,0,1,  0,1,  1,0,0,0,  0,0,0,0 };
        const SkinnedPbrGpuVertex br{ xMax, -1, 0.5f,  0,0,-1,  1,0,0,1,  1,1,  1,0,0,0,  0,0,0,0 };
        const SkinnedPbrGpuVertex tr{ xMax,  1, 0.5f,  0,0,-1,  1,0,0,1,  1,0,  1,0,0,0,  0,0,0,0 };
        const std::vector<SkinnedPbrGpuVertex> verts = { tl, bl, br, tl, br, tr };

        VertexBuffer vb(dev, static_cast<int>(verts.size()));
        vb.SetDataRaw(verts.data(), static_cast<int>(verts.size()), static_cast<int>(sizeof(SkinnedPbrGpuVertex)));
        return vb;
    }

    // Check D's two-bone quad: both bone slots populated (index0/weight0=1 -> bone0;
    // index1/weight1=1 -> bone1) so WeightsPerVertex alone decides whether bone1 contributes.
    VertexBuffer MakeTwoBoneQuad(GraphicsDevice& dev, float xMin, float xMax)
    {
        const SkinnedPbrGpuVertex tl{ xMin,  1, 0.5f,  0,0,-1,  1,0,0,1,  0,0,  1,1,0,0,  0,1,0,0 };
        const SkinnedPbrGpuVertex bl{ xMin, -1, 0.5f,  0,0,-1,  1,0,0,1,  0,1,  1,1,0,0,  0,1,0,0 };
        const SkinnedPbrGpuVertex br{ xMax, -1, 0.5f,  0,0,-1,  1,0,0,1,  1,1,  1,1,0,0,  0,1,0,0 };
        const SkinnedPbrGpuVertex tr{ xMax,  1, 0.5f,  0,0,-1,  1,0,0,1,  1,0,  1,1,0,0,  0,1,0,0 };
        const std::vector<SkinnedPbrGpuVertex> verts = { tl, bl, br, tl, br, tr };

        VertexBuffer vb(dev, static_cast<int>(verts.size()));
        vb.SetDataRaw(verts.data(), static_cast<int>(verts.size()), static_cast<int>(sizeof(SkinnedPbrGpuVertex)));
        return vb;
    }
}

class WebGpuSkinnedPbr3DTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    Texture2D whiteTex_;
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

    static void ConfigureCommon(SkinnedPbrEffect& fx, Texture2D& tex)
    {
        fx.setWorldProperty(Matrix::getIdentityProperty());
        fx.setViewProperty(Matrix::getIdentityProperty());
        fx.setProjectionProperty(Matrix::getIdentityProperty());
        fx.setTextureProperty(&tex);
        fx.setRoughnessFactorProperty(1.0f);
        fx.setMetallicFactorProperty(0.0f);
        fx.setWeightsPerVertexProperty(1);
    }

protected:
    void LoadContent() override
    {
        whiteTex_ = Texture2D::CreateFromPixels(getGraphicsDeviceProperty(), 1, 1,
                                                 std::vector<std::uint8_t>{255, 255, 255, 255});
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
            SkinnedPbrEffect fx(dev);
            ConfigureCommon(fx, whiteTex_);
            std::vector<Matrix> bones = { Matrix::getIdentityProperty() };
            fx.SetBoneTransforms(bones);
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
            SkinnedPbrEffect fx(dev);
            ConfigureCommon(fx, whiteTex_);
            std::vector<Matrix> bones = { Matrix::getIdentityProperty() };
            fx.SetBoneTransforms(bones);
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
                  "(B) a light facing the surface produces real (non-black) BRDF energy");
        }

        // Check C: the same light behind the quad.
        {
            dev.Clear(Color::Black);
            VertexBuffer vb = MakeFacingQuad(dev, -1.0f, 1.0f);
            SkinnedPbrEffect fx(dev);
            ConfigureCommon(fx, whiteTex_);
            std::vector<Matrix> bones = { Matrix::getIdentityProperty() };
            fx.SetBoneTransforms(bones);
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
                  "(C) a light behind the surface contributes no diffuse or specular (black)");
        }

        // Check D: bone-palette translation + WeightsPerVertex gating, verified independently for
        // this shader (not assumed identical to plain SkinnedEffect's).
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
                SkinnedPbrEffect fx(dev);
                ConfigureCommon(fx, whiteTex_);
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

        std::printf("=== %d/%d PASS ===\n", passCount_, checkCount_);
        result_ = (passCount_ == checkCount_) ? 0 : 1;
        Exit();
    }

public:
    WebGpuSkinnedPbr3DTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kSize);
        gdm_->setPreferredBackBufferHeightProperty(kSize);
    }

    int getResult() const { return result_; }
};

int main()
{
    WebGpuSkinnedPbr3DTest game;
    game.Run();
    return game.getResult();
}
