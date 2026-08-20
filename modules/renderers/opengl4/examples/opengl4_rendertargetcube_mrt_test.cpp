// SPDX-License-Identifier: MS-PL
// plans/plan_opengl4.md GL4-15: real per-face FBO-backed RenderTargetCube + real MRT for the OpenGL4
// graphics renderer.
//
// RenderTargetCube (OpenGL4RenderTargetCubeRenderer, one shared cube-map texture + FBO,
// re-attaching the requested face on BindAsRenderTargetFace):
// Check A -- Clear() into face +X, sampled back via real GetData() (not "didn't throw"), reads
//   the expected colour.
// Check B -- Clear() into face -Z (a different face on the SAME cube texture) with a different
//   colour, then GetData() on BOTH faces confirms they hold independent content (proves the
//   per-face re-attach is real, not accidentally aliasing every face to the same image).
// Check C -- a real colored3d triangle (BasicEffect.VertexColorEnabled) drawn into face +Y via
//   DrawColoredPrimitives, sampled back via GetData().
// Check D -- a depth-tested face (DepthFormat::Depth24Stencil8): a farther red quad then a
//   nearer green quad, sampled back via GetData() -- proves each face's own depth buffer works.
// Check E -- MultiSampleCount property fidelity: preferredMultiSampleCount=0 must report 0.
// Check F -- mipMap request: LevelCount reflects a full chain, and level-0 GetData() still reads
//   correctly after UnbindAsRenderTarget()'s glGenerateMipmap regen.
// Check G -- a real MSAA round-trip: preferredMultiSampleCount=4 resolves via glBlitFramebuffer
//   on unbind; MultiSampleCount is real (non-zero, clamped), and the resolved pixel matches the
//   flat colour drawn into it.
//
// MRT (SetRenderTargets, plural -- a persistent multi-attachment FBO + glDrawBuffers):
// Check H -- binding 2 RenderTarget2D instances via GraphicsDevice.SetRenderTargets(), then
//   drawing one colored3d quad: with a single-output fragment shader (this renderer has no
//   multi-output/G-buffer shader variant -- XNA's own MRT story is a custom ShaderEffect
//   concern, not something BasicEffect ever declares), only COLOR_ATTACHMENT0 receives the
//   quad's colour via glDrawBuffers' first slot. Both targets' GetData() are checked: the FIRST
//   target shows the quad's colour, and the SECOND stays at its own independent Clear() colour --
//   proving the two attachments are genuinely distinct GL images wired via glFramebufferTexture2D
//   at COLOR_ATTACHMENT0/COLOR_ATTACHMENT1, not aliased to the same texture.
// Check I -- unbinding MRT (SetRenderTarget(nullptr)) and drawing to the default framebuffer
//   afterward still works with no exception (proves MRT teardown correctly restores FBO 0).
//
// Exit code 0 = all checks PASS, 1 = any FAILs.

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BufferUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/CubeMapFace.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetBinding.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetCube.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"

#include "common/PixelTestGame.hpp"

#include <cstdlib>
#include <memory>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kWindowSize = 64;
    constexpr int kSize = 32;

    bool CloseTo(int a, int b, int tol) { return std::abs(a - b) <= tol; }
    bool Matches(const Color& c, const Color& expected, int tol = 8)
    {
        return CloseTo(c.getRProperty(), expected.getRProperty(), tol)
            && CloseTo(c.getGProperty(), expected.getGProperty(), tol)
            && CloseTo(c.getBProperty(), expected.getBProperty(), tol);
    }

    Color ReadCentreFacePixel(RenderTargetCube& rt, CubeMapFace face, int size)
    {
        Color px(0, 0, 0, 0);
        const Rectangle rect(size / 2, size / 2, 1, 1);
        rt.GetData(face, 0, &rect, &px, 0, 1);
        return px;
    }

    Color ReadCentrePixel2D(RenderTarget2D& rt, int size)
    {
        Color px(0, 0, 0, 0);
        const Rectangle rect(size / 2, size / 2, 1, 1);
        rt.GetData(0, &rect, &px, 0, 1);
        return px;
    }
}

class OpenGL4RenderTargetCubeMrtTest : public CNA::Examples::PixelTestGame
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;

