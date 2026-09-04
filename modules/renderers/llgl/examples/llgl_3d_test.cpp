// SPDX-License-Identifier: MS-PL
// plans/plan_llgl.md LLGL-24: the colour-only 3D path, asserted against real pixels.
//
// Every draw goes through the public API -- BasicEffect with VertexColorEnabled, a real
// VertexBuffer, GraphicsDevice::DrawPrimitives / DrawIndexedPrimitives -- so this exercises the
// same route a game takes, not a renderer-internal shortcut.
//
// The projection is orthographic over the back buffer in pixels, so a vertex at (x, y) lands at
// window pixel (x, y) and every expectation below is a coordinate, not a guess.
//
// Check A -- a vertex-coloured triangle rasterizes, and the colour at a vertex is that vertex's own
//   colour (which also proves the vertex colour attribute reaches the shader at the right offset).
// Check B -- depth testing works: a quad drawn SECOND but further away does not overwrite the near
//   one, while the same pair drawn with depth testing off does. This is also what proves the
//   depth-range correction, since a scene squeezed into the wrong half of the clip range still
//   draws but stops ordering correctly.
// Check C -- an indexed draw of the same quad produces the same pixels as the non-indexed one.
// Check D -- CullMode::CullClockwiseFace really removes a clockwise-wound triangle, and
//   CullMode::None keeps it.
// Check E -- a draw whose vertex range leaves the buffer is refused, not clamped.
// Check F -- an effect configuration this path cannot honour (a texture) fails loudly instead of
//   rendering an untextured surface that merely looks plausible.
// Check G -- FillMode::WireFrame really draws edges only: a point on an edge is painted and the
//   middle of the triangle is not.
//
// Exit code 0 = all checks PASS, 1 = any FAILs.

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/BufferUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/ClearOptions.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexElementSize.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/CullMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/FillMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"

#include "CNA/GraphicsCapability.hpp"
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

    // A quad covering pixels x = 40..200, y = 40..160 at a chosen depth, in one solid colour.
    //
    // XNA's CreateOrthographicOffCenter is right-handed: the camera looks down -Z, so visible
    // geometry has NEGATIVE z, and -0.2 is nearer than -0.8. Positive z is behind the camera and is
    // clipped away entirely -- which is exactly what this test caught the first time it ran.
    std::vector<VertexPositionColor> MakeQuad(float depth, const Color& color)
    {
        const Vector3 topLeft(40.0f, 40.0f, depth);
        const Vector3 topRight(200.0f, 40.0f, depth);
        const Vector3 bottomLeft(40.0f, 160.0f, depth);
        const Vector3 bottomRight(200.0f, 160.0f, depth);
        return {
            VertexPositionColor(topLeft, color),
            VertexPositionColor(topRight, color),
            VertexPositionColor(bottomLeft, color),
            VertexPositionColor(bottomLeft, color),
            VertexPositionColor(topRight, color),
            VertexPositionColor(bottomRight, color),
        };
    }
}

class Llgl3DTest : public CNA::Examples::PixelTestGame
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    std::unique_ptr<BasicEffect> effect_;
    int logicalWidth_ = 0;
    int logicalHeight_ = 0;

    // Orthographic over the LOGICAL canvas in pixels, with the origin top-left, so a vertex
    // position is a pixel position and every expectation below is a coordinate.
    //
    // The extent comes from the renderer rather than from the requested back-buffer size: under the
    // default FixedHeightDynamicWidth presentation the canvas keeps the requested height but takes
    // its width from the window's aspect ratio, so a projection built from the requested width
    // would stretch the whole scene sideways -- which is exactly what happened the first time this
    // test ran, and is a mistake a game could make just as easily.
    void SetUpEffect(GraphicsDevice& device)
    {
        auto& renderer = static_cast<CNA::Internal::Renderers::Llgl::LlglRenderer&>(
            device.GetRenderer());
        renderer.GetViewportSize(logicalWidth_, logicalHeight_);

        effect_ = std::make_unique<BasicEffect>(device);
        effect_->VertexColorEnabled = true;
        effect_->setTextureEnabledProperty(false);
        effect_->setLightingEnabledProperty(false);
        effect_->setWorldProperty(Matrix::getIdentityProperty());
        effect_->setViewProperty(Matrix::getIdentityProperty());
        effect_->setProjectionProperty(Matrix::CreateOrthographicOffCenter(
            0.0f, static_cast<float>(logicalWidth_), static_cast<float>(logicalHeight_),
            0.0f, 0.0f, 1.0f));
    }

    void DrawQuad(GraphicsDevice& device, const std::vector<VertexPositionColor>& vertices)
    {
        VertexBuffer buffer(device, VertexPositionColor::getVertexDeclarationStatic(),
                            static_cast<int>(vertices.size()), BufferUsage::None);
        buffer.SetData(vertices.data(), static_cast<int>(vertices.size()));
        device.SetVertexBuffer(&buffer);
        effect_->Apply();
        device.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
    }

