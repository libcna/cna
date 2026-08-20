// SPDX-License-Identifier: MS-PL
// plans/plan_igl.md IGL-36/IGL-55: SkinnedEffect pixel conformance -- the first bone-uniform exercise
// for this renderer family (72-bone std140 block, IglRenderer::BoneUniforms).
//
// Identity bone palette: every vertex has weight 1.0 on bone 0, and SkinnedEffect's own default
// bone palette is 72 identity matrices, so the mesh renders exactly where it was authored -- this
// isolates "did the bone/weight/index attributes and the bone uniform upload reach the shader at
// all" from "is the actual per-bone matrix math correct" (a separate, harder question this test
// does not attempt). SkinnedEffect always has lighting on (cannot be disabled, matching real
// XNA/FNA), so this checks a directional-light-tinted colour is redder than it is green inside the
// mesh, and the mesh's own footprint against the green clear colour outside it -- not exact
// lit-colour values, mirroring llgl_skinnedeffect_identity_bones_test.cpp's own established
// technique for the same reason.
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
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SkinnedEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionNormalTextureSkinned.hpp"

#include "common/PixelTestGame.hpp"

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kSize = 64;
}

class IglSkinnedEffectTest : public CNA::Examples::PixelTestGame
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

        auto texture = std::make_unique<Texture2D>(device, 1, 1);
        const Color red[1] = {Color(static_cast<bytecs>(255), static_cast<bytecs>(0),
                                    static_cast<bytecs>(0), static_cast<bytecs>(255))};
        texture->SetData(red, 1);

        SkinnedEffect effect(device);
        effect.setTextureProperty(texture.get());
        effect.setWorldProperty(Matrix::getIdentityProperty());
        effect.setViewProperty(Matrix::getIdentityProperty());
        effect.setProjectionProperty(Matrix::getIdentityProperty());
        effect.setWeightsPerVertexProperty(1);
        effect.EnableDefaultLighting();

        // A quad covering the LEFT half only, weight 1.0 on bone 0 for every vertex (identity).
        const Vector3 normal(0.0f, 0.0f, 1.0f);
        const Vector4 boneWeight(1.0f, 0.0f, 0.0f, 0.0f);
        const std::array<std::uint8_t, 4> boneIndices{0, 0, 0, 0};
        const std::vector<VertexPositionNormalTextureSkinned> vertices = {
            VertexPositionNormalTextureSkinned(Vector3(-1.0f, -1.0f, 0.5f), normal,
                                               Vector2(0.0f, 1.0f), boneWeight, boneIndices),
            VertexPositionNormalTextureSkinned(Vector3(0.0f, -1.0f, 0.5f), normal,
                                               Vector2(1.0f, 1.0f), boneWeight, boneIndices),
            VertexPositionNormalTextureSkinned(Vector3(0.0f, 1.0f, 0.5f), normal,
                                               Vector2(1.0f, 0.0f), boneWeight, boneIndices),
            VertexPositionNormalTextureSkinned(Vector3(-1.0f, 1.0f, 0.5f), normal,
                                               Vector2(0.0f, 0.0f), boneWeight, boneIndices),
        };
        const std::uint16_t indices[6] = {0, 1, 2, 0, 2, 3};

        VertexBuffer vertexBuffer(device,
                                  VertexPositionNormalTextureSkinned::getVertexDeclarationStatic(),
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

        const Rectangle leftRegion(kSize / 4, kSize / 2, 1, 1);
        Color leftPixel(0, 0, 0, 0);
        device.GetBackBufferData(&leftRegion, &leftPixel, 0, 1);
        const Rectangle rightRegion(3 * kSize / 4, kSize / 2, 1, 1);
        Color rightPixel(0, 0, 0, 0);
        device.GetBackBufferData(&rightRegion, &rightPixel, 0, 1);

        ExpectTrue("the identity-skinned mesh renders a lit, reddish quad on the left half",
                  leftPixel.getRProperty() > leftPixel.getGProperty() &&
                      leftPixel.getRProperty() > 30);
        ExpectTrue("the right half (outside the mesh) stays the green clear colour",
                  rightPixel.getGProperty() > rightPixel.getRProperty());
    }

public:
    IglSkinnedEffectTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kSize);
        gdm_->setPreferredBackBufferHeightProperty(kSize);
        gdm_->setSynchronizeWithVerticalRetraceProperty(false);
    }
};

int main()
{
    return CNA::Examples::RunPixelTest<IglSkinnedEffectTest>();
}
