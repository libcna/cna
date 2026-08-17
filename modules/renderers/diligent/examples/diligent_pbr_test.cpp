// SPDX-License-Identifier: MS-PL
//
// plan_diligent.md DILIGENT-36: real-device proof of the glTF metallic-roughness BRDF (PbrEffect
// and SkinnedPbrEffect), using the same analytically hand-derived technique as
// vulkan_pbreffect_handderived_test.cpp.
//
// A flat quad (normal (0,0,1)) is viewed straight down -Z with the camera at (0,0,3) looking at
// the origin, and light0's direction is also (0,0,-1). At the backbuffer's exact centre pixel the
// world-space position is (0,0,0), so N = V = L = (0,0,1) exactly, collapsing every dot product in
// PbrLight() (NdotL, NdotV, NdotH, VdotH) to exactly 1 -- the whole BRDF reduces to a closed-form
// constant, independently re-derived in Python from the same GGX/Smith-Schlick-GGX/Schlick-Fresnel
// formula the shader implements (see each check's own comment), not just captured from one run.
//
// Check A -- PbrEffect, white albedo, roughness=0.5, metallic=0.0: analytic value (91,91,91).
// Check B -- PbrEffect, red albedo, roughness=0.5, metallic=1.0 (fully metallic): analytic value
//   (97,0,0).
// Check C -- PbrEffect, red albedo, roughness=0.5, metallic=0.0 (fully dielectric): analytic value
//   (27,4,4), and genuinely differs from check B -- MetallicFactor actually changes the BRDF, not
//   just a uniform that happens to be uploaded and ignored.
// Check D -- SkinnedPbrEffect, identical scene to check A, single identity bone (a mathematical
//   no-op skin transform) -- must reproduce check A's own value exactly.
//
// Exit code 0 = all checks PASS, 1 = any FAIL, 77 = no usable device.

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/MathHelper.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
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
#include <exception>
#include <memory>
#include <string>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kSize = 64;
    constexpr int kChecks = 5;

    // Stride 48: position/normal/tangent/uv, matching DiligentRenderer's own Pbr3D layout.
    struct PbrGpuVertex
    {
        float px, py, pz;
        float nx, ny, nz;
        float tx, ty, tz, tw;
        float u, v;
    };
    static_assert(sizeof(PbrGpuVertex) == 48, "PBR vertex must be 48 bytes");

    // A single quad, large enough that the backbuffer's centre pixel is always covered regardless
    // of exact FOV/aspect rounding; flat normal (0,0,1), tangent (1,0,0,1).
    std::vector<PbrGpuVertex> MakeQuad()
    {
        PbrGpuVertex tl{}, bl{}, br{}, tr{};
        auto fill = [](PbrGpuVertex& v, float x, float y) {
            v.px = x; v.py = y; v.pz = 0.0f;
            v.nx = 0.0f; v.ny = 0.0f; v.nz = 1.0f;
            v.tx = 1.0f; v.ty = 0.0f; v.tz = 0.0f; v.tw = 1.0f;
        };
        fill(tl, -4.0f, 4.0f); tl.u = 0.0f; tl.v = 0.0f;
        fill(bl, -4.0f, -4.0f); bl.u = 0.0f; bl.v = 1.0f;
        fill(br, 4.0f, -4.0f); br.u = 1.0f; br.v = 1.0f;
        fill(tr, 4.0f, 4.0f); tr.u = 1.0f; tr.v = 0.0f;
        return {tl, bl, br, tl, br, tr};
    }

    // Stride 68: the PBR layout above with BlendWeight/BlendIndices appended, matching
    // DiligentRenderer's own SkinnedPbr3D layout.
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

    std::vector<SkinnedPbrGpuVertex> MakeSkinnedQuad()
    {
        SkinnedPbrGpuVertex tl{}, bl{}, br{}, tr{};
        auto fill = [](SkinnedPbrGpuVertex& v, float x, float y) {
            v.px = x; v.py = y; v.pz = 0.0f;
            v.nx = 0.0f; v.ny = 0.0f; v.nz = 1.0f;
            v.tx = 1.0f; v.ty = 0.0f; v.tz = 0.0f; v.tw = 1.0f;
            v.w0 = 1.0f; v.w1 = v.w2 = v.w3 = 0.0f;
            v.i0 = v.i1 = v.i2 = v.i3 = 0;
        };
        fill(tl, -4.0f, 4.0f); tl.u = 0.0f; tl.v = 0.0f;
        fill(bl, -4.0f, -4.0f); bl.u = 0.0f; bl.v = 1.0f;
        fill(br, 4.0f, -4.0f); br.u = 1.0f; br.v = 1.0f;
        fill(tr, 4.0f, 4.0f); tr.u = 1.0f; tr.v = 0.0f;
        return {tl, bl, br, tl, br, tr};
    }

    Color ReadCenter(GraphicsDevice& device)
    {
        const Rectangle region(kSize / 2, kSize / 2, 1, 1);
        Color pixel(0, 0, 0, 0);
        device.GetBackBufferData(&region, &pixel, 0, 1);
        return pixel;
    }

    bool CloseTo(int a, int b, int tolerance) { return std::abs(a - b) <= tolerance; }
    bool Matches(const Color& c, const Color& expected, int tolerance = 12)
    {
        return CloseTo(c.getRProperty(), expected.getRProperty(), tolerance) &&
               CloseTo(c.getGProperty(), expected.getGProperty(), tolerance) &&
               CloseTo(c.getBProperty(), expected.getBProperty(), tolerance);
    }
}

class DiligentPbrTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> graphicsDeviceManager_;
    bool done_ = false;
    int passCount_ = 0;
    int result_ = 1;

    void Check(bool ok, const char* label, const Color& got, const char* expected)
    {
        std::printf("[%s] %s: got=(%d,%d,%d)%s\n", ok ? "PASS" : "FAIL", label,
                    got.getRProperty(), got.getGProperty(), got.getBProperty(),
                    ok ? "" : (std::string(" expected ") + expected).c_str());
        if (ok)
            ++passCount_;
    }

    Color RenderPbr(GraphicsDevice& device, Texture2D& albedo, float metallic, float roughness,
                    const Vector3& light0Dir, const Vector3& light0Diffuse)
    {
        PbrEffect effect(device);
        effect.setBaseColorTextureIsSrgbEXTProperty(false);
        effect.setEmissiveTextureIsSrgbEXTProperty(false);
        effect.setEncodeOutputToSrgbEXTProperty(false);
        effect.setTextureProperty(&albedo);
        effect.setNormalMapProperty(nullptr);
        effect.setMetallicFactorProperty(metallic);
        effect.setRoughnessFactorProperty(roughness);
        effect.setAmbientLightColorProperty(Vector3::Zero);
        effect.DirectionalLight0.setEnabledProperty(true);
        effect.DirectionalLight0.setDirectionProperty(light0Dir);
        effect.DirectionalLight0.setDiffuseColorProperty(light0Diffuse);
        effect.DirectionalLight1.setEnabledProperty(false);
        effect.DirectionalLight2.setEnabledProperty(false);
        effect.setWorldProperty(Matrix::getIdentityProperty());
        effect.setViewProperty(Matrix::CreateLookAt(Vector3(0.0f, 0.0f, 3.0f), Vector3::Zero,
                                                     Vector3(0.0f, 1.0f, 0.0f)));
        effect.setProjectionProperty(
            Matrix::CreatePerspectiveFieldOfView(MathHelper::PiOver4, 1.0f, 0.1f, 100.0f));

        auto verts = MakeQuad();
        VertexBuffer vb(device, static_cast<int>(verts.size()));
        vb.SetDataRaw(verts.data(), static_cast<int>(verts.size()),
                      static_cast<int>(sizeof(PbrGpuVertex)));

        device.Clear(Color(0, 255, 0, 255));
        device.SetDepthTestEnabled(false);
        device.setBlendStateProperty(BlendState::Opaque);
        device.setRasterizerStateProperty(RasterizerState::CullNone);
        device.SetVertexBuffer(&vb);
        effect.Apply();
        device.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
        return ReadCenter(device);
    }

    // Renders the identical scene as RenderPbr(white, 0.0, 0.5, ...) via SkinnedPbrEffect with a
    // single identity bone (weight 1.0 on bone 0, default Identity) -- a mathematical no-op skin
    // transform, so the result must equal PbrEffect's own.
    Color RenderSkinnedPbrIdentity(GraphicsDevice& device, Texture2D& albedo)
    {
        SkinnedPbrEffect effect(device);
        effect.setBaseColorTextureIsSrgbEXTProperty(false);
        effect.setEmissiveTextureIsSrgbEXTProperty(false);
        effect.setEncodeOutputToSrgbEXTProperty(false);
        effect.setTextureProperty(&albedo);
        effect.setNormalMapProperty(nullptr);
        effect.setMetallicFactorProperty(0.0f);
        effect.setRoughnessFactorProperty(0.5f);
        effect.setAmbientLightColorProperty(Vector3::Zero);
        effect.DirectionalLight0.setEnabledProperty(true);
        effect.DirectionalLight0.setDirectionProperty(Vector3(0.0f, 0.0f, -1.0f));
        effect.DirectionalLight0.setDiffuseColorProperty(Vector3(1.0f, 1.0f, 1.0f));
        effect.DirectionalLight1.setEnabledProperty(false);
        effect.DirectionalLight2.setEnabledProperty(false);
        effect.setWorldProperty(Matrix::getIdentityProperty());
        effect.setViewProperty(Matrix::CreateLookAt(Vector3(0.0f, 0.0f, 3.0f), Vector3::Zero,
                                                     Vector3(0.0f, 1.0f, 0.0f)));
        effect.setProjectionProperty(
            Matrix::CreatePerspectiveFieldOfView(MathHelper::PiOver4, 1.0f, 0.1f, 100.0f));
        effect.SetBoneTransforms(std::vector<Matrix>{Matrix::getIdentityProperty()});
        effect.setWeightsPerVertexProperty(1);

        auto verts = MakeSkinnedQuad();
        VertexBuffer vb(device, static_cast<int>(verts.size()));
        vb.SetDataRaw(verts.data(), static_cast<int>(verts.size()),
                      static_cast<int>(sizeof(SkinnedPbrGpuVertex)));

        device.Clear(Color(0, 255, 0, 255));
        device.SetDepthTestEnabled(false);
        device.setBlendStateProperty(BlendState::Opaque);
        device.setRasterizerStateProperty(RasterizerState::CullNone);
        device.SetVertexBuffer(&vb);
        effect.Apply();
        device.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
        return ReadCenter(device);
    }

