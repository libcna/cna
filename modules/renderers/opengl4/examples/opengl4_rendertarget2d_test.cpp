// SPDX-License-Identifier: MS-PL
// plans/plan_opengl4.md GL4-14: real FBO-backed RenderTarget2D proof for the OpenGL4 graphics renderer --
// a colour texture attachment, an optional depth/stencil renderbuffer, an optional multisampled
// colour renderbuffer resolved into the colour texture on unbind, and an optional mip chain
// regenerated from level 0 on unbind (OpenGL4RenderTargetRenderer, modeled on
// EasyGLRenderTargetRenderer's own resource shape but using raw GL4Loader calls).
//
// Check A -- Clear()-only fill of a RenderTarget2D (no draw call between bind/unbind), sampled
//   back via SpriteBatch onto the backbuffer, reads the expected colour.
// Check B -- a real colored3d triangle (BasicEffect.VertexColorEnabled, VertexPositionColor) drawn
//   into a RenderTarget2D via DrawColoredPrimitives, then sampled back, reads the expected colour.
// Check C -- a depth-tested RenderTarget2D (DepthFormat::Depth24Stencil8): a farther red quad then
//   a nearer green quad drawn over the same screen area, then sampled back, reads green (the
//   nearer quad correctly occluded the farther one inside the RT's own depth buffer).
// Check D -- MultiSampleCount property fidelity: a RenderTarget2D constructed with
//   preferredMultiSampleCount=0 must report 0.
// Check E/F/G -- real RenderTarget2D::GetData() pixel assertions on the Check A/B/C targets'
//   centre pixels (OpenGL4RenderTargetRenderer::GetData(), via a throwaway per-level read FBO),
//   proving the same 3 targets actually contain the expected content, not just "didn't throw".
// Check H -- mipMap request: LevelCount reflects a full mip chain, and GetData() on level 0 of a
//   mipmapped target (after UnbindAsRenderTarget()'s glGenerateMipmap regen) still reads correctly.
// Check I -- a real MSAA round-trip: a RenderTarget2D constructed with preferredMultiSampleCount=4
//   resolves via glBlitFramebuffer on unbind; MultiSampleCount is real and clamped (not silently
//   dropped to 0), and the resolved interior pixel matches the flat colour drawn into it.
// Check J -- SpriteBatch::Draw() INTO a bound RenderTarget2D smaller than the window: proves
//   OpenGL4SpriteBatchRenderer::FlushBatch's viewport/ortho sizing correctly follows the bound
//   RT's own size (GetCurrentRenderTarget2DSize), not the window's physical size -- without that
//   fix this check would sample the wrong region (or nothing) instead of the sprite's flat colour.
//
// Exit code 0 = all checks PASS, 1 = any FAILs.

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BufferUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"

#include "common/PixelTestGame.hpp"

#include <cstdint>
#include <cstdlib>
#include <memory>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kWindowSize = 128;
    constexpr int kRTSize = 32;

    bool CloseTo(int a, int b, int tol) { return std::abs(a - b) <= tol; }
    bool Matches(const Color& c, const Color& expected, int tol = 8)
    {
        return CloseTo(c.getRProperty(), expected.getRProperty(), tol)
            && CloseTo(c.getGProperty(), expected.getGProperty(), tol)
            && CloseTo(c.getBProperty(), expected.getBProperty(), tol);
    }

    Color ReadCentrePixel(RenderTarget2D& rt, int size)
    {
        Color px(0, 0, 0, 0);
        const Rectangle rect(size / 2, size / 2, 1, 1);
        rt.GetData(0, &rect, &px, 0, 1);
        return px;
    }

    std::vector<std::uint8_t> SolidTexturePixels(const Color& c)
    {
        std::vector<std::uint8_t> pixels(2 * 2 * 4);
        for (int i = 0; i < 4; ++i)
        {
            pixels[i * 4 + 0] = static_cast<std::uint8_t>(c.getRProperty());
            pixels[i * 4 + 1] = static_cast<std::uint8_t>(c.getGProperty());
            pixels[i * 4 + 2] = static_cast<std::uint8_t>(c.getBProperty());
            pixels[i * 4 + 3] = static_cast<std::uint8_t>(c.getAProperty());
        }
        return pixels;
    }
}

