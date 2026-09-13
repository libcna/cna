// SPDX-License-Identifier: MS-PL
// plans/plan_webgpu.md WEBGPU-159 (harness: WEBGPU-207): `DualTextureEffect` must consume TEXCOORD0
// and TEXCOORD1 INDEPENDENTLY.
//
// That is the whole point of the effect -- lightmapping puts the base map on the mesh's own UVs and
// the lightmap on a second, separately unwrapped set. A shader that declares one `uv` and samples
// both textures with it cannot express it, and the canonical XNA `PositionNormalDualTexture` vertex
// (stride 40) was refused outright by a stride table that has no entry for 40.
//
// The oracle is `easygl_dualtextureeffect_independent_uv_test`'s (SAMPLE-073), reused rather than
// reinvented, and widened into three columns. FNA's dual-texture formula is
// `saturate(2 * base) * overlay * diffuse`, so a base texel of 128 becomes white and the result is
// exactly the overlay texel -- which makes each column's expected colour a texel value, not a shade:
//
//   0  uv0 -> base.white, uv1 -> overlay.blue   -> blue
//   1  uv0 -> base.white, uv1 -> overlay.green  -> green
//   2  a declaration with NO TEXCOORD1          -> the measured unbound-attribute value
//
// Column 2 is the case the plan asked to MEASURE rather than assume. Both candidate reference
// behaviours agree: D3D9 fills a vertex register's missing components with (0,0,0,1) and OpenGL's
// disabled generic vertex attribute has the same default, so uv1 is (0,0) and the overlay is
// sampled at its first texel -- the same result as column 1. This fixture asserts that equality
// under BOTH renderers, so if either disagrees the run says so instead of the assumption standing.

