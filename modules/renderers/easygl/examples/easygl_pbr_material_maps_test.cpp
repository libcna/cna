// SPDX-License-Identifier: MS-PL
// plan_gltf.md GLTF-224..227/264: real EasyGL pixel checks for material-map semantics and the
// skinned PBR joint normal matrix.
//
// Two one-pixel textures are deliberately channel-asymmetric. Occlusion (64,128,192) makes the
// required red-channel read distinguishable from green or blue. Normal (255,128,191) decodes to
// approximately (1,0,.498), so scaling tangent-space X/Y only changes the normalized direction;
// scaling all three components would leave every scale case identical.

#include "common/PixelTestGame.hpp"

#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
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

    PbrVertex MakePbrVertex(float x, float y, float u, float v)
    {
        return {x, y, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 0.0f, 1.0f, u, v};
    }

    std::vector<PbrVertex> RigidQuad()
    {
        const PbrVertex tl = MakePbrVertex(-1.0f,  1.0f, 0.0f, 0.0f);
        const PbrVertex bl = MakePbrVertex(-1.0f, -1.0f, 0.0f, 1.0f);
        const PbrVertex br = MakePbrVertex( 1.0f, -1.0f, 1.0f, 1.0f);
        const PbrVertex tr = MakePbrVertex( 1.0f,  1.0f, 1.0f, 0.0f);
        return {tl, bl, br, tl, br, tr};
    }

    std::vector<SkinnedPbrVertex> SkinnedQuad()
    {
        std::vector<SkinnedPbrVertex> result;
        for (const PbrVertex& vertex : RigidQuad())
            result.push_back({vertex, 1.0f, 0.0f, 0.0f, 0.0f, 0, 0, 0, 0});
        return result;
    }

    std::vector<SkinnedPbrVertex> SkinnedTiltedNormalQuad()
    {
        std::vector<SkinnedPbrVertex> result = SkinnedQuad();
        for (SkinnedPbrVertex& vertex : result)
        {
            vertex.base.nx = 0.0f;
            vertex.base.ny = 0.6f;
            vertex.base.nz = 0.8f;
        }
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
        effect.setEncodeOutputToSrgbEXTProperty(true);
        effect.DirectionalLight0.setEnabledProperty(false);
        effect.DirectionalLight1.setEnabledProperty(false);
        effect.DirectionalLight2.setEnabledProperty(false);
    }

    template <typename Effect>
    void RunMapCases(CNA::Examples::PixelTestGame& test,
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

        effect.setTextureProperty(&white);
        effect.setNormalMapProperty(nullptr);
        effect.setOcclusionMapProperty(&occlusion);
        effect.setAmbientLightColorProperty(Vector3::One);

        // The occlusion map is linear. Red=64/255; the final ordinary UNORM framebuffer receives
        // OETF(red)=137. Reading green or blue would instead produce about 188 or 225.
        effect.setOcclusionStrengthEXTProperty(1.0f);
        drawAndExpect("occlusion red channel strength 1", Color(137, 137, 137, 255), 1);

        // 1 + .5*(64/255 - 1) = .62549 linear, whose encoded byte is 207.
        effect.setOcclusionStrengthEXTProperty(0.5f);
        drawAndExpect("occlusion specification formula strength .5",
                      Color(207, 207, 207, 255), 1);

        // Strength zero means no occlusion regardless of the texture, never black.
        effect.setOcclusionStrengthEXTProperty(0.0f);
        drawAndExpect("occlusion strength zero is neutral", Color(255, 255, 255, 255), 0);

        effect.setOcclusionMapProperty(nullptr);
        effect.setOcclusionStrengthEXTProperty(1.0f);
        effect.setNormalMapProperty(&normal);
        effect.setAmbientLightColorProperty(Vector3::Zero);
        effect.DirectionalLight0.setEnabledProperty(true);
        effect.DirectionalLight0.setDirectionProperty(Vector3(0.0f, 0.0f, -1.0f));
        effect.DirectionalLight0.setDiffuseColorProperty(Vector3::One);

        // With V=L=+Z at the centre and roughness=1, the production BRDF reduces to a directly
        // hand-checkable function of N.z. Decoding (255,128,191) via rgb*2-1 and applying XY-only
        // scales 1/.35/0 gives N.z=.4458/.8182/1 and encoded bytes 104/138/151 respectively.
        // Omitting rgb*2-1, scaling Z too, or ignoring the uniform cannot match all three.
        effect.setNormalScaleEXTProperty(1.0f);
        drawAndExpect("normal rgb decode scale 1", Color(104, 104, 104, 255), 3);

        effect.setNormalScaleEXTProperty(0.35f);
        drawAndExpect("normal XY-only scale .35", Color(138, 138, 138, 255), 3);

        effect.setNormalScaleEXTProperty(0.0f);
        drawAndExpect("normal scale zero restores geometric normal",
                      Color(151, 151, 151, 255), 3);
    }
}