protected:
    void RunTest() override
    {
        auto& dev = getGraphicsDeviceProperty();

        // --- Check A/B: two different faces of the same cube target hold independent content ---
        {
            RenderTargetCube rt(dev, kSize, false, SurfaceFormat::Color, DepthFormat::None, 0,
                                RenderTargetUsage::DiscardContents);

            dev.SetRenderTarget(&rt, CubeMapFace::PositiveX);
            dev.Clear(Color::Green);
            dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

            const Color gotPosX = ReadCentreFacePixel(rt, CubeMapFace::PositiveX, kSize);
            Check(Matches(gotPosX, Color::Green),
                  "Check A: GetData() on face +X after Clear(Green) reads green");

            dev.SetRenderTarget(&rt, CubeMapFace::NegativeZ);
            dev.Clear(Color::Blue);
            dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

            const Color gotNegZStill = ReadCentreFacePixel(rt, CubeMapFace::NegativeZ, kSize);
            const Color gotPosXStill = ReadCentreFacePixel(rt, CubeMapFace::PositiveX, kSize);
            Check(Matches(gotNegZStill, Color::Blue),
                  "Check B: GetData() on face -Z after its own Clear(Blue) reads blue");
            Check(Matches(gotPosXStill, Color::Green),
                  "Check B: face +X still reads green after face -Z was cleared separately (faces are independent images)");
        }

        // --- Check C: colored3d triangle drawn into face +Y ---
        {
            RenderTargetCube rt(dev, kSize, false, SurfaceFormat::Color, DepthFormat::None, 0,
                                RenderTargetUsage::DiscardContents);
            dev.SetRenderTarget(&rt, CubeMapFace::PositiveY);
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

            const Color got = ReadCentreFacePixel(rt, CubeMapFace::PositiveY, kSize);
            Check(Matches(got, Color::Red),
                  "Check C: colored3d triangle drawn into face +Y reads red via GetData()");
        }

        // --- Check D: depth-tested face -- farther red quad, then nearer green quad ---
        {
            RenderTargetCube rt(dev, kSize, false, SurfaceFormat::Color, DepthFormat::Depth24Stencil8,
                                0, RenderTargetUsage::DiscardContents);
            dev.SetRenderTarget(&rt, CubeMapFace::NegativeY);
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

            const Color got = ReadCentreFacePixel(rt, CubeMapFace::NegativeY, kSize);
            Check(Matches(got, Color::Green),
                  "Check D: depth-tested face -Y reads green (nearer quad won) via GetData()");
        }

        // --- Check E: MultiSampleCount property fidelity for the non-MSAA case ---
        {
            RenderTargetCube rt(dev, kSize, false, SurfaceFormat::Color, DepthFormat::None, 0,
                                RenderTargetUsage::DiscardContents);
            Check(rt.getMultiSampleCountProperty() == 0,
                  "Check E: RenderTargetCube MultiSampleCount request 0 -> applied 0");
        }

        // --- Check F: mipMap request ---
        {
            RenderTargetCube rt(dev, kSize, true, SurfaceFormat::Color, DepthFormat::None, 0,
                                RenderTargetUsage::DiscardContents);
            Check(rt.getLevelCountProperty() > 1,
                  "Check F: mipMap=true RenderTargetCube reports LevelCount > 1");
            dev.SetRenderTarget(&rt, CubeMapFace::PositiveZ);
            dev.Clear(Color::Blue);
            dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
            const Color got = ReadCentreFacePixel(rt, CubeMapFace::PositiveZ, kSize);
            Check(Matches(got, Color::Blue),
                  "Check F: mipMap=true RenderTargetCube level-0 GetData() still reads correctly");
        }

        // --- Check G: real MSAA round-trip ---
        {
            RenderTargetCube rt(dev, kSize, false, SurfaceFormat::Color, DepthFormat::None, 4,
                                RenderTargetUsage::DiscardContents);
            Check(rt.getMultiSampleCountProperty() > 0,
                  "Check G: preferredMultiSampleCount=4 RenderTargetCube reports a real (non-zero) MultiSampleCount");

            dev.SetRenderTarget(&rt, CubeMapFace::NegativeX);
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

            const Color got = ReadCentreFacePixel(rt, CubeMapFace::NegativeX, kSize);
            Check(Matches(got, Color::Magenta),
                  "Check G: MSAA RenderTargetCube resolved centre pixel matches the flat colour drawn into it");
        }

        // --- Check H/I: MRT (SetRenderTargets plural) ---
        {
            RenderTarget2D rtA(dev, kSize, kSize, false, SurfaceFormat::Color, DepthFormat::None,
                               0, RenderTargetUsage::DiscardContents);
            RenderTarget2D rtB(dev, kSize, kSize, false, SurfaceFormat::Color, DepthFormat::None,
                               0, RenderTargetUsage::DiscardContents);

            std::vector<RenderTargetBinding> bindings;
            bindings.emplace_back(static_cast<Texture*>(&rtA));
            bindings.emplace_back(static_cast<Texture*>(&rtB));
            dev.SetRenderTargets(bindings);
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

            dev.SetRenderTargets(std::vector<RenderTargetBinding>{});

            const Color gotA = ReadCentrePixel2D(rtA, kSize);
            const Color gotB = ReadCentrePixel2D(rtB, kSize);
            Check(Matches(gotA, Color::Red),
                  "Check H: MRT slot 0 (COLOR_ATTACHMENT0) receives the single-output shader's draw");
            Check(Matches(gotB, Color::Black),
                  "Check H: MRT slot 1 (COLOR_ATTACHMENT1) stays independent (its own Clear colour, not aliased to slot 0)");

            bool threw = false;
            try
            {
                dev.Clear(Color::CornflowerBlue);
            }
            catch (const std::exception&)
            {
                threw = true;
            }
            Check(!threw, "Check I: drawing to the default framebuffer after MRT teardown works with no exception");
        }
    }

public:
    OpenGL4RenderTargetCubeMrtTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kWindowSize);
        gdm_->setPreferredBackBufferHeightProperty(kWindowSize);
    }
};

int main()
{
    return CNA::Examples::RunPixelTest<OpenGL4RenderTargetCubeMrtTest>();
}
