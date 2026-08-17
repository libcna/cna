// SPDX-License-Identifier: MS-PL
// PbrEffect / SkinnedPbrEffect pixel test (Vulkan renderer) -- proves the stride-48
// VertexPositionNormalTangentTexture / stride-68 VertexPositionNormalTangentTextureSkinned
// pipelines, TBN construction, and the glTF metallic-roughness BRDF fragment stage
// (pbr3d.vert/frag.glsl, pbr3d_skinned.vert/frag.glsl) work end-to-end via a real GPU draw.
//
// Unlike easygl_pbreffect_golden_test.cpp (also registered for Vulkan, reused verbatim, see
// VulkanTests.cmake) this scene uses a REAL perspective camera aimed straight down -Z at a flat
// quad centred on the origin, sampling only the exact centre pixel of the backbuffer -- at that
// one pixel, the world-space position is exactly (0,0,0), so V = normalize(eye-worldPos) =
// (0,0,1) = N (the quad's flat, unperturbed geometric normal) exactly. Choosing light0's
// direction as (0,0,-1) too makes L = -light0Dir = (0,0,1) = N = V as well, so every dot product
// in the BRDF (NdotL, NdotV, NdotH, VdotH) is exactly 1 at that pixel -- a fully analytically
// tractable case, independently re-derived below in Python (see each check's own comment) from
// the exact same GGX/Smith-Schlick-GGX/Schlick-Fresnel formula PbrLight() in pbr3d.frag.glsl
// implements, not just captured-and-pasted from a single run.
//
// Exit code 0 = PASS, 1 = FAIL.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/MathHelper.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PbrEffect.hpp"
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
    // Stride-48: matches VulkanRenderer's pbr3d.vert.glsl attribute layout exactly
    // (position/normal/tangent/uv), and ApplyLayout's stride==48 case on EasyGL.
    struct PbrGpuVertex
    {
        float px, py, pz;
        float nx, ny, nz;
        float tx, ty, tz, tw;
        float u, v;
    };
    static_assert(sizeof(PbrGpuVertex) == 48, "PBR vertex must be 48 bytes");

    // Stride-68: the above with BlendWeight/BlendIndices appended (pbr3d_skinned.vert.glsl).
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

    // A single quad, large enough that the backbuffer's centre pixel is always covered
    // regardless of exact FOV/aspect rounding, flat normal (0,0,1), tangent (1,0,0,1).
    template <typename V>
    std::vector<V> MakeQuad()
    {
        V tl{}, bl{}, br{}, tr{};
        auto fill = [](V& v, float x, float y) {
            v.px = x; v.py = y; v.pz = 0.0f;
            v.nx = 0.0f; v.ny = 0.0f; v.nz = 1.0f;
            v.tx = 1.0f; v.ty = 0.0f; v.tz = 0.0f; v.tw = 1.0f;
        };
        fill(tl, -4.0f,  4.0f); tl.u = 0.0f; tl.v = 0.0f;
        fill(bl, -4.0f, -4.0f); bl.u = 0.0f; bl.v = 1.0f;
        fill(br,  4.0f, -4.0f); br.u = 1.0f; br.v = 1.0f;
        fill(tr,  4.0f,  4.0f); tr.u = 1.0f; tr.v = 0.0f;
        return { tl, bl, br, tl, br, tr };
    }
}

static constexpr int kSize = 64;

static bool closeTo(int a, int b, int tol) { return std::abs(a - b) <= tol; }
static bool matches(const Color& c, const Color& expected, int tol = 10)
{
    return closeTo(c.getRProperty(), expected.getRProperty(), tol)
        && closeTo(c.getGProperty(), expected.getGProperty(), tol)
        && closeTo(c.getBProperty(), expected.getBProperty(), tol);
}