protected:
    void Draw(const GameTime&) override
    {
        if (done_)
            return;
        done_ = true;

        auto& device = getGraphicsDeviceProperty();

        const std::vector<std::uint8_t> white = {255, 255, 255, 255};
        Texture2D whiteTexture = Texture2D::CreateFromPixels(device, 1, 1, white);
        const std::vector<std::uint8_t> red = {255, 0, 0, 255};
        Texture2D redTexture = Texture2D::CreateFromPixels(device, 1, 1, red);

        const Vector3 dirStraight(0.0f, 0.0f, -1.0f);

        // (a) a2=0.5^4=0.0625; D=a2/(pi*a2*a2)=5.092958; k=(1.5)^2/8=0.28125; G=1 (NdotV=NdotL=1
        // cancels exactly); F=F0=mix(0.04,albedo,0)=0.04 (VdotH=1 -> (1-1)^5=0);
        // specular=D*G*F/4=5.092958*0.04/4=0.050930; kd=1-F=0.96; diffuseColor=albedo*(1-0)=1;
        // Lo=(0.96/pi+0.050930)*1*1=0.305577+0.050930=0.356507 -> round(*255)=91.
        const Color a =
            RenderPbr(device, whiteTexture, 0.0f, 0.5f, dirStraight, Vector3(1.0f, 1.0f, 1.0f));
        Check(Matches(a, Color(91, 91, 91, 255), 12),
              "white/roughness=0.5/metallic=0: analytic BRDF value", a, "(91,91,91)");

        // (b) F0=mix(0.04,(1,0,0),1)=(1,0,0); diffuseColor=albedo*(1-1)=0 (no Lambertian term);
        // specular.r=D*G*F0.r/4=5.092958*1/4=1.273239, specular.g=specular.b=0 (F0=0);
        // Lo.r=(0+1.273239)*0.3*1=0.381972 -> round(*255)=97; Lo.g=Lo.b=0.
        const Color b =
            RenderPbr(device, redTexture, 1.0f, 0.5f, dirStraight, Vector3(0.3f, 0.3f, 0.3f));
        Check(Matches(b, Color(97, 0, 0, 255), 14),
              "red/roughness=0.5/metallic=1 (fully metallic): analytic BRDF value", b, "(97,0,0)");

        // (c) same as (b) but metallic=0 (fully dielectric): F0=(0.04,0.04,0.04),
        // diffuseColor=albedo=(1,0,0), kd=(0.96,0.96,0.96).
        // Lo.r=(0.96*1/pi+0.050930)*0.3=(0.305577+0.050930)*0.3=0.106952 -> 27
        // Lo.g=Lo.b=(0+0.050930)*0.3=0.015279 -> 4
        const Color c =
            RenderPbr(device, redTexture, 0.0f, 0.5f, dirStraight, Vector3(0.3f, 0.3f, 0.3f));
        Check(Matches(c, Color(27, 4, 4, 255), 14),
              "red/roughness=0.5/metallic=0 (dielectric): analytic BRDF value", c, "(27,4,4)");
        Check(!Matches(b, c, 10),
              "metallic=1 differs from metallic=0 -- MetallicFactor genuinely changes the BRDF", b,
              "!= (27,4,4)");

        // (D) SkinnedPbrEffect, identical scene to check A, single identity bone -- must reproduce
        // check A's own value exactly (mathematical no-op skin transform).
        const Color d = RenderSkinnedPbrIdentity(device, whiteTexture);
        Check(Matches(d, a, 10),
              "SkinnedPbrEffect identity bone reproduces PbrEffect's own (a) value", d, "== (a)");

        std::printf("=== %d/%d PASS ===\n", passCount_, kChecks);
        result_ = passCount_ == kChecks ? 0 : 1;
        Exit();
    }

public:
    DiligentPbrTest()
    {
        graphicsDeviceManager_ = std::make_unique<GraphicsDeviceManager>(this);
        graphicsDeviceManager_->setPreferredBackBufferWidthProperty(kSize);
        graphicsDeviceManager_->setPreferredBackBufferHeightProperty(kSize);
    }

    [[nodiscard]] int GetResult() const { return result_; }
};

int main()
{
    try
    {
        DiligentPbrTest game;
        game.Run();
        return game.GetResult();
    }
    catch (const std::exception& error)
    {
        const std::string message = error.what();
        const bool noDevice = message.find("no device type could be created") != std::string::npos ||
                              message.find("unsupported SDL video driver") != std::string::npos ||
                              message.find("SDL_Init") != std::string::npos ||
                              message.find("live SDL window") != std::string::npos;
        if (noDevice)
        {
            std::printf("[SKIP] CNA Diligent smoke: no usable device (%s)\n", error.what());
            return 77;
        }
        std::printf("[FAIL] unexpected exception: %s\n", error.what());
        return 1;
    }
}
