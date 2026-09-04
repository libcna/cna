// SPDX-License-Identifier: MS-PL
// PbrEffect pixel test (LLGL renderer) -- proves the stride-48 VertexPositionNormalTangentTexture
// pipeline, TBN construction, and the glTF metallic-roughness BRDF fragment stage (pbr3d.vert/
// frag.glsl) work end-to-end via a real GPU draw.
//
// Adapted (not verbatim, unlike some other ported LLGL tests) from
// examples/vulkan_pbreffect_handderived_test.cpp, which is itself already fully renderer-agnostic
// (real public XNA API + VertexBuffer::SetDataRaw, no Vulkan-specific code at all). One
// deliberate difference from that source:
//   - Draws into an off-screen RenderTarget2D sized kSize x kSize and reads back with GetData(),
//     matching this renderer's own PixelTestGame convention (see llgl_shadereffect_test.cpp/
//     llgl_rendertargetcube_test.cpp), rather than the Vulkan source's own hand-rolled Game
//     subclass that shrinks the WHOLE WINDOW's back buffer to kSize via its own
//     GraphicsDeviceManager. PixelTestGame's Game construction does not expose a way to override
//     the default back-buffer size, and reading a hard-coded small pixel address (kSize/2,kSize/2)
//     from the real (much larger) default back buffer would silently sample nowhere near the
//     quad's projected centre -- found empirically: GetBackBufferData(Rectangle(32,32,1,1)) on an
//     un-resized ~800x480 default back buffer read a world position over a full unit away from
//     the coordinate origin the analytic derivation below assumes.
//
// This scene uses a REAL perspective camera aimed straight down -Z at a flat quad centred on the
// origin, sampling only the exact centre texel of the render target -- at that one texel, the
// world-space position is exactly (0,0,0), so V = normalize(eye-worldPos) = (0,0,1) = N (the
// quad's flat, unperturbed geometric normal) exactly. Choosing light0's direction as (0,0,-1) too
// makes L = -light0Dir = (0,0,1) = N = V as well, so every dot product in the BRDF (NdotL, NdotV,
// NdotH, VdotH) is exactly 1 at that texel -- a fully analytically tractable case, independently
// re-derived in Python (see each check's own comment) from the exact same GGX/Smith-Schlick-GGX/
// Schlick-Fresnel formula PbrLight() in pbr3d.frag.glsl implements, not just captured-and-pasted
// from a single run.
//
// Exit code 0 = PASS, 1 = FAIL.

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/MathHelper.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PbrEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/SkinnedPbrEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"

#include "common/PixelTestGame.hpp"

#include <cmath>
#include <cstdint>
#include <memory>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kSize = 64;

    // Stride-48: matches pbr3d.vert.glsl's attribute layout exactly (position/normal/tangent/uv).
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

    // A single quad, large enough that the render target's centre texel is always covered
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

class LlglPbrEffectHandDerivedTest : public CNA::Examples::PixelTestGame
{
public:
    // Renders one quad with PbrEffect into a kSize x kSize RenderTarget2D and returns its centre
    // texel.
    Color RenderPbr(GraphicsDevice& dev, Texture2D& albedoTex, float metallic, float roughness,
                    const Vector3& light0Dir, const Vector3& light0Diffuse)
    {
        RenderTarget2D renderTarget(dev, kSize, kSize, false, SurfaceFormat::Color, DepthFormat::None);

        PbrEffect fx(dev);
        fx.setBaseColorTextureIsSrgbEXTProperty(false);
        fx.setEmissiveTextureIsSrgbEXTProperty(false);
        fx.setEncodeOutputToSrgbEXTProperty(false);
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

        dev.SetRenderTarget(&renderTarget);
        dev.Clear(Color(0, 255, 0, 255));
        dev.SetDepthTestEnabled(false);
        dev.setBlendStateProperty(BlendState::Opaque);
        dev.setRasterizerStateProperty(RasterizerState::CullNone);
        dev.SetVertexBuffer(&vb);
        fx.Apply();
        dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
        dev.SetRenderTarget(nullptr);

        Color got(0, 0, 0, 0);
        const Rectangle region(kSize / 2, kSize / 2, 1, 1);
        renderTarget.GetData(0, &region, &got, 0, 1);
        return got;
    }

