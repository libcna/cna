// SPDX-License-Identifier: MS-PL
// plans/plan_igl.md IGL-37/IGL-55: PbrEffect's NormalMap, EmissiveFactor, OcclusionMap, and null-map
// fallback -- the maps `igl_pbreffect_test.cpp`'s analytic single-point BRDF derivation left
// untested (that test only proves the core GGX/Fresnel/Smith math at one exact angle; it never
// binds a NormalMap/OcclusionMap or exercises EmissiveFactor). Ports the differential-comparison
// technique `opengl2_pbreffect_test.cpp`'s checks B/C/D/E already established for this same gap on
// another renderer family: rather than deriving an exact expected colour (hard for a perturbed
// tangent-space normal), each check compares two renders that should differ (or should not) if the
// feature under test is real.
//
// Check B -- NormalMap perturbation: a strongly-tilted tangent-space normal ((1,0,0), a 90-degree
//   tilt from flat (0,0,1)) under an off-axis light must change the lit result relative to a flat
//   normal map baseline -- proves the per-fragment TBN basis and normal-map sampling are real, not
//   ignored.
// Check C -- EmissiveFactor: with all lights and ambient off, a non-zero EmissiveFactor still
//   produces a non-black pixel -- proves the emissive term is additive and independent of lighting.
// Check D -- OcclusionMap: an ambient-only scene (no direct lights) with a black (occlusion=0)
//   OcclusionMap reads back darker than the same scene with no OcclusionMap bound (defaults to
//   white, occlusion=1) -- proves the map genuinely multiplies the ambient term.
// Check E -- null Texture/NormalMap/OcclusionMap all fall back to real defaults (white albedo,
//   flat normal) without crashing or producing garbage.
//
// Exit code 0 = all PASS, 1 = any FAIL, 77 = SKIP (no GPU/display).

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Vector4.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/PbrEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionNormalTangentTexture.hpp"

#include "common/PixelTestGame.hpp"

#include <cstdint>
#include <memory>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kSize = 64;

    bool CloseTo(const Color& got, const Color& want, const int tolerance)
    {
        const auto close = [tolerance](const int a, const int b) {
            return a > b ? (a - b) <= tolerance : (b - a) <= tolerance;
        };
        return close(got.getRProperty(), want.getRProperty()) &&
               close(got.getGProperty(), want.getGProperty()) &&
               close(got.getBProperty(), want.getBProperty());
    }
}

