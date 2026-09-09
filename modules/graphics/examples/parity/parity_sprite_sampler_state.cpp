// SPDX-License-Identifier: MS-PL
// plans/plan_webgpu.md WEBGPU-205 (harness: WEBGPU-207): does a `SamplerState` field that is not
// the filter or the two address modes reach a `SpriteBatch` draw at all?
//
// `WEBGPU-205` implemented `SamplerState.MipMapLevelOfDetailBias` on every stock 3D route and left
// `SpriteBatch` open, recording the blocker as "the sprite pipeline binds no uniform buffer, so the
// bias has no channel to travel in". That reading stops one layer too low. `ISpriteBatchRenderer`
// carries exactly three sampler values -- `SetSamplerFilter(int)` and
// `SetSamplerAddressMode(int, int)` -- and `SpriteBatch::Begin()` forwards only those, so
// `MipMapLevelOfDetailBias`, `MaxMipLevel`, `MaxAnisotropy` and `AddressW` never leave the
// framework. XNA does not narrow the state that way: `SpriteBatch.Begin(samplerState)` assigns
// `GraphicsDevice.SamplerStates[0]`, and all six fields apply.
//
// This fixture measures that, on both renderers, rather than asserting it from the interface.
//
// Row 1 is the CONTROL and is deliberately read first: the same texture, the same 1/4 scale and the
// same three biases on a 3D quad, where `WEBGPU-205` did land. If the control is flat the fixture is
// broken -- a chain without distinct levels, or a scale that never leaves level 0 -- and nothing
// row 0 shows would mean anything.
//
// Row 0 is the sprite route: the same three biases handed to `SpriteBatch::Begin`, and a fourth
// column with no bias at all as the reference the other three are compared against.
//
// The DEVICE route -- `GraphicsDevice.SamplerStates[0]`, which is the channel XNA's own
// `SpriteBatch.PrepRenderState` writes through -- cannot be exercised from a sprite-only frame at
// all, and that is itself part of the finding: `applySamplerStatesToRenderer()` is called from
// `DrawPrimitives` and its siblings, never from a `SpriteBatch` flush, so assigning the collection
// before a sprite draw pushes nothing. Which is why row 1's LAST column deliberately uses bias 0:
// EasyGL applies the bias to a GL SAMPLER OBJECT bound to texture unit 0 and its sprite flush calls
// `ApplySamplerState` (which by contract does not touch the mip state), so whatever bias the last
// 3D draw left behind is still bound when the sprites go through. Leaving a non-zero bias there
// would make this fixture measure that leak instead of the question it asks, and would make the two
// renderers disagree for a reason neither of them owns.
//
// MEASURED 2026-09-06: the bias reaches neither renderer's sprite route, and the two renderers
// agree pixel for pixel, so this is a shared framework gap rather than a WebGPU one -- recorded in
// `plans/plan_graphics.md` and NOT worked around inside the renderer, which could only clone one
// renderer's accident into the other. A related leak was probed and ruled out: EasyGL applies the
// bias to a GL sampler object that survives a draw, but its `ApplySamplerState` writes
// `GL_TEXTURE_LOD_BIAS = 0` unconditionally on desktop core, so a preceding 3D draw's bias does not
// survive into a following batch on either renderer. That was checked by temporarily giving row 1's
// last column a bias of +1 and re-running both; both stayed on the natural level.
//
// The mip levels are flat, distinct colours for `parity_sampler_max_mip_level`'s reason: a correct
// chain is nearly self-similar, so a test built on one cannot tell level 2 from level 3. The texture
// is 256x256 drawn into a 64x64 cell, i.e. minified 4x, so the natural level of detail is 2 and a
// bias has room to move DOWN as well as up -- a magnified quad would pin every column to level 0 and
// measure nothing.

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
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteSortMode.hpp"
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
    constexpr int kHeight = 128;
    constexpr int kColumns = 4;
    constexpr int kRows = 2;
    /// 256x256 with a full chain: 256,128,64,32,16,8,4,2,1. Drawn into a 64x64 cell, so the natural
    /// level of detail is exactly 2 and a bias can move in either direction from it.
    constexpr int kTextureSize = 256;
    constexpr int kLevels = 9;
    constexpr int kNaturalLevel = 2;

    const Color kClearColor(9, 13, 17, 255);
    /// One flat colour per mip level, so "which level was read" is a colour, not a sharpness.
    const std::array<Color, kLevels> kLevelColors{
        Color(220, 40, 40, 255),    // level 0  (256)
        Color(40, 200, 60, 255),    // level 1  (128)
        Color(50, 80, 230, 255),    // level 2  (64)   <- the natural level here
        Color(240, 200, 30, 255),   // level 3  (32)
        Color(200, 60, 200, 255),   // level 4  (16)
        Color(60, 200, 200, 255),   // level 5  (8)
        Color(255, 255, 255, 255),  // level 6  (4)
        Color(120, 120, 120, 255),  // level 7  (2)
        Color(0, 0, 0, 255)};       // level 8  (1)

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

