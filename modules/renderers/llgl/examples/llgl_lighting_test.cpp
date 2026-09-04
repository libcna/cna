// SPDX-License-Identifier: MS-PL
// plans/plan_llgl.md LLGL-25: BasicEffect's lit path, asserted against real pixels.
//
// A single quad, facing the camera, lit by one directional light. The light direction and the
// quad's normal make the geometry itself the test: a light pointed straight at the surface is
// full brightness, a light behind the surface is unlit but for ambient, and turning the light off
// leaves only ambient plus emissive -- three states no single shading bug reproduces identically.
//
// Check A -- a front-facing light fully lit by a white light produces (close to) full brightness.
// Check B -- AmbientLightColor alone (no directional light) sets a flat, dim, non-zero level.
// Check C -- a light behind the surface contributes nothing beyond ambient (N·L clamped at zero).
// Check D -- DiffuseColor still tints the lit result.
// Check E -- EmissiveColor is added on top and is visible even with light and ambient both off.
// Check F -- a specular highlight appears where the reflection direction points at the camera, and
//   is small/dim off that point (SpecularColor/SpecularPower actually reaching the shader).
// Check G -- lighting without a texture, but WITH vertex colours, lights the surface for real
//   (LLGL-31) -- no fabricated white texture, a genuine untextured-but-lit shader variant.
// Check H -- lighting without a texture AND without vertex colours (LLGL-52) lights the surface
//   using DiffuseColor alone, the same as Check A -- no fabricated vertex colour, a genuine
//   untextured/uncoloured-but-lit shader variant with no colour attribute declared at all.
//
// Exit code 0 = all checks PASS, 1 = any FAILs.

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BufferUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/DirectionalLight.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexDeclaration.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElement.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionNormalTexture.hpp"

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

    // A flat quad facing the camera (+Z normal, matching a right-handed view looking down -Z), at
    // z = -0.5, filling a big square in the centre of the screen.
    std::vector<VertexPositionNormalTexture> MakeFacingQuad()
    {
        const Vector3 normal(0.0f, 0.0f, 1.0f);
        const auto vertex = [&normal](float x, float y, float u, float v) {
            return VertexPositionNormalTexture(Vector3(x, y, -0.5f), normal, Vector2(u, v));
        };
        return {
            vertex(60.0f, 40.0f, 0.0f, 0.0f),
            vertex(260.0f, 40.0f, 1.0f, 0.0f),
            vertex(60.0f, 200.0f, 0.0f, 1.0f),
            vertex(60.0f, 200.0f, 0.0f, 1.0f),
            vertex(260.0f, 40.0f, 1.0f, 0.0f),
            vertex(260.0f, 200.0f, 1.0f, 1.0f),
        };
    }

    // No stock XNA vertex type combines Position, Normal, and Color -- Check G needs exactly that
    // to exercise the lit-untextured shader variant (LLGL-31), so this test declares its own,
    // matching the offsetof/VertexElement pattern VertexPositionColor.cpp itself uses. The colour
    // is stored as raw r,g,b,a bytes rather than an actual Color object: Color implements
    // IPackedVectorT (virtual methods), so a Color member carries a vtable pointer ahead of its
    // 4-byte packed value -- embedding it directly and describing it to the GPU as a 4-byte
    // RGBA8UNorm attribute would read the vtable pointer's bytes, not the colour.
    struct VertexPositionNormalColor
    {
        Vector3 Position;
        Vector3 Normal;
        std::uint8_t R, G, B, A;

        VertexPositionNormalColor(const Vector3& position, const Vector3& normal, const Color& color)
            : Position(position), Normal(normal),
              R(color.getRProperty()), G(color.getGProperty()),
              B(color.getBProperty()), A(color.getAProperty())
        {
        }

        static const VertexDeclaration& getVertexDeclarationStatic()
        {
            static const VertexDeclaration decl(
                static_cast<int>(sizeof(VertexPositionNormalColor)),
                {
                    VertexElement(0,  VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
                    VertexElement(12, VertexElementFormat::Vector3, VertexElementUsage::Normal, 0),
                    VertexElement(24, VertexElementFormat::Color,   VertexElementUsage::Color, 0),
                });
            return decl;
        }
    };

    // Same facing quad as MakeFacingQuad(), but carrying vertex colours instead of texture
    // coordinates -- geometry and lighting setup mirror Check A exactly, so the pixel expectation
    // can too.
    std::vector<VertexPositionNormalColor> MakeFacingColoredQuad(const Color& color)
    {
        const Vector3 normal(0.0f, 0.0f, 1.0f);
        const auto vertex = [&normal, &color](float x, float y) {
            return VertexPositionNormalColor(Vector3(x, y, -0.5f), normal, color);
        };
        return {
            vertex(60.0f, 40.0f),   vertex(260.0f, 40.0f),  vertex(60.0f, 200.0f),
            vertex(60.0f, 200.0f),  vertex(260.0f, 40.0f),  vertex(260.0f, 200.0f),
        };
    }
}

