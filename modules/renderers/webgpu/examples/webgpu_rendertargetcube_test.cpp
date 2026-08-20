// SPDX-License-Identifier: MS-PL
// WEBGPU-114: verify WebGPURenderTargetCubeRenderer / WebGPURenderer::CreateRenderTargetCube()
// -- this renderer's first real render-into-a-cube-face support. Before this task,
// CreateRenderTargetCube on this renderer was IGraphicsRenderer's own safe nullptr-returning default,
// so any game creating a RenderTargetCube on WEBGPU (e.g. for a dynamic reflection/environment map)
// got a null renderer handle.
//
// Architecture note: this renderer renders every queued Clear()/3D-draw/SpriteBatch command into
// exactly ONE deferred render pass per logical frame, lazily flushed the instant the bound target
// changes (see WebGPURenderTargetRenderer/WebGPURenderer::SetRenderTarget2D()'s own doc
// comments, WEBGPU-53/54). RenderTargetCube extends the same "flush on target switch" design so
// each of the 6 faces acts as its own distinct target identity -- WebGPURenderer::
// FlushCurrentRenderTarget()/currentRenderTargetCubeFace_ generalise the pre-existing
// RenderTarget2D-only switch logic to also cover a cube face (see WebGPURenderTargetCubeRenderer::
// BindAsRenderTargetFace()'s own comment).
//
// Check A -- each of the 6 faces bound DIRECTLY one after another (face i -> face i+1, no
//   intervening SetRenderTarget(nullptr)) and Clear()ed to its own distinct colour, then read back
//   via TextureCube::GetData() after finally unbinding: every face reads back exactly its own
//   colour, none other -- proves per-face addressing is correct AND that switching directly
//   between two DIFFERENT faces (not just face<->backbuffer) genuinely flushes the outgoing face's
//   own render pass instead of collapsing all 6 Clear()s into whichever face happens to be bound
//   when something finally reads back.
// Check B -- a real BasicEffect/VertexPositionColor quad drawn into one face, read back via
//   GetData(): proves a genuine 3D draw dispatch reaches a cube face render target, not just
//   Clear().
// Check C -- EnvironmentMapEffect sampling round trip (mirrors vulkan_rendertargetcube_sample_test.
//   cpp, adapted to a real 3D draw instead of SpriteBatch -- see this check's own in-body comment
//   for why): solid blue drawn into all 6 faces, unbound, then sampled back as
//   EnvironmentMapEffect.EnvironmentMap on a full-screen quad -- centre pixel must be blue. This
//   specifically exercises IWebGPUCubeSamplable/ResolveCubeSamplable(): before this task,
//   GpuDrawParams::envMap was resolved via a dynamic_cast to the concrete WebGPUTextureCubeRenderer
//   type only, so a WebGPURenderTargetCubeRenderer (a different concrete class) would have silently
//   failed that cast and rendered the 1x1 white-cube fallback instead -- this check would fail
//   (read back white/grey, not blue) if that wiring regressed.
// Check D -- the critical architecture check (mirrors webgpu_rendertarget2d_test.cpp's own Check
//   E): Clear(Blue) on the backbuffer, THEN SetRenderTarget(cube, face)+Clear(Red)+
//   SetRenderTarget(nullptr) targeting a cube face, then read back the BACKBUFFER's centre pixel
//   -- must still be Blue (the intervening cube-face-targeted Clear() must not leak into the
//   backbuffer's own render pass), AND the cube face itself must read back Red.
// Check E -- CreateRenderTargetCube(mipMap=true) throws a clear exception rather than silently
//   creating a single-level target while the XNA layer expects a full mip chain (a deliberate,
//   documented scope cut -- see plans/plan_webgpu.md WEBGPU-114).
// Check F -- MultiSampleCount property fidelity: MSAA is not implemented for RenderTargetCube on
//   this renderer (a deliberate, documented scope cut, separate from RenderTarget2D's own real MSAA
//   support) -- requesting 4 must still honestly report 0.
//
// Exit code 0 = all checks PASS, 1 = any FAILs.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/CubeMapFace.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/EnvironmentMapEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetCube.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionNormalTexture.hpp"