class VulkanPbrEffectHandDerivedTest : public Game
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

    static Matrix ViewProj()
    {
        return Matrix::CreateLookAt(Vector3(0.0f, 0.0f, 3.0f), Vector3::Zero, Vector3(0.0f, 1.0f, 0.0f))
             * Matrix::CreatePerspectiveFieldOfView(MathHelper::PiOver4, 1.0f, 0.1f, 100.0f);
    }

    template <typename Effect>
    void UseLinearTransferForAnalyticCases(Effect& effect)
    {
        // These pre-existing witnesses derive and compare the shader's linear BRDF value. Keep
        // that contract explicit now that Vulkan honors the public glTF colour-space controls.
        effect.setBaseColorTextureIsSrgbEXTProperty(false);
        effect.setEmissiveTextureIsSrgbEXTProperty(false);
        effect.setEncodeOutputToSrgbEXTProperty(false);
    }

    // Renders one quad with PbrEffect and returns the centre pixel.
    Color renderPbr(GraphicsDevice& dev, Texture2D& albedoTex,
                     float metallic, float roughness,
                     const Vector3& light0Dir, const Vector3& light0Diffuse)
    {
        PbrEffect fx(dev);
        UseLinearTransferForAnalyticCases(fx);
        fx.setTextureProperty(&albedoTex);
        fx.setNormalMapProperty(nullptr);
        fx.setMetallicFactorProperty(metallic);
        fx.setRoughnessFactorProperty(roughness);
        fx.setAmbientLightColorProperty(Vector3::Zero);
        fx.DirectionalLight0.setEnabledProperty(true);
        fx.DirectionalLight0.setDirectionProperty(light0Dir);
        fx.DirectionalLight0.setDiffuseColorProperty(light0Diffuse);
        fx.DirectionalLight1.setEnabledProperty(false);
        fx.DirectionalLight2.setEnabledProperty(false);
        fx.setWorldProperty(Matrix::getIdentityProperty());
        fx.setViewProperty(Matrix::CreateLookAt(Vector3(0.0f, 0.0f, 3.0f), Vector3::Zero, Vector3(0.0f, 1.0f, 0.0f)));
        fx.setProjectionProperty(Matrix::CreatePerspectiveFieldOfView(MathHelper::PiOver4, 1.0f, 0.1f, 100.0f));

        auto verts = MakeQuad<PbrGpuVertex>();
        VertexBuffer vb(dev, static_cast<int>(verts.size()));
        vb.SetDataRaw(verts.data(), static_cast<int>(verts.size()), static_cast<int>(sizeof(PbrGpuVertex)));

        Color got(0, 0, 0, 0);
        for (int i = 0; i < 20; ++i) {
            dev.Clear(Color(0, 255, 0, 255));
            dev.SetDepthTestEnabled(false);
            dev.setBlendStateProperty(BlendState::Opaque);
            dev.setRasterizerStateProperty(RasterizerState::CullNone);
            dev.SetVertexBuffer(&vb);
            fx.Apply();
            dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
            got = readCenter(dev);
            if (got.getRProperty() != 0 || got.getGProperty() != 0 || got.getBProperty() != 0)
                break;
        }
        return got;
    }

    // Renders the identical scene as renderPbr(white,0.5,0.0,...) via SkinnedPbrEffect with a
    // single identity bone (weight 1.0 on bone 0, default Identity) -- a mathematical no-op skin
    // transform, so the result must equal PbrEffect's own (mirrors
    // easygl_skinnedpbreffect_golden_test.cpp's own oracle).
    Color renderSkinnedPbrIdentity(GraphicsDevice& dev, Texture2D& albedoTex)
    {
        SkinnedPbrEffect fx(dev);
        UseLinearTransferForAnalyticCases(fx);
        fx.setTextureProperty(&albedoTex);
        fx.setNormalMapProperty(nullptr);
        fx.setMetallicFactorProperty(0.0f);
        fx.setRoughnessFactorProperty(0.5f);
        fx.setAmbientLightColorProperty(Vector3::Zero);
        fx.DirectionalLight0.setEnabledProperty(true);
        fx.DirectionalLight0.setDirectionProperty(Vector3(0.0f, 0.0f, -1.0f));
        fx.DirectionalLight0.setDiffuseColorProperty(Vector3(1.0f, 1.0f, 1.0f));
        fx.DirectionalLight1.setEnabledProperty(false);
        fx.DirectionalLight2.setEnabledProperty(false);
        fx.setWorldProperty(Matrix::getIdentityProperty());
        fx.setViewProperty(Matrix::CreateLookAt(Vector3(0.0f, 0.0f, 3.0f), Vector3::Zero, Vector3(0.0f, 1.0f, 0.0f)));
        fx.setProjectionProperty(Matrix::CreatePerspectiveFieldOfView(MathHelper::PiOver4, 1.0f, 0.1f, 100.0f));
        std::vector<Matrix> bones = { Matrix::getIdentityProperty() };
        fx.SetBoneTransforms(bones);
        fx.setWeightsPerVertexProperty(1);

        auto verts = MakeQuad<SkinnedPbrGpuVertex>();
        for (auto& v : verts) { v.w0 = 1.0f; v.w1 = v.w2 = v.w3 = 0.0f; v.i0 = v.i1 = v.i2 = v.i3 = 0; }
        VertexBuffer vb(dev, static_cast<int>(verts.size()));
        vb.SetDataRaw(verts.data(), static_cast<int>(verts.size()), static_cast<int>(sizeof(SkinnedPbrGpuVertex)));

        Color got(0, 0, 0, 0);
        for (int i = 0; i < 20; ++i) {
            dev.Clear(Color(0, 255, 0, 255));
            dev.SetDepthTestEnabled(false);
            dev.setBlendStateProperty(BlendState::Opaque);
            dev.setRasterizerStateProperty(RasterizerState::CullNone);
            dev.SetVertexBuffer(&vb);
            fx.Apply();
            dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
            got = readCenter(dev);
            if (got.getRProperty() != 0 || got.getGProperty() != 0 || got.getBProperty() != 0)
                break;
        }
        return got;
    }

    // GLTF-264: the joint scale must affect the normal through inverse-transpose, not through the
    // position/direction matrix. This is deliberately separate from the identity-bone smoke above.
    Color renderSkinnedPbrNonUniformJointNormal(GraphicsDevice& dev, Texture2D& albedoTex)
    {
        SkinnedPbrEffect fx(dev);
        UseLinearTransferForAnalyticCases(fx);
        fx.setTextureProperty(&albedoTex);
        fx.setNormalMapProperty(nullptr);
        fx.setMetallicFactorProperty(0.0f);
        fx.setRoughnessFactorProperty(1.0f);
        fx.setAmbientLightColorProperty(Vector3::Zero);
        fx.DirectionalLight0.setEnabledProperty(true);
        fx.DirectionalLight0.setDirectionProperty(Vector3(0.0f, -1.0f, 0.0f));
        fx.DirectionalLight0.setDiffuseColorProperty(Vector3::One);
        fx.DirectionalLight1.setEnabledProperty(false);
        fx.DirectionalLight2.setEnabledProperty(false);
        fx.setWorldProperty(Matrix::getIdentityProperty());
        fx.setViewProperty(Matrix::CreateLookAt(
            Vector3(0.0f, 0.0f, 3.0f), Vector3::Zero, Vector3(0.0f, 1.0f, 0.0f)));
        fx.setProjectionProperty(Matrix::CreatePerspectiveFieldOfView(
            MathHelper::PiOver4, 1.0f, 0.1f, 100.0f));
        fx.SetBoneTransforms({Matrix::CreateScale(1.0f, 2.0f, 1.0f)});
        fx.setWeightsPerVertexProperty(1);

        auto verts = MakeQuad<SkinnedPbrGpuVertex>();
        for (auto& vertex : verts)
        {
            vertex.nx = 0.0f; vertex.ny = 0.6f; vertex.nz = 0.8f;
            vertex.w0 = 1.0f; vertex.w1 = vertex.w2 = vertex.w3 = 0.0f;
            vertex.i0 = vertex.i1 = vertex.i2 = vertex.i3 = 0;
        }
        VertexBuffer vb(dev, static_cast<int>(verts.size()));
        vb.SetDataRaw(verts.data(), static_cast<int>(verts.size()),
                      static_cast<int>(sizeof(SkinnedPbrGpuVertex)));

        Color got(0, 0, 0, 0);
        for (int i = 0; i < 20; ++i)
        {
            dev.Clear(Color(0, 255, 0, 255));
            dev.SetDepthTestEnabled(false);
            dev.setBlendStateProperty(BlendState::Opaque);
            dev.setRasterizerStateProperty(RasterizerState::CullNone);
            dev.SetVertexBuffer(&vb);
            fx.Apply();
            dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
            got = readCenter(dev);
            if (got.getRProperty() != 0 || got.getGProperty() != 0 || got.getBProperty() != 0)
                break;
        }
        return got;
    }

    // GLTF-176: a mirrored World reverses the transformed tangent frame. The shader must fold
    // sign(det(World3x3)) into tangent.w so this +Y tangent-space normal remains world +Y.
    Color renderPbrMirroredWorldTangent(GraphicsDevice& dev, Texture2D& albedoTex,
                                        Texture2D& positiveYNormal)
    {
        PbrEffect fx(dev);
        UseLinearTransferForAnalyticCases(fx);
        fx.setTextureProperty(&albedoTex);
        fx.setNormalMapProperty(&positiveYNormal);
        fx.setMetallicFactorProperty(0.0f);
        fx.setRoughnessFactorProperty(1.0f);
        fx.setAmbientLightColorProperty(Vector3::Zero);
        fx.DirectionalLight0.setEnabledProperty(true);
        fx.DirectionalLight0.setDirectionProperty(Vector3(0.0f, -1.0f, 0.0f));
        fx.DirectionalLight0.setDiffuseColorProperty(Vector3::One);
        fx.DirectionalLight1.setEnabledProperty(false);
        fx.DirectionalLight2.setEnabledProperty(false);
        fx.setWorldProperty(Matrix::CreateScale(-1.0f, 1.0f, 1.0f));
        fx.setViewProperty(Matrix::CreateLookAt(
            Vector3(0.0f, 0.0f, 3.0f), Vector3::Zero, Vector3(0.0f, 1.0f, 0.0f)));
        fx.setProjectionProperty(Matrix::CreatePerspectiveFieldOfView(
            MathHelper::PiOver4, 1.0f, 0.1f, 100.0f));

        auto verts = MakeQuad<PbrGpuVertex>();
        VertexBuffer vb(dev, static_cast<int>(verts.size()));
        vb.SetDataRaw(verts.data(), static_cast<int>(verts.size()),
                      static_cast<int>(sizeof(PbrGpuVertex)));

        Color got(0, 0, 0, 0);
        for (int i = 0; i < 20; ++i)
        {
            dev.Clear(Color(0, 255, 0, 255));
            dev.SetDepthTestEnabled(false);
            dev.setBlendStateProperty(BlendState::Opaque);
            dev.setRasterizerStateProperty(RasterizerState::CullNone);
            dev.SetVertexBuffer(&vb);
            fx.Apply();
            dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
            got = readCenter(dev);
            if (got.getRProperty() != 0 || got.getGProperty() != 0 || got.getBProperty() != 0)
                break;
        }
        return got;
    }

    // The same handedness rule applies to a negative-determinant blended joint matrix. World is
    // identity here, so this case cannot accidentally pass on the World correction alone.
    Color renderSkinnedPbrMirroredJointTangent(GraphicsDevice& dev, Texture2D& albedoTex,
                                               Texture2D& positiveYNormal)
    {
        SkinnedPbrEffect fx(dev);
        UseLinearTransferForAnalyticCases(fx);
        fx.setTextureProperty(&albedoTex);
        fx.setNormalMapProperty(&positiveYNormal);
        fx.setMetallicFactorProperty(0.0f);
        fx.setRoughnessFactorProperty(1.0f);
        fx.setAmbientLightColorProperty(Vector3::Zero);
        fx.DirectionalLight0.setEnabledProperty(true);
        fx.DirectionalLight0.setDirectionProperty(Vector3(0.0f, -1.0f, 0.0f));
        fx.DirectionalLight0.setDiffuseColorProperty(Vector3::One);
        fx.DirectionalLight1.setEnabledProperty(false);
        fx.DirectionalLight2.setEnabledProperty(false);
        fx.setWorldProperty(Matrix::getIdentityProperty());
        fx.setViewProperty(Matrix::CreateLookAt(
            Vector3(0.0f, 0.0f, 3.0f), Vector3::Zero, Vector3(0.0f, 1.0f, 0.0f)));
        fx.setProjectionProperty(Matrix::CreatePerspectiveFieldOfView(
            MathHelper::PiOver4, 1.0f, 0.1f, 100.0f));
        fx.SetBoneTransforms({Matrix::CreateScale(-1.0f, 1.0f, 1.0f)});
        fx.setWeightsPerVertexProperty(1);

        auto verts = MakeQuad<SkinnedPbrGpuVertex>();
        for (auto& vertex : verts)
        {
            vertex.w0 = 1.0f; vertex.w1 = vertex.w2 = vertex.w3 = 0.0f;
            vertex.i0 = vertex.i1 = vertex.i2 = vertex.i3 = 0;
        }
        VertexBuffer vb(dev, static_cast<int>(verts.size()));
        vb.SetDataRaw(verts.data(), static_cast<int>(verts.size()),
                      static_cast<int>(sizeof(SkinnedPbrGpuVertex)));

        Color got(0, 0, 0, 0);
        for (int i = 0; i < 20; ++i)
        {
            dev.Clear(Color(0, 255, 0, 255));
            dev.SetDepthTestEnabled(false);
            dev.setBlendStateProperty(BlendState::Opaque);
            dev.setRasterizerStateProperty(RasterizerState::CullNone);
            dev.SetVertexBuffer(&vb);
            fx.Apply();
            dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
            got = readCenter(dev);
            if (got.getRProperty() != 0 || got.getGProperty() != 0 || got.getBProperty() != 0)
                break;
        }
        return got;
    }

