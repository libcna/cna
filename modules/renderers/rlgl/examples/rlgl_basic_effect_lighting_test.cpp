// SPDX-License-Identifier: MS-PL
// plans/plan_rlgl.md RLGL-034: renderer-local coverage for the inverse-transpose normal
// matrix under non-uniform World scale. The shared EasyGL-derived tests cover the remaining
// material, light, specular, vertex-colour, and lighting-stage semantics.

#include "common/PixelTestGame.hpp"

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionNormalTexture.hpp"

#include <array>
#include <memory>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kSize = 64;
}

class RlglBasicEffectLightingTest final : public CNA::Examples::PixelTestGame
{
public:
    RlglBasicEffectLightingTest()
    {
        graphics_ = std::make_unique<GraphicsDeviceManager>(this);
        graphics_->setGraphicsProfileProperty(GraphicsProfile::HiDef);
        graphics_->setPreferredBackBufferWidthProperty(kSize);
        graphics_->setPreferredBackBufferHeightProperty(kSize);
        graphics_->setPreferredPresentationModeProperty(PresentationMode::NativeBackBuffer);
        graphics_->setSynchronizeWithVerticalRetraceProperty(false);
    }

protected:
    void RunTest() override
    {
        auto& device = getGraphicsDeviceProperty();
        device.setBlendStateProperty(BlendState::Opaque);
        device.setDepthStencilStateProperty(DepthStencilState::None);
        device.setRasterizerStateProperty(RasterizerState::CullNone);

        const Vector3 diagonalNormal(0.70710678f, 0.70710678f, 0.0f);
        const std::array<VertexPositionNormalTexture, 3> triangle{
            VertexPositionNormalTexture(
                Vector3(-0.45f, -0.75f, 0.0f), diagonalNormal, Vector2::Zero),
            VertexPositionNormalTexture(
                Vector3(0.45f, -0.75f, 0.0f), diagonalNormal, Vector2::Zero),
            VertexPositionNormalTexture(
                Vector3(0.0f, 0.75f, 0.0f), diagonalNormal, Vector2::Zero)};

        BasicEffect effect(device);
        effect.setWorldProperty(Matrix::CreateScale(2.0f, 1.0f, 1.0f));
        effect.setViewProperty(Matrix::getIdentityProperty());
        effect.setProjectionProperty(Matrix::getIdentityProperty());
        effect.setTextureEnabledProperty(false);
        effect.setVertexColorEnabledProperty(false);
        effect.setLightingEnabledProperty(true);
        effect.setPreferPerPixelLightingProperty(false);
        effect.setAmbientLightColorProperty(Vector3::Zero);
        effect.setDiffuseColorProperty(Vector3::One);
        effect.setEmissiveColorProperty(Vector3::Zero);
        effect.setSpecularColorProperty(Vector3::Zero);
        effect.DirectionalLight0.setEnabledProperty(true);
        effect.DirectionalLight0.setDirectionProperty(Vector3(-1.0f, 0.0f, 0.0f));
        effect.DirectionalLight0.setDiffuseColorProperty(Vector3::One);
        effect.DirectionalLight1.setEnabledProperty(false);
        effect.DirectionalLight2.setEnabledProperty(false);

        device.Clear(Color::Black);
        effect.Apply();
        device.DrawUserPrimitives(
            PrimitiveType::TriangleList, triangle.data(), 0, 1);

        // inverse-transpose(scale(2,1,1)) * normalize(1,1,0) normalizes to
        // (0.4472,0.8944,0); the +X light therefore produces 0.4472 * 255 ~= 114.
        ExpectPixel(
            "non-uniform World scale uses inverse-transpose normal matrix",
            Rectangle(kSize / 2, kSize / 2, 1, 1), Color(114, 114, 114, 255), 4);

        const std::array<VertexPositionNormalTexture, 3> faceOnTriangle{
            VertexPositionNormalTexture(
                Vector3(-0.75f, -0.75f, -0.5f), Vector3::UnitZ, Vector2::Zero),
            VertexPositionNormalTexture(
                Vector3(0.75f, -0.75f, -0.5f), Vector3::UnitZ, Vector2::Zero),
            VertexPositionNormalTexture(
                Vector3(0.0f, 0.75f, -0.5f), Vector3::UnitZ, Vector2::Zero)};
        effect.setWorldProperty(Matrix::getIdentityProperty());
        effect.setAlphaProperty(0.5f);
        effect.DirectionalLight0.setDirectionProperty(Vector3(0.0f, 0.0f, -1.0f));
        device.Clear(Color::Black);
        effect.Apply();
        device.DrawUserPrimitives(
            PrimitiveType::TriangleList, faceOnTriangle.data(), 0, 1);
        ExpectPixel(
            "lit BasicEffect Alpha premultiplies RGB under opaque blending",
            Rectangle(kSize / 2, kSize / 2, 1, 1), Color(128, 128, 128, 128), 4);

        effect.setAlphaProperty(1.0f);
        effect.setFogEnabledProperty(true);
        effect.setFogStartProperty(0.0f);
        effect.setFogEndProperty(1.0f);
        effect.setFogColorProperty(Vector3(0.0f, 0.0f, 1.0f));
        device.Clear(Color::Black);
        effect.Apply();
        device.DrawUserPrimitives(
            PrimitiveType::TriangleList, faceOnTriangle.data(), 0, 1);
        ExpectPixel(
            "lit BasicEffect fog uses the object-space view-Z vector",
            Rectangle(kSize / 2, kSize / 2, 1, 1), Color(128, 128, 255, 255), 4);
    }

private:
    std::unique_ptr<GraphicsDeviceManager> graphics_;
};

int main()
{
    return CNA::Examples::RunPixelTest<RlglBasicEffectLightingTest>();
}
