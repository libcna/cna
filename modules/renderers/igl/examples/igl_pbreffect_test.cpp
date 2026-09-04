// SPDX-License-Identifier: MS-PL
// plans/plan_igl.md IGL-37/IGL-55: PbrEffect pixel conformance, hand-derived rather than
// captured-and-pasted -- see vulkan_pbreffect_handderived_test.cpp for the full independent
// GGX/Smith-Schlick-GGX/Schlick-Fresnel re-derivation this scene and expected value are taken
// from (case (a) only, to keep this renderer's own test suite in its established single-scenario
// style).
//
// A real perspective camera at (0,0,3) looks straight down -Z at a flat quad at the origin; at the
// exact centre pixel the world position is (0,0,0), so the view vector V, the quad's own geometric
// normal N, and the light direction L (light0 pointed straight at the camera) are ALL exactly
// (0,0,1) -- every dot product in the BRDF (NdotL, NdotV, NdotH, VdotH) is exactly 1, which is what
// makes the analytic derivation below tractable at all.
//
// White albedo, roughness=0.5, metallic=0.0, light0 diffuse=(1,1,1), no ambient:
//   a2 = 0.5^4 = 0.0625; D = a2/(pi*a2^2) = 5.092958
//   k = (0.5+1)^2/8 = 0.28125; G = (1/(1*(1-k)+k))^2 = 1 (NdotV=NdotL=1 cancels exactly)
//   F = mix(0.04, albedo, metallic=0) = 0.04 (VdotH=1 -> (1-VdotH)^5 = 0)
//   specular = D*G*F/(4*NdotV*NdotL) = 5.092958*1*0.04/4 = 0.050930
//   kd = 1-F = 0.96; diffuseColor = albedo*(1-metallic) = 1
//   Lo = (kd*diffuseColor/pi + specular)*lightColor*NdotL = 0.305577+0.050930 = 0.356507
//   final = Lo -> round(0.356507*255) = 91
//
// Exit code 0 = all PASS, 1 = any FAIL, 77 = SKIP (no GPU/display).

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/MathHelper.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
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
}

class IglPbrEffectTest : public CNA::Examples::PixelTestGame
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;