#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kCubeSize = 32;
    constexpr int kBackbufferSize = 64;

    int passCount = 0;
    int totalCount = 0;

    void check(bool ok, const std::string& label)
    {
        ++totalCount;
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label.c_str());
        if (ok) ++passCount;
    }

    bool CloseTo(int a, int b, int tol) { return std::abs(a - b) <= tol; }
    bool Matches(const Color& c, const Color& expected, int tol = 12)
    {
        return CloseTo(c.getRProperty(), expected.getRProperty(), tol)
            && CloseTo(c.getGProperty(), expected.getGProperty(), tol)
            && CloseTo(c.getBProperty(), expected.getBProperty(), tol);
    }

    std::string ColorStr(const Color& c)
    {
        return "(" + std::to_string(c.getRProperty()) + "," + std::to_string(c.getGProperty()) +
               "," + std::to_string(c.getBProperty()) + ")";
    }

    Color ReadFaceCentre(RenderTargetCube& rtc, CubeMapFace face)
    {
        std::vector<Color> pix(static_cast<std::size_t>(kCubeSize) * kCubeSize, Color(0, 0, 0, 0));
        rtc.GetData(face, pix.data(), static_cast<int>(pix.size()));
        return pix[(kCubeSize / 2) * kCubeSize + kCubeSize / 2];
    }

    Color ReadBackbufferCentre(GraphicsDevice& dev)
    {
        const Rectangle rect(kBackbufferSize / 2, kBackbufferSize / 2, 1, 1);
        Color px(0, 0, 0, 0);
        dev.GetBackBufferData(&rect, &px, 0, 1);
        return px;
    }

    void ApplyBasicEffect(GraphicsDevice& dev)
    {
        static BasicEffect* fx = nullptr;
        if (fx == nullptr) fx = new BasicEffect(dev);
        fx->setWorldProperty(Matrix::getIdentityProperty());
        fx->setViewProperty(Matrix::getIdentityProperty());
        fx->setProjectionProperty(Matrix::getIdentityProperty());
        fx->VertexColorEnabled = true;
        fx->Apply();
    }

    // Full-screen quad (clip-space -1..1), single solid colour -- renders into whichever render
    // target is currently bound (a RenderTargetCube face here).
    void DrawFullScreenQuad(GraphicsDevice& dev, const Color& color)
    {
        const VertexPositionColor verts[6] = {
            { Vector3(-1.0f,  1.0f, 0.0f), color },
            { Vector3(-1.0f, -1.0f, 0.0f), color },
            { Vector3( 1.0f, -1.0f, 0.0f), color },
            { Vector3(-1.0f,  1.0f, 0.0f), color },
            { Vector3( 1.0f, -1.0f, 0.0f), color },
            { Vector3( 1.0f,  1.0f, 0.0f), color },
        };
        ApplyBasicEffect(dev);
        dev.DrawUserPrimitives(PrimitiveType::TriangleList, verts, 0, 2);
    }

    const CubeMapFace kFaces[6] = {
        CubeMapFace::PositiveX, CubeMapFace::NegativeX,
        CubeMapFace::PositiveY, CubeMapFace::NegativeY,
        CubeMapFace::PositiveZ, CubeMapFace::NegativeZ,
    };
    const char* kFaceNames[6] = { "PositiveX", "NegativeX", "PositiveY", "NegativeY", "PositiveZ", "NegativeZ" };
    const Color kFaceColors[6] = {
        Color(255, 0, 0, 255),     // +X red
        Color(0, 255, 0, 255),     // -X green
        Color(0, 0, 255, 255),     // +Y blue
        Color(255, 255, 0, 255),   // -Y yellow
        Color(255, 0, 255, 255),   // +Z magenta
        Color(0, 255, 255, 255),   // -Z cyan
    };
}

class WebGpuRenderTargetCubeTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    bool done_ = false;
    int result_ = 1;

