// SPDX-License-Identifier: MS-PL
// PbrEffect / SkinnedPbrEffect at an analytically exact pixel: the glTF metallic-roughness BRDF at
// roughness 0.5, and tangent-frame handedness under a mirrored skin joint.
//
// Moved from the retired VULKAN suite (vulkan_pbreffect_handderived_test.cpp; leg G is GLTF-176).
// The legs that suite shared with other renderers' programs (identity-bone skinning, the
// non-uniform joint normal, the mirrored World) are not repeated here.
//
// The camera sits at (0,0,3) looking down -Z at a flat quad through the origin, and the centre
// pixel is read. There the world position is the origin, so V = N = (0,0,1); light 0 points along
// (0,0,-1), so L = N as well and every dot product of the BRDF is exactly 1:
//
//   roughness 0.5:  a2 = 0.0625, D = a2 / (pi * a2^2) = 5.092958, G = 1, F = F0
//   specular = D * F0 / 4;  diffuse = kd * albedo * (1 - metallic) / pi,  kd = 1 - F0
//
//   A  white, dielectric (F0 0.04), light 1:  0.96/pi + 0.050930 = 0.356507 -> 91
//   B  red, metallic (F0 = albedo), light 0.3: 1.273239 * 0.3 = 0.381972 -> (97, 0, 0)
//   C  red, dielectric, light 0.3: (0.356507, 0.050930, 0.050930) * 0.3 -> (27, 4, 4)
//   D  B and C differ: MetallicFactor really changes the BRDF.
//   G  SkinnedPbrEffect with World = identity and one joint = Scale(-1,1,1), normal map texel
//      (128,255,128) = tangent-space +Y, light along -Y, roughness 1: the joint mirrors T to -X,
//      and only folding the joint's negative determinant into tangent.w keeps B = +Y. The
//      analytic dielectric value is about 79; a lost sign leaves B = -Y and the pixel black.
//
// All legs keep the BRDF linear: the sRGB decode/encode extension switches are turned off.
//
// Exit code 0 = PASS, 1 = FAIL, 77 = skipped (no display or no 3D rendering).

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/MathHelper.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/BufferUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsProfile.hpp"
#include "Microsoft/Xna/Framework/Graphics/PbrEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SkinnedPbrEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexDeclaration.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElement.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementUsage.hpp"
#include "CNA/GraphicsCapability.hpp"

