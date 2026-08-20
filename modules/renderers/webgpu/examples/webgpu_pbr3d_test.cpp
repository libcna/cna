// SPDX-License-Identifier: MS-PL
// plans/plan_cnj.md CNB-58 WebGPU counterpart: verify WebGPURenderer's pbr3d.wgsl /
// GetOrCreatePipelinePbr3D() / DrawPrimitivesEx() dispatch for stride-48
// (VertexPositionNormalTangentTexture) PbrEffect draws -- the WebGPU renderer's real glTF 2.0
// metallic-roughness BRDF shader (GGX distribution + Smith-Schlick-GGX visibility + Schlick
// Fresnel), ported from EasyGLRenderer::EnsurePbrProgram()'s GLSL shader. UNSKINNED only
// (SkinnedPbrEffect/stride 68 is a separate, pre-existing, out-of-scope gap -- this renderer has no
// skinning shader variant at all yet).
//
// All checks use World=View=Projection=Identity, a quad at z=0.5 with Normal=(0,0,-1) (facing the
// camera at the origin, same convention as webgpu_littextured3d_test.cpp's own MakeFacingQuad) and
// Tangent=(1,0,0,1). At the exact screen center this puts N=V=L=H=(0,0,-1) for a light directly
// facing the surface (NdotL=NdotV=NdotH=VdotH=1), which was independently hand-traced against the
// exact same GGX/Smith-Schlick-GGX/Schlick-Fresnel formulas pbr3d.wgsl implements:
//   - roughness=1, metallic=0, white albedo, one white light, NdotL=1: D=1/pi, k=0.5, G=1,
//     F=F0=0.04, Lo = (0.96*1/pi + 0.3183*1*0.04/4) ~= 0.309 (linear) -- a clear, non-near-zero,
//     non-near-one mid value, deliberately NOT asserted as an exact pixel (this renderer's own
//     established sRGB-swapchain-gamma caveat, see webgpu_littextured3d_test.cpp's Check D
//     comment), only as "clearly brighter than black" (Check B) and via a relative comparison
//     that stays valid under any monotonic gamma encoding (Check E).
//   - the same geometry with metallic=1, red albedo: F0=albedo=(1,0,0), diffuseColor term is
//     exactly zero (albedo*(1-metallic)), so Lo_red = D*G*F0_red/4 ~= 0.0796 (red-only specular
//     sheen, G/B channels exactly zero) -- strictly darker in R than the metallic=0 (dielectric)
//     case's Lo_red ~= 0.309 (Check E), proving MetallicFactor actually reaches the BRDF.
//
// Check A -- AmbientLightColor=(1,1,1), all 3 directional lights disabled, white base color, no
//   normal/metallic-roughness/emissive/occlusion maps bound: ambient = ambientColor*albedo*
//   occlusion(=1, default white fallback) = white, Lo=0 (no lights) -- renders white. Also proves
//   the stride-48 dispatch actually reaches a real pipeline (not silently falling back to
//   colored3d.wgsl's own stride-16 vertex layout on a 48-byte buffer, which would render garbage).
// Check B -- AmbientLightColor=black, DirectionalLight0 facing the visible side of the quad,
//   white base color: renders clearly non-black (proves a light reaching the shader and the BRDF
//   producing real energy, not silently zero).
// Check C -- same as B but DirectionalLight0.Direction flipped (hits the back of the quad, not
//   the visible face): NdotL clamps to 0, Lo=0, ambient=0 -- renders black.
// Check D -- same as B but with an explicit normal map tilting the tangent-space normal 90
//   degrees toward the tangent direction (encoded (255,128,128), decodes to (1,0,0)): the
//   resulting world-space normal becomes exactly the tangent (1,0,0), perpendicular to the
//   light, so NdotL=0 and the result is black -- proves the normal map is actually sampled and
//   the TBN basis genuinely perturbs the lit result, not silently ignored.
// Check E -- red base color, same facing-light geometry as B: MetallicFactor=1 renders strictly
//   darker in the red channel than MetallicFactor=0 -- proves MetallicFactor reaches the BRDF
//   (metallic zeroes the Lambertian diffuse term, leaving only a much dimmer specular sheen).
//
// Exit code 0 = all checks PASS, 1 = any FAILs.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Vector4.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/DirectionalLight.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PbrEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
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

    // Matches VertexPositionNormalTangentTexture's 48-byte layout (Position12+Normal12+
    // Tangent16+TextureCoordinate8) -- ApplyLayout's/QueuePbrDraw's stride==48 case.
    struct PbrGpuVertex
    {
        float px, py, pz;
        float nx, ny, nz;
        float tx, ty, tz, tw;
        float u, v;
    };
    static_assert(sizeof(PbrGpuVertex) == 48, "PBR vertex must be 48 bytes");

    Color readCenter(GraphicsDevice& dev)
    {
        const Rectangle region(kSize / 2, kSize / 2, 1, 1);
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

    VertexBuffer MakeFacingQuad(GraphicsDevice& dev)
    {
        // Normal=(0,0,-1) faces the camera at the origin (quad sits at z=0.5); Tangent=(1,0,0,1)
        // is perpendicular to the normal with a positive bitangent handedness, matching glTF's
        // TANGENT accessor convention.
        const PbrGpuVertex tl{ -1,  1, 0.5f,  0,0,-1,  1,0,0,1,  0,0 };
        const PbrGpuVertex bl{ -1, -1, 0.5f,  0,0,-1,  1,0,0,1,  0,1 };
        const PbrGpuVertex br{  1, -1, 0.5f,  0,0,-1,  1,0,0,1,  1,1 };
        const PbrGpuVertex tr{  1,  1, 0.5f,  0,0,-1,  1,0,0,1,  1,0 };
        const std::vector<PbrGpuVertex> verts = { tl, bl, br, tl, br, tr };

        VertexBuffer vb(dev, static_cast<int>(verts.size()));
        vb.SetDataRaw(verts.data(), static_cast<int>(verts.size()), static_cast<int>(sizeof(PbrGpuVertex)));
        return vb;
    }
}

