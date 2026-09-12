// SPDX-License-Identifier: MS-PL
// plans/plan_rlgl.md RLGL-037: renderer-local four-weight, uniform-budget,
// null-texture, declaration-diagnostic, and stock-transition coverage.

#include "common/PixelTestGame.hpp"

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Vector4.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/BufferUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SkinnedEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexDeclaration.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElement.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"
#include "System/NotSupportedException.hpp"

#include "RlglBridge.hpp"

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kWidth = 64;
    constexpr int kHeight = 48;

    struct SkinnedVertex
    {
        float px, py, pz;
        float nx, ny, nz;
        float u, v;
        float w0, w1, w2, w3;
        std::uint8_t i0, i1, i2, i3;
    };

    static_assert(sizeof(SkinnedVertex) == 52);

    struct MissingIndicesVertex
    {
        Vector3 position;
        Vector3 normal;
        Vector2 textureCoordinate;
        Vector4 blendWeight;
    };

    static_assert(sizeof(MissingIndicesVertex) == 48);

    [[nodiscard]] std::array<SkinnedVertex, 6> FourWeightQuad()
    {
        const auto vertex = [](const float x, const float y)
        {
            return SkinnedVertex{
                x, y, 0.0f, 0.0f, 0.0f, -1.0f, 0.0f, 0.0f,
                0.25f, 0.25f, 0.25f, 0.25f, 0, 1, 2, 3};
        };
        return {{
            vertex(-1.0f, 0.5f), vertex(-1.0f, -0.5f), vertex(-0.5f, -0.5f),
            vertex(-1.0f, 0.5f), vertex(-0.5f, -0.5f), vertex(-0.5f, 0.5f),
        }};
    }
}