/// WEBGPU-205: whether a SamplerState field beyond filter/address reaches a SpriteBatch draw.
class SpriteSamplerStateParityFixture : public CNA::Parity::ParityFixture
{
public:
    SpriteSamplerStateParityFixture() : ParityFixture(kWidth, kHeight) {}

protected:
    void RunFixture() override
    {
        auto& device = getGraphicsDeviceProperty();
        const CNA::Parity::ParityGrid grid{kWidth, kHeight, kColumns, kRows};

        Texture2D texture(device, kTextureSize, kTextureSize, true, SurfaceFormat::Color);
        for (int level = 0; level < kLevels; ++level)
        {
            const int extent = kTextureSize >> level;
            std::vector<Color> pixels(static_cast<std::size_t>(extent) * extent,
                                      kLevelColors[static_cast<std::size_t>(level)]);
            texture.SetData(level, nullptr, pixels.data(), 0, static_cast<int>(pixels.size()));
        }

        RasterizerState rs;
        rs.setCullModeProperty(CullMode::None);
        device.setRasterizerStateProperty(rs);
        device.setBlendStateProperty(BlendState::Opaque);
        device.setDepthStencilStateProperty(DepthStencilState::None);
        device.Clear(kClearColor);

        // --- Row 1: the 3D control, where WEBGPU-205 did land ---------------------------------
        BasicEffect effect(device);
        effect.setWorldProperty(Matrix::getIdentityProperty());
        effect.setViewProperty(Matrix::getIdentityProperty());
        effect.setProjectionProperty(Matrix::getIdentityProperty());
        effect.setLightingEnabledProperty(false);
        effect.setTextureEnabledProperty(true);
        effect.setTextureProperty(&texture);
        effect.setVertexColorEnabledProperty(false);

        const auto drawQuad = [&](int column, float bias)
        {
            device.getSamplerStatesProperty()[0] = PointClampWithLodBias(bias);
            // QuadCorners returns TL, BL, BR, TR -- ring order; a triangle STRIP needs the "Z"
            // order TL, BL, TR, BR.
            const auto corners = grid.QuadCorners(column, 1);
            struct Vertex { float x, y, z; float u, v; };
            const std::array<Vertex, 4> verts{
                Vertex{corners[0].X, corners[0].Y, 0.0f, 0.0f, 0.0f},
                Vertex{corners[1].X, corners[1].Y, 0.0f, 0.0f, 1.0f},
                Vertex{corners[3].X, corners[3].Y, 0.0f, 1.0f, 0.0f},
                Vertex{corners[2].X, corners[2].Y, 0.0f, 1.0f, 1.0f}};
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

        drawQuad(0,  0.0f);
        drawQuad(1,  1.0f);
        drawQuad(2, -2.0f);
        // Bias 0, and load-bearing rather than filler: this is the sampler state still bound when
        // the sprite row below is drawn. See the header.
        drawQuad(3,  0.0f);

        // --- Row 0: the sprite route ----------------------------------------------------------
        const auto drawSprite = [&](int column, const SamplerState& batchSampler)
        {
            const int cellW = grid.getCellWidthProperty();
            const int cellH = grid.getCellHeightProperty();
            const Rectangle destination(column * cellW, 0, cellW, cellH);
            SpriteBatch batch(device);
            batch.Begin(SpriteSortMode::Deferred, BlendState::Opaque, &batchSampler, nullptr,
                        nullptr);
            batch.Draw(texture, destination, Rectangle(0, 0, kTextureSize, kTextureSize),
                       Color::White);
            batch.End();
        };

        // Columns 0-2: the bias handed to Begin(), which is where a game would put it.
        drawSprite(0, PointClampWithLodBias(0.0f));
        drawSprite(1, PointClampWithLodBias(1.0f));
        drawSprite(2, PointClampWithLodBias(-2.0f));
        // Column 3: no bias at all, the reference the other three are read against.
        drawSprite(3, SamplerState::PointClamp);

        // --- The control, read first ----------------------------------------------------------
        ExpectAverage("control: a 3D quad at 1/4 scale samples the natural level 2",
                      grid.Interior(0, 1), kLevelColors[kNaturalLevel], 2);
        ExpectAverage("control: bias +1 moves the 3D quad to level 3",
                      grid.Interior(1, 1), kLevelColors[kNaturalLevel + 1], 2);
        ExpectAverage("control: bias -2 moves the 3D quad to level 0",
                      grid.Interior(2, 1), kLevelColors[0], 2);
        ExpectDistinct("control: the three biases are three different levels",
                       grid.Interior(0, 1), grid.Interior(1, 1), /*minDelta=*/100);

        // --- The sprite route -----------------------------------------------------------------
        // MEASURED on both renderers, 2026-09-06, and asserted as measured rather than as XNA
        // specifies, because a fixture that failed here would be reporting a defect it cannot fix
        // and would be red in every run forever. What XNA specifies is in the header and in
        // plans/plan_graphics.md; what CNA does is here.
        //
        // The bias never arrives. All four columns read the natural level regardless of what
        // SpriteBatch::Begin was handed -- the value stops in the framework, at an
        // ISpriteBatchRenderer that carries the filter and the two address modes and nothing else,
        // so no renderer ever gets the chance to apply or refuse it.
        ExpectAverage("a sprite at 1/4 scale samples the natural level 2",
                      grid.Interior(0, 0), kLevelColors[kNaturalLevel], 2);
        ExpectAverage("SpriteBatch.Begin's MipMapLevelOfDetailBias +1 does NOT reach the sprite",
                      grid.Interior(1, 0), kLevelColors[kNaturalLevel], 2);
        ExpectAverage("SpriteBatch.Begin's MipMapLevelOfDetailBias -2 does NOT reach it either",
                      grid.Interior(2, 0), kLevelColors[kNaturalLevel], 2);
        ExpectSameRegion("a biased batch and an unbiased one are the same picture",
                         grid.Interior(1, 0), grid.Interior(3, 0), 2);
        // And the pairing ExpectSameRegion needs: the sprite row is not trivially equal because
        // nothing rendered. It rendered, and it rendered the level the 3D control's first column
        // also read, which is the level the geometry alone selects.
        ExpectDistinct("the sprite row did render -- it differs from the -2 control column",
                       grid.Interior(1, 0), grid.Interior(2, 1), /*minDelta=*/100);
        ExpectSameRegion("an unbiased sprite and an unbiased 3D quad agree on the natural level",
                         grid.Interior(0, 0), grid.Interior(0, 1), 2);
    }
};

CNA_PARITY_FIXTURE_MAIN(SpriteSamplerStateParityFixture)
