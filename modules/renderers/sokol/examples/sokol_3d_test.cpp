// SPDX-License-Identifier: MS-PL
// plans/plan_sokol.md SOKOL-20: 3D vertical-slice proof for the sokol_gfx graphics renderer -- real
// vertex-coloured geometry drawn through the public XNA API (`VertexBuffer` + `BasicEffect` +
// `GraphicsDevice.DrawPrimitives`/`DrawIndexedPrimitives`), every result verified by reading the
// rendered pixels back off the real back buffer.
//
// The scene is deliberately built from screen-aligned quads under an orthographic projection, so
// each expected pixel colour follows from the geometry alone rather than from whatever the renderer
// happened to produce.
//
// Check A -- a non-indexed vertex-coloured quad renders its own vertex colour.
// Check B -- an indexed draw of the same geometry renders identically, so the index path is real
//   and not silently falling back to the non-indexed one.
// Check C -- BasicEffect.DiffuseColor multiplies the vertex colour (a red diffuse over a white
//   quad gives red; over a blue quad gives black -- only a real per-channel multiply does both).
// Check D -- with VertexColorEnabled off, the vertex colours drop out entirely and the quad is
//   the diffuse colour alone.
// Check E -- DEPTH OCCLUSION: a far quad drawn AFTER a near one does not overwrite it, while the
//   same pair with the depth test disabled does. This is the check that proves the depth buffer is
//   genuinely attached and tested, not merely requested.
// Check F -- the still-unimplemented textured/lit path throws rather than rendering an unshaded
//   approximation that would look plausible and be wrong.
//
// Exit code 0 = all checks PASS, 1 = any FAILs, 77 = skipped (no GPU/display).

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/BufferUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/ClearOptions.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexElementSize.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"

#include "common/PixelTestGame.hpp"

#include <cstdint>
#include <memory>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kBackBufferWidth = 320;
    constexpr int kBackBufferHeight = 240;

    const Color kRed(255, 0, 0, 255);
    const Color kGreen(0, 255, 0, 255);
    const Color kBlue(0, 0, 255, 255);
    const Color kWhite(255, 255, 255, 255);
    const Color kBlack(0, 0, 0, 255);

    /// Right-handed ortho: larger z is nearer the camera.
    constexpr float kNearZ = 0.5f;
    constexpr float kFarZ = -0.5f;
    constexpr float kMidZ = 0.0f;

    /// Builds two triangles covering [x, x+size] x [y, y+size] in pixel space at depth z.
    std::vector<VertexPositionColor> MakeQuad(float x, float y, float size, float z,
                                              const Color& color)
    {
        const Vector3 topLeft(x, y, z);
        const Vector3 topRight(x + size, y, z);
        const Vector3 bottomRight(x + size, y + size, z);
        const Vector3 bottomLeft(x, y + size, z);
        return {
            VertexPositionColor(topLeft, color),
            VertexPositionColor(topRight, color),
            VertexPositionColor(bottomRight, color),
            VertexPositionColor(topLeft, color),
            VertexPositionColor(bottomRight, color),
            VertexPositionColor(bottomLeft, color),
        };
    }
}

