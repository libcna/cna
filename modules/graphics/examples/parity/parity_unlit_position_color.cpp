// SPDX-License-Identifier: MS-PL
// plans/plan_webgpu.md WEBGPU-158 (harness: WEBGPU-207): a vertex that declares NO Normal renders
// unlit and keeps its declared colour -- whatever its stride happens to be.
//
// This is REMED-GFX-234's shape. Stride 32 is `VertexPositionNormalTexture`'s, so a renderer that
// reads the layout out of the stride binds a lit program to a Position+Colour record padded to 32
// bytes: the declared colour has no input to bind to and is silently dropped, and twelve bytes of
// padding are lit as if they were a normal. The reference renderer asks the declaration instead
// (`DeclarationNamesUsage(..., Normal)`), and so must this one.
//
// Both halves of the statement are in one fixture on purpose -- "no normal means unlit" is only
// half a rule, and a renderer that answered it by never lighting anything would pass alone:
//   0  Position+Colour padded to stride 32, lighting ENABLED -> unlit, the declared colour
//   1  a genuine VertexPositionNormalTexture, same stride    -> still lit

#include "parity/ParityFixture.hpp"

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/BufferUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexElementSize.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexDeclaration.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElement.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementUsage.hpp"

#include <array>
#include <cstdint>
#include <cstring>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kWidth = 128;
    constexpr int kHeight = 64;
    constexpr int kColumns = 2;
    constexpr int kStride = 32;

    const Color kClearColor(9, 13, 17, 255);
    /// Distinct in all three channels, and nowhere near either a lit or an unlit grey.
    const Color kDeclaredColor(40, 200, 120, 255);

    /// Position at 0, Colour at 12, twelve bytes of padding -- 32 bytes, and no Normal anywhere.
    [[nodiscard]] VertexDeclaration PaddedPositionColorDeclaration()
    {
        return VertexDeclaration(kStride, {
            VertexElement(0, VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
            VertexElement(12, VertexElementFormat::Color, VertexElementUsage::Color, 0),
        });
    }

    /// The real VertexPositionNormalTexture, at the same 32 bytes.
    [[nodiscard]] VertexDeclaration PositionNormalTextureDeclaration()
    {
        return VertexDeclaration(kStride, {
            VertexElement(0, VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
            VertexElement(12, VertexElementFormat::Vector3, VertexElementUsage::Normal, 0),
            VertexElement(24, VertexElementFormat::Vector2,
                          VertexElementUsage::TextureCoordinate, 0),
        });
    }
}

/// WEBGPU-158: stride 32 does not mean "lit"; the declaration does.
class UnlitPositionColorParityFixture : public CNA::Parity::ParityFixture
{
public:
    UnlitPositionColorParityFixture() : ParityFixture(kWidth, kHeight) {}

protected:
    void RunFixture() override
    {
        auto& device = getGraphicsDeviceProperty();
        const CNA::Parity::ParityGrid grid{kWidth, kHeight, kColumns, 1};

        BasicEffect effect(device);
        effect.setWorldProperty(Matrix::getIdentityProperty());
        effect.setViewProperty(Matrix::getIdentityProperty());
        effect.setProjectionProperty(Matrix::getIdentityProperty());
        // Lighting stays ON for BOTH columns: the point is that the DECLARATION, not the effect
        // state and not the stride, decides whether a vertex can be lit at all.
        effect.setLightingEnabledProperty(true);
        effect.setPreferPerPixelLightingProperty(false);
        effect.setTextureEnabledProperty(false);
        effect.setVertexColorEnabledProperty(true);
        effect.setAmbientLightColorProperty(Vector3::Zero);
        effect.setDiffuseColorProperty(Vector3::One);
        effect.setEmissiveColorProperty(Vector3::Zero);
        effect.setSpecularColorProperty(Vector3::Zero);
        effect.DirectionalLight0.setEnabledProperty(true);
        // Facing AWAY from the geometry: a lit draw is black, an unlit one keeps its colour, so
        // the two columns cannot be confused for one another.
        effect.DirectionalLight0.setDirectionProperty(Vector3(0.0f, 0.0f, 1.0f));
        effect.DirectionalLight0.setDiffuseColorProperty(Vector3::One);
        effect.DirectionalLight0.setSpecularColorProperty(Vector3::Zero);
        effect.DirectionalLight1.setEnabledProperty(false);
        effect.DirectionalLight2.setEnabledProperty(false);

        device.setBlendStateProperty(BlendState::Opaque);
        device.setRasterizerStateProperty(RasterizerState::CullNone);
        device.SetDepthTestEnabled(false);
        device.Clear(kClearColor);

        const std::array<std::uint16_t, 6> indexData{0, 1, 2, 0, 2, 3};
        IndexBuffer indices(device, IndexElementSize::SixteenBits,
                            static_cast<int>(indexData.size()), BufferUsage::None);
        indices.SetData(indexData.data(), static_cast<int>(indexData.size()));
        device.setIndicesProperty(&indices);

        // Column 0: Position + Colour, padded to 32 bytes.
        {
            const auto corners = grid.QuadCorners(0, 0);
            std::vector<std::uint8_t> bytes(static_cast<std::size_t>(4 * kStride), 0u);
            for (int corner = 0; corner < 4; ++corner)
            {
                std::uint8_t* record = bytes.data() +
                    static_cast<std::size_t>(corner) * static_cast<std::size_t>(kStride);
                const Vector3& p = corners[static_cast<std::size_t>(corner)];
                const std::array<float, 3> position{p.X, p.Y, p.Z};
                const std::uint32_t packed = kDeclaredColor.getPackedValueProperty();
                std::memcpy(record + 0, position.data(), sizeof(position));
                std::memcpy(record + 12, &packed, sizeof(packed));
            }
            const VertexDeclaration declaration = PaddedPositionColorDeclaration();
            VertexBuffer vertices(device, declaration, 4, BufferUsage::None);
            vertices.SetDataRaw(bytes.data(), 4, kStride);
            device.SetVertexBuffer(&vertices);
            effect.Apply();
            device.DrawIndexedPrimitives(PrimitiveType::TriangleList, 0, 0, 4, 0, 2);
            device.SetVertexBuffer(nullptr);
        }

        // Column 1: the real lit vertex, at the same stride. Its normal faces the camera, and the
        // light points away from it, so a correctly lit draw is black.
        {
            const auto corners = grid.QuadCorners(1, 0);
            std::vector<std::uint8_t> bytes(static_cast<std::size_t>(4 * kStride), 0u);
            for (int corner = 0; corner < 4; ++corner)
            {
                std::uint8_t* record = bytes.data() +
                    static_cast<std::size_t>(corner) * static_cast<std::size_t>(kStride);
                const Vector3& p = corners[static_cast<std::size_t>(corner)];
                const std::array<float, 3> position{p.X, p.Y, p.Z};
                const std::array<float, 3> normal{0.0f, 0.0f, 1.0f};
                const std::array<float, 2> uv{0.0f, 0.0f};
                std::memcpy(record + 0, position.data(), sizeof(position));
                std::memcpy(record + 12, normal.data(), sizeof(normal));
                std::memcpy(record + 24, uv.data(), sizeof(uv));
            }
            const VertexDeclaration declaration = PositionNormalTextureDeclaration();
            VertexBuffer vertices(device, declaration, 4, BufferUsage::None);
            vertices.SetDataRaw(bytes.data(), 4, kStride);
            device.SetVertexBuffer(&vertices);
            effect.Apply();
            device.DrawIndexedPrimitives(PrimitiveType::TriangleList, 0, 0, 4, 0, 2);
            device.SetVertexBuffer(nullptr);
        }

        ExpectAverage("a stride-32 declaration with no Normal keeps its declared colour",
                      grid.Interior(0, 0), kDeclaredColor, 2);
        ExpectAverage("a genuine VertexPositionNormalTexture at the same stride still lights",
                      grid.Interior(1, 0), Color(0, 0, 0, 255), 2);
        ExpectDistinct("the two stride-32 declarations render differently",
                       grid.Interior(0, 0), grid.Interior(1, 0), /*minDelta=*/100);
        ExpectFlat("the unlit coloured interior is flat", grid.Interior(0, 0), /*maxSpread=*/2);
    }
};

CNA_PARITY_FIXTURE_MAIN(UnlitPositionColorParityFixture)