public:
    Llgl3DTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kWidth);
        gdm_->setPreferredBackBufferHeightProperty(kHeight);
        gdm_->setSynchronizeWithVerticalRetraceProperty(false);
    }

    void RunTest() override
    {
        auto& device = getGraphicsDeviceProperty();
        SetUpEffect(device);

        // --- Check A: a vertex-coloured triangle ------------------------------------------------
        // The depth buffer is cleared even though this check is about colour: the device's default
        // DepthStencilState has depth testing ON, and a depth buffer whose contents were never
        // defined tests against whatever happens to be in memory -- which showed up here as a
        // triangle drawn everywhere except near its apex.
        device.Clear(ClearOptions::Target | ClearOptions::DepthBuffer, kClear, 1.0f, 0);
        {
            const std::vector<VertexPositionColor> triangle{
                VertexPositionColor(Vector3(160.0f, 30.0f, -0.5f), Color(255, 0, 0, 255)),
                VertexPositionColor(Vector3(280.0f, 200.0f, -0.5f), Color(0, 255, 0, 255)),
                VertexPositionColor(Vector3(40.0f, 200.0f, -0.5f), Color(0, 0, 255, 255)),
            };
            VertexBuffer buffer(device, VertexPositionColor::getVertexDeclarationStatic(), 3,
                                BufferUsage::None);
            buffer.SetData(triangle.data(), static_cast<int>(triangle.size()));
            device.SetVertexBuffer(&buffer);
            effect_->Apply();
            device.DrawPrimitives(PrimitiveType::TriangleList, 0, 1);

            // Just inside each corner: the interpolated colour there is dominated by that corner's
            // own vertex colour, which no single flat-colour bug could reproduce for all three.
            ExpectPixel("triangle top vertex is red", Rectangle(160, 40, 1, 1),
                        Color(200, 20, 20, 255), /*tolerance=*/60);
            ExpectPixel("triangle bottom-right vertex is green", Rectangle(265, 195, 1, 1),
                        Color(20, 200, 20, 255), /*tolerance=*/60);
            ExpectPixel("triangle bottom-left vertex is blue", Rectangle(55, 195, 1, 1),
                        Color(20, 20, 200, 255), /*tolerance=*/60);
            ExpectPixel("outside the triangle stays the clear colour", Rectangle(10, 10, 1, 1), kClear);
        }

        // --- Check B: depth test ----------------------------------------------------------------
        device.setDepthStencilStateProperty(DepthStencilState::Default);
        device.Clear(ClearOptions::Target | ClearOptions::DepthBuffer, kClear, 1.0f, 0);
        DrawQuad(device, MakeQuad(-0.2f, Color(0, 0, 255, 255)));   // near, drawn first
        DrawQuad(device, MakeQuad(-0.8f, Color(255, 0, 0, 255)));   // far, drawn second
        ExpectPixel("depth testing keeps the nearer surface when the farther one is drawn later",
                    Rectangle(120, 100, 1, 1), Color(0, 0, 255, 255));

        device.setDepthStencilStateProperty(DepthStencilState::None);
        device.Clear(ClearOptions::Target | ClearOptions::DepthBuffer, kClear, 1.0f, 0);
        DrawQuad(device, MakeQuad(-0.2f, Color(0, 0, 255, 255)));
        DrawQuad(device, MakeQuad(-0.8f, Color(255, 0, 0, 255)));
        ExpectPixel("with depth testing off the later surface wins regardless of depth",
                    Rectangle(120, 100, 1, 1), Color(255, 0, 0, 255));

        // --- Check C: indexed draw --------------------------------------------------------------
        device.setDepthStencilStateProperty(DepthStencilState::None);
        device.Clear(kClear);
        {
            const std::vector<VertexPositionColor> corners{
                VertexPositionColor(Vector3(40.0f, 40.0f, -0.5f), Color(255, 255, 0, 255)),
                VertexPositionColor(Vector3(200.0f, 40.0f, -0.5f), Color(255, 255, 0, 255)),
                VertexPositionColor(Vector3(40.0f, 160.0f, -0.5f), Color(255, 255, 0, 255)),
                VertexPositionColor(Vector3(200.0f, 160.0f, -0.5f), Color(255, 255, 0, 255)),
            };
            const std::vector<std::uint16_t> indices{0, 1, 2, 2, 1, 3};

            VertexBuffer vertexBuffer(device, VertexPositionColor::getVertexDeclarationStatic(), 4,
                                      BufferUsage::None);
            vertexBuffer.SetData(corners.data(), static_cast<int>(corners.size()));
            IndexBuffer indexBuffer(device, IndexElementSize::SixteenBits,
                                    static_cast<int>(indices.size()), BufferUsage::None);
            indexBuffer.SetData(indices.data(), static_cast<int>(indices.size()));

            device.SetVertexBuffer(&vertexBuffer);
            device.setIndicesProperty(&indexBuffer);
            effect_->Apply();
            device.DrawIndexedPrimitives(PrimitiveType::TriangleList, 0, 0, 4, 0, 2);
            device.setIndicesProperty(nullptr);

            ExpectPixel("an indexed draw fills the same quad", Rectangle(120, 100, 1, 1),
                        Color(255, 255, 0, 255));
            ExpectPixel("outside the indexed quad stays the clear colour", Rectangle(300, 220, 1, 1), kClear);
        }

        // --- Check D: cull mode -----------------------------------------------------------------
        {
            // Clockwise on screen with Y pointing down, which is XNA's front face.
            const std::vector<VertexPositionColor> clockwise{
                VertexPositionColor(Vector3(160.0f, 40.0f, -0.5f), Color(255, 0, 255, 255)),
                VertexPositionColor(Vector3(260.0f, 180.0f, -0.5f), Color(255, 0, 255, 255)),
                VertexPositionColor(Vector3(60.0f, 180.0f, -0.5f), Color(255, 0, 255, 255)),
            };

            device.setRasterizerStateProperty(RasterizerState::CullClockwise);
            device.Clear(kClear);
            {
                VertexBuffer buffer(device, VertexPositionColor::getVertexDeclarationStatic(), 3,
                                    BufferUsage::None);
                buffer.SetData(clockwise.data(), static_cast<int>(clockwise.size()));
                device.SetVertexBuffer(&buffer);
                effect_->Apply();
                device.DrawPrimitives(PrimitiveType::TriangleList, 0, 1);
            }
            ExpectPixel("CullClockwiseFace removes a clockwise triangle", Rectangle(160, 140, 1, 1), kClear);

            device.setRasterizerStateProperty(RasterizerState::CullNone);
            device.Clear(kClear);
            {
                VertexBuffer buffer(device, VertexPositionColor::getVertexDeclarationStatic(), 3,
                                    BufferUsage::None);
                buffer.SetData(clockwise.data(), static_cast<int>(clockwise.size()));
                device.SetVertexBuffer(&buffer);
                effect_->Apply();
                device.DrawPrimitives(PrimitiveType::TriangleList, 0, 1);
            }
            ExpectPixel("CullMode::None keeps the same triangle", Rectangle(160, 140, 1, 1),
                        Color(255, 0, 255, 255));
        }

        // --- Check G: wireframe fill mode -------------------------------------------------------
        {
            // The bottom edge sits at y = 180.5, i.e. exactly on the centre of pixel row 180. A
            // whole-number y would put the line on the boundary between two rows, where either is
            // a legitimate rasterization and the check would be testing the tie-break rule rather
            // than wireframe mode.
            const std::vector<VertexPositionColor> triangle{
                VertexPositionColor(Vector3(160.5f, 40.0f, -0.5f), Color(0, 255, 255, 255)),
                VertexPositionColor(Vector3(260.5f, 180.5f, -0.5f), Color(0, 255, 255, 255)),
                VertexPositionColor(Vector3(60.5f, 180.5f, -0.5f), Color(0, 255, 255, 255)),
            };

            RasterizerState wireframe;
            wireframe.setCullModeProperty(CullMode::None);
            wireframe.setFillModeProperty(FillMode::WireFrame);
            device.setRasterizerStateProperty(wireframe);

            // Wireframe is module-dependent here, so the check follows the capability rather than
            // asserting one answer: where it is supported the edges must really be drawn, and
            // where it is not the draw must fail loudly instead of quietly producing an empty
            // frame. Both are real assertions; neither lets an unsupported path pass silently.
            const bool wireFrameSupported = device.SupportsCapability(CNA::GraphicsCapability::WireFrame);

            device.Clear(kClear);
            bool refusedWireFrame = false;
            {
                VertexBuffer buffer(device, VertexPositionColor::getVertexDeclarationStatic(), 3,
                                    BufferUsage::None);
                buffer.SetData(triangle.data(), static_cast<int>(triangle.size()));
                device.SetVertexBuffer(&buffer);
                effect_->Apply();
                try
                {
                    device.DrawPrimitives(PrimitiveType::TriangleList, 0, 1);
                }
                catch (const std::exception&)
                {
                    refusedWireFrame = true;
                }
            }

            if (wireFrameSupported)
            {
                // The bottom edge runs along y = 180.5 between x = 60.5 and x = 260.5, so a point
                // on it must be drawn while the middle of the triangle must not -- which is the
                // whole difference between a wireframe and a solid fill.
                ExpectTrue("WireFrame draws instead of throwing where it is supported", !refusedWireFrame);
                ExpectPixel("WireFrame draws the triangle's edge", Rectangle(160, 180, 1, 1),
                            Color(0, 255, 255, 255));
                ExpectPixel("WireFrame leaves the triangle's interior unfilled",
                            Rectangle(160, 140, 1, 1), kClear);
            }
            else
            {
                ExpectTrue("WireFrame fails loudly on a module that cannot draw it", refusedWireFrame);
            }

            device.setRasterizerStateProperty(RasterizerState::CullCounterClockwise);
        }

        // --- Check E/F: refusals ----------------------------------------------------------------
        {
            VertexBuffer buffer(device, VertexPositionColor::getVertexDeclarationStatic(), 3,
                                BufferUsage::None);
            const std::vector<VertexPositionColor> tiny{
                VertexPositionColor(Vector3(0.0f, 0.0f, -0.5f), Color(255, 255, 255, 255)),
                VertexPositionColor(Vector3(10.0f, 0.0f, -0.5f), Color(255, 255, 255, 255)),
                VertexPositionColor(Vector3(0.0f, 10.0f, -0.5f), Color(255, 255, 255, 255)),
            };
            buffer.SetData(tiny.data(), static_cast<int>(tiny.size()));
            device.SetVertexBuffer(&buffer);
            effect_->Apply();

            bool refusedOverrun = false;
            try
            {
                device.DrawPrimitives(PrimitiveType::TriangleList, 0, 4);
            }
            catch (const std::exception&)
            {
                refusedOverrun = true;
            }
            ExpectTrue("a draw whose vertex range leaves the buffer is refused", refusedOverrun);

            const std::vector<std::uint8_t> white{255, 255, 255, 255};
            Texture2D texture = Texture2D::CreateFromPixels(device, 1, 1, white);
            effect_->setTextureEnabledProperty(true);
            effect_->setTextureProperty(&texture);

            bool refusedTexture = false;
            try
            {
                effect_->Apply();
                device.DrawPrimitives(PrimitiveType::TriangleList, 0, 1);
            }
            catch (const std::exception&)
            {
                refusedTexture = true;
            }
            ExpectTrue("a textured effect fails loudly instead of drawing an untextured surface",
                       refusedTexture);

            effect_->setTextureEnabledProperty(false);
            effect_->setTextureProperty(nullptr);
        }
    }
};

int main()
{
    return CNA::Examples::RunPixelTest<Llgl3DTest>();
}