    // Renders the identical scene as RenderPbr(white,0.5,0.0,...) via SkinnedPbrEffect with a
    // single identity bone (weight 1.0 on bone 0, default Identity) -- a mathematical no-op skin
    // transform, so the result must equal PbrEffect's own (mirrors
    // vulkan_pbreffect_handderived_test.cpp's own oracle).
    Color RenderSkinnedPbrIdentity(GraphicsDevice& dev, Texture2D& albedoTex)
    {
        RenderTarget2D renderTarget(dev, kSize, kSize, false, SurfaceFormat::Color, DepthFormat::None);

        SkinnedPbrEffect fx(dev);
        fx.setBaseColorTextureIsSrgbEXTProperty(false);
        fx.setEmissiveTextureIsSrgbEXTProperty(false);
        fx.setEncodeOutputToSrgbEXTProperty(false);
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

        dev.SetRenderTarget(&renderTarget);
        dev.Clear(Color(0, 255, 0, 255));
        dev.SetDepthTestEnabled(false);
        dev.setBlendStateProperty(BlendState::Opaque);
        dev.setRasterizerStateProperty(RasterizerState::CullNone);
        dev.SetVertexBuffer(&vb);
        fx.Apply();
        dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
        dev.SetRenderTarget(nullptr);

        Color got(0, 0, 0, 0);
        const Rectangle region(kSize / 2, kSize / 2, 1, 1);
        renderTarget.GetData(0, &region, &got, 0, 1);
        return got;
    }

    void RunTest() override
    {
        auto& dev = getGraphicsDeviceProperty();

        const std::vector<std::uint8_t> white = { 255, 255, 255, 255 };
        Texture2D whiteTex = Texture2D::CreateFromPixels(dev, 1, 1, white);
        const std::vector<std::uint8_t> red = { 255, 0, 0, 255 };
        Texture2D redTex = Texture2D::CreateFromPixels(dev, 1, 1, red);

        // (a) White albedo, roughness=0.5, metallic=0.0, light0=(0,0,-1)/(1,1,1), no ambient.
        // At the centre texel N=V=L=(0,0,1) exactly (see file header), so every dot product is 1
        // and PbrLight()'s D/G/F/specular/diffuse terms all reduce to closed-form constants:
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
        const Color a = RenderPbr(dev, whiteTex, 0.0f, 0.5f, dirStraight, Vector3(1.0f, 1.0f, 1.0f));
        ExpectTrue("(a) PbrEffect white/rough=0.5/metallic=0: analytic BRDF value",
                  std::abs(a.getRProperty() - 91) <= 12 && std::abs(a.getGProperty() - 91) <= 12 &&
                  std::abs(a.getBProperty() - 91) <= 12);

        // (b) Red albedo, roughness=0.5, metallic=1.0 (fully metallic), light0 diffuse=(0.3,..).
        //   F0 = mix(0.04, (1,0,0), 1) = (1,0,0); diffuseColor = albedo*(1-1) = 0 (no Lambertian term)
        //   specular.r = D*G*F0.r/4 = 5.092958*1*1/4 = 1.273239; specular.g=specular.b=0 (F0=0)
        //   Lo.r = (0*... + 1.273239)*0.3*1 = 0.381972 -> round(*255) = 97; Lo.g=Lo.b=0
        const Color b = RenderPbr(dev, redTex, 1.0f, 0.5f, dirStraight, Vector3(0.3f, 0.3f, 0.3f));
        ExpectTrue("(b) PbrEffect red/rough=0.5/metallic=1 (fully metallic): analytic BRDF value",
                  std::abs(b.getRProperty() - 97) <= 14 && b.getGProperty() <= 14 && b.getBProperty() <= 14);

        // (c) Same as (b) but metallic=0.0 (fully dielectric): F0=(0.04,0.04,0.04),
        //   diffuseColor=albedo=(1,0,0). kd=(0.96,0.96,0.96).
        //   Lo.r = (0.96*1/pi + 5.092958*1*0.04/4)*0.3 = (0.305577+0.050930)*0.3 = 0.106952 -> 27
        //   Lo.g = Lo.b = (0.96*0/pi + 5.092958*1*0.04/4)*0.3 = 0.050930*0.3 = 0.015279 -> 4
        const Color c = RenderPbr(dev, redTex, 0.0f, 0.5f, dirStraight, Vector3(0.3f, 0.3f, 0.3f));
        ExpectTrue("(c) PbrEffect red/rough=0.5/metallic=0 (dielectric): analytic BRDF value",
                  std::abs(c.getRProperty() - 27) <= 14 && std::abs(c.getGProperty() - 4) <= 14 &&
                  std::abs(c.getBProperty() - 4) <= 14);
        ExpectTrue("(b) metallic differs from (c) dielectric -- MetallicFactor genuinely changes the BRDF",
                  std::abs(b.getRProperty() - c.getRProperty()) > 10 ||
                  std::abs(b.getGProperty() - c.getGProperty()) > 10);

        // (d) SkinnedPbrEffect, identical scene to (a), single identity bone -- must reproduce
        // (a)'s own value exactly (mathematical no-op skin transform).
        const Color d = RenderSkinnedPbrIdentity(dev, whiteTex);
        ExpectTrue("(d) SkinnedPbrEffect identity bone reproduces PbrEffect (a)'s own value",
                  std::abs(d.getRProperty() - a.getRProperty()) <= 10 &&
                  std::abs(d.getGProperty() - a.getGProperty()) <= 10 &&
                  std::abs(d.getBProperty() - a.getBProperty()) <= 10);
    }
};

int main()
{
    return CNA::Examples::RunPixelTest<LlglPbrEffectHandDerivedTest>();
}
