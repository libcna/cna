// SPDX-License-Identifier: MS-PL
// plans/plan_webgpu.md WEBGPU-205 (harness: WEBGPU-207): `SamplerState.MipMapLevelOfDetailBias`
// must change which mip level a sample comes from.
//
// `WGPUSamplerDescriptor` has NO `lodBias` field -- checked field by field in the pin: it carries
// addressModeU/V/W, magFilter, minFilter, mipmapFilter, lodMinClamp, lodMaxClamp, compare and
// maxAnisotropy, and nothing else. That absence was once taken as proof the state must stay
// unsupported, which is the same argument that was already disproved for `FillMode::WireFrame`: an
// absent STATE FIELD is not an absent CAPABILITY. WGSL's `textureSampleBias(t, s, coords, bias)`
// applies exactly XNA's semantic -- a bias added to the computed level of detail -- in the fragment
// stage, which is where every one of this renderer's sampling calls already sits. The bias travels
// in the per-draw uniform block.
//
// The mip chain's levels are each a flat, distinct colour for the reason `parity_sampler_max_mip_level`
// explains: a correct chain is nearly self-similar, so a test built on one cannot tell level 1 from
// level 0. The texture is sized to the quad so the natural level of detail is ~0 -- see kTextureSize
// for why that is load-bearing rather than incidental -- and the filter is Point so a level is
// SELECTED rather than blended.
//
// The negative column is not decoration. A bias of -1 at a natural LOD of 0 asks for level -1,
// which must CLAMP to level 0 rather than wrap to the coarsest level or read out of range -- so it
// must produce exactly the same picture as bias 0, and a renderer that wrapped would be caught here
// and nowhere else in this fixture.

#include "parity/ParityFixture.hpp"

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/BufferUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/CullMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureAddressMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureFilter.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexDeclaration.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElement.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementUsage.hpp"

#include <array>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kWidth = 256;
    constexpr int kHeight = 64;
    constexpr int kColumns = 4;
    /// 64x64 with a full chain: 64, 32, 16, 8, 4, 2, 1.
    ///
    /// THE TEXTURE HAS TO BE ABOUT THE SIZE OF THE QUAD. A bias is ADDED to the computed level of
    /// detail, so it only moves the selected level while that computation is in range: an 8x8
    /// texture on a 60-pixel quad is magnified by 7.5x, i.e. LOD = -2.9, and adding +1 leaves it
    /// negative and still magnifying, so every bias reads level 0 and the test measures nothing.
    /// (`parity_sampler_max_mip_level` gets away with a small texture because `lodMinClamp` clamps
    /// the level regardless of magnification; a bias does not.) At 64x64 against a ~60-pixel quad
    /// the natural LOD is ~0.09, so +1 and +2 land squarely on levels 1 and 2.
    constexpr int kTextureSize = 64;
    constexpr int kLevels = 7;

    const Color kClearColor(9, 13, 17, 255);
    /// One flat colour per mip level, so "which level was read" is a colour, not a sharpness.
    const std::array<Color, kLevels> kLevelColors{
        Color(220, 40, 40, 255),    // level 0  (64x64)
        Color(40, 200, 60, 255),    // level 1  (32x32)
        Color(50, 80, 230, 255),    // level 2  (16x16)
        Color(240, 200, 30, 255),   // level 3  (8x8)
        Color(200, 60, 200, 255),   // level 4  (4x4)
        Color(60, 200, 200, 255),   // level 5  (2x2)
        Color(255, 255, 255, 255)}; // level 6  (1x1)

    SamplerState PointClampWithLodBias(float bias)
    {
        SamplerState state;
        state.setFilterProperty(TextureFilter::Point);
        state.setAddressUProperty(TextureAddressMode::Clamp);
        state.setAddressVProperty(TextureAddressMode::Clamp);
        state.setMipMapLevelOfDetailBiasProperty(bias);
        return state;
    }
}