protected:
    void RunTest() override
    {
        auto& device = getGraphicsDeviceProperty();

        device.Clear(Color(static_cast<bytecs>(0), static_cast<bytecs>(255),
                           static_cast<bytecs>(0), static_cast<bytecs>(255)));
        device.setRasterizerStateProperty(RasterizerState::CullNone);
        device.setDepthStencilStateProperty(DepthStencilState::None);

        auto albedo = std::make_unique<Texture2D>(device, 1, 1);
        const Color white[1] = {Color(static_cast<bytecs>(255), static_cast<bytecs>(255),
                                      static_cast<bytecs>(255), static_cast<bytecs>(255))};
        albedo->SetData(white, 1);

        const Matrix view = Matrix::CreateLookAt(Vector3(0.0f, 0.0f, 3.0f), Vector3::Zero,
                                                 Vector3(0.0f, 1.0f, 0.0f));
        const Matrix projection =
            Matrix::CreatePerspectiveFieldOfView(MathHelper::PiOver4, 1.0f, 0.1f, 100.0f);

        PbrEffect effect(device);
        effect.setTextureProperty(albedo.get());
        effect.setNormalMapProperty(nullptr);
        effect.setMetallicFactorProperty(0.0f);
        effect.setRoughnessFactorProperty(0.5f);
        effect.setAmbientLightColorProperty(Vector3::Zero);
        // plans/plan_gltf.md GLTF-476: the value derived above is LINEAR, and it is the value the
        // reference re-derivation (vulkan_pbreffect_handderived_test.cpp) measures -- with the
        // same three switches turned off, for the same reason. This test used to omit them and
        // still read 91 only because this renderer had no colour management at all; once it
        // gained one, an unqualified expectation would have measured the transfer function
        // instead of the BRDF.
        effect.setBaseColorTextureIsSrgbEXTProperty(false);
        effect.setEmissiveTextureIsSrgbEXTProperty(false);
        effect.setEncodeOutputToSrgbEXTProperty(false);
        effect.DirectionalLight0.setEnabledProperty(true);
        effect.DirectionalLight0.setDirectionProperty(Vector3(0.0f, 0.0f, -1.0f));
        effect.DirectionalLight0.setDiffuseColorProperty(Vector3(1.0f, 1.0f, 1.0f));
        effect.DirectionalLight1.setEnabledProperty(false);
        effect.DirectionalLight2.setEnabledProperty(false);
        effect.setWorldProperty(Matrix::getIdentityProperty());
        effect.setViewProperty(view);
        effect.setProjectionProperty(projection);

        // A large flat quad at the origin, its own geometric normal (0,0,1), tangent (1,0,0,1).
        const Vector3 normal(0.0f, 0.0f, 1.0f);
        const Vector4 tangent(1.0f, 0.0f, 0.0f, 1.0f);
        const std::vector<VertexPositionNormalTangentTexture> vertices = {
            VertexPositionNormalTangentTexture(Vector3(-4.0f, -4.0f, 0.0f), normal, tangent,
                                               Vector2(0.0f, 1.0f)),
            VertexPositionNormalTangentTexture(Vector3(4.0f, -4.0f, 0.0f), normal, tangent,
                                               Vector2(1.0f, 1.0f)),
            VertexPositionNormalTangentTexture(Vector3(4.0f, 4.0f, 0.0f), normal, tangent,
                                               Vector2(1.0f, 0.0f)),
            VertexPositionNormalTangentTexture(Vector3(-4.0f, 4.0f, 0.0f), normal, tangent,
                                               Vector2(0.0f, 0.0f)),
        };

        const std::uint16_t indices[6] = {0, 1, 2, 0, 2, 3};

        VertexBuffer vertexBuffer(device,
                                  VertexPositionNormalTangentTexture::getVertexDeclarationStatic(),
                                  static_cast<int>(vertices.size()), BufferUsage::WriteOnly);
        vertexBuffer.SetData(vertices.data(), 0, static_cast<int>(vertices.size()));

        IndexBuffer indexBuffer(device, IndexElementSize::SixteenBits, 6, BufferUsage::WriteOnly);
        indexBuffer.SetData(indices, 0, 6);

        device.SetVertexBuffer(&vertexBuffer);
        device.setIndicesProperty(&indexBuffer);

        for (EffectPass& pass : effect.getCurrentTechniqueProperty()->getPassesProperty())
        {
            pass.Apply();
            device.DrawIndexedPrimitives(PrimitiveType::TriangleList, 0, 0, 4, 0, 2);
        }

        ExpectPixel("the analytic GGX/Fresnel/Smith BRDF value at N=V=L reaches the shader",
                    Rectangle(kSize / 2, kSize / 2, 1, 1),
                    Color(static_cast<bytecs>(91), static_cast<bytecs>(91),
                          static_cast<bytecs>(91), static_cast<bytecs>(255)),
                    /*tolerance=*/14);

        // plans/plan_gltf.md GLTF-476, the same scene with KHR_materials_specular's scalar strength at 0.
        // The extension weights BOTH Fresnel endpoints, so F0 = 0.04 * 0 = 0 and F90 = 0, and at
        // VdotH = 1 the Schlick term is exactly F0 -- the specular lobe vanishes and kd rises to 1:
        //   Lo = (1 * 1 / pi + 0) * 1 * 1 = 0.318310 -> round(0.318310 * 255) = 81
        // against the 91 above. That ten-code difference is the whole of the extension's factor-only
        // effect on this scene, and this renderer used to answer 91 for both, because it hard-coded
        // 0.04 in the shader and transported neither endpoint.
        device.Clear(Color(static_cast<bytecs>(0), static_cast<bytecs>(0), static_cast<bytecs>(0),
                           static_cast<bytecs>(255)));
        effect.setSpecularFactorEXTProperty(0.0f);
        for (EffectPass& pass : effect.getCurrentTechniqueProperty()->getPassesProperty())
        {
            pass.Apply();
            device.DrawIndexedPrimitives(PrimitiveType::TriangleList, 0, 0, 4, 0, 2);
        }

        ExpectPixel("KHR_materials_specular's scalar strength weights both Fresnel endpoints",
                    Rectangle(kSize / 2, kSize / 2, 1, 1),
                    Color(static_cast<bytecs>(81), static_cast<bytecs>(81),
                          static_cast<bytecs>(81), static_cast<bytecs>(255)),
                    /*tolerance=*/6);
    }

public:
    IglPbrEffectTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kSize);
        gdm_->setPreferredBackBufferHeightProperty(kSize);
        gdm_->setSynchronizeWithVerticalRetraceProperty(false);
    }
};

int main()
{
    return CNA::Examples::RunPixelTest<IglPbrEffectTest>();
}
