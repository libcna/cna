// SPDX-License-Identifier: MS-PL
// plans/plan_llgl.md LLGL-25: the textured, tinted, fogged and alpha-tested parts of the stock effect
// family, asserted against real pixels through the public BasicEffect/AlphaTestEffect API.
//
// Geometry is a screen-filling quad drawn with an orthographic projection over the logical canvas,
// so every expectation below is a coordinate. The texture is a 2x2 image of four distinct colours,
// sampled with a Point sampler so each texel owns a quarter of the quad and no interpolation can
// blur one expectation into another.
//
// Check A -- a textured draw samples the texture: each source texel lands in its own quadrant, which
//   proves the texture coordinate attribute, the sampler binding and the orientation at once.
// Check B -- DiffuseColor tints the sampled texel (a red tint over a white texel leaves red).
// Check C -- Alpha reaches the output: a half-alpha effect over a known background blends.
// Check D -- vertex colours and the texture multiply together when the layout carries both AND
//   VertexColorEnabled is true; with it false, the same colour-carrying layout must NOT tint the
//   texel (a real bug found and fixed here: the shader used to multiply unconditionally whenever
//   the vertex buffer merely carried a colour attribute).
// Check E -- fog blends towards the fog colour with distance: the far end of a long quad is fogged
//   and the near end is not.
// Check F -- AlphaTestEffect discards texels below the reference alpha and keeps those above it.
//
// Exit code 0 = all checks PASS, 1 = any FAILs.

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/AlphaTestEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/BufferUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/ClearOptions.hpp"
#include "Microsoft/Xna/Framework/Graphics/CompareFunction.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColorTexture.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionTexture.hpp"

#include "CNA/Internal/Renderers/Llgl/LlglRenderer.hpp"

#include "common/PixelTestGame.hpp"

#include <cstdint>
#include <memory>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kWidth = 320;
    constexpr int kHeight = 240;

    const Color kClear{0, 0, 0, 255};

    // 2x2 RGBA8: red / green / blue / white, one texel per quadrant.
    std::vector<std::uint8_t> MakeQuadrantPixels()
    {
        return {
            255, 0, 0, 255,   0, 255, 0, 255,
            0, 0, 255, 255,   255, 255, 255, 255,
        };
    }

    // 2x2 RGBA8 where the alpha varies: the top row is opaque, the bottom row nearly transparent.
    std::vector<std::uint8_t> MakeAlphaRampPixels()
    {
        return {
            255, 255, 255, 255,   255, 255, 255, 255,
            255, 255, 255,  20,   255, 255, 255,  20,
        };
    }
}

class LlglBasicEffectTest : public CNA::Examples::PixelTestGame
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    int logicalWidth_ = 0;
    int logicalHeight_ = 0;

    [[nodiscard]] Matrix LogicalProjection() const
    {
        return Matrix::CreateOrthographicOffCenter(
            0.0f, static_cast<float>(logicalWidth_), static_cast<float>(logicalHeight_),
            0.0f, 0.0f, 1.0f);
    }

    // A quad over the given logical rectangle, at z = -0.5 (XNA's projection is right-handed, so
    // visible geometry has negative z), with texture coordinates spanning the whole texture.
    static std::vector<VertexPositionTexture> MakeTexturedQuad(float left, float top,
                                                               float right, float bottom)
    {
        const auto vertex = [](float x, float y, float u, float v) {
            return VertexPositionTexture(Vector3(x, y, -0.5f), Vector2(u, v));
        };
        return {
            vertex(left, top, 0.0f, 0.0f),
            vertex(right, top, 1.0f, 0.0f),
            vertex(left, bottom, 0.0f, 1.0f),
            vertex(left, bottom, 0.0f, 1.0f),
            vertex(right, top, 1.0f, 0.0f),
            vertex(right, bottom, 1.0f, 1.0f),
        };
    }

    static std::vector<VertexPositionColorTexture> MakeColoredTexturedQuad(
        float left, float top, float right, float bottom, const Color& color)
    {
        const auto vertex = [&color](float x, float y, float u, float v) {
            return VertexPositionColorTexture(Vector3(x, y, -0.5f), color, Vector2(u, v));
        };
        return {
            vertex(left, top, 0.0f, 0.0f),
            vertex(right, top, 1.0f, 0.0f),
            vertex(left, bottom, 0.0f, 1.0f),
            vertex(left, bottom, 0.0f, 1.0f),
            vertex(right, top, 1.0f, 0.0f),
            vertex(right, bottom, 1.0f, 1.0f),
        };
    }

    template <typename TVertex>
    void DrawQuad(GraphicsDevice& device, Effect& effect, const VertexDeclaration& declaration,
                  const std::vector<TVertex>& vertices)
    {
        VertexBuffer buffer(device, declaration, static_cast<int>(vertices.size()), BufferUsage::None);
        buffer.SetData(vertices.data(), static_cast<int>(vertices.size()));
        device.SetVertexBuffer(&buffer);
        effect.Apply();
        device.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
    }

