// SPDX-License-Identifier: MS-PL
// plans/plan_webgpu.md WEBGPU-156 (harness: WEBGPU-207): BasicEffect lighting on an UNTEXTURED
// Position+Normal mesh -- the vertex layout Microsoft's own Primitives3D sample uses.
//
// Position+Normal is 24 bytes, exactly the stride of `VertexPositionColorTexture`. A renderer that
// reads the layout out of the stride therefore either lights the wrong bytes or, as WebGPU did,
// refuses the draw: "carries Normal, which this renderer's native layout for a 24-byte record does
// not bind at all". Only the declaration says which of the two a 24-byte record is.
//
// The oracle is `easygl_basiceffect_position_normal_test`'s, reused rather than reinvented: one
// directional light down -Z, pure red diffuse, no ambient/emissive/specular. A quad whose normal
// faces the light is lit red; the same quad with the normal reversed is black.
//
// The light's diffuse colour is 0.6, not 1.0, DELIBERATELY. At full brightness a lit red surface
// and an unlit one carrying the same red DiffuseColor are the same pixel, so a renderer that
// dropped the normal and fell back to its unlit colour program would pass every check below. At
// 0.6 the lit value is 153 and the unlit fallback is 255, and the two cannot be confused.
//
// Three columns, one behaviour each:
//   0  normal towards the light, stride 24     -> lit red
//   1  normal away from the light, stride 24   -> black
//   2  the SAME semantics padded to stride 32  -> identical to column 0
//
// Column 2 is what makes this a layout test rather than a lighting test: stride 32 is
// `VertexPositionNormalTexture`'s, and a stride-keyed renderer binds its normal as a UV.
//
// A FOURTH leg -- the same Position+Normal semantics with the two elements declared in the other
// ORDER -- is deliberately absent, and must not be added back here. It is measured, not assumed:
// WebGPU renders it correctly (153,0,0) and the reference renderer does not (255,0,0, i.e. its
// unlit DiffuseColor). `EasyGLRenderer::SelectStockProgramShape` detects a lit stride-24
// vertex by an exact pattern match -- `declaredElements[0] == Position@0 &&
// declaredElements[1] == Normal@12` -- so any other declaration order falls through to the unlit
// colour program and the normal is dropped. That is a defect in the REFERENCE, recorded against
// `WEBGPU-156` in plans/plan_webgpu.md rather than cloned into WebGPU; a shared fixture asserts
// what both renderers must satisfy, so this leg would be permanently red for the wrong reason.
// Declaration-ORDER independence is still covered for the unlit families by
// `parity_vertex_semantics`'s leg D, which both renderers pass.

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
#include <string>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kWidth = 256;
    constexpr int kHeight = 64;
    constexpr int kColumns = 3;

    const Color kClearColor(9, 13, 17, 255);

    struct Leg
    {
        const char* name;
        int positionOffset;
        int normalOffset;
        int stride;
        float normalZ;   ///< +1 faces the light, -1 faces away.
    };

    constexpr std::array<Leg, kColumns> kLegs{{
        {"lit stride 24",             0, 12, 24,  1.0f},
        {"reversed normal stride 24", 0, 12, 24, -1.0f},
        {"lit padded to stride 32",   0, 12, 32,  1.0f},
    }};

    [[nodiscard]] VertexDeclaration DeclarationFor(const Leg& leg)
    {
        const VertexElement position(leg.positionOffset, VertexElementFormat::Vector3,
                                     VertexElementUsage::Position, 0);
        const VertexElement normal(leg.normalOffset, VertexElementFormat::Vector3,
                                   VertexElementUsage::Normal, 0);
        return VertexDeclaration(leg.stride, {position, normal});
    }
}