class Sokol3DTest : public CNA::Examples::PixelTestGame
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    std::unique_ptr<BasicEffect> effect_;

    /// Pixel-space orthographic projection: vertex coordinates are back-buffer pixels with the
    /// origin top-left, so an expected pixel is read straight off the quad's own rectangle.
    ///
    /// The -1..1 depth range is the one SpriteBatch itself uses. XNA orthographic projections are
    /// right-handed -- the camera looks down -Z -- so within this range a LARGER z is NEARER, which
    /// is what kNearZ/kFarZ below encode. Getting that backwards is what made the first version of
    /// check E assert the opposite of what it meant.
    static Matrix PixelSpaceProjection()
    {
        return Matrix::CreateOrthographicOffCenter(
            0.0f, static_cast<float>(kBackBufferWidth),
            static_cast<float>(kBackBufferHeight), 0.0f,
            -1.0f, 1.0f);
    }

    void DrawQuad(const std::vector<VertexPositionColor>& vertices)
    {
        auto& device = getGraphicsDeviceProperty();
        VertexBuffer buffer(device, VertexPositionColor::getVertexDeclarationStatic(),
                            static_cast<int>(vertices.size()), BufferUsage::None);
        buffer.SetData(vertices.data(), static_cast<int>(vertices.size()));
        device.SetVertexBuffer(&buffer);
        effect_->Apply();
        device.DrawPrimitives(PrimitiveType::TriangleList, 0,
                              static_cast<int>(vertices.size()) / 3);
        device.SetVertexBuffer(nullptr);
    }

    void DrawIndexedQuad(const std::vector<VertexPositionColor>& corners)
    {
        auto& device = getGraphicsDeviceProperty();
        VertexBuffer buffer(device, VertexPositionColor::getVertexDeclarationStatic(),
                            static_cast<int>(corners.size()), BufferUsage::None);
        buffer.SetData(corners.data(), static_cast<int>(corners.size()));

        const std::vector<std::uint16_t> indices = {0, 1, 2, 0, 2, 3};
        IndexBuffer indexBuffer(device, IndexElementSize::SixteenBits,
                                static_cast<int>(indices.size()), BufferUsage::None);
        indexBuffer.SetData(indices.data(), static_cast<int>(indices.size()));

        device.SetVertexBuffer(&buffer);
        device.SetIndexBuffer(&indexBuffer);
        effect_->Apply();
        device.DrawIndexedPrimitives(PrimitiveType::TriangleList, 0, 0,
                                     static_cast<int>(corners.size()), 0, 2);
        device.SetIndexBuffer(nullptr);
        device.SetVertexBuffer(nullptr);
    }

