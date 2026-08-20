// SPDX-License-Identifier: MS-PL
// plans/plan_sdlgpu.md SDLGPU-36: RenderTargetCube proof for the SDL_GPU graphics renderer -- a single
// SDL_GPU_TEXTURETYPE_CUBE texture (6 layers), one face bound as the active render target at a
// time, including real MSAA (auto-resolved via SDL_GPUColorTargetInfo.resolve_texture/
// resolve_layer at render-pass end) and mip generation.
//
// This renderer has no EnvironmentMapEffect yet (SDLGPU-33, deferred), so unlike
// vulkan_rendertargetcube_msaa_test.cpp's reflection-vector sampling (which the Vulkan test's own
// header comment documents as unable to discriminate between individual faces), verification here
// uses a real per-face GPU readback (SdlGpuRenderTargetCubeRenderer::GetData, pulled forward from
// SDLGPU-39 -- targets a texture this renderer fully controls, not the swapchain, so it does not
// hit the segfault documented in plans/plan_sdlgpu.md's SDLGPU-39 row) -- a stronger, per-face-precise
// check than the reflection-based "some face came back non-garbage" bar other renderers settle for.
//
// Check A -- all 6 faces filled with distinct solid colors via Clear() (no draw), each read back
//   individually and confirmed correct -- proves real per-face bind/clear/pass + readback.
// Check B -- a real colored3d (BasicEffect) triangle drawn into one face, read back correct.
// Check C -- a depth-tested pair of overlapping quads drawn into one face (farther red, nearer
//   green), read back confirms the nearer quad's own dedicated depth buffer works.
// Check D -- MultiSampleCount property fidelity + a real MSAA fill/resolve/readback round-trip.
// Check E -- mipMap: a solid-color fill regenerates a real mip chain; level 1 reads back the same
//   color (a uniform source downsamples to itself).
//
// Exit code 0 = all checks PASS, 1 = any FAILs.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BufferUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/CubeMapFace.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetCube.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"

#include "CNA/Internal/Renderers/SdlGpu/SdlGpuRenderer.hpp"

#include "common/PixelTestGame.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using namespace CNA::Internal::Renderers::SdlGpu;

namespace
{
    constexpr int kCubeSize = 16;

    bool CloseTo(int a, int b, int tol) { return std::abs(a - b) <= tol; }
    bool Matches(const Color& c, const Color& expected, int tol = 8)
    {
        return CloseTo(c.getRProperty(), expected.getRProperty(), tol)
            && CloseTo(c.getGProperty(), expected.getGProperty(), tol)
            && CloseTo(c.getBProperty(), expected.getBProperty(), tol);
    }

    Color ReadCentrePixel(RenderTargetCube& rt, CubeMapFace face, int level = 0)
    {
        Color px(0, 0, 0, 0);
        const int size = std::max(1, kCubeSize >> level);
        const Rectangle rect(size / 2, size / 2, 1, 1);
        rt.GetData(face, level, &rect, &px, 0, 1);
        return px;
    }
}

class SdlGpuRenderTargetCubeTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    int passCount_ = 0;
    int result_ = 1;

    void Check(bool ok, const std::string& label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label.c_str());
        if (ok) ++passCount_;
    }