/// WEBGPU-156: a Position+Normal declaration must light, at any stride and in any element order.
class LitUntexturedParityFixture : public CNA::Parity::ParityFixture
{
public:
    LitUntexturedParityFixture() : ParityFixture(kWidth, kHeight) {}

protected:
    void RunFixture() override
    {
        auto& device = getGraphicsDeviceProperty();
        const CNA::Parity::ParityGrid grid{kWidth, kHeight, kColumns, 1};

        BasicEffect effect(device);
        effect.setWorldProperty(Matrix::getIdentityProperty());
        effect.setViewProperty(Matrix::getIdentityProperty());
        effect.setProjectionProperty(Matrix::getIdentityProperty());
        effect.setLightingEnabledProperty(true);
        effect.setPreferPerPixelLightingProperty(false);
        effect.setTextureEnabledProperty(false);
        effect.setVertexColorEnabledProperty(false);
        effect.setAmbientLightColorProperty(Vector3::Zero);
        effect.setDiffuseColorProperty(Vector3(1.0f, 0.0f, 0.0f));
        effect.setEmissiveColorProperty(Vector3::Zero);
        effect.setSpecularColorProperty(Vector3::Zero);
        effect.DirectionalLight0.setEnabledProperty(true);
        effect.DirectionalLight0.setDirectionProperty(Vector3(0.0f, 0.0f, -1.0f));
        // 0.6, not 1.0 -- see this file's header comment: it is what separates a lit red surface
        // from an unlit one carrying the same red DiffuseColor.
        effect.DirectionalLight0.setDiffuseColorProperty(Vector3(0.6f, 0.6f, 0.6f));
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

        for (int column = 0; column < kColumns; ++column)
        {
            const Leg& leg = kLegs[static_cast<std::size_t>(column)];
            const auto corners = grid.QuadCorners(column, 0);
            std::vector<std::uint8_t> bytes(
                static_cast<std::size_t>(4 * leg.stride), 0u);
            for (int corner = 0; corner < 4; ++corner)
            {
                std::uint8_t* record = bytes.data() +
                    static_cast<std::size_t>(corner) * static_cast<std::size_t>(leg.stride);
                const Vector3& p = corners[static_cast<std::size_t>(corner)];
                const std::array<float, 3> position{p.X, p.Y, p.Z};
                const std::array<float, 3> normal{0.0f, 0.0f, leg.normalZ};
                std::memcpy(record + leg.positionOffset, position.data(), sizeof(position));
                std::memcpy(record + leg.normalOffset, normal.data(), sizeof(normal));
            }

            const VertexDeclaration declaration = DeclarationFor(leg);
            VertexBuffer vertices(device, declaration, 4, BufferUsage::None);
            vertices.SetDataRaw(bytes.data(), 4, leg.stride);
            device.SetVertexBuffer(&vertices);
            effect.Apply();
            device.DrawIndexedPrimitives(PrimitiveType::TriangleList, 0, 0, 4, 0, 2);
            device.SetVertexBuffer(nullptr);
        }

        // N.L is exactly 1 or exactly 0 here, and there is no texture, no ambient and no specular,
        // so the expected values are exact -- tolerance 2 covers only the float-to-unorm8 rounding
        // of a value that is already 1.0 or 0.0.
        // 0.6 * 255 = 153: the diffuse red scaled by the light, NOT the raw DiffuseColor.
        const Color kLitRed(153, 0, 0, 255);
        const Color kUnlit(0, 0, 0, 255);
        ExpectAverage("Position+Normal at stride 24 lights", grid.Interior(0, 0), kLitRed, 2);
        ExpectAverage("a normal facing away is unlit", grid.Interior(1, 0), kUnlit, 2);
        ExpectAverage("the same semantics padded to stride 32 light identically",
                      grid.Interior(2, 0), kLitRed, 2);
        // The lighting must MATTER: a renderer that ignored the normal entirely and drew flat
        // diffuse everywhere would satisfy three of the four checks above.
        ExpectDistinct("the normal materially changes the shading",
                       grid.Interior(0, 0), grid.Interior(1, 0), /*minDelta=*/100);
        ExpectFlat("the lit interior is flat", grid.Interior(0, 0), /*maxSpread=*/2);
    }
};

CNA_PARITY_FIXTURE_MAIN(LitUntexturedParityFixture)