class LlglLightingTest : public CNA::Examples::PixelTestGame
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

    void DrawFacingQuad(GraphicsDevice& device, BasicEffect& effect)
    {
        const std::vector<VertexPositionNormalTexture> quad = MakeFacingQuad();
        VertexBuffer buffer(device, VertexPositionNormalTexture::getVertexDeclarationStatic(),
                            static_cast<int>(quad.size()), BufferUsage::None);
        buffer.SetData(quad.data(), static_cast<int>(quad.size()));
        device.SetVertexBuffer(&buffer);
        effect.Apply();
        device.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
    }

public:
    LlglLightingTest()
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

        // A plain white texture: DiffuseColor and the lighting equation are the whole picture,
        // with no texture pattern to muddy the readings.
        const std::vector<std::uint8_t> white{255, 255, 255, 255};
        Texture2D whiteTexture = Texture2D::CreateFromPixels(device, 1, 1, white);

        BasicEffect effect(device);
        effect.setTextureEnabledProperty(true);
        effect.setTextureProperty(&whiteTexture);
        effect.setWorldProperty(Matrix::getIdentityProperty());
        effect.setViewProperty(Matrix::getIdentityProperty());
        effect.setProjectionProperty(LogicalProjection());
        effect.setLightingEnabledProperty(true);
        effect.setAmbientLightColorProperty(Vector3::Zero);
        // Eye position for specular is derived from View's inverse translation, not set directly;
        // View is Identity, so the eye sits at the world origin -- see Check F's own comment for
        // why that matters there specifically.

        // --- Check A: a front-facing light gives full brightness ---------------------------------
        auto& light0 = effect.getDirectionalLight0Property();
        light0.setEnabledProperty(true);
        light0.setDirectionProperty(Vector3(0.0f, 0.0f, -1.0f));   // points INTO the surface -> lights it
        light0.setDiffuseColorProperty(Vector3(1.0f, 1.0f, 1.0f));
        light0.setSpecularColorProperty(Vector3::Zero);

        device.Clear(kClear);
        DrawFacingQuad(device, effect);
        ExpectPixel("a light facing the surface gives near-full brightness", Rectangle(160, 120, 1, 1),
                    Color(255, 255, 255, 255), /*tolerance=*/10);

        // --- Check B: ambient alone, no directional light ----------------------------------------
        light0.setEnabledProperty(false);
        effect.setAmbientLightColorProperty(Vector3(0.3f, 0.3f, 0.3f));
        device.Clear(kClear);
        DrawFacingQuad(device, effect);
        ExpectPixel("AmbientLightColor alone gives a flat, dim, non-zero level", Rectangle(160, 120, 1, 1),
                    Color(77, 77, 77, 255), /*tolerance=*/15);

        // --- Check C: a light behind the surface contributes nothing beyond ambient -------------
        light0.setEnabledProperty(true);
        light0.setDirectionProperty(Vector3(0.0f, 0.0f, 1.0f));    // points AWAY from the surface
        device.Clear(kClear);
        DrawFacingQuad(device, effect);
        ExpectPixel("a light behind the surface adds nothing beyond ambient", Rectangle(160, 120, 1, 1),
                    Color(77, 77, 77, 255), /*tolerance=*/15);

        light0.setDirectionProperty(Vector3(0.0f, 0.0f, -1.0f));
        effect.setAmbientLightColorProperty(Vector3::Zero);

        // --- Check D: DiffuseColor tints the lit result -------------------------------------------
        effect.setDiffuseColorProperty(Vector3(1.0f, 0.0f, 0.0f));
        device.Clear(kClear);
        DrawFacingQuad(device, effect);
        ExpectPixel("DiffuseColor tints the lit surface", Rectangle(160, 120, 1, 1),
                    Color(255, 0, 0, 255), /*tolerance=*/10);
        effect.setDiffuseColorProperty(Vector3(1.0f, 1.0f, 1.0f));

        // --- Check E: EmissiveColor is visible with no light or ambient at all --------------------
        light0.setEnabledProperty(false);
        effect.setEmissiveColorProperty(Vector3(0.0f, 0.5f, 0.0f));
        device.Clear(kClear);
        DrawFacingQuad(device, effect);
        ExpectPixel("EmissiveColor shows through with no light and no ambient", Rectangle(160, 120, 1, 1),
                    Color(0, 128, 0, 255), /*tolerance=*/15);
        effect.setEmissiveColorProperty(Vector3::Zero);
        light0.setEnabledProperty(true);

        // --- Check F: specular highlight ----------------------------------------------------------
        {
            // Eye position for specular is derived from View's inverse translation (View =
            // Identity here => eye at world origin), and every OTHER check in this file poses
            // vertices in raw pixel coordinates -- eyePos - worldPos would then range over
            // hundreds of units across one quad, which is not a coherent camera and would make
            // the reflection direction meaningless. This check uses its own small-scale world
            // space instead: an orthographic projection spanning world x/y in [-1, 1] over the
            // whole window, and a quad at z = -1 -- eyePos=(0,0,0) is then a physically sensible
            // camera a unit away from a surface a couple of units across, where the half-vector
            // geometry the specular term depends on actually means something.
            effect.setProjectionProperty(
                Matrix::CreateOrthographicOffCenter(-1.0f, 1.0f, 1.0f, -1.0f, 0.0f, 10.0f));

            const Vector3 normal(0.0f, 0.0f, 1.0f);
            const auto vertex = [&normal](float x, float y, float u, float v) {
                return VertexPositionNormalTexture(Vector3(x, y, -1.0f), normal, Vector2(u, v));
            };
            const std::vector<VertexPositionNormalTexture> smallQuad{
                vertex(-1.0f, -1.0f, 0.0f, 0.0f), vertex(1.0f, -1.0f, 1.0f, 0.0f),
                vertex(-1.0f, 1.0f, 0.0f, 1.0f),  vertex(-1.0f, 1.0f, 0.0f, 1.0f),
                vertex(1.0f, -1.0f, 1.0f, 0.0f),  vertex(1.0f, 1.0f, 1.0f, 1.0f),
            };

            light0.setDiffuseColorProperty(Vector3::Zero);
            light0.setSpecularColorProperty(Vector3(1.0f, 1.0f, 1.0f));
            effect.setSpecularColorProperty(Vector3(1.0f, 1.0f, 1.0f));
            effect.setSpecularPowerProperty(64.0f);

            // Eye straight ahead and light straight-on: the half-vector equals the surface normal,
            // so the highlight peaks at the centre and falls off towards the edges.
            VertexBuffer buffer(device, VertexPositionNormalTexture::getVertexDeclarationStatic(),
                                static_cast<int>(smallQuad.size()), BufferUsage::None);
            buffer.SetData(smallQuad.data(), static_cast<int>(smallQuad.size()));
            device.SetVertexBuffer(&buffer);
            device.Clear(kClear);
            effect.Apply();
            device.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);

            Color centre(0, 0, 0, 0);
            Color edge(0, 0, 0, 0);
            const Rectangle centreRegion(kWidth / 2, kHeight / 2, 1, 1);
            const Rectangle edgeRegion(kWidth / 2 - kWidth * 4 / 10, kHeight / 2 - kHeight * 4 / 10, 1, 1);
            device.GetBackBufferData(&centreRegion, &centre, 0, 1);
            device.GetBackBufferData(&edgeRegion, &edge, 0, 1);

            ExpectTrue("a specular highlight peaks near the reflection direction",
                       centre.getRProperty() > 60);
            ExpectTrue("the specular highlight falls off away from the reflection direction",
                       edge.getRProperty() < centre.getRProperty());

            effect.setProjectionProperty(LogicalProjection());

            effect.setSpecularColorProperty(Vector3::Zero);
            light0.setSpecularColorProperty(Vector3::Zero);
            light0.setDiffuseColorProperty(Vector3(1.0f, 1.0f, 1.0f));
        }

        // --- Check G: lighting without a texture, with vertex colours, lights for real (LLGL-31) --
        {
            effect.setTextureEnabledProperty(false);
            effect.setTextureProperty(nullptr);
            effect.VertexColorEnabled = true;

            const std::vector<VertexPositionNormalColor> coloredQuad =
                MakeFacingColoredQuad(Color::White);
            VertexBuffer buffer(device, VertexPositionNormalColor::getVertexDeclarationStatic(),
                                static_cast<int>(coloredQuad.size()), BufferUsage::None);
            buffer.SetDataRaw(coloredQuad.data(), static_cast<int>(coloredQuad.size()),
                              static_cast<int>(sizeof(VertexPositionNormalColor)));
            device.SetVertexBuffer(&buffer);

            device.Clear(kClear);
            effect.Apply();
            device.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
            ExpectPixel("a lit, untextured-but-coloured surface lights for real (LLGL-31)",
                        Rectangle(160, 120, 1, 1), Color(255, 255, 255, 255), /*tolerance=*/10);

            // A non-white vertex colour still tints the lit result, the same way DiffuseColor does
            // for the textured path in Check D.
            const std::vector<VertexPositionNormalColor> tintedQuad =
                MakeFacingColoredQuad(Color(255, 0, 0, 255));
            VertexBuffer tintedBuffer(device, VertexPositionNormalColor::getVertexDeclarationStatic(),
                                      static_cast<int>(tintedQuad.size()), BufferUsage::None);
            tintedBuffer.SetDataRaw(tintedQuad.data(), static_cast<int>(tintedQuad.size()),
                                    static_cast<int>(sizeof(VertexPositionNormalColor)));
            device.SetVertexBuffer(&tintedBuffer);

            device.Clear(kClear);
            effect.Apply();
            device.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
            ExpectPixel("a red vertex colour tints the lit untextured surface", Rectangle(160, 120, 1, 1),
                        Color(255, 0, 0, 255), /*tolerance=*/10);

            effect.VertexColorEnabled = false;
        }

        // --- Check H: lighting without a texture or vertex colours lights for real (LLGL-52) ------
        {
            effect.setTextureEnabledProperty(false);
            effect.setTextureProperty(nullptr);

            const std::vector<VertexPositionNormalTexture> untexturedQuad = MakeFacingQuad();
            VertexBuffer buffer(device, VertexPositionNormalTexture::getVertexDeclarationStatic(),
                                static_cast<int>(untexturedQuad.size()), BufferUsage::None);
            buffer.SetData(untexturedQuad.data(), static_cast<int>(untexturedQuad.size()));
            device.SetVertexBuffer(&buffer);

            device.Clear(kClear);
            effect.Apply();
            device.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
            ExpectPixel("lighting without a texture or vertex colours lights the surface with "
                        "DiffuseColor alone", Rectangle(160, 120, 1, 1), Color(255, 255, 255, 255),
                        /*tolerance=*/10);
        }
    }
};

int main()
{
    return CNA::Examples::RunPixelTest<LlglLightingTest>();
}