protected:
    void Draw(const GameTime&) override
    {
        auto& dev = getGraphicsDeviceProperty();

        const std::vector<std::uint8_t> white = { 255, 255, 255, 255 };
        Texture2D whiteTex = Texture2D::CreateFromPixels(dev, 1, 1, white);
        const std::vector<std::uint8_t> red = { 255, 0, 0, 255 };
        Texture2D redTex = Texture2D::CreateFromPixels(dev, 1, 1, red);
        const std::vector<std::uint8_t> positiveY = { 128, 255, 128, 255 };
        Texture2D positiveYNormal = Texture2D::CreateFromPixels(dev, 1, 1, positiveY);

        // (a) White albedo, roughness=0.5, metallic=0.0, light0=(0,0,-1)/(1,1,1), no ambient.
        // At the centre pixel N=V=L=(0,0,1) exactly (see file header), so every dot product is
        // 1 and PbrLight()'s D/G/F/specular/diffuse terms all reduce to closed-form constants:
        //   a2 = 0.5^4 = 0.0625; dTerm = 1*(a2-1)+1 = a2 = 0.0625
        //   D  = a2 / (pi*a2^2)  = 5.092958...
        //   k  = (0.5+1)^2/8 = 0.28125; G = (1/(1*(1-k)+k))^2 = 1 (NdotV=NdotL=1 cancels exactly)
        //   F  = F0 + (1-F0)*0^5 = F0 = mix(0.04, albedo, 0) = 0.04 (VdotH=1 -> (1-1)^5=0)
        //   specular = D*G*F / (4*1*1) = 5.092958*1*0.04/4 = 0.050930
        //   diffuseColor = albedo*(1-metallic) = 1; kd = 1-F = 0.96
        //   Lo = (kd*diffuseColor/pi + specular) * lightColor * NdotL
        //      = (0.96/3.14159265 + 0.050930) * 1 * 1 = 0.305577 + 0.050930 = 0.356507
        //   final = ambient(0) + Lo + emissive(0) = 0.356507 -> round(0.356507*255) = 91
        const Vector3 dirStraight(0.0f, 0.0f, -1.0f);
        const Color a = renderPbr(dev, whiteTex, 0.0f, 0.5f, dirStraight, Vector3(1.0f, 1.0f, 1.0f));
        check(matches(a, Color(91, 91, 91, 255), 12),
              "(a) PbrEffect white/rough=0.5/metallic=0: analytic BRDF value", a, "(91,91,91)");

        // (b) Red albedo, roughness=0.5, metallic=1.0 (fully metallic), light0 diffuse=(0.3,..).
        //   F0 = mix(0.04, (1,0,0), 1) = (1,0,0); diffuseColor = albedo*(1-1) = 0 (no Lambertian term)
        //   specular.r = D*G*F0.r/4 = 5.092958*1*1/4 = 1.273239; specular.g=specular.b=0 (F0=0)
        //   Lo.r = (0*... + 1.273239)*0.3*1 = 0.381972 -> round(*255) = 97; Lo.g=Lo.b=0
        const Color b = renderPbr(dev, redTex, 1.0f, 0.5f, dirStraight, Vector3(0.3f, 0.3f, 0.3f));
        check(matches(b, Color(97, 0, 0, 255), 14),
              "(b) PbrEffect red/rough=0.5/metallic=1 (fully metallic): analytic BRDF value", b, "(97,0,0)");

        // (c) Same as (b) but metallic=0.0 (fully dielectric): F0=(0.04,0.04,0.04),
        //   diffuseColor=albedo=(1,0,0). kd=(0.96,0.96,0.96).
        //   Lo.r = (0.96*1/pi + 5.092958*1*0.04/4)*0.3 = (0.305577+0.050930)*0.3 = 0.106952 -> 27
        //   Lo.g = Lo.b = (0.96*0/pi + 5.092958*1*0.04/4)*0.3 = 0.050930*0.3 = 0.015279 -> 4
        const Color c = renderPbr(dev, redTex, 0.0f, 0.5f, dirStraight, Vector3(0.3f, 0.3f, 0.3f));
        check(matches(c, Color(27, 4, 4, 255), 14),
              "(c) PbrEffect red/rough=0.5/metallic=0 (dielectric): analytic BRDF value", c, "(27,4,4)");
        check(!matches(b, c, 10), "(b) metallic differs from (c) dielectric -- MetallicFactor genuinely changes the BRDF",
              b, "!= (27,4,4)");

        // (d) SkinnedPbrEffect, identical scene to (a), single identity bone -- must reproduce
        // (a)'s own value exactly (mathematical no-op skin transform).
        const Color d = renderSkinnedPbrIdentity(dev, whiteTex);
        check(matches(d, a, 10),
              "(d) SkinnedPbrEffect identity bone reproduces PbrEffect (a)'s own value", d, "== (a)");

        // Inverse-transpose(S[1,2,1]) sends (0,.6,.8) to normalize(0,.3,.8). With L=+Y,
        // V=+Z and rough dielectric PBR this shader's linear output is 0.10886 -> byte 28. The
        // former direct joint transform normalized (0,1.2,.8) and produced about byte 66.
        const Color e = renderSkinnedPbrNonUniformJointNormal(dev, whiteTex);
        check(matches(e, Color(28, 28, 28, 255), 4),
              "(e) SkinnedPbrEffect inverse-transposes a non-uniform joint normal", e,
              "(28,28,28), not the old direct-transform (~66)");

        // Decoding (128,255,128) gives approximately tangent-space +Y. Both mirrors transform
        // T=+X to -X; multiplying the authored +1 by the negative determinant restores B=+Y.
        // With L=+Y, V=+Z and roughness 1 the analytic dielectric result is about byte 79. Omitting
        // the relevant determinant leaves B=-Y and therefore renders black.
        const Color f = renderPbrMirroredWorldTangent(dev, whiteTex, positiveYNormal);
        check(matches(f, Color(79, 79, 79, 255), 4),
              "(f) PbrEffect preserves TBN handedness under mirrored World", f,
              "about (79,79,79), not black");
        const Color g = renderSkinnedPbrMirroredJointTangent(dev, whiteTex, positiveYNormal);
        check(matches(g, Color(79, 79, 79, 255), 4),
              "(g) SkinnedPbrEffect preserves TBN handedness under a mirrored joint", g,
              "about (79,79,79), not black");

        std::printf("\nResult: %d/%d PASS\n", pass_, pass_ + fail_);
        Exit();
    }

public:
    VulkanPbrEffectHandDerivedTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kSize);
        gdm_->setPreferredBackBufferHeightProperty(kSize);
    }

    int getResult() const { return fail_ == 0 ? 0 : 1; }
};

int main()
{
    VulkanPbrEffectHandDerivedTest game;
    game.Run();
    return game.getResult();
}