public:
    LlglBasicEffectTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kWidth);
        gdm_->setPreferredBackBufferHeightProperty(kHeight);
        gdm_->setSynchronizeWithVerticalRetraceProperty(false);
    }

    void RunTest() override
    {
        auto& device = getGraphicsDeviceProperty();
        auto& renderer = static_cast<CNA::Internal::Renderers::Llgl::LlglRenderer&>(
            device.GetRenderer());
        renderer.GetViewportSize(logicalWidth_, logicalHeight_);

        device.setDepthStencilStateProperty(DepthStencilState::None);
        device.setRasterizerStateProperty(RasterizerState::CullNone);
        SamplerState pointClamp = SamplerState::PointClamp;
        device.getSamplerStatesProperty()[0] = pointClamp;

        Texture2D quadrants = Texture2D::CreateFromPixels(device, 2, 2, MakeQuadrantPixels());

        BasicEffect effect(device);
        effect.setLightingEnabledProperty(false);
        effect.setWorldProperty(Matrix::getIdentityProperty());
        effect.setViewProperty(Matrix::getIdentityProperty());
        effect.setProjectionProperty(LogicalProjection());

        // --- Check A: the texture is sampled ----------------------------------------------------
        effect.setTextureEnabledProperty(true);
        effect.setTextureProperty(&quadrants);
        device.Clear(kClear);
        DrawQuad(device, effect, VertexPositionTexture::getVertexDeclarationStatic(),
                 MakeTexturedQuad(40.0f, 40.0f, 200.0f, 200.0f));

        ExpectPixel("textured draw: top-left texel is red", Rectangle(70, 70, 1, 1), Color(255, 0, 0, 255));
        ExpectPixel("textured draw: top-right texel is green", Rectangle(170, 70, 1, 1), Color(0, 255, 0, 255));
        ExpectPixel("textured draw: bottom-left texel is blue", Rectangle(70, 170, 1, 1), Color(0, 0, 255, 255));
        ExpectPixel("textured draw: bottom-right texel is white", Rectangle(170, 170, 1, 1),
                    Color(255, 255, 255, 255));

        // --- Check B: DiffuseColor tints the sample ---------------------------------------------
        effect.setDiffuseColorProperty(Vector3(1.0f, 0.0f, 0.0f));
        device.Clear(kClear);
        DrawQuad(device, effect, VertexPositionTexture::getVertexDeclarationStatic(),
                 MakeTexturedQuad(40.0f, 40.0f, 200.0f, 200.0f));
        ExpectPixel("DiffuseColor tints the sampled texel", Rectangle(170, 170, 1, 1),
                    Color(255, 0, 0, 255));
        ExpectPixel("a tint that zeroes a channel really removes it", Rectangle(170, 70, 1, 1),
                    Color(0, 0, 0, 255));
        effect.setDiffuseColorProperty(Vector3(1.0f, 1.0f, 1.0f));

        // --- Check C: Alpha blends against the background ---------------------------------------
        // BasicEffect premultiplies DiffuseColor by Alpha before it reaches the shader. Its
        // matching XNA blend state is therefore AlphaBlend (One/InverseSourceAlpha); using
        // NonPremultiplied here would multiply the half-alpha RGB by SourceAlpha a second time.
        device.setBlendStateProperty(BlendState::AlphaBlend);
        effect.setAlphaProperty(0.5f);
        device.Clear(kClear);
        DrawQuad(device, effect, VertexPositionTexture::getVertexDeclarationStatic(),
                 MakeTexturedQuad(40.0f, 40.0f, 200.0f, 200.0f));
        ExpectPixel("Alpha blends the white texel halfway to the background",
                    Rectangle(170, 170, 1, 1), Color(128, 128, 128, 255), /*tolerance=*/6);
        effect.setAlphaProperty(1.0f);
        device.setBlendStateProperty(BlendState::Opaque);

        // --- Check D: vertex colour multiplies with the texture ---------------------------------
        // VertexColorEnabled must be explicitly requested for the vertex colour attribute to
        // apply at all -- a vertex layout that merely CARRIES a colour attribute is not enough
        // (real XNA semantics; a bug that ignored this property and always multiplied unconditionally
        // was found and fixed here).
        effect.VertexColorEnabled = true;
        device.Clear(kClear);
        DrawQuad(device, effect, VertexPositionColorTexture::getVertexDeclarationStatic(),
                 MakeColoredTexturedQuad(40.0f, 40.0f, 200.0f, 200.0f, Color(0, 255, 255, 255)));
        ExpectPixel("vertex colour multiplies the white texel", Rectangle(170, 170, 1, 1),
                    Color(0, 255, 255, 255));
        ExpectPixel("vertex colour multiplies the red texel to black", Rectangle(70, 70, 1, 1),
                    Color(0, 0, 0, 255));
        effect.VertexColorEnabled = false;

        // Same colour-carrying vertex buffer, but with VertexColorEnabled off: the vertex colour
        // attribute must NOT apply, even though the buffer still carries one.
        device.Clear(kClear);
        DrawQuad(device, effect, VertexPositionColorTexture::getVertexDeclarationStatic(),
                 MakeColoredTexturedQuad(40.0f, 40.0f, 200.0f, 200.0f, Color(0, 255, 255, 255)));
        ExpectPixel("VertexColorEnabled=false leaves the white texel untinted", Rectangle(170, 170, 1, 1),
                    Color(255, 255, 255, 255));
        ExpectPixel("VertexColorEnabled=false leaves the red texel untinted", Rectangle(70, 70, 1, 1),
                    Color(255, 0, 0, 255));

        // --- Check E: fog -----------------------------------------------------------------------
        // A quad stretching away from the camera in view space: with fog from z = 0.2 to z = 0.8,
        // its near end must be untouched and its far end must reach the fog colour.
        {
            BasicEffect fogEffect(device);
            fogEffect.setLightingEnabledProperty(false);
            fogEffect.setTextureEnabledProperty(true);
            fogEffect.setTextureProperty(&quadrants);
            fogEffect.setWorldProperty(Matrix::getIdentityProperty());
            fogEffect.setViewProperty(Matrix::getIdentityProperty());
            fogEffect.setProjectionProperty(LogicalProjection());
            fogEffect.setFogEnabledProperty(true);
            fogEffect.setFogColorProperty(Vector3(1.0f, 0.0f, 1.0f));
            fogEffect.setFogStartProperty(0.2f);
            fogEffect.setFogEndProperty(0.8f);

            // The quad's own z varies from -0.25 (near) to -0.75 (far), which under this projection
            // is view-space depth 0.25 to 0.75 -- inside the fog range at both ends.
            const auto vertex = [](float x, float y, float z, float u, float v) {
                return VertexPositionTexture(Vector3(x, y, z), Vector2(u, v));
            };
            const std::vector<VertexPositionTexture> depthQuad{
                vertex(220.0f, 40.0f, -0.25f, 1.0f, 1.0f),
                vertex(300.0f, 40.0f, -0.25f, 1.0f, 1.0f),
                vertex(220.0f, 200.0f, -0.75f, 1.0f, 1.0f),
                vertex(220.0f, 200.0f, -0.75f, 1.0f, 1.0f),
                vertex(300.0f, 40.0f, -0.25f, 1.0f, 1.0f),
                vertex(300.0f, 200.0f, -0.75f, 1.0f, 1.0f),
            };

            device.Clear(kClear);
            DrawQuad(device, fogEffect, VertexPositionTexture::getVertexDeclarationStatic(), depthQuad);

            Color nearEnd(0, 0, 0, 0);
            Color farEnd(0, 0, 0, 0);
            const Rectangle nearRegion(260, 45, 1, 1);
            const Rectangle farRegion(260, 195, 1, 1);
            device.GetBackBufferData(&nearRegion, &nearEnd, 0, 1);
            device.GetBackBufferData(&farRegion, &farEnd, 0, 1);

            // The white texel fogged towards magenta loses green with distance, so the far end must
            // be measurably greener-free than the near end without either being the other's value.
            ExpectTrue("fog leaves the near end close to the unfogged colour",
                       nearEnd.getGProperty() > 200);
            ExpectTrue("fog pulls the far end towards the fog colour",
                       farEnd.getGProperty() < nearEnd.getGProperty() - 40);
        }

        // --- Check F: AlphaTestEffect -----------------------------------------------------------
        {
            Texture2D alphaRamp = Texture2D::CreateFromPixels(device, 2, 2, MakeAlphaRampPixels());

            AlphaTestEffect alphaEffect(device);
            alphaEffect.setTextureProperty(&alphaRamp);
            alphaEffect.setWorldProperty(Matrix::getIdentityProperty());
            alphaEffect.setViewProperty(Matrix::getIdentityProperty());
            alphaEffect.setProjectionProperty(LogicalProjection());
            alphaEffect.setAlphaFunctionProperty(CompareFunction::Greater);
            alphaEffect.setReferenceAlphaProperty(128);

            device.Clear(kClear);
            DrawQuad(device, alphaEffect, VertexPositionTexture::getVertexDeclarationStatic(),
                     MakeTexturedQuad(40.0f, 40.0f, 200.0f, 200.0f));

            ExpectPixel("AlphaTest keeps texels above the reference alpha", Rectangle(70, 70, 1, 1),
                        Color(255, 255, 255, 255));
            ExpectPixel("AlphaTest discards texels below the reference alpha", Rectangle(70, 170, 1, 1),
                        kClear);
        }
    }
};

int main()
{
    return CNA::Examples::RunPixelTest<LlglBasicEffectTest>();
}