class WebGpuPbr3DTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    Texture2D whiteTex_;
    Texture2D redTex_;
    Texture2D tiltedNormalTex_;
    bool done_ = false;
    int passCount_ = 0;
    int result_ = 1;

    void check(bool ok, const char* label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label);
        if (ok) ++passCount_;
    }

protected:
    void LoadContent() override
    {
        whiteTex_ = Texture2D::CreateFromPixels(getGraphicsDeviceProperty(), 1, 1,
                                                 std::vector<std::uint8_t>{255, 255, 255, 255});
        redTex_ = Texture2D::CreateFromPixels(getGraphicsDeviceProperty(), 1, 1,
                                               std::vector<std::uint8_t>{255, 0, 0, 255});
        // Tangent-space normal tilted 90 degrees toward +X: encoded (255,128,128) decodes to
        // (1,0,0) after the shader's rgb*2-1.
        tiltedNormalTex_ = Texture2D::CreateFromPixels(getGraphicsDeviceProperty(), 1, 1,
                                                        std::vector<std::uint8_t>{255, 128, 128, 255});
    }

    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;

        auto& dev = getGraphicsDeviceProperty();
        dev.setRasterizerStateProperty(RasterizerState::CullNone);
        dev.setDepthStencilStateProperty(DepthStencilState::None);

        // Check A: ambient-only -- no directional lights, white ambient, white base color.
        {
            dev.Clear(Color::Black);
            VertexBuffer vb = MakeFacingQuad(dev);
            PbrEffect fx(dev);
            fx.setWorldProperty(Matrix::getIdentityProperty());
            fx.setViewProperty(Matrix::getIdentityProperty());
            fx.setProjectionProperty(Matrix::getIdentityProperty());
            fx.setTextureProperty(&whiteTex_);
            fx.setRoughnessFactorProperty(1.0f);
            fx.setMetallicFactorProperty(0.0f);
            fx.setAmbientLightColorProperty(Vector3(1.0f, 1.0f, 1.0f));
            fx.DirectionalLight0.setEnabledProperty(false);
            fx.DirectionalLight1.setEnabledProperty(false);
            fx.DirectionalLight2.setEnabledProperty(false);
            fx.Apply();
            dev.SetVertexBuffer(&vb);
            dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
            dev.SetVertexBuffer(nullptr);
            check(isNearWhite(readCenter(dev)),
                  "ambient alone (no directional lights) renders white");
        }

        // Check B: one light facing the visible side of the quad -- clearly non-black.
        {
            dev.Clear(Color::Black);
            VertexBuffer vb = MakeFacingQuad(dev);
            PbrEffect fx(dev);
            fx.setWorldProperty(Matrix::getIdentityProperty());
            fx.setViewProperty(Matrix::getIdentityProperty());
            fx.setProjectionProperty(Matrix::getIdentityProperty());
            fx.setTextureProperty(&whiteTex_);
            fx.setRoughnessFactorProperty(1.0f);
            fx.setMetallicFactorProperty(0.0f);
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
            check(!isNearBlack(readCenter(dev)),
                  "a light facing the surface produces real (non-black) BRDF energy");
        }

        // Check C: the same light, now shining on the back of the quad -- NdotL clamps to 0.
        {
            dev.Clear(Color::Black);
            VertexBuffer vb = MakeFacingQuad(dev);
            PbrEffect fx(dev);
            fx.setWorldProperty(Matrix::getIdentityProperty());
            fx.setViewProperty(Matrix::getIdentityProperty());
            fx.setProjectionProperty(Matrix::getIdentityProperty());
            fx.setTextureProperty(&whiteTex_);
            fx.setRoughnessFactorProperty(1.0f);
            fx.setMetallicFactorProperty(0.0f);
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
            check(isNearBlack(readCenter(dev)),
                  "a light behind the surface contributes no diffuse or specular (black)");
        }

        // Check D: same facing light as B, but a normal map tilts the surface 90 degrees away.
        {
            dev.Clear(Color::Black);
            VertexBuffer vb = MakeFacingQuad(dev);
            PbrEffect fx(dev);
            fx.setWorldProperty(Matrix::getIdentityProperty());
            fx.setViewProperty(Matrix::getIdentityProperty());
            fx.setProjectionProperty(Matrix::getIdentityProperty());
            fx.setTextureProperty(&whiteTex_);
            fx.setNormalMapProperty(&tiltedNormalTex_);
            fx.setRoughnessFactorProperty(1.0f);
            fx.setMetallicFactorProperty(0.0f);
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
            check(isNearBlack(readCenter(dev)),
                  "a normal map tilting the surface 90 degrees away from the light zeroes NdotL (black)");
        }

        // Check E: red base color, same facing light -- MetallicFactor=1 must render strictly
        // darker (in the red channel) than MetallicFactor=0.
        int metallicRed = 0;
        int dielectricRed = 0;
        for (int pass = 0; pass < 2; ++pass)
        {
            dev.Clear(Color::Black);
            VertexBuffer vb = MakeFacingQuad(dev);
            PbrEffect fx(dev);
            fx.setWorldProperty(Matrix::getIdentityProperty());
            fx.setViewProperty(Matrix::getIdentityProperty());
            fx.setProjectionProperty(Matrix::getIdentityProperty());
            fx.setTextureProperty(&redTex_);
            fx.setRoughnessFactorProperty(1.0f);
            fx.setMetallicFactorProperty(pass == 0 ? 1.0f : 0.0f);
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
            const int r = readCenter(dev).getRProperty();
            if (pass == 0) metallicRed = r; else dielectricRed = r;
        }
        std::printf("    (metallicRed=%d dielectricRed=%d)\n", metallicRed, dielectricRed);
        check(metallicRed < dielectricRed,
              "MetallicFactor=1 renders strictly darker (red channel) than MetallicFactor=0");

        std::printf("=== %d/5 PASS ===\n", passCount_);
        result_ = (passCount_ == 5) ? 0 : 1;
        Exit();
    }

public:
    WebGpuPbr3DTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kSize);
        gdm_->setPreferredBackBufferHeightProperty(kSize);
    }

    int getResult() const { return result_; }
};

int main()
{
    WebGpuPbr3DTest game;
    game.Run();
    return game.getResult();
}
