// SPDX-License-Identifier: MS-PL
// plan_igl.md IGL-36/IGL-55: SkinnedEffect with a real, non-identity bone transform.
//
// `igl_skinnedeffect_test.cpp` only proved the bone/weight/index attributes and the 72-bone
// uniform block reach the shader at all, using SkinnedEffect's own default identity palette (so
// the mesh renders exactly where it was authored, regardless of whether the actual per-bone
// matrix multiply in the shader is correct). This test closes that gap: bone 0 is set to a real
// translation (`Matrix::CreateTranslation(0.5, 0, 0)`), matching
// `bgfx_skinnedeffect_translation_bone_test.cpp`'s own derivation. A quad authored on the LEFT
// half ([-1, 0] in local X, weight 1.0 on bone 0 for every vertex) must render shifted right by
// 0.5 NDC units -- centred on screen, not on the left where it was authored -- which is only true
// if `cnaBones.uBones[0]` genuinely transforms the vertex position rather than being ignored or
// applied as an identity regardless of its uploaded value.
//
// Three checks: the ORIGINAL left-quarter position (where an un-skinned/identity-skinned quad
// would show) stays the green clear colour, since the real quad moved away from there; the centre
// (where the shifted quad now sits) is lit/reddish; the far-right quarter (past the shifted
// quad's new right edge at NDC +0.5) stays green too, ruling out "the mesh just got bigger"
// rather than genuinely translated.
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

class IglSkinnedEffectTranslationBoneTest : public CNA::Examples::PixelTestGame
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

        const std::vector<Matrix> bones = {Matrix::CreateTranslation(0.5f, 0.0f, 0.0f)};
        effect.SetBoneTransforms(bones);

        // A quad authored on the LEFT half only; bone 0's translation shifts it to the centre.
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

        const auto readPixel = [&](const int x) {
            const Rectangle region(x, kSize / 2, 1, 1);
            Color pixel(0, 0, 0, 0);
            device.GetBackBufferData(&region, &pixel, 0, 1);
            return pixel;
        };

        const Color originalPosition = readPixel(kSize / 8);
        const Color shiftedPosition = readPixel(kSize / 2);
        const Color farRight = readPixel(7 * kSize / 8);

        ExpectTrue("the quad's ORIGINAL (un-translated) position stays the green clear colour",
                  originalPosition.getGProperty() > originalPosition.getRProperty());
        ExpectTrue("the bone-translated quad renders lit and reddish at its NEW position",
                  shiftedPosition.getRProperty() > shiftedPosition.getGProperty() &&
                      shiftedPosition.getRProperty() > 30);
        ExpectTrue("past the shifted quad's new right edge stays the green clear colour",
                  farRight.getGProperty() > farRight.getRProperty());
    }

public:
    IglSkinnedEffectTranslationBoneTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kSize);
        gdm_->setPreferredBackBufferHeightProperty(kSize);
        gdm_->setSynchronizeWithVerticalRetraceProperty(false);
    }
};

int main()
{
    return CNA::Examples::RunPixelTest<IglSkinnedEffectTranslationBoneTest>();
}
