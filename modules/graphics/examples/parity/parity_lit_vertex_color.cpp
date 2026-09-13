// SPDX-License-Identifier: MS-PL
// plans/plan_webgpu.md WEBGPU-157 (harness: WEBGPU-207): the vertex the stock XNA `ModelProcessor`
// actually emits for a mesh with a colour channel -- Position + Normal + Color + TextureCoordinate,
// 36 bytes -- must render LIT and keep its colour.
//
// 36 is in no renderer's canonical stride table, so a stride-keyed dispatch matches nothing and the
// draw falls through to whatever the fallback is: on EasyGL, before `plans/plan_fx.md` FX-125, that
// was the unlit colour program, and SAMPLE-047's shaded sphere rendered as a flat green disc; on
// WebGPU it was an outright refusal. XNA's own answer is `BasicEffect`'s "Vc" family
// (`VSBasicVertexLightingVc` and friends), which multiplies the lit diffuse by the vertex colour.
//
// Three columns:
//   0  lit, VertexColorEnabled = true   -> lit diffuse TIMES the vertex colour
//   1  lit, VertexColorEnabled = false  -> lit diffuse only, so the colour must NOT show
//   2  normal reversed, colour enabled  -> unlit, so the colour must not show either
//
// Column 1 is the discriminating A/B: without it, a renderer that ignored `VertexColorEnabled` and
// always multiplied would pass column 0.
//
// The light's diffuse colour is 0.6, not 1.0, for the same reason `parity_lit_untextured` uses 0.6:
// at full brightness a lit white surface and an unlit one are the same pixel, so a renderer that
// dropped the lighting entirely would pass column 1.

#include "parity/ParityFixture.hpp"

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
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
    constexpr int kWidth = 192;
    constexpr int kHeight = 64;
    constexpr int kColumns = 3;

    const Color kClearColor(9, 13, 17, 255);

    /// The stock ModelProcessor's own record: Position, Normal, Color, TextureCoordinate.
    constexpr int kStride = 36;
    constexpr int kPositionOffset = 0;
    constexpr int kNormalOffset = 12;
    constexpr int kColorOffset = 24;
    constexpr int kTexCoordOffset = 28;

    /// Half brightness in green and blue, full in red: an ignored tint and an applied one differ in
    /// every channel, and none of the three values is 0 or 255 by accident.
    const Color kVertexColor(255, 128, 64, 255);

    [[nodiscard]] VertexDeclaration ModelProcessorDeclaration()
    {
        return VertexDeclaration(kStride, {
            VertexElement(kPositionOffset, VertexElementFormat::Vector3,
                          VertexElementUsage::Position, 0),
            VertexElement(kNormalOffset, VertexElementFormat::Vector3,
                          VertexElementUsage::Normal, 0),
            VertexElement(kColorOffset, VertexElementFormat::Color,
                          VertexElementUsage::Color, 0),
            VertexElement(kTexCoordOffset, VertexElementFormat::Vector2,
                          VertexElementUsage::TextureCoordinate, 0),
        });
    }
}

/// WEBGPU-157: a stride-36 lit + vertex-coloured mesh renders lit, and its colour is gated.
class LitVertexColorParityFixture : public CNA::Parity::ParityFixture
{
public:
    LitVertexColorParityFixture() : ParityFixture(kWidth, kHeight) {}

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
        effect.setAmbientLightColorProperty(Vector3::Zero);
        effect.setDiffuseColorProperty(Vector3::One);
        effect.setEmissiveColorProperty(Vector3::Zero);
        effect.setSpecularColorProperty(Vector3::Zero);
        effect.DirectionalLight0.setEnabledProperty(true);
        effect.DirectionalLight0.setDirectionProperty(Vector3(0.0f, 0.0f, -1.0f));
        // 0.6, not 1.0 -- see this file's header comment.
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

        const VertexDeclaration declaration = ModelProcessorDeclaration();

        const auto drawColumn = [&](int column, float normalZ, bool vertexColorEnabled)
        {
            const auto corners = grid.QuadCorners(column, 0);
            std::vector<std::uint8_t> bytes(static_cast<std::size_t>(4 * kStride), 0u);
            for (int corner = 0; corner < 4; ++corner)
            {
                std::uint8_t* record = bytes.data() +
                    static_cast<std::size_t>(corner) * static_cast<std::size_t>(kStride);
                const Vector3& p = corners[static_cast<std::size_t>(corner)];
                const std::array<float, 3> position{p.X, p.Y, p.Z};
                const std::array<float, 3> normal{0.0f, 0.0f, normalZ};
                const std::uint32_t packed = kVertexColor.getPackedValueProperty();
                const std::array<float, 2> uv{0.0f, 0.0f};
                std::memcpy(record + kPositionOffset, position.data(), sizeof(position));
                std::memcpy(record + kNormalOffset, normal.data(), sizeof(normal));
                std::memcpy(record + kColorOffset, &packed, sizeof(packed));
                std::memcpy(record + kTexCoordOffset, uv.data(), sizeof(uv));
            }
            VertexBuffer vertices(device, declaration, 4, BufferUsage::None);
            vertices.SetDataRaw(bytes.data(), 4, kStride);
            effect.setVertexColorEnabledProperty(vertexColorEnabled);
            device.SetVertexBuffer(&vertices);
            effect.Apply();
            device.DrawIndexedPrimitives(PrimitiveType::TriangleList, 0, 0, 4, 0, 2);
            device.SetVertexBuffer(nullptr);
        };

        drawColumn(0,  1.0f, true);
        drawColumn(1,  1.0f, false);
        drawColumn(2, -1.0f, true);

        // N.L is exactly 1 with no ambient/specular, so a lit pixel is 0.6 * white * tint. Every
        // expected value below is that product rounded to unorm8; tolerance 2 covers the rounding
        // and nothing else.
        //   0.6 * 255      = 153      0.6 * 128 = 76.8 -> 77      0.6 * 64 = 38.4 -> 38
        ExpectAverage("stride-36 lit vertex colour reaches the shader",
                      grid.Interior(0, 0), Color(153, 77, 38, 255), 2);
        ExpectAverage("VertexColorEnabled=false renders the lit diffuse alone",
                      grid.Interior(1, 0), Color(153, 153, 153, 255), 2);
        ExpectAverage("a normal facing away is unlit whatever the colour says",
                      grid.Interior(2, 0), Color(0, 0, 0, 255), 2);
        // Both halves have to matter: the colour must change the result, and so must the normal.
        ExpectDistinct("VertexColorEnabled materially changes the result",
                       grid.Interior(0, 0), grid.Interior(1, 0), /*minDelta=*/100);
        // 153 is the whole lit range now that the light's diffuse is 0.6, so anything above ~120
        // separates "lit" from "unlit" without demanding a brightness the scene never produces.
        ExpectDistinct("the normal still drives the lighting on a coloured vertex",
                       grid.Interior(0, 0), grid.Interior(2, 0), /*minDelta=*/120);
        ExpectFlat("the lit coloured interior is flat", grid.Interior(0, 0), /*maxSpread=*/2);
    }
};

CNA_PARITY_FIXTURE_MAIN(LitVertexColorParityFixture)
