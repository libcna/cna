// SPDX-License-Identifier: MS-PL
// plan_gltf.md GLTF-343/344: real EasyGL pixel witness for KHR_materials_ior/specular factors.
//
// The scene deliberately makes the expected BRDF trivial. Every quad is black, fully rough,
// non-metallic, viewed and lit straight along +Z, with no ambient or emissive term. At its centre
// N=V=L=H=(0,0,1), so D=1/pi, G=1, Fresnel=F0, diffuse=0 and the linear output is exactly
// F0/(4*pi). The extension witness (IOR=2, strength=.3, colour=(.25,1,12)) therefore must encode
// approximately (2,9,43) rather than the core default's grey (11,11,11). That channel-separated
// blue value also distinguishes the required clamp-before-strength order.

#include "common/PixelTestGame.hpp"

#include "Microsoft/Xna/Framework/MathHelper.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/PbrEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SkinnedPbrEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"

#include <cstdint>
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

    template <typename Vertex, typename MakeVertex>
    std::vector<Vertex> TwoQuads(float left, float middle, float right, MakeVertex makeVertex)
    {
        std::vector<Vertex> out;
        out.reserve(12);
        const auto append = [&](float x0, float x1)
        {
            const Vertex tl = makeVertex(x0,  1.0f, 0.0f, 0.0f);
            const Vertex bl = makeVertex(x0, -1.0f, 0.0f, 1.0f);
            const Vertex br = makeVertex(x1, -1.0f, 1.0f, 1.0f);
            const Vertex tr = makeVertex(x1,  1.0f, 1.0f, 0.0f);
            out.insert(out.end(), {tl, bl, br, tl, br, tr});
        };
        append(left, middle);
        append(middle, right);
        return out;
    }

    PbrVertex MakePbrVertex(float x, float y, float u, float v)
    {
        return {x, y, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 0.0f, 1.0f, u, v};
    }

    SkinnedPbrVertex MakeSkinnedPbrVertex(float x, float y, float u, float v)
    {
        return {MakePbrVertex(x, y, u, v), 1.0f, 0.0f, 0.0f, 0.0f, 0, 0, 0, 0};
    }

    template <typename Effect>
    void Configure(Effect& effect)
    {
        effect.setWorldProperty(Matrix::getIdentityProperty());
        effect.setViewProperty(Matrix::CreateLookAt(
            Vector3(0.0f, 0.0f, 3.0f), Vector3::Zero, Vector3(0.0f, 1.0f, 0.0f)));
        effect.setProjectionProperty(Matrix::CreateOrthographic(2.0f, 2.0f, 0.1f, 10.0f));
        effect.setDiffuseColorProperty(Vector3::Zero);
        effect.setAmbientLightColorProperty(Vector3::Zero);
        effect.setEmissiveFactorProperty(Vector3::Zero);
        effect.setMetallicFactorProperty(0.0f);
        effect.setRoughnessFactorProperty(1.0f);
        effect.setEncodeOutputToSrgbEXTProperty(true);
        effect.DirectionalLight0.setEnabledProperty(true);
        effect.DirectionalLight0.setDirectionProperty(Vector3(0.0f, 0.0f, -1.0f));
        effect.DirectionalLight0.setDiffuseColorProperty(Vector3::One);
        effect.DirectionalLight1.setEnabledProperty(false);
        effect.DirectionalLight2.setEnabledProperty(false);
    }

    template <typename Effect>
    void SetExtensionWitness(Effect& effect)
    {
        effect.setIorEXTProperty(2.0f);
        effect.setSpecularFactorEXTProperty(0.3f);
        effect.setSpecularColorFactorEXTProperty(Vector3(0.25f, 1.0f, 12.0f));
    }

    Color ReadPixel(GraphicsDevice& device, int x, int y)
    {
        Color pixel(0, 0, 0, 0);
        const Rectangle region(x, y, 1, 1);
        device.GetBackBufferData(&region, &pixel, 0, 1);
        return pixel;
    }
}