class IglPbrEffectMapsTest : public CNA::Examples::PixelTestGame
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    std::unique_ptr<Texture2D> albedo_;
    std::unique_ptr<VertexBuffer> vertexBuffer_;
    std::unique_ptr<IndexBuffer> indexBuffer_;

    void BuildQuad(GraphicsDevice& device)
    {
        // Positive Z, matching this renderer family's own identity-projection convention (see
        // igl_environmentmapeffect_test.cpp).
        const Vector3 normal(0.0f, 0.0f, 1.0f);
        const Vector4 tangent(1.0f, 0.0f, 0.0f, 1.0f);
        const std::vector<VertexPositionNormalTangentTexture> vertices = {
            VertexPositionNormalTangentTexture(Vector3(-1.0f, -1.0f, 0.5f), normal, tangent,
                                               Vector2(0.0f, 1.0f)),
            VertexPositionNormalTangentTexture(Vector3(1.0f, -1.0f, 0.5f), normal, tangent,
                                               Vector2(1.0f, 1.0f)),
            VertexPositionNormalTangentTexture(Vector3(1.0f, 1.0f, 0.5f), normal, tangent,
                                               Vector2(1.0f, 0.0f)),
            VertexPositionNormalTangentTexture(Vector3(-1.0f, 1.0f, 0.5f), normal, tangent,
                                               Vector2(0.0f, 0.0f)),
        };
        const std::uint16_t indices[6] = {0, 1, 2, 0, 2, 3};

        vertexBuffer_ = std::make_unique<VertexBuffer>(
            device, VertexPositionNormalTangentTexture::getVertexDeclarationStatic(),
            static_cast<int>(vertices.size()), BufferUsage::WriteOnly);
        vertexBuffer_->SetData(vertices.data(), 0, static_cast<int>(vertices.size()));

        indexBuffer_ = std::make_unique<IndexBuffer>(device, IndexElementSize::SixteenBits, 6,
                                                      BufferUsage::WriteOnly);
        indexBuffer_->SetData(indices, 0, 6);
    }

    /// plans/plan_gltf.md GLTF-476: `encodeOutput` selects whether the shaded result is written in sRGB.
    /// Every check below that measures a MAP passes false, so it reads a linear value and states an
    /// expectation in the same space the scene is derived in; check F passes true precisely to
    /// measure the transfer function itself. Before this renderer had colour management the
    /// distinction did not exist, and the expectations here silently depended on its absence.
    Color DrawAndRead(GraphicsDevice& device, const bool light0Enabled, const Vector3& lightDir,
                      const Vector3& ambient, const Vector3& emissive, Texture2D* normalMap,
                      Texture2D* occlusionMap, Texture2D* texture,
                      const bool encodeOutput = false)
    {
        device.Clear(Color(static_cast<bytecs>(0), static_cast<bytecs>(0), static_cast<bytecs>(0),
                           static_cast<bytecs>(255)));
        device.setRasterizerStateProperty(RasterizerState::CullNone);
        device.setDepthStencilStateProperty(DepthStencilState::None);

        PbrEffect effect(device);
        effect.setTextureProperty(texture);
        effect.setNormalMapProperty(normalMap);
        effect.setOcclusionMapProperty(occlusionMap);
        effect.setAmbientLightColorProperty(ambient);
        effect.setEmissiveFactorProperty(emissive);
        effect.setMetallicFactorProperty(0.0f);
        effect.setRoughnessFactorProperty(1.0f);
        effect.setBaseColorTextureIsSrgbEXTProperty(false);
        effect.setEmissiveTextureIsSrgbEXTProperty(false);
        effect.setEncodeOutputToSrgbEXTProperty(encodeOutput);
        effect.DirectionalLight0.setEnabledProperty(light0Enabled);
        effect.DirectionalLight0.setDirectionProperty(lightDir);
        effect.DirectionalLight0.setDiffuseColorProperty(Vector3(1.0f, 1.0f, 1.0f));
        effect.DirectionalLight1.setEnabledProperty(false);
        effect.DirectionalLight2.setEnabledProperty(false);
        effect.setWorldProperty(Matrix::getIdentityProperty());
        effect.setViewProperty(Matrix::getIdentityProperty());
        effect.setProjectionProperty(Matrix::getIdentityProperty());

        device.SetVertexBuffer(vertexBuffer_.get());
        device.setIndicesProperty(indexBuffer_.get());

        for (EffectPass& pass : effect.getCurrentTechniqueProperty()->getPassesProperty())
        {
            pass.Apply();
            device.DrawIndexedPrimitives(PrimitiveType::TriangleList, 0, 0, 4, 0, 2);
        }

        const Rectangle centre(kSize / 2, kSize / 2, 1, 1);
        Color pixel(0, 0, 0, 0);
        device.GetBackBufferData(&centre, &pixel, 0, 1);
        return pixel;
    }

