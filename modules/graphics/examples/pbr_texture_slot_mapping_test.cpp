// SPDX-License-Identifier: MS-PL
// plans/plan_gltf.md GLTF-344/373/379: discriminating real-pixel proof of all seven PBR texture
// bindings, both PBR map scalars, and glTF MASK coverage staying inside rigid and skinned PBR
// programs.
//
// Output sRGB transfer stays disabled so exact linear-UNORM oracle values remain portable. Five
// solid one-pixel sentinels make a slot swap observable: red base colour, green metallic-roughness
// (G=roughness=1, B=metallic=0), blue emissive, channel-asymmetric occlusion, and a tilted normal.
//
// Two cases carried over from retired renderers: an unbound base-colour/emissive map falls back to
// the neutral opaque white (DIRECTX11, CNB-58/67), and the two KHR_materials_specular maps each
// honour their own coordinate set and transform on a stride-60 record (METAL, AM4-085).

#include "common/PixelTestGame.hpp"

#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/BufferUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/AlphaModeEXT.hpp"
#include "Microsoft/Xna/Framework/Graphics/PbrEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerStateCollection.hpp"
#include "Microsoft/Xna/Framework/Graphics/SkinnedPbrEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureCollection.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureTransformEXT.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexDeclaration.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElement.hpp"

