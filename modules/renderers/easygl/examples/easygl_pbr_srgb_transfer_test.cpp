// SPDX-License-Identifier: MS-PL
// plan_gltf.md GLTF-210/212/223: discriminating EasyGL pixel test for PBR colour transfer.
//
// A texture byte of 128 is the load-bearing witness: sRGB decoding maps 128/255 to
// 0.2158605 linear. Encoding that result for the ordinary UNORM backbuffer returns byte 128,
// while treating 128/255 as already linear and encoding it produces byte 188. Endpoint textures
// (0 or 255) cannot distinguish those two paths because both transfers are the identity there.
// The test repeats every case on the rigid and identity-skinned programs.

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

    template <typename Effect>
    void Configure(Effect& effect)
    {
        effect.setWorldProperty(Matrix::getIdentityProperty());
        effect.setViewProperty(Matrix::getIdentityProperty());
        effect.setProjectionProperty(Matrix::getIdentityProperty());
        effect.setMetallicFactorProperty(0.0f);
        effect.setRoughnessFactorProperty(1.0f);
        effect.setEncodeOutputToSrgbEXTProperty(true);
        effect.DirectionalLight0.setEnabledProperty(false);
        effect.DirectionalLight1.setEnabledProperty(false);
        effect.DirectionalLight2.setEnabledProperty(false);
    }

    template <typename Effect>
    void RunTransferCases(CNA::Examples::PixelTestGame& test,
                          GraphicsDevice& device,
                          Effect& effect,
                          Texture2D& black,
                          Texture2D& midGrey,
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

        // Ambient=1 makes the base-colour branch exactly the decoded albedo; no direct light or
        // emissive term contributes. With decode enabled the output round-trips to byte 128.
        effect.setTextureProperty(&midGrey);
        effect.setDiffuseColorProperty(Vector3::One);
        effect.setAmbientLightColorProperty(Vector3::One);
        effect.setEmissiveMapProperty(nullptr);
        effect.setEmissiveFactorProperty(Vector3::Zero);
        effect.setBaseColorTextureIsSrgbEXTProperty(true);
        drawAndExpect("base sRGB decode", Color(128, 128, 128, 255), 1);

        effect.setBaseColorTextureIsSrgbEXTProperty(false);
        drawAndExpect("base linear bypass", Color(188, 188, 188, 255), 1);

        // The factor is already linear. 0.5 * decode(128/255), encoded once, is byte 92. A
        // shader that transfers the factor too or multiplies after output encoding cannot match.
        effect.setBaseColorTextureIsSrgbEXTProperty(true);
        effect.setDiffuseColorProperty(Vector3(0.5f, 0.5f, 0.5f));
        drawAndExpect("base linear factor after decode", Color(92, 92, 92, 255), 2);

        // Isolate emissive with a black base. The same 128-vs-188 split proves that uSrgb.y is
        // independent from uSrgb.x and reaches the real shader rather than only the L6 carrier.
        effect.setTextureProperty(&black);
        effect.setDiffuseColorProperty(Vector3::One);
        effect.setAmbientLightColorProperty(Vector3::One);
        effect.setEmissiveMapProperty(&midGrey);
        effect.setEmissiveFactorProperty(Vector3::One);
        effect.setEmissiveTextureIsSrgbEXTProperty(true);
        drawAndExpect("emissive sRGB decode", Color(128, 128, 128, 255), 1);

        effect.setEmissiveTextureIsSrgbEXTProperty(false);
        drawAndExpect("emissive linear bypass", Color(188, 188, 188, 255), 1);

        // GLTF-223: base and emissive are independently textured in linear space, then added.
        // OETF(decode(128/255)*0.25 + decode(128/255)*0.5) gives byte 112. Multiplying the two
        // branches, dropping either one or adding after output encoding all give different bytes.
        effect.setTextureProperty(&midGrey);
        effect.setDiffuseColorProperty(Vector3(0.25f, 0.25f, 0.25f));
        effect.setEmissiveTextureIsSrgbEXTProperty(true);
        effect.setEmissiveFactorProperty(Vector3(0.5f, 0.5f, 0.5f));
        drawAndExpect("base plus emissive in linear space", Color(112, 112, 112, 255), 2);
    }
}

class PbrSrgbTransferTest final : public CNA::Examples::PixelTestGame
{
protected:
    void RunTest() override
    {
        auto& device = getGraphicsDeviceProperty();
        const auto& viewport = device.getViewportProperty();
        const int sampleX = viewport.getWidthProperty() / 2;
        const int sampleY = viewport.getHeightProperty() / 2;

        const std::vector<std::uint8_t> blackPixel = {0, 0, 0, 255};
        const std::vector<std::uint8_t> midGreyPixel = {128, 128, 128, 255};
        Texture2D black = Texture2D::CreateFromPixels(device, 1, 1, blackPixel);
        Texture2D midGrey = Texture2D::CreateFromPixels(device, 1, 1, midGreyPixel);

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
        RunTransferCases(*this, device, rigidEffect, black, midGrey, sampleX, sampleY, "PbrEffect");

        const std::vector<SkinnedPbrVertex> skinned = SkinnedQuad();
        VertexBuffer skinnedBuffer(device, static_cast<int>(skinned.size()));
        skinnedBuffer.SetDataRaw(skinned.data(), static_cast<int>(skinned.size()),
                                 static_cast<int>(sizeof(SkinnedPbrVertex)));
        device.SetVertexBuffer(&skinnedBuffer);
        SkinnedPbrEffect skinnedEffect(device);
        Configure(skinnedEffect);
        skinnedEffect.SetBoneTransforms({Matrix::getIdentityProperty()});
        skinnedEffect.setWeightsPerVertexProperty(1);
        RunTransferCases(
            *this, device, skinnedEffect, black, midGrey, sampleX, sampleY, "SkinnedPbrEffect");

        device.SetVertexBuffer(nullptr);
    }
};

int main()
{
    return CNA::Examples::RunPixelTest<PbrSrgbTransferTest>();
}