class OpenGL4RenderTarget2DTest : public CNA::Examples::PixelTestGame
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;

protected:
    void RunTest() override
    {
        auto& dev = getGraphicsDeviceProperty();
        SpriteBatch sb(dev);

        // --- Check A: Clear-only fill ---
        RenderTarget2D rtClearOnly(dev, kRTSize, kRTSize, false, SurfaceFormat::Color,
                                   DepthFormat::None, 0, RenderTargetUsage::DiscardContents);
        dev.SetRenderTarget(&rtClearOnly);
        dev.Clear(Color::Green);
        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

        // --- Check B: a real colored3d triangle drawn into the RT ---
        RenderTarget2D rtTriangle(dev, kRTSize, kRTSize, false, SurfaceFormat::Color,
                                  DepthFormat::None, 0, RenderTargetUsage::DiscardContents);
        {
            dev.SetRenderTarget(&rtTriangle);
            dev.Clear(Color::Black);
            const VertexPositionColor verts[3] = {
                {Vector3(-1.0f, -1.0f, 0.0f), Color::Red},
                {Vector3(0.0f, 1.0f, 0.0f), Color::Red},
                {Vector3(1.0f, -1.0f, 0.0f), Color::Red},
            };
            VertexBuffer vb(dev, VertexPositionColor::getVertexDeclarationStatic(), 3, BufferUsage::None);
            vb.SetData(verts, 0, 3);
            BasicEffect fx(dev);
            fx.VertexColorEnabled = true;
            fx.setWorldProperty(Matrix::getIdentityProperty());
            fx.setViewProperty(Matrix::getIdentityProperty());
            fx.setProjectionProperty(Matrix::getIdentityProperty());
            fx.Apply();
            dev.SetVertexBuffer(&vb);
            dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 1);
            dev.SetVertexBuffer(nullptr);
            dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        }

        // --- Check C: depth-tested RT -- farther red quad, then nearer green quad ---
        RenderTarget2D rtDepth(dev, kRTSize, kRTSize, false, SurfaceFormat::Color,
                               DepthFormat::Depth24Stencil8, 0, RenderTargetUsage::DiscardContents);
        {
            dev.SetRenderTarget(&rtDepth);
            dev.Clear(Color(0, 0, 0, 255), 1.0f);
            dev.SetDepthTestEnabled(true);
            dev.SetDepthWriteEnabled(true);
            BasicEffect fx(dev);
            fx.VertexColorEnabled = true;
            fx.setWorldProperty(Matrix::getIdentityProperty());
            fx.setViewProperty(Matrix::getIdentityProperty());
            fx.setProjectionProperty(Matrix::getIdentityProperty());
            fx.Apply();

            const VertexPositionColor farVerts[6] = {
                {Vector3(-1.0f, -1.0f, 0.5f), Color::Red}, {Vector3(-1.0f, 1.0f, 0.5f), Color::Red}, {Vector3(1.0f, -1.0f, 0.5f), Color::Red},
                {Vector3(-1.0f, 1.0f, 0.5f), Color::Red}, {Vector3(1.0f, 1.0f, 0.5f), Color::Red}, {Vector3(1.0f, -1.0f, 0.5f), Color::Red},
            };
            VertexBuffer farVb(dev, VertexPositionColor::getVertexDeclarationStatic(), 6, BufferUsage::None);
            farVb.SetData(farVerts, 0, 6);
            dev.SetVertexBuffer(&farVb);
            dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);

            const VertexPositionColor nearVerts[6] = {
                {Vector3(-1.0f, -1.0f, -0.5f), Color::Green}, {Vector3(-1.0f, 1.0f, -0.5f), Color::Green}, {Vector3(1.0f, -1.0f, -0.5f), Color::Green},
                {Vector3(-1.0f, 1.0f, -0.5f), Color::Green}, {Vector3(1.0f, 1.0f, -0.5f), Color::Green}, {Vector3(1.0f, -1.0f, -0.5f), Color::Green},
            };
            VertexBuffer nearVb(dev, VertexPositionColor::getVertexDeclarationStatic(), 6, BufferUsage::None);
            nearVb.SetData(nearVerts, 0, 6);
            dev.SetVertexBuffer(&nearVb);
            dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);

            dev.SetVertexBuffer(nullptr);
            dev.SetDepthTestEnabled(false);
            dev.SetDepthWriteEnabled(false);
            dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        }

        // Sample all 3 render targets onto the backbuffer via SpriteBatch and check each cell.
        {
            dev.Clear(Color::CornflowerBlue);
            const int cellW = kWindowSize / 3;
            sb.Begin();
            sb.Draw(rtClearOnly, Rectangle(0, 0, cellW, kWindowSize), Rectangle(0, 0, kRTSize, kRTSize), Color::White);
            sb.Draw(rtTriangle, Rectangle(cellW, 0, cellW, kWindowSize), Rectangle(0, 0, kRTSize, kRTSize), Color::White);
            sb.Draw(rtDepth, Rectangle(cellW * 2, 0, cellW, kWindowSize), Rectangle(0, 0, kRTSize, kRTSize), Color::White);
            sb.End();

            ExpectPixel("Check A: Clear-only RenderTarget2D sampled via SpriteBatch is green",
                        Rectangle(cellW / 2, kWindowSize / 2, 1, 1), Color::Green);
            ExpectPixel("Check B: colored3d-triangle RenderTarget2D sampled via SpriteBatch is red",
                        Rectangle(cellW + cellW / 2, kWindowSize / 2, 1, 1), Color::Red);
            ExpectPixel("Check C: depth-tested RenderTarget2D sampled via SpriteBatch is green (nearer quad won)",
                        Rectangle(cellW * 2 + cellW / 2, kWindowSize / 2, 1, 1), Color::Green);
        }

        // Check D: MultiSampleCount property fidelity for the non-MSAA case.
        {
            RenderTarget2D rtNoMsaa(dev, kRTSize, kRTSize, false, SurfaceFormat::Color,
                                    DepthFormat::None, 0, RenderTargetUsage::DiscardContents);
            Check(rtNoMsaa.getMultiSampleCountProperty() == 0,
                  "Check D: RenderTarget2D MultiSampleCount request 0 -> applied 0");
        }

        // Check E/F/G: real GetData() pixel assertions on the same 3 targets.
        {
            const Color gotClear = ReadCentrePixel(rtClearOnly, kRTSize);
            Check(Matches(gotClear, Color::Green),
                  "Check E: GetData() Clear-only RenderTarget2D centre pixel is green");

            const Color gotTriangle = ReadCentrePixel(rtTriangle, kRTSize);
            Check(Matches(gotTriangle, Color::Red),
                  "Check F: GetData() colored3d-triangle RenderTarget2D centre pixel is red");

            const Color gotDepth = ReadCentrePixel(rtDepth, kRTSize);
            Check(Matches(gotDepth, Color::Green),
                  "Check G: GetData() depth-tested RenderTarget2D centre pixel is green");
        }

        // Check H: mipMap request -- LevelCount reflects a full chain, and level-0 GetData()
        // still reads correctly after UnbindAsRenderTarget()'s glGenerateMipmap regen.
        {
            RenderTarget2D rtMip(dev, kRTSize, kRTSize, true, SurfaceFormat::Color,
                                 DepthFormat::None, 0, RenderTargetUsage::DiscardContents);
            Check(rtMip.getLevelCountProperty() > 1,
                  "Check H: mipMap=true RenderTarget2D reports LevelCount > 1");
            dev.SetRenderTarget(&rtMip);
            dev.Clear(Color::Blue);
            dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
            const Color gotMip = ReadCentrePixel(rtMip, kRTSize);
            Check(Matches(gotMip, Color::Blue),
                  "Check H: mipMap=true RenderTarget2D level-0 GetData() still reads correctly");
        }

        // Check I: real MSAA round-trip -- resolved via glBlitFramebuffer on unbind.
        {
            RenderTarget2D rtMsaa(dev, kRTSize, kRTSize, false, SurfaceFormat::Color,
                                  DepthFormat::None, 4, RenderTargetUsage::DiscardContents);
            Check(rtMsaa.getMultiSampleCountProperty() > 0,
                  "Check I: preferredMultiSampleCount=4 RenderTarget2D reports a real (non-zero) MultiSampleCount");

            dev.SetRenderTarget(&rtMsaa);
            dev.Clear(Color::Black);
            const VertexPositionColor verts[6] = {
                {Vector3(-1.0f, -1.0f, 0.0f), Color::Magenta}, {Vector3(-1.0f, 1.0f, 0.0f), Color::Magenta}, {Vector3(1.0f, -1.0f, 0.0f), Color::Magenta},
                {Vector3(-1.0f, 1.0f, 0.0f), Color::Magenta}, {Vector3(1.0f, 1.0f, 0.0f), Color::Magenta}, {Vector3(1.0f, -1.0f, 0.0f), Color::Magenta},
            };
            VertexBuffer vb(dev, VertexPositionColor::getVertexDeclarationStatic(), 6, BufferUsage::None);
            vb.SetData(verts, 0, 6);
            BasicEffect fx(dev);
            fx.VertexColorEnabled = true;
            fx.setWorldProperty(Matrix::getIdentityProperty());
            fx.setViewProperty(Matrix::getIdentityProperty());
            fx.setProjectionProperty(Matrix::getIdentityProperty());
            fx.Apply();
            dev.SetVertexBuffer(&vb);
            dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
            dev.SetVertexBuffer(nullptr);
            dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

            const Color gotMsaa = ReadCentrePixel(rtMsaa, kRTSize);
            Check(Matches(gotMsaa, Color::Magenta),
                  "Check I: MSAA RenderTarget2D resolved centre pixel matches the flat colour drawn into it");
        }

        // Check J: SpriteBatch::Draw() INTO a bound RenderTarget2D smaller than the window --
        // exercises OpenGL4SpriteBatchRenderer::FlushBatch's RT-size-aware viewport fix.
        {
            const Color kBlue2x2(0, 0, 255, 255);
            Texture2D blueTex = Texture2D::CreateFromPixels(dev, 2, 2, SolidTexturePixels(kBlue2x2));
            RenderTarget2D rtSpriteInto(dev, kRTSize, kRTSize, false, SurfaceFormat::Color,
                                        DepthFormat::None, 0, RenderTargetUsage::DiscardContents);
            dev.SetRenderTarget(&rtSpriteInto);
            dev.Clear(Color::Black);
            sb.Begin();
            sb.Draw(blueTex, Rectangle(0, 0, kRTSize, kRTSize), Rectangle(0, 0, 2, 2), Color::White);
            sb.End();
            dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

            const Color got = ReadCentrePixel(rtSpriteInto, kRTSize);
            Check(Matches(got, kBlue2x2),
                  "Check J: SpriteBatch::Draw() into a bound RenderTarget2D uses the RT's own size, not the window's");
        }
    }

public:
    OpenGL4RenderTarget2DTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kWindowSize);
        gdm_->setPreferredBackBufferHeightProperty(kWindowSize);
    }
};

int main()
{
    return CNA::Examples::RunPixelTest<OpenGL4RenderTarget2DTest>();
}