#include <cstdint>
#include <string>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
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
        PbrVertex base;
        float w0, w1, w2, w3;
        std::uint8_t i0, i1, i2, i3;
    };
    static_assert(sizeof(SkinnedPbrVertex) == 68);

    std::vector<PbrVertex> RigidQuad()
    {
        const auto vertex = [](float x, float y, float u, float v) {
            return PbrVertex{x, y, 0.0f, 0.0f, 0.0f, 1.0f,
                             1.0f, 0.0f, 0.0f, 1.0f, u, v};
        };
        const PbrVertex tl = vertex(-1.0f,  1.0f, 0.0f, 0.0f);
        const PbrVertex bl = vertex(-1.0f, -1.0f, 0.0f, 1.0f);
        const PbrVertex br = vertex( 1.0f, -1.0f, 1.0f, 1.0f);
        const PbrVertex tr = vertex( 1.0f,  1.0f, 1.0f, 0.0f);
        return {tl, bl, br, tl, br, tr};
    }

    template <typename Effect>
    void RunMapScalarCases(CNA::Examples::PixelTestGame& test,
                           GraphicsDevice& device,
                           Effect& effect,
                           Texture2D& white,
                           Texture2D& occlusion,
                           Texture2D& normal,
                           int sampleX,
                           int sampleY,
                           const char* prefix)
    {
        const auto drawAndExpect = [&](const char* caseName, const Color& expected, int tolerance)
        {
            device.Clear(Color(0, 255, 0, 255));
            effect.Apply();
            device.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
            const std::string label = std::string(prefix) + " " + caseName;
            test.ExpectPixel(label.c_str(), Rectangle(sampleX, sampleY, 1, 1), expected, tolerance);
        };

        effect.setAlphaModeEXTProperty(AlphaModeEXT::Opaque);
        effect.setTextureProperty(&white);
        effect.setNormalMapProperty(nullptr);
        effect.setMetallicRoughnessMapProperty(nullptr);
        effect.setEmissiveMapProperty(nullptr);
        effect.setOcclusionMapProperty(&occlusion);
        effect.setEmissiveFactorProperty(Vector3::Zero);
        effect.setMetallicFactorProperty(0.0f);
        effect.setRoughnessFactorProperty(1.0f);
        effect.setNormalScaleEXTProperty(1.0f);
        effect.setOcclusionStrengthEXTProperty(0.5f);
        effect.setAmbientLightColorProperty(Vector3::One);
        effect.DirectionalLight0.setEnabledProperty(false);
        // glTF §3.9.3: 1 + 0.5 * (64/255 - 1) = 159.5/255, rounded to byte 160.
        drawAndExpect("occlusion strength interpolates from one", Color(160, 160, 160, 255), 1);

        effect.setOcclusionMapProperty(nullptr);
        effect.setOcclusionStrengthEXTProperty(1.0f);
        effect.setAmbientLightColorProperty(Vector3::Zero);
        effect.setNormalMapProperty(&normal);
        effect.setNormalScaleEXTProperty(0.0f);
        effect.DirectionalLight0.setEnabledProperty(true);
        effect.DirectionalLight0.setDirectionProperty(Vector3(0.0f, 0.0f, -1.0f));
        effect.DirectionalLight0.setDiffuseColorProperty(Vector3::One);
        // Scaling tangent-space X/Y to zero restores the geometric +Z normal: byte 79 instead of
        // the tilted sample's byte 35. This distinguishes a consumed zero from an ignored scalar.
        drawAndExpect("normal scale zero restores geometric normal", Color(79, 79, 79, 255), 2);
    }

    std::vector<SkinnedPbrVertex> SkinnedQuad()
    {
        std::vector<SkinnedPbrVertex> result;
        for (const PbrVertex& vertex : RigidQuad())
            result.push_back({vertex, 1.0f, 0.0f, 0.0f, 0.0f, 0, 0, 0, 0});
        return result;
    }

    template <typename Effect>
    void Configure(Effect& effect)
    {
        effect.setWorldProperty(Matrix::getIdentityProperty());
        effect.setViewProperty(Matrix::CreateLookAt(
            Vector3(0.0f, 0.0f, 100.0f), Vector3::Zero, Vector3(0.0f, 1.0f, 0.0f)));
        effect.setProjectionProperty(Matrix::CreateOrthographic(2.0f, 2.0f, 0.1f, 200.0f));
        effect.setDiffuseColorProperty(Vector3::One);
        effect.setEmissiveFactorProperty(Vector3::Zero);
        effect.setMetallicFactorProperty(0.0f);
        effect.setRoughnessFactorProperty(1.0f);
        effect.setEncodeOutputToSrgbEXTProperty(false);
        effect.DirectionalLight0.setEnabledProperty(false);
        effect.DirectionalLight1.setEnabledProperty(false);
        effect.DirectionalLight2.setEnabledProperty(false);
    }

    template <typename Effect>
    void RunSlotCases(CNA::Examples::PixelTestGame& test,
                      GraphicsDevice& device,
                      Effect& effect,
                      Texture2D& white,
                      Texture2D& redBaseColor,
                      Texture2D& metallicRoughness,
                      Texture2D& blueEmissive,
                      Texture2D& occlusion,
                      Texture2D& normal,
                      int sampleX,
                      int sampleY,
                      const char* prefix)
    {
        const auto drawAndExpect = [&](const char* caseName, const Color& expected, int tolerance)
        {
            device.Clear(Color(0, 255, 0, 255));
            effect.Apply();
            device.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
            const std::string label = std::string(prefix) + " " + caseName;
            test.ExpectPixel(label.c_str(), Rectangle(sampleX, sampleY, 1, 1), expected, tolerance);
        };

        effect.setNormalMapProperty(nullptr);
        effect.setMetallicRoughnessMapProperty(nullptr);
        effect.setEmissiveMapProperty(nullptr);
        effect.setOcclusionMapProperty(nullptr);
        effect.setAmbientLightColorProperty(Vector3::One);
        effect.setTextureProperty(&redBaseColor);
        drawAndExpect("base-color map -> slot 0", Color(255, 0, 0, 255), 0);

        effect.setTextureProperty(&white);
        effect.setAmbientLightColorProperty(Vector3::Zero);
        effect.setEmissiveMapProperty(&blueEmissive);
        effect.setEmissiveFactorProperty(Vector3::One);
        drawAndExpect("emissive map -> slot 3", Color(0, 0, 255, 255), 0);

        effect.setTextureProperty(&redBaseColor);
        effect.setEmissiveMapProperty(nullptr);
        effect.setEmissiveFactorProperty(Vector3::Zero);
        effect.setMetallicRoughnessMapProperty(&metallicRoughness);
        effect.setMetallicFactorProperty(1.0f);
        effect.DirectionalLight0.setEnabledProperty(true);
        effect.DirectionalLight0.setDirectionProperty(Vector3(0.0f, 0.0f, -1.0f));
        effect.DirectionalLight0.setDiffuseColorProperty(Vector3::One);
        // N=V=L=+Z, roughness=1, metallic=0: red=.97/pi, green=blue=.01/pi.
        drawAndExpect("metallic-roughness map -> slot 2", Color(79, 1, 1, 255), 2);

        effect.DirectionalLight0.setEnabledProperty(false);
        effect.setMetallicRoughnessMapProperty(nullptr);
        effect.setMetallicFactorProperty(0.0f);
        effect.setTextureProperty(&white);
        effect.setAmbientLightColorProperty(Vector3::One);
        effect.setOcclusionMapProperty(&occlusion);
        // The occlusion shader input is the red byte (64), not green (128) or blue (192).
        drawAndExpect("occlusion map -> slot 4/red channel", Color(64, 64, 64, 255), 1);

        effect.setOcclusionMapProperty(nullptr);
        effect.setAmbientLightColorProperty(Vector3::Zero);
        effect.setNormalMapProperty(&normal);
        effect.DirectionalLight0.setEnabledProperty(true);
        effect.DirectionalLight0.setDirectionProperty(Vector3(0.0f, 0.0f, -1.0f));
        // (255,128,191) decodes to a tilted normal; the shared rough-dielectric BRDF is byte 35
        // in a linear UNORM framebuffer, versus 79 at the geometric normal.
        drawAndExpect("normal map -> slot 1", Color(35, 35, 35, 255), 2);
    }

    template <typename Effect>
    void RunAlphaMaskCases(CNA::Examples::PixelTestGame& test,
                           GraphicsDevice& device,
                           Effect& effect,
                           Texture2D& belowCutoff,
                           Texture2D& aboveCutoff,
                           int sampleX,
                           int sampleY,
                           const char* prefix)
    {
        effect.DirectionalLight0.setEnabledProperty(false);
        effect.setNormalMapProperty(nullptr);
        effect.setMetallicRoughnessMapProperty(nullptr);
        effect.setEmissiveMapProperty(nullptr);
        effect.setOcclusionMapProperty(nullptr);
        effect.setAmbientLightColorProperty(Vector3::One);
        effect.setEmissiveFactorProperty(Vector3::Zero);
        effect.setMetallicFactorProperty(0.0f);
        effect.setAlphaModeEXTProperty(AlphaModeEXT::Mask);
        effect.setAlphaCutoffEXTProperty(0.5f);

        const auto drawAndExpect = [&](const char* caseName, Texture2D& texture,
                                       const Color& expected)
        {
            device.Clear(Color(0, 255, 0, 255));
            effect.setTextureProperty(&texture);
            effect.Apply();
            device.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
            const std::string label = std::string(prefix) + " " + caseName;
            test.ExpectPixel(label.c_str(), Rectangle(sampleX, sampleY, 1, 1), expected, 0);
        };
        drawAndExpect("MASK discards alpha below cutoff", belowCutoff, Color(0, 255, 0, 255));
        drawAndExpect("MASK keeps alpha above cutoff", aboveCutoff, Color(255, 0, 0, 255));
    }

    template <typename Effect>
    void RunSpecularTextureCases(CNA::Examples::PixelTestGame& test,
                                 GraphicsDevice& device,
                                 Effect& effect,
                                 Texture2D& white,
                                 Texture2D& zeroStrength,
                                 Texture2D& redSpecularColor,
                                 int sampleX,
                                 int sampleY,
                                 const char* prefix)
    {
        const auto drawAndExpect = [&](const char* caseName, const Color& expected)
        {
            device.Clear(Color(0, 255, 0, 255));
            effect.Apply();
            device.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
            const std::string label = std::string(prefix) + " " + caseName;
            test.ExpectPixel(label.c_str(), Rectangle(sampleX, sampleY, 1, 1), expected, 2);
        };

        effect.setAlphaModeEXTProperty(AlphaModeEXT::Opaque);
        effect.setTextureProperty(&white);
        effect.setNormalMapProperty(nullptr);
        effect.setMetallicRoughnessMapProperty(nullptr);
        effect.setEmissiveMapProperty(nullptr);
        effect.setOcclusionMapProperty(nullptr);
        effect.setEmissiveFactorProperty(Vector3::Zero);
        effect.setAmbientLightColorProperty(Vector3::Zero);
        effect.setMetallicFactorProperty(0.0f);
        effect.setRoughnessFactorProperty(1.0f);
        effect.setIorEXTProperty(3.0f); // dielectric F0 = 0.25: a strong slot discriminator
        effect.setSpecularFactorEXTProperty(1.0f);
        effect.setSpecularColorFactorEXTProperty(Vector3::One);
        effect.DirectionalLight0.setEnabledProperty(true);
        effect.DirectionalLight0.setDirectionProperty(Vector3(0.0f, 0.0f, -1.0f));
        effect.DirectionalLight0.setDiffuseColorProperty(Vector3::One);

        // Alpha=0 makes both F0 and F90 zero.  At N=V=L with roughness=1 this leaves exactly
        // white/pi diffuse (~81), versus ~66 with the white strength fallback and F0=.25.
        effect.setSpecularMapEXTProperty(&zeroStrength);
        effect.setSpecularColorMapEXTProperty(nullptr);
        drawAndExpect("specular-strength map alpha -> slot 5", Color(81, 81, 81, 255));

        // Restore strength=1 and tint only the dielectric F0 red.  Red stays ~66 while G/B lose
        // no diffuse energy to Fresnel and rise to ~81, proving the colour texture is sampled from
        // its own slot rather than aliased to the scalar texture.
        effect.setSpecularMapEXTProperty(&white);
        effect.setSpecularColorMapEXTProperty(&redSpecularColor);
        drawAndExpect("specular-colour map RGB -> slot 6", Color(66, 81, 81, 255));
    }

    // From the retired DIRECTX11 renderer's directx11_pbr_vertexcolor_test (plans/plan_cnj.md
    // CNB-58/CNB-67, quads A/B): with NO base-colour texture bound the base colour is the neutral
    // opaque white -- alpha included -- and with no emissive map the emissive factor stands alone.
    // NonPremultiplied over the green clear makes the alpha visible: a transparent fallback would
    // read green, a black one black.
    template <typename Effect>
    void RunUnboundFallbackCases(CNA::Examples::PixelTestGame& test,
                                 GraphicsDevice& device,
                                 Effect& effect,
                                 int sampleX,
                                 int sampleY,
                                 const char* prefix)
    {
        const auto drawAndExpect = [&](const char* caseName, const Color& expected)
        {
            device.Clear(Color(0, 255, 0, 255));
            effect.Apply();
            device.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
            const std::string label = std::string(prefix) + " " + caseName;
            test.ExpectPixel(label.c_str(), Rectangle(sampleX, sampleY, 1, 1), expected, 1);
        };

        effect.setAlphaModeEXTProperty(AlphaModeEXT::Blend);
        effect.setTextureProperty(nullptr);
        effect.setNormalMapProperty(nullptr);
        effect.setMetallicRoughnessMapProperty(nullptr);
        effect.setEmissiveMapProperty(nullptr);
        effect.setOcclusionMapProperty(nullptr);
        effect.setSpecularMapEXTProperty(nullptr);
        effect.setSpecularColorMapEXTProperty(nullptr);
        effect.setIorEXTProperty(1.5f);
        effect.setSpecularFactorEXTProperty(1.0f);
        effect.setSpecularColorFactorEXTProperty(Vector3::One);
        effect.setMetallicFactorProperty(0.0f);
        effect.setRoughnessFactorProperty(1.0f);
        effect.DirectionalLight0.setEnabledProperty(false);
        device.setBlendStateProperty(BlendState::NonPremultiplied);

        effect.setEmissiveFactorProperty(Vector3::Zero);
        effect.setAmbientLightColorProperty(Vector3::One);
        drawAndExpect("unbound base-colour map -> opaque white", Color(255, 255, 255, 255));

        effect.setAmbientLightColorProperty(Vector3::Zero);
        effect.setEmissiveFactorProperty(Vector3(1.0f, 0.0f, 0.0f));
        drawAndExpect("unbound emissive map -> the emissive factor, opaque", Color(255, 0, 0, 255));

        device.setBlendStateProperty(BlendState::Opaque);
        effect.setAlphaModeEXTProperty(AlphaModeEXT::Opaque);
        effect.setEmissiveFactorProperty(Vector3::Zero);
    }

    // The rigid glTF record carrying TEXCOORD_1 (and an unused white COLOR_0): stride 60.
    struct DualUvPbrVertex
    {
        float px, py, pz;
        float nx, ny, nz;
        float tx, ty, tz, tw;
        float u0, v0;
        float u1, v1;
        std::uint32_t packedColor;
    };
    static_assert(sizeof(DualUvPbrVertex) == 60);

    // From the retired METAL renderer's metal_pbr_dual_uv_specular_test (Apple M4
    // AM4-085, rows B3/B5): KHR_materials_specular's strength and colour maps each honour their OWN
    // coordinate set and texture transform. Every vertex carries TEXCOORD_0 = (0.25, 0.5) and
    // TEXCOORD_1 = (0.75, 0.5), so a 2x1 map answers which set (or a +0.5 offset) reached the
    // sample: its left texel or its right one. The rough dielectric at N=V=L with IOR 3 reads ~66
    // where F0 = 0.25 and ~81 where the left texel zeroes it (alpha 0 strength, black colour).
    void RunSpecularCoordinateCases(CNA::Examples::PixelTestGame& test,
                                    GraphicsDevice& device,
                                    Texture2D& white,
                                    int sampleX,
                                    int sampleY)
    {
        const VertexDeclaration declaration(std::vector<VertexElement>{
            VertexElement(0, VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
            VertexElement(12, VertexElementFormat::Vector3, VertexElementUsage::Normal, 0),
            VertexElement(24, VertexElementFormat::Vector4, VertexElementUsage::Tangent, 0),
            VertexElement(40, VertexElementFormat::Vector2, VertexElementUsage::TextureCoordinate, 0),
            VertexElement(48, VertexElementFormat::Vector2, VertexElementUsage::TextureCoordinate, 1),
            VertexElement(56, VertexElementFormat::Color, VertexElementUsage::Color, 0)});
        std::vector<DualUvPbrVertex> vertices;
        for (const PbrVertex& corner : RigidQuad())
        {
            vertices.push_back({corner.px, corner.py, corner.pz, corner.nx, corner.ny, corner.nz,
                                corner.tx, corner.ty, corner.tz, corner.tw,
                                0.25f, 0.5f, 0.75f, 0.5f, Color::White.getPackedValueProperty()});
        }
        VertexBuffer buffer(device, declaration, static_cast<int>(vertices.size()), BufferUsage::WriteOnly);
        buffer.SetData(vertices.data(), 0, static_cast<int>(vertices.size()));
        device.SetVertexBuffer(&buffer);
        for (int slot = 0; slot < 8; ++slot)
            device.getSamplerStatesProperty()(slot, SamplerState::PointClamp);

        const auto pair = [&](const Color& left, const Color& right) {
            Texture2D texture(device, 2, 1);
            const Color texels[2] = {left, right};
            texture.SetData(texels, 0, 2);
            return texture;
        };
        Texture2D strength = pair(Color(255, 255, 255, 0), Color(255, 255, 255, 255));
        Texture2D colour = pair(Color(0, 0, 0, 255), Color(255, 255, 255, 255));
        TextureTransformEXT shift;
        shift.Offset = Vector2(0.5f, 0.0f);

        PbrEffect effect(device);
        Configure(effect);
        effect.setTextureProperty(&white);
        effect.setAmbientLightColorProperty(Vector3::Zero);
        effect.setIorEXTProperty(3.0f);
        effect.setSpecularFactorEXTProperty(1.0f);
        effect.setSpecularColorFactorEXTProperty(Vector3::One);
        effect.DirectionalLight0.setEnabledProperty(true);
        effect.DirectionalLight0.setDirectionProperty(Vector3(0.0f, 0.0f, -1.0f));
        effect.DirectionalLight0.setDiffuseColorProperty(Vector3::One);

        const auto drawAndExpect = [&](const char* caseName, const Color& expected)
        {
            device.Clear(Color(0, 255, 0, 255));
            effect.Apply();
            device.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
            const std::string label = std::string("PbrEffect stride 60 ") + caseName;
            test.ExpectPixel(label.c_str(), Rectangle(sampleX, sampleY, 1, 1), expected, 2);
        };

        effect.setSpecularMapEXTProperty(&strength);
        drawAndExpect("specular-strength map on TEXCOORD_0 reads the left (alpha 0) texel",
                      Color(81, 81, 81, 255));
        effect.setSpecularTextureCoordinateSetEXTProperty(1);
        drawAndExpect("specular-strength map on TEXCOORD_1 reads the right (alpha 1) texel",
                      Color(66, 66, 66, 255));
        effect.setSpecularTextureCoordinateSetEXTProperty(0);
        effect.setSpecularTextureTransformEXTProperty(shift);
        drawAndExpect("specular-strength map's own +0.5 transform reaches the right texel",
                      Color(66, 66, 66, 255));
        effect.setSpecularTextureTransformEXTProperty(TextureTransformEXT{});
        effect.setSpecularMapEXTProperty(nullptr);

        effect.setSpecularColorMapEXTProperty(&colour);
        drawAndExpect("specular-colour map on TEXCOORD_0 reads the left (black) texel",
                      Color(81, 81, 81, 255));
        effect.setSpecularColorTextureCoordinateSetEXTProperty(1);
        drawAndExpect("specular-colour map on TEXCOORD_1 reads the right (white) texel",
                      Color(66, 66, 66, 255));
        effect.setSpecularColorTextureCoordinateSetEXTProperty(0);
        effect.setSpecularColorTextureTransformEXTProperty(shift);
        drawAndExpect("specular-colour map's own +0.5 transform reaches the right texel",
                      Color(66, 66, 66, 255));

        device.SetVertexBuffer(nullptr);
        for (int slot = 0; slot < 8; ++slot)
        {
            device.getTexturesProperty()(slot, nullptr);
            device.getSamplerStatesProperty()(slot, SamplerState::LinearWrap);
        }
    }
}