/// WEBGPU-205: MipMapLevelOfDetailBias shifts which mip level a sample comes from.
class SamplerLodBiasParityFixture : public CNA::Parity::ParityFixture
{
public:
    SamplerLodBiasParityFixture() : ParityFixture(kWidth, kHeight) {}

protected:
    void RunFixture() override
    {
        auto& device = getGraphicsDeviceProperty();
        const CNA::Parity::ParityGrid grid{kWidth, kHeight, kColumns, 1};

        Texture2D texture(device, kTextureSize, kTextureSize, true, SurfaceFormat::Color);
        for (int level = 0; level < kLevels; ++level)
        {
            const int extent = kTextureSize >> level;
            std::vector<Color> pixels(static_cast<std::size_t>(extent) * extent,
                                      kLevelColors[static_cast<std::size_t>(level)]);
            texture.SetData(level, nullptr, pixels.data(), 0, static_cast<int>(pixels.size()));
        }

        BasicEffect effect(device);
        effect.setWorldProperty(Matrix::getIdentityProperty());
        effect.setViewProperty(Matrix::getIdentityProperty());
        effect.setProjectionProperty(Matrix::getIdentityProperty());
        effect.setLightingEnabledProperty(false);
        effect.setTextureEnabledProperty(true);
        effect.setTextureProperty(&texture);
        effect.setVertexColorEnabledProperty(false);

        RasterizerState rs;
        rs.setCullModeProperty(CullMode::None);
        device.setRasterizerStateProperty(rs);
        device.setBlendStateProperty(BlendState::Opaque);
        device.setDepthStencilStateProperty(DepthStencilState::None);
        device.Clear(kClearColor);

        const auto drawColumn = [&](int column, float bias)
        {
            device.getSamplerStatesProperty()[0] = PointClampWithLodBias(bias);

            // ParityGrid::QuadCorners returns TL, BL, BR, TR -- ring order. A triangle STRIP needs
            // the "Z" order TL, BL, TR, BR; feeding it the ring draws (TL,BL,BR) and (BL,BR,TR),
            // which is not the quad and leaves a quarter of it uncovered.
            const auto corners = grid.QuadCorners(column, 0);
            struct Vertex { float x, y, z; float u, v; };
            const std::array<Vertex, 4> verts{
                Vertex{corners[0].X, corners[0].Y, 0.0f, 0.0f, 0.0f},   // top-left
                Vertex{corners[1].X, corners[1].Y, 0.0f, 0.0f, 1.0f},   // bottom-left
                Vertex{corners[3].X, corners[3].Y, 0.0f, 1.0f, 0.0f},   // top-right
                Vertex{corners[2].X, corners[2].Y, 0.0f, 1.0f, 1.0f}};  // bottom-right
            VertexBuffer vb(device,
                            VertexDeclaration(20,
                                {VertexElement(0, VertexElementFormat::Vector3,
                                               VertexElementUsage::Position, 0),
                                 VertexElement(12, VertexElementFormat::Vector2,
                                               VertexElementUsage::TextureCoordinate, 0)}),
                            static_cast<int>(verts.size()), BufferUsage::None);
            vb.SetDataRaw(verts.data(), static_cast<int>(verts.size()), 20);
            device.SetVertexBuffer(&vb);
            effect.Apply();
            device.DrawPrimitives(PrimitiveType::TriangleStrip, 0, 2);
            device.SetVertexBuffer(nullptr);
        };

        drawColumn(0,  0.0f);   // the natural level, which at 1:1 is 0
        drawColumn(1,  1.0f);   // one level coarser
        drawColumn(2,  2.0f);   // two levels coarser
        drawColumn(3, -1.0f);   // below level 0 -- must clamp, not wrap

        // Each level is a flat colour and the filter selects rather than blends, so every value is
        // exact; tolerance 2 covers the unorm8 round trip and nothing else.
        ExpectAverage("bias 0 samples level 0",  grid.Interior(0, 0), kLevelColors[0], 2);
        ExpectAverage("bias +1 samples level 1", grid.Interior(1, 0), kLevelColors[1], 2);
        ExpectAverage("bias +2 samples level 2", grid.Interior(2, 0), kLevelColors[2], 2);
        // A bias below level 0 clamps rather than wrapping to the coarsest level.
        ExpectAverage("bias -1 clamps to level 0", grid.Interior(3, 0), kLevelColors[0], 2);

        // The state has to MATTER, stated between adjacent columns so a renderer that applied the
        // bias only at one magnitude cannot pass on the extremes alone.
        ExpectDistinct("bias 0 and +1 read different levels",
                       grid.Interior(0, 0), grid.Interior(1, 0), /*minDelta=*/100);
        ExpectDistinct("bias +1 and +2 read different levels",
                       grid.Interior(1, 0), grid.Interior(2, 0), /*minDelta=*/100);
        ExpectFlat("a biased column is a flat level, not a blend of two",
                   grid.Interior(2, 0), /*maxSpread=*/2);
    }
};

CNA_PARITY_FIXTURE_MAIN(SamplerLodBiasParityFixture)