protected:
    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;

        auto& dev = getGraphicsDeviceProperty();
        dev.setRasterizerStateProperty(RasterizerState::CullNone);

        // Check A: 6 faces, bound DIRECTLY one after another (no intervening backbuffer switch),
        // each Clear()ed to its own distinct colour.
        {
            RenderTargetCube rtc(dev, kCubeSize, false, SurfaceFormat::Color, DepthFormat::None);
            for (int f = 0; f < 6; ++f)
            {
                dev.SetRenderTarget(&rtc, kFaces[f]);
                dev.Clear(kFaceColors[f]);
            }
            dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

            for (int f = 0; f < 6; ++f)
            {
                const Color got = ReadFaceCentre(rtc, kFaces[f]);
                check(Matches(got, kFaceColors[f]),
                      std::string("Check A: face ") + kFaceNames[f] +
                      " reads back its own Clear() colour after direct face-to-face switching, "
                      "not another face's: got=" + ColorStr(got));
            }
        }

        // Check B: a real BasicEffect quad drawn into one face.
        {
            RenderTargetCube rtc(dev, kCubeSize, false, SurfaceFormat::Color, DepthFormat::None);
            dev.SetRenderTarget(&rtc, CubeMapFace::PositiveY);
            dev.Clear(Color::Black);
            DrawFullScreenQuad(dev, Color::Red);
            dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

            const Color got = ReadFaceCentre(rtc, CubeMapFace::PositiveY);
            check(Matches(got, Color::Red),
                  "Check B: BasicEffect quad drawn into RenderTargetCube face PositiveY reads "
                  "back Red via GetData(): got=" + ColorStr(got));
        }

        // Check C: EnvironmentMapEffect sampling round trip -- proves IWebGPUCubeSamplable
        // resolves a WebGPURenderTargetCubeRenderer, not just a WebGPUTextureCubeRenderer.
        //
        // NOTE (real finding, out of scope here): the 6 faces below are painted via
        // DrawFullScreenQuad() (a real 3D/BasicEffect draw), NOT SpriteBatch, deliberately.
        // WebGPURenderer::QueueSprite() computes each sprite's clip-space geometry from
        // ComputeLogicalViewport(), which is always derived from the BACKBUFFER's own
        // virtualWidth_/virtualHeight_/physicalWidth_/physicalHeight_ -- never from whatever
        // render target (a RenderTarget2D OR a RenderTargetCube face) happens to be currently
        // bound. A SpriteBatch.Draw() issued while a smaller/differently-sized off-screen target
        // is bound therefore maps its destination rectangle against the WRONG (backbuffer)
        // logical size, so it does not necessarily cover the whole bound target the way a caller
        // would expect (confirmed empirically: with this test's 64x64 backbuffer and 32x32 cube
        // faces, a SpriteBatch Draw(dest=(0,0,32,32)) only covered one quadrant of each face,
        // leaving the centre pixel un-painted). This is a pre-existing renderer-wide gap (equally
        // true for RenderTarget2D, not introduced by RenderTargetCube/WEBGPU-114), previously
        // untested because no existing WebGPU test draws INTO an off-screen target via
        // SpriteBatch (webgpu_rendertarget2d_test.cpp's own SpriteBatch check only samples FROM
        // render targets onto the backbuffer). Left unfixed here as a separate, real, honestly-
        // reported finding -- see plans/plan_webgpu.md's WEBGPU-114 row.
        {
            RenderTargetCube rtc(dev, kCubeSize, false, SurfaceFormat::Color, DepthFormat::None);
            const std::vector<std::uint8_t> white = { 255, 255, 255, 255 };
            Texture2D whiteTex = Texture2D::CreateFromPixels(dev, 1, 1, white);

            for (int f = 0; f < 6; ++f)
            {
                dev.SetRenderTarget(&rtc, kFaces[f]);
                dev.Clear(Color::Black);
                DrawFullScreenQuad(dev, Color::Blue);
            }
            dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

            dev.Clear(Color::Black);
            EnvironmentMapEffect fx(dev);
            fx.setDiffuseColorProperty(Vector3(1.0f, 1.0f, 1.0f));
            fx.setEmissiveColorProperty(Vector3(0.0f, 0.0f, 0.0f));
            fx.setEnvironmentMapAmountProperty(1.0f);
            fx.setEnvironmentMapSpecularProperty(Vector3(0.0f, 0.0f, 0.0f));
            fx.setTextureProperty(&whiteTex);
            fx.setEnvironmentMapProperty(&rtc);
            fx.setWorldProperty(Matrix::getIdentityProperty());
            fx.setViewProperty(Matrix::getIdentityProperty());
            fx.setProjectionProperty(Matrix::getIdentityProperty());
            fx.Apply();

            const Vector3 n(0.0f, 0.0f, 1.0f);
            const VertexPositionNormalTexture verts[6] = {
                { Vector3(-1.0f,  1.0f, 0.0f), n, Vector2(0.0f, 1.0f) },
                { Vector3(-1.0f, -1.0f, 0.0f), n, Vector2(0.0f, 0.0f) },
                { Vector3( 1.0f, -1.0f, 0.0f), n, Vector2(1.0f, 0.0f) },
                { Vector3(-1.0f,  1.0f, 0.0f), n, Vector2(0.0f, 1.0f) },
                { Vector3( 1.0f, -1.0f, 0.0f), n, Vector2(1.0f, 0.0f) },
                { Vector3( 1.0f,  1.0f, 0.0f), n, Vector2(1.0f, 1.0f) },
            };
            // Defensive re-apply (CullNone is already set at the top of Draw() and nothing in
            // this file resets it) -- kept explicit since vulkan_rendertargetcube_sample_test.cpp's
            // own Task 896 finding is that this exact quad's winding is back-facing under the
            // real default RasterizerState.
            dev.setRasterizerStateProperty(RasterizerState::CullNone);
            dev.DrawUserPrimitives(PrimitiveType::TriangleList, verts, 0, 2);

            const Color got = ReadBackbufferCentre(dev);
            check(Matches(got, Color::Blue, 60),
                  "Check C: EnvironmentMapEffect samples a RenderTargetCube (rendered all-blue) "
                  "and the backbuffer centre pixel reads back blue, not the 1x1 white-cube "
                  "fallback: got=" + ColorStr(got));
        }

        // Check D: the critical architecture check -- an intervening cube-face-targeted Clear()
        // must not leak into the backbuffer's own render pass.
        {
            dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
            dev.Clear(Color::Blue);

            RenderTargetCube rtc(dev, kCubeSize, false, SurfaceFormat::Color, DepthFormat::None);
            dev.SetRenderTarget(&rtc, CubeMapFace::PositiveZ);
            dev.Clear(Color::Red);
            dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

            const Color gotBack = ReadBackbufferCentre(dev);
            check(Matches(gotBack, Color::Blue),
                  "Check D: backbuffer stays Blue across an intervening RenderTargetCube-face-"
                  "targeted Clear(Red) -- proves target separation: got=" + ColorStr(gotBack));

            const Color gotFace = ReadFaceCentre(rtc, CubeMapFace::PositiveZ);
            check(Matches(gotFace, Color::Red),
                  "Check D (other half): the cube face itself really did receive Clear(Red): got=" +
                  ColorStr(gotFace));
        }

        // Check E: mipMap=true is a deliberate, documented scope cut -- must throw.
        {
            bool threw = false;
            try
            {
                RenderTargetCube rtcMip(dev, kCubeSize, true, SurfaceFormat::Color, DepthFormat::None);
            }
            catch (const std::exception&)
            {
                threw = true;
            }
            check(threw, "Check E: RenderTargetCube(mipMap=true) throws a clear exception "
                        "(mip regeneration not yet implemented, WEBGPU-114)");
        }

        // Check F: MultiSampleCount property fidelity -- MSAA is not implemented for
        // RenderTargetCube on this renderer; requesting 4 must still honestly report 0.
        {
            RenderTargetCube rtcMsaa(dev, kCubeSize, false, SurfaceFormat::Color, DepthFormat::None, 4);
            check(rtcMsaa.getMultiSampleCountProperty() == 0,
                  "Check F: RenderTargetCube MultiSampleCount request 4 -> honestly reports 0 "
                  "(MSAA not implemented for RenderTargetCube on this renderer, see WEBGPU-114)");
        }

        std::printf("=== %d/%d PASS ===\n", passCount, totalCount);
        result_ = (passCount == totalCount) ? 0 : 1;
        Exit();
    }

public:
    WebGpuRenderTargetCubeTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kBackbufferSize);
        gdm_->setPreferredBackBufferHeightProperty(kBackbufferSize);
    }

    int getResult() const { return result_; }
};

int main()
{
    WebGpuRenderTargetCubeTest game;
    game.Run();
    return game.getResult();
}