class PbrTextureSlotMappingTest final : public CNA::Examples::PixelTestGame
{
protected:
    void RunTest() override
    {
        auto& device = getGraphicsDeviceProperty();
        const auto& viewport = device.getViewportProperty();
        const int sampleX = viewport.getWidthProperty() / 2;
        const int sampleY = viewport.getHeightProperty() / 2;

        const auto texture = [&](std::initializer_list<std::uint8_t> rgba) {
            return Texture2D::CreateFromPixels(device, 1, 1, std::vector<std::uint8_t>(rgba));
        };
        Texture2D white = texture({255, 255, 255, 255});
        Texture2D redBaseColor = texture({255, 0, 0, 255});
        Texture2D redBelowCutoff = texture({255, 0, 0, 64});
        Texture2D metallicRoughness = texture({0, 255, 0, 255});
        Texture2D blueEmissive = texture({0, 0, 255, 255});
        Texture2D occlusion = texture({64, 128, 192, 255});
        Texture2D normal = texture({255, 128, 191, 255});
        Texture2D zeroSpecularStrength = texture({255, 255, 255, 0});
        Texture2D redSpecularColor = texture({255, 0, 0, 255});

        device.SetDepthTestEnabled(false);
        device.setBlendStateProperty(BlendState::Opaque);
        device.setRasterizerStateProperty(RasterizerState::CullNone);

        const std::vector<PbrVertex> rigid = RigidQuad();
        VertexBuffer rigidBuffer(device, static_cast<int>(rigid.size()));
        rigidBuffer.SetDataRaw(rigid.data(), static_cast<int>(rigid.size()), sizeof(PbrVertex));
        device.SetVertexBuffer(&rigidBuffer);
        PbrEffect rigidEffect(device);
        Configure(rigidEffect);
        RunSlotCases(*this, device, rigidEffect, white, redBaseColor, metallicRoughness,
                     blueEmissive, occlusion, normal, sampleX, sampleY, "PbrEffect");
        RunMapScalarCases(*this, device, rigidEffect, white, occlusion, normal,
                          sampleX, sampleY, "PbrEffect");
        RunAlphaMaskCases(*this, device, rigidEffect, redBelowCutoff, redBaseColor,
                          sampleX, sampleY, "PbrEffect");
        RunSpecularTextureCases(*this, device, rigidEffect, white, zeroSpecularStrength,
                                redSpecularColor, sampleX, sampleY, "PbrEffect");
        RunUnboundFallbackCases(*this, device, rigidEffect, sampleX, sampleY, "PbrEffect");
        RunSpecularCoordinateCases(*this, device, white, sampleX, sampleY);

        const std::vector<SkinnedPbrVertex> skinned = SkinnedQuad();
        VertexBuffer skinnedBuffer(device, static_cast<int>(skinned.size()));
        skinnedBuffer.SetDataRaw(
            skinned.data(), static_cast<int>(skinned.size()), sizeof(SkinnedPbrVertex));
        device.SetVertexBuffer(&skinnedBuffer);
        SkinnedPbrEffect skinnedEffect(device);
        Configure(skinnedEffect);
        skinnedEffect.SetBoneTransforms({Matrix::getIdentityProperty()});
        skinnedEffect.setWeightsPerVertexProperty(1);
        RunSlotCases(*this, device, skinnedEffect, white, redBaseColor, metallicRoughness,
                     blueEmissive, occlusion, normal, sampleX, sampleY, "SkinnedPbrEffect");
        RunMapScalarCases(*this, device, skinnedEffect, white, occlusion, normal,
                          sampleX, sampleY, "SkinnedPbrEffect");
        RunAlphaMaskCases(*this, device, skinnedEffect, redBelowCutoff, redBaseColor,
                          sampleX, sampleY, "SkinnedPbrEffect");
        RunSpecularTextureCases(*this, device, skinnedEffect, white, zeroSpecularStrength,
                                redSpecularColor, sampleX, sampleY, "SkinnedPbrEffect");
        RunUnboundFallbackCases(*this, device, skinnedEffect, sampleX, sampleY, "SkinnedPbrEffect");

        device.SetVertexBuffer(nullptr);
    }
};

int main()
{
    return CNA::Examples::RunPixelTest<PbrTextureSlotMappingTest>();
}