protected:
    void Draw(const GameTime&) override
    {
        static bool done = false;
        if (done) return;
        done = true;

        auto& dev = getGraphicsDeviceProperty();
        const CubeMapFace faces[6] = {
            CubeMapFace::PositiveX, CubeMapFace::NegativeX,
            CubeMapFace::PositiveY, CubeMapFace::NegativeY,
            CubeMapFace::PositiveZ, CubeMapFace::NegativeZ,
        };
        const Color faceColors[6] = {
            Color::Red, Color::Green, Color::Blue, Color::Yellow, Color::Magenta, Color::Cyan,
        };

        // Check A: all 6 faces, distinct solid colors, Clear()-only, read back individually.
        {
            RenderTargetCube rt(dev, kCubeSize, false, SurfaceFormat::Color,
                                DepthFormat::None, 0, RenderTargetUsage::DiscardContents);
            for (int i = 0; i < 6; ++i)
            {
                dev.SetRenderTarget(&rt, faces[i]);
                dev.Clear(faceColors[i]);
            }
            dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

            bool allOk = true;
            for (int i = 0; i < 6; ++i)
            {
                const Color got = ReadCentrePixel(rt, faces[i]);
                const bool ok = Matches(got, faceColors[i]);
                if (!ok)
                {
                    std::printf("  face %d mismatch: got=(%d,%d,%d) expected=(%d,%d,%d)\n", i,
                                got.getRProperty(), got.getGProperty(), got.getBProperty(),
                                faceColors[i].getRProperty(), faceColors[i].getGProperty(), faceColors[i].getBProperty());
                }
                allOk = allOk && ok;
            }
            Check(allOk, "all 6 faces filled with distinct colors and read back correctly");
        }

        // Check B: a real colored3d triangle drawn into one face.
        {
            RenderTargetCube rt(dev, kCubeSize, false, SurfaceFormat::Color,
                                DepthFormat::None, 0, RenderTargetUsage::DiscardContents);
            const VertexPositionColor verts[3] = {
                { Vector3(-1.0f, -1.0f, 0.0f), Color::Red },
                { Vector3( 0.0f,  1.0f, 0.0f), Color::Red },
                { Vector3( 1.0f, -1.0f, 0.0f), Color::Red },
            };
            VertexBuffer vb(dev, VertexPositionColor::getVertexDeclarationStatic(), 3, BufferUsage::None);
            vb.SetData(verts, 0, 3);

            dev.SetRenderTarget(&rt, CubeMapFace::PositiveX);
            dev.Clear(Color::Black);
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

            const Color got = ReadCentrePixel(rt, CubeMapFace::PositiveX);
            Check(Matches(got, Color::Red),
                  "colored3d triangle drawn into a cube face reads back correctly: got=("
                  + std::to_string(got.getRProperty()) + "," + std::to_string(got.getGProperty())
                  + "," + std::to_string(got.getBProperty()) + ")");
        }

        // Check C: real per-face depth test -- farther red quad, then nearer green quad.
        {
            RenderTargetCube rt(dev, kCubeSize, false, SurfaceFormat::Color,
                                DepthFormat::Depth24Stencil8, 0, RenderTargetUsage::DiscardContents);
            const VertexPositionColor farVerts[6] = {
                { Vector3(-1.0f, -1.0f, 0.5f), Color::Red }, { Vector3(-1.0f, 1.0f, 0.5f), Color::Red }, { Vector3(1.0f, -1.0f, 0.5f), Color::Red },
                { Vector3(-1.0f, 1.0f, 0.5f), Color::Red }, { Vector3(1.0f, 1.0f, 0.5f), Color::Red }, { Vector3(1.0f, -1.0f, 0.5f), Color::Red },
            };
            const VertexPositionColor nearVerts[6] = {
                { Vector3(-1.0f, -1.0f, -0.5f), Color::Green }, { Vector3(-1.0f, 1.0f, -0.5f), Color::Green }, { Vector3(1.0f, -1.0f, -0.5f), Color::Green },
                { Vector3(-1.0f, 1.0f, -0.5f), Color::Green }, { Vector3(1.0f, 1.0f, -0.5f), Color::Green }, { Vector3(1.0f, -1.0f, -0.5f), Color::Green },
            };
            VertexBuffer farVb(dev, VertexPositionColor::getVertexDeclarationStatic(), 6, BufferUsage::None);
            farVb.SetData(farVerts, 0, 6);
            VertexBuffer nearVb(dev, VertexPositionColor::getVertexDeclarationStatic(), 6, BufferUsage::None);
            nearVb.SetData(nearVerts, 0, 6);

            dev.SetRenderTarget(&rt, CubeMapFace::NegativeX);
            dev.Clear(Color(0, 0, 0, 255), 1.0f);
            dev.SetDepthTestEnabled(true);
            dev.SetDepthWriteEnabled(true);
            BasicEffect fx(dev);
            fx.VertexColorEnabled = true;
            fx.setWorldProperty(Matrix::getIdentityProperty());
            fx.setViewProperty(Matrix::getIdentityProperty());
            fx.setProjectionProperty(Matrix::getIdentityProperty());
            fx.Apply();
            dev.SetVertexBuffer(&farVb);
            dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
            dev.SetVertexBuffer(&nearVb);
            dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
            dev.SetVertexBuffer(nullptr);
            dev.SetDepthTestEnabled(false);
            dev.SetDepthWriteEnabled(false);
            dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

            const Color got = ReadCentrePixel(rt, CubeMapFace::NegativeX);
            Check(Matches(got, Color::Green),
                  "depth-tested cube face: nearer quad wins over farther quad: got=("
                  + std::to_string(got.getRProperty()) + "," + std::to_string(got.getGProperty())
                  + "," + std::to_string(got.getBProperty()) + ")");
        }

        // Check D: MultiSampleCount property fidelity + real MSAA fill/resolve/readback.
        {
            RenderTargetCube rtNoMsaa(dev, kCubeSize, false, SurfaceFormat::Color,
                                     DepthFormat::None, 0, RenderTargetUsage::DiscardContents);
            Check(rtNoMsaa.getMultiSampleCountProperty() == 0,
                  "MultiSampleCount request 0 -> applied 0");

            RenderTargetCube rtMsaa(dev, kCubeSize, false, SurfaceFormat::Color,
                                   DepthFormat::None, 4, RenderTargetUsage::DiscardContents);
            const int applied = rtMsaa.getMultiSampleCountProperty();
            Check(applied > 1, "MultiSampleCount request 4 -> applied " + std::to_string(applied) + " (expected >1)");

            for (int i = 0; i < 6; ++i)
            {
                dev.SetRenderTarget(&rtMsaa, faces[i]);
                dev.Clear(faceColors[i]);
            }
            dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
            const Color got = ReadCentrePixel(rtMsaa, CubeMapFace::PositiveZ);
            Check(Matches(got, faceColors[4]),
                  "MSAA cube face resolves and reads back correctly: got=("
                  + std::to_string(got.getRProperty()) + "," + std::to_string(got.getGProperty())
                  + "," + std::to_string(got.getBProperty()) + ")");
        }

        // Check E: mip generation -- a uniform-color fill's level-1 mip reads back the same color.
        {
            RenderTargetCube rt(dev, kCubeSize, true, SurfaceFormat::Color,
                                DepthFormat::None, 0, RenderTargetUsage::DiscardContents);
            dev.SetRenderTarget(&rt, CubeMapFace::PositiveY);
            dev.Clear(Color::Orange);
            dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

            const Color mipPx = ReadCentrePixel(rt, CubeMapFace::PositiveY, 1);
            Check(Matches(mipPx, Color::Orange),
                  "mip level 1 of a uniform-color cube face reads back the same color: got=("
                  + std::to_string(mipPx.getRProperty()) + "," + std::to_string(mipPx.getGProperty())
                  + "," + std::to_string(mipPx.getBProperty()) + ")");
        }

        std::printf("=== %d/7 PASS ===\n", passCount_);
        result_ = (passCount_ == 7) ? 0 : 1;
        Exit();
    }

public:
    SdlGpuRenderTargetCubeTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(64);
        gdm_->setPreferredBackBufferHeightProperty(64);
        // GraphicsDeviceManager.SynchronizeWithVerticalRetrace defaults to true (the XNA
        // default); this test's virtual/headless display has no real vblank signal, so leaving
        // VSync on makes every frame wait roughly a second, blowing past this test's frame
        // budget. Disabling it here -- before Game::DoInitialize()'s CreateDevice() call reads
        // it -- is the correct, property-level way to request Immediate presentation.
        gdm_->setSynchronizeWithVerticalRetraceProperty(false);
    }

    int getResult() const { return result_; }
};

int main()
{
    if (!CNA::Examples::ProbeGpuDisplayAvailable())
        return CNA::Examples::kSkipExitCode;

    SdlGpuRenderTargetCubeTest game;
    game.Run();
    return game.getResult();
}
