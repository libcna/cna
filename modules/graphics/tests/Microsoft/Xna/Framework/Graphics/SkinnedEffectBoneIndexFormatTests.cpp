// SPDX-License-Identifier: MS-PL
//
// plans/plan_apple_m4.md AM4-152: BLENDINDICES in a format other than Byte4 still selects its bone.
// Direct3D 9 hands every vertex input to the shader as float, so XNA draws a Short4 BLENDINDICES
// element exactly as a Byte4 one; FX-127's Vector4 case is the EasyGL oracle
// easygl_skinnedeffect_vector4_bone_indices_test. Metal cannot fetch Short4 as float, so it reads
// the element normalized and scales it back -- the arithmetic this test makes visible: index 1
// must select bone 1, not bone 0 (a truncated 0.99997) and not "no bone".

#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

#include "CNA/RendererTestGate.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/SkinnedEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexDeclaration.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElement.hpp"

using Microsoft::Xna::Framework::Color;
using Microsoft::Xna::Framework::Matrix;
using Microsoft::Xna::Framework::Rectangle;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    /// pos(12) + normal(12) + uv(8) + weights Vector4(16) + indices Short4(8) = 56 bytes.
    struct ShortIndexVertex
    {
        float px, py, pz;
        float nx, ny, nz;
        float u, v;
        float w0, w1, w2, w3;
        std::int16_t i0, i1, i2, i3;
    };
    static_assert(sizeof(ShortIndexVertex) == 56, "the layout under test is the 56-byte one");

    /// Draws a full-target quad skinned by bone `index`: bone 0 moves it off the target, bone 1
    /// leaves it in place. Returns the centre pixel -- red when the quad covers it.
    Color DrawWithBoneIndex(GraphicsDevice& device, std::int16_t index)
    {
        constexpr int kSize = 8;
        RenderTarget2D target(device, kSize, kSize, false, SurfaceFormat::Color, DepthFormat::None);
        Texture2D red(device, 1, 1);
        const Color redPixel(255, 0, 0, 255);
        red.SetData(&redPixel, 1);

        const VertexDeclaration declaration(56, {
            VertexElement(0, VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
            VertexElement(12, VertexElementFormat::Vector3, VertexElementUsage::Normal, 0),
            VertexElement(24, VertexElementFormat::Vector2, VertexElementUsage::TextureCoordinate, 0),
            VertexElement(32, VertexElementFormat::Vector4, VertexElementUsage::BlendWeight, 0),
            VertexElement(48, VertexElementFormat::Short4, VertexElementUsage::BlendIndices, 0)});
        const auto vertex = [index](float x, float y) {
            return ShortIndexVertex{x, y, 0.5f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f,
                                    1.0f, 0.0f, 0.0f, 0.0f, index, index, index, index};
        };
        const std::vector<ShortIndexVertex> quad{
            vertex(-1, 1), vertex(1, 1), vertex(-1, -1),
            vertex(1, 1), vertex(1, -1), vertex(-1, -1)};
        VertexBuffer buffer(device, declaration, static_cast<int>(quad.size()), BufferUsage::None);
        buffer.SetData(quad.data(), static_cast<int>(quad.size()));

        SkinnedEffect effect(device);
        effect.setTextureProperty(&red);
        effect.setWorldProperty(Matrix::getIdentityProperty());
        effect.setViewProperty(Matrix::getIdentityProperty());
        effect.setProjectionProperty(Matrix::getIdentityProperty());
        effect.SetBoneTransforms({Matrix::CreateTranslation(10.0f, 0.0f, 0.0f),
                                  Matrix::getIdentityProperty()});
        effect.setWeightsPerVertexProperty(1);
        effect.setAmbientLightColorProperty({1.0f, 1.0f, 1.0f});

        device.SetRenderTarget(&target);
        device.Clear(Color(0, 255, 0, 255));
        device.setBlendStateProperty(BlendState::Opaque);
        device.setDepthStencilStateProperty(DepthStencilState::None);
        device.setRasterizerStateProperty(RasterizerState::CullNone);
        effect.Apply();
        device.SetVertexBuffer(&buffer);
        device.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
        device.SetVertexBuffer(nullptr);
        device.SetRenderTarget(nullptr);

        Color centre(0, 0, 0, 0);
        const Rectangle pixel(kSize / 2, kSize / 2, 1, 1);
        target.GetData(0, &pixel, &centre, 0, 1);
        return centre;
    }
}

TEST(SkinnedEffectBoneIndexFormatTest, Short4BlendIndicesSelectTheBoneTheyName)
{
    using namespace CNA::Testing::Renderers;   // NOLINT(google-build-using-namespace)
    CNA_SKIP_IF_RENDERER_IS_NONE_OF(Metal);

    GraphicsDevice device;
    const Color selected = DrawWithBoneIndex(device, 1);
    EXPECT_GT(selected.getRProperty(), selected.getGProperty() + 40)
        << "index 1 must select bone 1, which leaves the quad over the centre";
    const Color moved = DrawWithBoneIndex(device, 0);
    EXPECT_GT(moved.getGProperty(), moved.getRProperty() + 40)
        << "index 0 selects bone 0, which moves the quad off the target";
}