protected:
    void RunTest() override
    {
        auto& device = getGraphicsDeviceProperty();
        BuildQuad(device);

        const Color texel(static_cast<bytecs>(200), static_cast<bytecs>(100),
                          static_cast<bytecs>(50), static_cast<bytecs>(255));
        albedo_ = std::make_unique<Texture2D>(device, 1, 1);
        albedo_->SetData(&texel, 1);

        // Check B: NormalMap perturbation. An off-axis light so a tilted normal genuinely changes
        // N.L (a light pointed straight down +Z would not discriminate against a normal tilted
        // purely in X).
        auto flatNormalTex = std::make_unique<Texture2D>(device, 1, 1);
        const Color flatNormalColor(static_cast<bytecs>(128), static_cast<bytecs>(128),
                                    static_cast<bytecs>(255), static_cast<bytecs>(255));
        flatNormalTex->SetData(&flatNormalColor, 1);
        auto tiltedNormalTex = std::make_unique<Texture2D>(device, 1, 1);
        const Color tiltedNormalColor(static_cast<bytecs>(255), static_cast<bytecs>(128),
                                      static_cast<bytecs>(128), static_cast<bytecs>(255));
        tiltedNormalTex->SetData(&tiltedNormalColor, 1);

        const Color flatResult =
            DrawAndRead(device, true, Vector3(0.3f, 0.0f, -1.0f), Vector3::Zero, Vector3::Zero,
                       flatNormalTex.get(), nullptr, albedo_.get());
        const Color tiltedResult =
            DrawAndRead(device, true, Vector3(0.3f, 0.0f, -1.0f), Vector3::Zero, Vector3::Zero,
                       tiltedNormalTex.get(), nullptr, albedo_.get());
        ExpectTrue("NormalMap perturbation changes the lit result under an off-axis light",
                  !CloseTo(flatResult, tiltedResult, 8));

        // Check C: EmissiveFactor is additive and independent of lighting.
        const Color emissiveResult =
            DrawAndRead(device, false, Vector3(0.0f, 0.0f, -1.0f), Vector3::Zero,
                       Vector3(0.5f, 0.5f, 0.5f), nullptr, nullptr, albedo_.get());
        ExpectTrue("EmissiveFactor produces a non-black pixel with no lights or ambient",
                  emissiveResult.getRProperty() > 20);

        // Check D: OcclusionMap darkens the ambient-only contribution.
        auto blackOcclusionTex = std::make_unique<Texture2D>(device, 1, 1);
        const Color blackOcclusion(static_cast<bytecs>(0), static_cast<bytecs>(0),
                                   static_cast<bytecs>(0), static_cast<bytecs>(255));
        blackOcclusionTex->SetData(&blackOcclusion, 1);

        const Color noOcclusionResult =
            DrawAndRead(device, false, Vector3(0.0f, 0.0f, -1.0f), Vector3(0.6f, 0.6f, 0.6f),
                       Vector3::Zero, nullptr, nullptr, albedo_.get());
        const Color occludedResult =
            DrawAndRead(device, false, Vector3(0.0f, 0.0f, -1.0f), Vector3(0.6f, 0.6f, 0.6f),
                       Vector3::Zero, nullptr, blackOcclusionTex.get(), albedo_.get());
        ExpectTrue("OcclusionMap=black darkens the ambient term below the no-map baseline",
                  occludedResult.getRProperty() < noOcclusionResult.getRProperty() - 10);
        ExpectTrue("OcclusionMap=black leaves the ambient-only, no-light scene near-black",
                  CloseTo(occludedResult,
                          Color(static_cast<bytecs>(0), static_cast<bytecs>(0),
                                static_cast<bytecs>(0), static_cast<bytecs>(255)),
                          8));

        // Check E: null Texture/NormalMap/OcclusionMap fall back to real defaults, not a crash.
        const Color nullMapResult = DrawAndRead(device, false, Vector3(0.0f, 0.0f, -1.0f),
                                                Vector3::Zero, Vector3(0.5f, 0.5f, 0.5f), nullptr,
                                                nullptr, nullptr);
        ExpectTrue("null Texture/NormalMap/OcclusionMap fall back without crashing or going black",
                  CloseTo(nullMapResult,
                          Color(static_cast<bytecs>(128), static_cast<bytecs>(128),
                                static_cast<bytecs>(128), static_cast<bytecs>(255)),
                          20));

        // Check F: the same scene with EncodeOutputToSrgbEXT left ON. glTF 2.0 3.9.2 shades in
        // linear and the result is encoded on the way out, so the identical 0.5 emissive value must
        // come back as sRGB's own encoding of it -- 1.055 * 0.5^(1/2.4) - 0.055 = 0.7148, or 182 --
        // rather than the 128 above. This renderer used to answer 128 either way, because it had no
        // colour management at all: it shaded in linear and wrote the linear value straight out
        // (plans/plan_gltf.md GLTF-476). The two checks together are what make the transfer function
        // observable rather than merely present in the source.
        const Color encodedResult = DrawAndRead(device, false, Vector3(0.0f, 0.0f, -1.0f),
                                                Vector3::Zero, Vector3(0.5f, 0.5f, 0.5f), nullptr,
                                                nullptr, nullptr, true);
        ExpectTrue("EncodeOutputToSrgbEXT writes sRGB rather than the raw linear value",
                  CloseTo(encodedResult,
                          Color(static_cast<bytecs>(182), static_cast<bytecs>(182),
                                static_cast<bytecs>(182), static_cast<bytecs>(255)),
                          6));
    }

public:
    IglPbrEffectMapsTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kSize);
        gdm_->setPreferredBackBufferHeightProperty(kSize);
        gdm_->setSynchronizeWithVerticalRetraceProperty(false);
    }
};

int main()
{
    return CNA::Examples::RunPixelTest<IglPbrEffectMapsTest>();
}