class PbrFresnelFactorsTest final : public CNA::Examples::PixelTestGame
{
protected:
    void RunTest() override
    {
        auto& device = getGraphicsDeviceProperty();
        const auto& viewport = device.getViewportProperty();
        const int sampleY = viewport.getHeightProperty() / 2;

        device.Clear(Color(0, 255, 0, 255));
        device.SetDepthTestEnabled(false);
        device.setBlendStateProperty(BlendState::Opaque);
        device.setRasterizerStateProperty(RasterizerState::CullNone);

        const std::vector<PbrVertex> rigid =
            TwoQuads<PbrVertex>(-1.0f, -0.5f, 0.0f, MakePbrVertex);
        VertexBuffer rigidBuffer(device, static_cast<int>(rigid.size()));
        rigidBuffer.SetDataRaw(
            rigid.data(), static_cast<int>(rigid.size()), static_cast<int>(sizeof(PbrVertex)));
        device.SetVertexBuffer(&rigidBuffer);

        PbrEffect rigidEffect(device);
        Configure(rigidEffect);
        rigidEffect.Apply();
        device.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
        SetExtensionWitness(rigidEffect);
        rigidEffect.Apply();
        device.DrawPrimitives(PrimitiveType::TriangleList, 6, 2);

        const std::vector<SkinnedPbrVertex> skinned =
            TwoQuads<SkinnedPbrVertex>(0.0f, 0.5f, 1.0f, MakeSkinnedPbrVertex);
        VertexBuffer skinnedBuffer(device, static_cast<int>(skinned.size()));
        skinnedBuffer.SetDataRaw(skinned.data(), static_cast<int>(skinned.size()),
                                 static_cast<int>(sizeof(SkinnedPbrVertex)));
        device.SetVertexBuffer(&skinnedBuffer);

        SkinnedPbrEffect skinnedEffect(device);
        Configure(skinnedEffect);
        skinnedEffect.SetBoneTransforms({Matrix::getIdentityProperty()});
        skinnedEffect.setWeightsPerVertexProperty(1);
        skinnedEffect.Apply();
        device.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
        SetExtensionWitness(skinnedEffect);
        skinnedEffect.Apply();
        device.DrawPrimitives(PrimitiveType::TriangleList, 6, 2);
        device.SetVertexBuffer(nullptr);

        const int width = viewport.getWidthProperty();
        const Color coreExpected(11, 11, 11, 255);
        const Color extensionExpected(2, 9, 43, 255);
        ExpectPixel("PbrEffect core dielectric F0", Rectangle(width / 8, sampleY, 1, 1),
                    coreExpected, 3);
        ExpectPixel("PbrEffect IOR/specular F0", Rectangle(width * 3 / 8, sampleY, 1, 1),
                    extensionExpected, 4);
        ExpectPixel("SkinnedPbrEffect core dielectric F0",
                    Rectangle(width * 5 / 8, sampleY, 1, 1), coreExpected, 3);
        ExpectPixel("SkinnedPbrEffect IOR/specular F0",
                    Rectangle(width * 7 / 8, sampleY, 1, 1), extensionExpected, 4);

        // Hold F0 at exactly .04 while changing only F90 from 1 to .3: IOR 1.5 gives .04,
        // colour 10/3 is multiplied and clamped before strength .3, yielding .04 again. The
        // symmetric grazing setup has V=(+.995,0,.0995), L=(-.995,0,.0995), hence H=N and a
        // substantial (1-VdotH)^5 term. A shader that uploads xyz but still hardcodes F90=1
        // would render both passes identically around encoded value 34.
        const std::vector<PbrVertex> grazing =
            TwoQuads<PbrVertex>(-1.0f, 0.0f, 1.0f, MakePbrVertex);
        VertexBuffer grazingBuffer(device, static_cast<int>(grazing.size()));
        grazingBuffer.SetDataRaw(grazing.data(), static_cast<int>(grazing.size()),
                                 static_cast<int>(sizeof(PbrVertex)));
        device.SetVertexBuffer(&grazingBuffer);

        const Vector3 grazingEye(3.0f, 0.0f, 0.3f);
        rigidEffect.setViewProperty(Matrix::CreateLookAt(
            grazingEye, Vector3::Zero, Vector3(0.0f, 1.0f, 0.0f)));
        rigidEffect.setProjectionProperty(Matrix::CreatePerspectiveFieldOfView(
            MathHelper::PiOver4,
            static_cast<float>(width) / static_cast<float>(viewport.getHeightProperty()),
            0.1f, 10.0f));
        Vector3 grazingDirection(1.0f, 0.0f, -0.1f);
        grazingDirection.Normalize();
        rigidEffect.DirectionalLight0.setDirectionProperty(grazingDirection);

        rigidEffect.setIorEXTProperty(1.5f);
        rigidEffect.setSpecularFactorEXTProperty(1.0f);
        rigidEffect.setSpecularColorFactorEXTProperty(Vector3::One);
        device.Clear(Color(0, 0, 0, 255));
        rigidEffect.Apply();
        device.DrawPrimitives(PrimitiveType::TriangleList, 0, 4);
        const Color coreF90 = ReadPixel(device, width / 2, sampleY);
        ExpectPixel("core F90=1 grazing response", Rectangle(width / 2, sampleY, 1, 1),
                    Color(34, 34, 34, 255), 5);

        rigidEffect.setSpecularFactorEXTProperty(0.3f);
        rigidEffect.setSpecularColorFactorEXTProperty(
            Vector3(10.0f / 3.0f, 10.0f / 3.0f, 10.0f / 3.0f));
        device.Clear(Color(0, 0, 0, 255));
        rigidEffect.Apply();
        device.DrawPrimitives(PrimitiveType::TriangleList, 0, 4);
        const Color reducedF90 = ReadPixel(device, width / 2, sampleY);
        ExpectPixel("specular F90=.3 grazing response", Rectangle(width / 2, sampleY, 1, 1),
                    Color(16, 16, 16, 255), 5);
        Check(coreF90.getRProperty() >= reducedF90.getRProperty() + 12,
              "changing F90 while holding F0 fixed changes the real shader result");
    }
};

int main()
{
    return CNA::Examples::RunPixelTest<PbrFresnelFactorsTest>();
}