class PbrMaterialMapsTest final : public CNA::Examples::PixelTestGame
{
protected:
    void RunTest() override
    {
        auto& device = getGraphicsDeviceProperty();
        const auto& viewport = device.getViewportProperty();
        const int sampleX = viewport.getWidthProperty() / 2;
        const int sampleY = viewport.getHeightProperty() / 2;

        const std::vector<std::uint8_t> whitePixel = {255, 255, 255, 255};
        const std::vector<std::uint8_t> occlusionPixel = {64, 128, 192, 255};
        const std::vector<std::uint8_t> normalPixel = {255, 128, 191, 255};
        Texture2D white = Texture2D::CreateFromPixels(device, 1, 1, whitePixel);
        Texture2D occlusion = Texture2D::CreateFromPixels(device, 1, 1, occlusionPixel);
        Texture2D normal = Texture2D::CreateFromPixels(device, 1, 1, normalPixel);

        device.SetDepthTestEnabled(false);
        device.setBlendStateProperty(BlendState::Opaque);
        device.setRasterizerStateProperty(RasterizerState::CullNone);

        const std::vector<PbrVertex> rigid = RigidQuad();
        VertexBuffer rigidBuffer(device, static_cast<int>(rigid.size()));
        rigidBuffer.SetDataRaw(
            rigid.data(), static_cast<int>(rigid.size()), static_cast<int>(sizeof(PbrVertex)));
        device.SetVertexBuffer(&rigidBuffer);
        PbrEffect rigidEffect(device);
        Configure(rigidEffect);
        RunMapCases(
            *this, device, rigidEffect, white, occlusion, normal, sampleX, sampleY, "PbrEffect");

        const std::vector<SkinnedPbrVertex> skinned = SkinnedQuad();
        VertexBuffer skinnedBuffer(device, static_cast<int>(skinned.size()));
        skinnedBuffer.SetDataRaw(skinned.data(), static_cast<int>(skinned.size()),
                                 static_cast<int>(sizeof(SkinnedPbrVertex)));
        device.SetVertexBuffer(&skinnedBuffer);
        SkinnedPbrEffect skinnedEffect(device);
        Configure(skinnedEffect);
        skinnedEffect.SetBoneTransforms({Matrix::getIdentityProperty()});
        skinnedEffect.setWeightsPerVertexProperty(1);
        RunMapCases(*this, device, skinnedEffect, white, occlusion, normal,
                    sampleX, sampleY, "SkinnedPbrEffect");

        // GLTF-264's exact corpus geometry normal and joint scale. Inverse-transpose(S[1,2,1])
        // sends (0,.6,.8) to normalize(0,.3,.8). With L=+Y, V=+Z and this test's rough dielectric
        // material the analytic PBR result encodes to byte 93; the old direct joint 3x3 multiply
        // produced 139. This pins the third EasyGL skinned vertex program in addition to
        // EasyGL_SkinnedEffect_WorldNormal's per-pixel/per-vertex stock-effect programs.
        const std::vector<SkinnedPbrVertex> tilted = SkinnedTiltedNormalQuad();
        VertexBuffer tiltedBuffer(device, static_cast<int>(tilted.size()));
        tiltedBuffer.SetDataRaw(tilted.data(), static_cast<int>(tilted.size()),
                                static_cast<int>(sizeof(SkinnedPbrVertex)));
        device.SetVertexBuffer(&tiltedBuffer);
        skinnedEffect.SetBoneTransforms({Matrix::CreateScale(1.0f, 2.0f, 1.0f)});
        skinnedEffect.setTextureProperty(&white);
        skinnedEffect.setNormalMapProperty(nullptr);
        skinnedEffect.setOcclusionMapProperty(nullptr);
        skinnedEffect.setAmbientLightColorProperty(Vector3::Zero);
        skinnedEffect.setEmissiveFactorProperty(Vector3::Zero);
        skinnedEffect.setMetallicFactorProperty(0.0f);
        skinnedEffect.setRoughnessFactorProperty(1.0f);
        skinnedEffect.DirectionalLight0.setEnabledProperty(true);
        skinnedEffect.DirectionalLight0.setDirectionProperty(Vector3(0.0f, -1.0f, 0.0f));
        skinnedEffect.DirectionalLight0.setDiffuseColorProperty(Vector3::One);
        device.Clear(Color(0, 255, 0, 255));
        skinnedEffect.Apply();
        device.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
        ExpectPixel("SkinnedPbrEffect inverse-transpose non-uniform joint normal",
                    Rectangle(sampleX, sampleY, 1, 1), Color(93, 93, 93, 255), 2);

        device.SetVertexBuffer(nullptr);
    }
};

int main()
{
    return CNA::Examples::RunPixelTest<PbrMaterialMapsTest>();
}