#include "parity/ParityFixture.hpp"

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/BufferUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/DualTextureEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexElementSize.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
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

    /// XNA's canonical dual-texture vertex: Position, Normal, TEXCOORD0, TEXCOORD1 -- 40 bytes.
    constexpr int kDualStride = 40;
    /// The same mesh with the second UV set simply absent -- 32 bytes.
    constexpr int kSingleStride = 32;

    [[nodiscard]] VertexDeclaration DualUvDeclaration()
    {
        return VertexDeclaration(kDualStride, {
            VertexElement(0, VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
            VertexElement(12, VertexElementFormat::Vector3, VertexElementUsage::Normal, 0),
            VertexElement(24, VertexElementFormat::Vector2,
                          VertexElementUsage::TextureCoordinate, 0),
            VertexElement(32, VertexElementFormat::Vector2,
                          VertexElementUsage::TextureCoordinate, 1),
        });
    }

    [[nodiscard]] VertexDeclaration SingleUvDeclaration()
    {
        return VertexDeclaration(kSingleStride, {
            VertexElement(0, VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
            VertexElement(12, VertexElementFormat::Vector3, VertexElementUsage::Normal, 0),
            VertexElement(24, VertexElementFormat::Vector2,
                          VertexElementUsage::TextureCoordinate, 0),
        });
    }
}

/// WEBGPU-159: the two UV sets must address their own texture.
class DualTextureUv1ParityFixture : public CNA::Parity::ParityFixture
{
public:
    DualTextureUv1ParityFixture() : ParityFixture(kWidth, kHeight) {}

protected:
    void RunFixture() override
    {
        auto& device = getGraphicsDeviceProperty();
        const CNA::Parity::ParityGrid grid{kWidth, kHeight, kColumns, 1};

        // Base: both texels 128, so `saturate(2 * base)` is white and the overlay texel survives
        // unchanged. Keeping the base neutral is what makes each expected colour exact.
        const Color basePixels[2] = {Color(128, 128, 128, 255), Color(128, 128, 128, 255)};
        // Overlay: two texels that share no channel, so sampling the wrong one is unmistakable.
        const Color overlayPixels[2] = {Color(0, 255, 0, 255), Color(0, 0, 255, 255)};
        Texture2D base(device, 2, 1);
        Texture2D overlay(device, 2, 1);
        base.SetData(basePixels, 2);
        overlay.SetData(overlayPixels, 2);

        DualTextureEffect effect(device);
        effect.setWorldProperty(Matrix::getIdentityProperty());
        effect.setViewProperty(Matrix::getIdentityProperty());
        effect.setProjectionProperty(Matrix::getIdentityProperty());
        effect.setTextureProperty(&base);
        effect.setTexture2Property(&overlay);

        device.setBlendStateProperty(BlendState::Opaque);
        device.setRasterizerStateProperty(RasterizerState::CullNone);
        device.SetDepthTestEnabled(false);
        device.getSamplerStatesProperty()[0] = SamplerState::PointClamp;
        device.getSamplerStatesProperty()[1] = SamplerState::PointClamp;
        device.Clear(kClearColor);

        const std::array<std::uint16_t, 6> indexData{0, 1, 2, 0, 2, 3};
        IndexBuffer indices(device, IndexElementSize::SixteenBits,
                            static_cast<int>(indexData.size()), BufferUsage::None);
        indices.SetData(indexData.data(), static_cast<int>(indexData.size()));
        device.setIndicesProperty(&indices);

        const auto drawColumn = [&](int column, const VertexDeclaration& declaration, int stride,
                                    Vector2 uv0, bool writeUv1, Vector2 uv1)
        {
            const auto corners = grid.QuadCorners(column, 0);
            std::vector<std::uint8_t> bytes(
                static_cast<std::size_t>(4) * static_cast<std::size_t>(stride), 0u);
            for (int corner = 0; corner < 4; ++corner)
            {
                std::uint8_t* record = bytes.data() +
                    static_cast<std::size_t>(corner) * static_cast<std::size_t>(stride);
                const Vector3& p = corners[static_cast<std::size_t>(corner)];
                const std::array<float, 3> position{p.X, p.Y, p.Z};
                const std::array<float, 3> normal{0.0f, 0.0f, 1.0f};
                const std::array<float, 2> t0{uv0.X, uv0.Y};
                std::memcpy(record + 0, position.data(), sizeof(position));
                std::memcpy(record + 12, normal.data(), sizeof(normal));
                std::memcpy(record + 24, t0.data(), sizeof(t0));
                if (writeUv1)
                {
                    const std::array<float, 2> t1{uv1.X, uv1.Y};
                    std::memcpy(record + 32, t1.data(), sizeof(t1));
                }
            }
            VertexBuffer vertices(device, declaration, 4, BufferUsage::None);
            vertices.SetDataRaw(bytes.data(), 4, stride);
            device.SetVertexBuffer(&vertices);
            effect.Apply();
            device.DrawIndexedPrimitives(PrimitiveType::TriangleList, 0, 0, 4, 0, 2);
            device.SetVertexBuffer(nullptr);
        };

        const VertexDeclaration dual = DualUvDeclaration();
        const VertexDeclaration single = SingleUvDeclaration();
        // Both columns 0 and 1 use the SAME TEXCOORD0; only TEXCOORD1 differs, so any difference
        // between them is attributable to the second UV set and to nothing else.
        drawColumn(0, dual, kDualStride, Vector2(0.25f, 0.5f), true, Vector2(0.75f, 0.5f));
        drawColumn(1, dual, kDualStride, Vector2(0.25f, 0.5f), true, Vector2(0.25f, 0.5f));
        drawColumn(2, single, kSingleStride, Vector2(0.75f, 0.5f), false, Vector2::Zero);

        const Color kOverlayGreen(0, 255, 0, 255);
        const Color kOverlayBlue(0, 0, 255, 255);
        // 128/255 * 2 = 1.004, clamped to 1.0 on write, so the expected value is the overlay texel
        // exactly; tolerance 2 covers the unorm8 round trip and nothing else.
        ExpectAverage("TEXCOORD1 = 0.75 samples the overlay's second texel",
                      grid.Interior(0, 0), kOverlayBlue, 2);
        ExpectAverage("TEXCOORD1 = 0.25 samples the overlay's first texel",
                      grid.Interior(1, 0), kOverlayGreen, 2);
        ExpectDistinct("TEXCOORD1 alone materially changes the result",
                       grid.Interior(0, 0), grid.Interior(1, 0), /*minDelta=*/200);
        // The measured unbound-TEXCOORD1 value: (0,0), so the overlay is sampled at its first
        // texel. TEXCOORD0 is 0.75 here, so a renderer that reused TEXCOORD0 for the second
        // sampler would produce the OTHER texel and fail this check rather than pass it by luck.
        ExpectAverage("an absent TEXCOORD1 samples the overlay at (0,0)",
                      grid.Interior(2, 0), kOverlayGreen, 2);
        ExpectFlat("the dual-textured interior is flat", grid.Interior(0, 0), /*maxSpread=*/2);
    }
};

CNA_PARITY_FIXTURE_MAIN(DualTextureUv1ParityFixture)