public:
    void RunTest() override
    {
        auto& device = getGraphicsDeviceProperty();
        device.setBlendStateProperty(BlendState::Opaque);
        // Left at XNA's own default (CullCounterClockwiseFace) on purpose -- check G
        // below depends on culling actually being active.
        device.setRasterizerStateProperty(RasterizerState::CullCounterClockwise);
        device.setDepthStencilStateProperty(DepthStencilState::Default);

        effect_ = std::make_unique<BasicEffect>(device);
        effect_->setWorldProperty(Matrix::getIdentityProperty());
        effect_->setViewProperty(Matrix::getIdentityProperty());
        effect_->setProjectionProperty(PixelSpaceProjection());
        effect_->VertexColorEnabled = true;

        // Target|DepthBuffer, not the Color-only overload: this scene depends on the depth test,
        // and XNA's Clear(Color) deliberately leaves the depth buffer alone, so a colour-only clear
        // would leave the first frame testing against never-initialised depth.
        device.Clear(static_cast<ClearOptions>(static_cast<int>(ClearOptions::Target) |
                                               static_cast<int>(ClearOptions::DepthBuffer)),
                     kBlack, 1.0f, 0);

        // Check A -- non-indexed vertex-coloured quad at (10, 10), 40x40.
        DrawQuad(MakeQuad(10.0f, 10.0f, 40.0f, kMidZ, kGreen));
        ExpectPixel("non-indexed vertex-coloured quad renders its vertex colour",
                    Rectangle(30, 30, 1, 1), kGreen);

        // Check B -- the same geometry through the indexed path at (70, 10).
        DrawIndexedQuad({
            VertexPositionColor(Vector3(70.0f, 10.0f, kMidZ), kBlue),
            VertexPositionColor(Vector3(110.0f, 10.0f, kMidZ), kBlue),
            VertexPositionColor(Vector3(110.0f, 50.0f, kMidZ), kBlue),
            VertexPositionColor(Vector3(70.0f, 50.0f, kMidZ), kBlue),
        });
        ExpectPixel("indexed draw renders the same quad", Rectangle(90, 30, 1, 1), kBlue);

        // Check C -- a red DiffuseColor multiplied into a white and then a blue quad.
        effect_->setDiffuseColorProperty(Vector3(1.0f, 0.0f, 0.0f));
        DrawQuad(MakeQuad(130.0f, 10.0f, 40.0f, kMidZ, kWhite));
        ExpectPixel("red DiffuseColor over a white quad gives red",
                    Rectangle(150, 30, 1, 1), kRed);
        DrawQuad(MakeQuad(190.0f, 10.0f, 40.0f, kMidZ, kBlue));
        ExpectPixel("red DiffuseColor over a blue quad gives black",
                    Rectangle(210, 30, 1, 1), kBlack);

        // Check D -- VertexColorEnabled off: the green vertex colour must drop out entirely and
        // leave the red diffuse alone.
        effect_->VertexColorEnabled = false;
        DrawQuad(MakeQuad(250.0f, 10.0f, 40.0f, kMidZ, kGreen));
        ExpectPixel("VertexColorEnabled off renders the diffuse colour alone",
                    Rectangle(270, 30, 1, 1), kRed);

        effect_->VertexColorEnabled = true;
        effect_->setDiffuseColorProperty(Vector3(1.0f, 1.0f, 1.0f));

        // Check E -- depth occlusion. The near green quad is drawn first, then a red quad at the
        // same place but farther away. With DepthStencilState::Default (test on, write on) the
        // far quad must lose; with DepthStencilState::None it must win.
        DrawQuad(MakeQuad(10.0f, 80.0f, 40.0f, kNearZ, kGreen));
        DrawQuad(MakeQuad(10.0f, 80.0f, 40.0f, kFarZ, kRed));
        ExpectPixel("depth test rejects a farther quad drawn later",
                    Rectangle(30, 100, 1, 1), kGreen);

        device.setDepthStencilStateProperty(DepthStencilState::None);
        DrawQuad(MakeQuad(70.0f, 80.0f, 40.0f, kNearZ, kGreen));
        DrawQuad(MakeQuad(70.0f, 80.0f, 40.0f, kFarZ, kRed));
        ExpectPixel("with the depth test off the later quad wins",
                    Rectangle(90, 100, 1, 1), kRed);
        device.setDepthStencilStateProperty(DepthStencilState::Default);

        // Check G -- culling. Under XNA's default CullCounterClockwiseFace a screen-space
        // clockwise triangle is front-facing and must survive, while the same triangle with two
        // corners swapped is counter-clockwise and must vanish. This pair is what caught the
        // renderer's cull-mode mapping being inverted, which rendered every triangle as background.
        const Color kYellow(255, 255, 0, 255);
        DrawQuad({
            VertexPositionColor(Vector3(190.0f, 80.0f, kMidZ), kYellow),
            VertexPositionColor(Vector3(230.0f, 80.0f, kMidZ), kYellow),
            VertexPositionColor(Vector3(230.0f, 120.0f, kMidZ), kYellow),
        });
        ExpectPixel("CullCounterClockwiseFace keeps a clockwise-wound triangle",
                    Rectangle(225, 115, 1, 1), kYellow);

        DrawQuad({
            VertexPositionColor(Vector3(250.0f, 80.0f, kMidZ), kYellow),
            VertexPositionColor(Vector3(250.0f, 120.0f, kMidZ), kYellow),
            VertexPositionColor(Vector3(290.0f, 120.0f, kMidZ), kYellow),
        });
        ExpectPixel("CullCounterClockwiseFace removes a counter-clockwise-wound triangle",
                    Rectangle(255, 115, 1, 1), kBlack);

        // Check F -- the unimplemented textured path must fail loudly, not render something
        // plausible-but-wrong.
        effect_->setLightingEnabledProperty(true);
        bool threw = false;
        try
        {
            DrawQuad(MakeQuad(130.0f, 80.0f, 40.0f, kMidZ, kWhite));
        }
        catch (const std::exception&)
        {
            threw = true;
        }
        effect_->setLightingEnabledProperty(false);
        if (threw)
        {
            std::printf("[PASS] the unimplemented lit 3D path throws instead of rendering wrong\n");
        }
        else
        {
            std::printf("[FAIL] the unimplemented lit 3D path silently rendered something\n");
            MarkFailedEXT();
        }
    }

    Sokol3DTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kBackBufferWidth);
        gdm_->setPreferredBackBufferHeightProperty(kBackBufferHeight);
        gdm_->setSynchronizeWithVerticalRetraceProperty(false);
    }
};

int main() { return CNA::Examples::RunPixelTest<Sokol3DTest>(); }