#include "common/PixelTestGame.hpp"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <vector>
#include "ConformanceSupport.hpp"

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kSize = 64;

    struct PbrVertex
    {
        float px, py, pz;
        float nx, ny, nz;
        float tx, ty, tz, tw;
        float u, v;
    };
    static_assert(sizeof(PbrVertex) == 48);

    struct SkinnedPbrVertex
    {
        float px, py, pz;
        float nx, ny, nz;
        float tx, ty, tz, tw;
        float u, v;
        float w0, w1, w2, w3;
        std::uint8_t i0, i1, i2, i3;
    };
    static_assert(sizeof(SkinnedPbrVertex) == 68);

    VertexDeclaration PbrDeclaration()
    {
        return VertexDeclaration(48, {
            VertexElement(0,  VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
            VertexElement(12, VertexElementFormat::Vector3, VertexElementUsage::Normal, 0),
            VertexElement(24, VertexElementFormat::Vector4, VertexElementUsage::Tangent, 0),
            VertexElement(40, VertexElementFormat::Vector2, VertexElementUsage::TextureCoordinate, 0),
        });
    }

    VertexDeclaration SkinnedPbrDeclaration()
    {
        return VertexDeclaration(68, {
            VertexElement(0,  VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
            VertexElement(12, VertexElementFormat::Vector3, VertexElementUsage::Normal, 0),
            VertexElement(24, VertexElementFormat::Vector4, VertexElementUsage::Tangent, 0),
            VertexElement(40, VertexElementFormat::Vector2, VertexElementUsage::TextureCoordinate, 0),
            VertexElement(48, VertexElementFormat::Vector4, VertexElementUsage::BlendWeight, 0),
            VertexElement(64, VertexElementFormat::Byte4, VertexElementUsage::BlendIndices, 0),
        });
    }

    // A quad far larger than the view, normal +Z, tangent +X with w = +1.
    template <typename V>
    std::vector<V> MakeQuad()
    {
        V tl{}, bl{}, br{}, tr{};
        auto fill = [](V& v, float x, float y, float u, float t) {
            v.px = x; v.py = y; v.pz = 0.0f;
            v.nx = 0.0f; v.ny = 0.0f; v.nz = 1.0f;
            v.tx = 1.0f; v.ty = 0.0f; v.tz = 0.0f; v.tw = 1.0f;
            v.u = u; v.v = t;
        };
        fill(tl, -4.0f,  4.0f, 0.0f, 0.0f);
        fill(bl, -4.0f, -4.0f, 0.0f, 1.0f);
        fill(br,  4.0f, -4.0f, 1.0f, 1.0f);
        fill(tr,  4.0f,  4.0f, 1.0f, 0.0f);
        return { tl, bl, br, tl, br, tr };
    }

    bool Matches(const Color& got, const Color& want, int tolerance)
    {
        return std::abs(got.getRProperty() - want.getRProperty()) <= tolerance
            && std::abs(got.getGProperty() - want.getGProperty()) <= tolerance
            && std::abs(got.getBProperty() - want.getBProperty()) <= tolerance;
    }

    template <typename Effect>
    void Configure(Effect& fx, Texture2D& albedo, Texture2D* normalMap, float metallic,
                   float roughness, const Vector3& lightDirection, const Vector3& lightColour)
    {
        fx.setBaseColorTextureIsSrgbEXTProperty(false);
        fx.setEmissiveTextureIsSrgbEXTProperty(false);
        fx.setEncodeOutputToSrgbEXTProperty(false);
        fx.setTextureProperty(&albedo);
        fx.setNormalMapProperty(normalMap);
        fx.setMetallicFactorProperty(metallic);
        fx.setRoughnessFactorProperty(roughness);
        fx.setAmbientLightColorProperty(Vector3::Zero);
        fx.DirectionalLight0.setEnabledProperty(true);
        fx.DirectionalLight0.setDirectionProperty(lightDirection);
        fx.DirectionalLight0.setDiffuseColorProperty(lightColour);
        fx.DirectionalLight1.setEnabledProperty(false);
        fx.DirectionalLight2.setEnabledProperty(false);
        fx.setWorldProperty(Matrix::getIdentityProperty());
        fx.setViewProperty(Matrix::CreateLookAt(Vector3(0.0f, 0.0f, 3.0f), Vector3::Zero,
                                                Vector3(0.0f, 1.0f, 0.0f)));
        fx.setProjectionProperty(Matrix::CreatePerspectiveFieldOfView(
            MathHelper::PiOver4, 1.0f, 0.1f, 100.0f));
    }
}

class PbrEffectHandDerivedTest final : public CNA::Examples::PixelTestGame
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    bool skipped_ = false;

    Color DrawAndReadCentre(Effect& fx, VertexBuffer& vb)
    {
        auto& dev = getGraphicsDeviceProperty();
        dev.Clear(Color(0, 255, 0, 255));
        dev.setDepthStencilStateProperty(DepthStencilState::None);
        dev.setBlendStateProperty(BlendState::Opaque);
        dev.setRasterizerStateProperty(RasterizerState::CullNone);
        dev.SetVertexBuffer(&vb);
        fx.Apply();
        dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
        dev.SetVertexBuffer(nullptr);
        const Rectangle centre(kSize / 2, kSize / 2, 1, 1);
        Color got(0, 0, 0, 0);
        dev.GetBackBufferData(&centre, &got, 0, 1);
        return got;
    }

    Color RenderPbr(Texture2D& albedo, float metallic, float roughness, const Vector3& lightColour)
    {
        auto& dev = getGraphicsDeviceProperty();
        PbrEffect fx(dev);
        Configure(fx, albedo, nullptr, metallic, roughness, Vector3(0.0f, 0.0f, -1.0f), lightColour);
        const auto verts = MakeQuad<PbrVertex>();
        VertexBuffer vb(dev, PbrDeclaration(), static_cast<int>(verts.size()), BufferUsage::None);
        vb.SetDataRaw(verts.data(), static_cast<int>(verts.size()), sizeof(PbrVertex));
        return DrawAndReadCentre(fx, vb);
    }

    Color RenderSkinnedMirroredJoint(Texture2D& albedo, Texture2D& positiveYNormal)
    {
        auto& dev = getGraphicsDeviceProperty();
        SkinnedPbrEffect fx(dev);
        Configure(fx, albedo, &positiveYNormal, 0.0f, 1.0f, Vector3(0.0f, -1.0f, 0.0f), Vector3::One);
        fx.SetBoneTransforms({ Matrix::CreateScale(-1.0f, 1.0f, 1.0f) });
        fx.setWeightsPerVertexProperty(1);
        auto verts = MakeQuad<SkinnedPbrVertex>();
        for (auto& v : verts)
        {
            v.w0 = 1.0f; v.w1 = v.w2 = v.w3 = 0.0f;
            v.i0 = v.i1 = v.i2 = v.i3 = 0;
        }
        VertexBuffer vb(dev, SkinnedPbrDeclaration(), static_cast<int>(verts.size()),
                        BufferUsage::None);
        vb.SetDataRaw(verts.data(), static_cast<int>(verts.size()), sizeof(SkinnedPbrVertex));
        return DrawAndReadCentre(fx, vb);
    }

    bool CheckColour(const char* label, const Color& got, const Color& want, int tolerance)
    {
        const bool ok = Matches(got, want, tolerance);
        std::printf("[%s] %s: got (%d,%d,%d), want (%d,%d,%d) +/- %d\n", ok ? "PASS" : "FAIL",
                    label, got.getRProperty(), got.getGProperty(), got.getBProperty(),
                    want.getRProperty(), want.getGProperty(), want.getBProperty(), tolerance);
        if (!ok) MarkFailedEXT();
        return ok;
    }

public:
    PbrEffectHandDerivedTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setGraphicsProfileProperty(GraphicsProfile::HiDef);
        gdm_->setPreferredBackBufferWidthProperty(kSize);
        gdm_->setPreferredBackBufferHeightProperty(kSize);
    }

    [[nodiscard]] int ExitCode() const
    {
        return skipped_ ? CNA::Examples::kSkipExitCode : getResultProperty();
    }

    void RunTest() override
    {
        auto& dev = getGraphicsDeviceProperty();
        if (!dev.SupportsCapability(CNA::GraphicsCapability::ThreeD))
        {
            std::printf("[SKIP] this renderer does not rasterize 3D stock effects\n");
            skipped_ = true;
            return;
        }

        Texture2D white = Texture2D::CreateFromPixels(dev, 1, 1, std::vector<std::uint8_t>{ 255, 255, 255, 255 });
        Texture2D red = Texture2D::CreateFromPixels(dev, 1, 1, std::vector<std::uint8_t>{ 255, 0, 0, 255 });
        Texture2D positiveY = Texture2D::CreateFromPixels(dev, 1, 1, std::vector<std::uint8_t>{ 128, 255, 128, 255 });

        const Color a = RenderPbr(white, 0.0f, 0.5f, Vector3(1.0f, 1.0f, 1.0f));
        CheckColour("A white dielectric, roughness 0.5: analytic BRDF", a, Color(91, 91, 91, 255), 12);

        const Color b = RenderPbr(red, 1.0f, 0.5f, Vector3(0.3f, 0.3f, 0.3f));
        CheckColour("B red metallic, roughness 0.5: specular only, tinted by F0", b,
                    Color(97, 0, 0, 255), 14);

        const Color c = RenderPbr(red, 0.0f, 0.5f, Vector3(0.3f, 0.3f, 0.3f));
        CheckColour("C red dielectric, roughness 0.5: Lambert plus untinted 4% specular", c,
                    Color(27, 4, 4, 255), 14);

        const bool differs = !Matches(b, c, 10);
        std::printf("[%s] D MetallicFactor changes the BRDF: (%d,%d,%d) vs (%d,%d,%d)\n",
                    differs ? "PASS" : "FAIL", b.getRProperty(), b.getGProperty(),
                    b.getBProperty(), c.getRProperty(), c.getGProperty(), c.getBProperty());
        if (!differs) MarkFailedEXT();

        const Color g = RenderSkinnedMirroredJoint(white, positiveY);
        CheckColour("G SkinnedPbrEffect keeps TBN handedness under a mirrored joint (black = "
                    "the joint's determinant was not folded into tangent.w)",
                    g, Color(79, 79, 79, 255), 4);
    }
};

int main()
{
    if (!CNA::Examples::Conformance::ProbePixelOutputAvailable("pbr_effect_hand_derived"))
        return CNA::Examples::Conformance::kSkipExitCode;
    if (!CNA::Examples::Conformance::ProbePbrLightingEvaluated("pbr_effect_hand_derived"))
        return CNA::Examples::Conformance::kSkipExitCode;
    if (!CNA::Examples::ProbeGpuDisplayAvailable())
        return CNA::Examples::kSkipExitCode;
    PbrEffectHandDerivedTest game;
    try
    {
        game.Run();
    }
    catch (const std::exception& e)
    {
        // Every draw here is a PbrEffect or SkinnedPbrEffect one, so a renderer with no
        // metallic-roughness model may refuse the whole program by name.
        if (!CNA::Examples::Conformance::IsNamedPbrRefusal(e))
            throw;
        std::printf("[SKIP] pbr_effect_hand_derived: %s\n", e.what());
        return CNA::Examples::Conformance::kSkipExitCode;
    }
    return game.ExitCode();
}