class RlglSkinnedEffectTest final : public CNA::Examples::PixelTestGame
{
public:
    RlglSkinnedEffectTest()
    {
        graphics_ = std::make_unique<GraphicsDeviceManager>(this);
        graphics_->setGraphicsProfileProperty(GraphicsProfile::HiDef);
        graphics_->setPreferredBackBufferWidthProperty(kWidth);
        graphics_->setPreferredBackBufferHeightProperty(kHeight);
        graphics_->setPreferredPresentationModeProperty(PresentationMode::NativeBackBuffer);
        graphics_->setSynchronizeWithVerticalRetraceProperty(false);
    }

protected:
    void RunTest() override
    {
        namespace Bridge = CNA::Internal::Renderers::Rlgl::Bridge;

        auto& device = getGraphicsDeviceProperty();
        device.setBlendStateProperty(BlendState::Opaque);
        device.setDepthStencilStateProperty(DepthStencilState::None);
        device.setRasterizerStateProperty(RasterizerState::CullNone);
        const Rectangle center(kWidth / 2, kHeight / 2, 1, 1);

        Check(
            Bridge::GetMaxVertexUniformComponentsForTesting() >= 1024,
            "the live context provides the GL 3.3 minimum vertex-uniform budget");

        SkinnedEffect skinned(device);
        skinned.setWorldProperty(Matrix::getIdentityProperty());
        skinned.setViewProperty(Matrix::getIdentityProperty());
        skinned.setProjectionProperty(Matrix::getIdentityProperty());
        skinned.setTextureProperty(nullptr);
        skinned.setDiffuseColorProperty(Vector3(1.0f, 0.0f, 0.0f));
        skinned.setAmbientLightColorProperty(Vector3::One);
        skinned.setEmissiveColorProperty(Vector3::Zero);
        skinned.setSpecularColorProperty(Vector3::Zero);
        skinned.DirectionalLight0.setEnabledProperty(false);
        skinned.DirectionalLight1.setEnabledProperty(false);
        skinned.DirectionalLight2.setEnabledProperty(false);
        skinned.SetBoneTransforms({
            Matrix::getIdentityProperty(),
            Matrix::CreateTranslation(0.5f, 0.0f, 0.0f),
            Matrix::CreateTranslation(1.0f, 0.0f, 0.0f),
            Matrix::CreateTranslation(1.5f, 0.0f, 0.0f),
        });
        skinned.setWeightsPerVertexProperty(4);

        const auto quad = FourWeightQuad();
        VertexBuffer vertexBuffer(device, 6);
        vertexBuffer.SetDataRaw(
            quad.data(), static_cast<int>(quad.size()), sizeof(SkinnedVertex));
        device.SetVertexBuffer(&vertexBuffer);
        device.Clear(Color::Black);
        skinned.Apply();
        device.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
        ExpectPixel(
            "four weighted bones sum to the expected translated position",
            center, Color::Red);
        Check(
            Bridge::GetPrimitiveDrawSnapshotForTesting().skinned,
            "the native primitive path records a skinned submission");

        const VertexDeclaration missingIndicesDeclaration(
            48,
            {
                VertexElement(0, VertexElementFormat::Vector3,
                              VertexElementUsage::Position, 0),
                VertexElement(12, VertexElementFormat::Vector3,
                              VertexElementUsage::Normal, 0),
                VertexElement(24, VertexElementFormat::Vector2,
                              VertexElementUsage::TextureCoordinate, 0),
                VertexElement(32, VertexElementFormat::Vector4,
                              VertexElementUsage::BlendWeight, 0),
            });
        const std::array<MissingIndicesVertex, 3> malformed{
            MissingIndicesVertex{Vector3(-0.5f, -0.5f, 0.0f), Vector3::UnitZ,
                                 Vector2::Zero, Vector4(1.0f, 0.0f, 0.0f, 0.0f)},
            MissingIndicesVertex{Vector3(0.5f, -0.5f, 0.0f), Vector3::UnitZ,
                                 Vector2::Zero, Vector4(1.0f, 0.0f, 0.0f, 0.0f)},
            MissingIndicesVertex{Vector3(0.0f, 0.5f, 0.0f), Vector3::UnitZ,
                                 Vector2::Zero, Vector4(1.0f, 0.0f, 0.0f, 0.0f)},
        };
        VertexBuffer missingIndicesBuffer(
            device, missingIndicesDeclaration,
            static_cast<int>(malformed.size()), BufferUsage::None);
        missingIndicesBuffer.SetData(
            malformed.data(), static_cast<int>(malformed.size()));
        device.SetVertexBuffer(&missingIndicesBuffer);
        bool missingIndicesRejected = false;
        skinned.Apply();
        try
        {
            device.DrawPrimitives(PrimitiveType::TriangleList, 0, 1);
        }
        catch (const System::NotSupportedException&)
        {
            missingIndicesRejected = true;
        }
        Check(
            missingIndicesRejected,
            "SkinnedEffect rejects a declaration without BlendIndices0");

        BasicEffect basic(device);
        basic.setWorldProperty(Matrix::getIdentityProperty());
        basic.setViewProperty(Matrix::getIdentityProperty());
        basic.setProjectionProperty(Matrix::getIdentityProperty());
        basic.setTextureEnabledProperty(false);
        basic.setVertexColorEnabledProperty(true);
        const std::array<VertexPositionColor, 3> greenTriangle{
            VertexPositionColor(Vector3(-0.75f, -0.75f, 0.0f), Color(0, 255, 0, 255)),
            VertexPositionColor(Vector3(0.75f, -0.75f, 0.0f), Color(0, 255, 0, 255)),
            VertexPositionColor(Vector3(0.0f, 0.75f, 0.0f), Color(0, 255, 0, 255)),
        };
        device.Clear(Color::Black);
        basic.Apply();
        device.DrawUserPrimitives(
            PrimitiveType::TriangleList, greenTriangle.data(), 0, 1);
        ExpectPixel(
            "transition to BasicEffect clears the skinning shader state",
            center, Color(0, 255, 0, 255));
        Check(
            !Bridge::GetPrimitiveDrawSnapshotForTesting().skinned,
            "the following native primitive submission is not marked skinned");
    }

private:
    std::unique_ptr<GraphicsDeviceManager> graphics_;
};

int main()
{
    return CNA::Examples::RunPixelTest<RlglSkinnedEffectTest>();
}
