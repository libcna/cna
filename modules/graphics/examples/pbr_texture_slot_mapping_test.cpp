// SPDX-License-Identifier: MS-PL
// plan_gltf.md GLTF-373/379: discriminating real-pixel proof of all five PBR texture bindings
// and of glTF MASK coverage staying inside both rigid and skinned PBR programs.
//
// This source deliberately uses only the PBR contract shared by every renderer: no output sRGB
// transfer, normalScale, occlusionStrength, or other later shader-semantic extensions. Five solid
// one-pixel sentinels make a slot swap observable: red base colour, green metallic-roughness
// (G=roughness=1, B=metallic=0), blue emissive, channel-asymmetric occlusion, and a tilted normal.

#include "common/PixelTestGame.hpp"

#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/AlphaModeEXT.hpp"
#include "Microsoft/Xna/Framework/Graphics/PbrEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SkinnedPbrEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"

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
        RunAlphaMaskCases(*this, device, rigidEffect, redBelowCutoff, redBaseColor,
                          sampleX, sampleY, "PbrEffect");

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
        RunAlphaMaskCases(*this, device, skinnedEffect, redBelowCutoff, redBaseColor,
                          sampleX, sampleY, "SkinnedPbrEffect");

        device.SetVertexBuffer(nullptr);
    }
};

int main()
{
    return CNA::Examples::RunPixelTest<PbrTextureSlotMappingTest>();
}
