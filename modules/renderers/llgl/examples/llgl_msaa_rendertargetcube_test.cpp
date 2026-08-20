// SPDX-License-Identifier: MS-PL
// plans/plan_llgl.md LLGL-34: RenderTargetCube MSAA -- a real antialiased edge resolved into a
// RenderTargetCube's own shared colour texture, not just a bookkeeping check. Mirrors
// examples/llgl_msaa_rendertarget_test.cpp's own RenderTarget2D technique and checks verbatim,
// applied to CreateRenderTargetCube's own multiSampleCount parameter against a single face
// (CubeMapFace::PositiveX) instead of a plain RenderTarget2D.
//
// Like RenderTarget2D (and unlike the back buffer, only ever honoured at swap-chain/GraphicsDevice
// CONSTRUCTION time), a RenderTargetCube's multiSampleCount is read directly at ITS OWN
// construction -- so this test can use a single, ordinary PixelTestGame device and just construct
// two cubes.
//
// A single right triangle -- vertices at (0,0), (kSize,0), (0,kSize) -- puts a diagonal edge along
// x + y = kSize. Scanning a FIXED row (y = kSize/2) across that edge crosses it transversally; at
// x = kSize/2 - 1 the pixel's own centre sits EXACTLY on the geometric line (perpendicular distance
// zero), so any sane multisample pattern splits that pixel's samples across the edge, while a
// single-sample (no MSAA) pixel is always a clean, deterministic in-or-out decision, never a blend.
//
// Check A -- MultiSampleCount=0 really applies no multisampling to a RenderTargetCube, and
//   GetData() works from that ordinary, non-multisampled face.
// Check B -- without MSAA, the diagonal edge is a hard, unblended step across the face.
// Check C -- GetData() from a genuinely MSAA-requested cube face does not throw (the resolve path
//   itself runs without error).
// Check D -- deep inside the triangle, well away from the edge, the resolved face still returns a
//   sane, fully-opaque pixel -- the resolve path returns real pixels, not garbage.
// Check E (module-dependent, skipped if this module does not apply MultiSampleCount to a
//   RenderTargetCube at all) -- GetMultiSampleCount() reports a real, renderer-applied sample count.
// Check F (same gate as E) -- with MSAA on, the SAME diagonal edge produces at least one genuinely
//   blended, mid-tone pixel -- real multisample antialiasing into a cube face, not merely "didn't
//   crash".
//
// No _OpenGL CTest variant is registered for this test: this project's own OpenGL module has no
// cube-texture support at all (every other RenderTargetCube test on this renderer is Vulkan-module
// only for the same reason -- see docs/llgl-renderer.md's "EnvironmentMapEffect" section).
//
// Exit code 0 = all checks PASS, 1 = any FAILs.

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BufferUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/CubeMapFace.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetCube.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"

#include "common/PixelTestGame.hpp"

#include <cstdio>
#include <stdexcept>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kSize = 64;
    constexpr int kScanY = kSize / 2;
    constexpr int kScanXStart = kSize / 2 - 12;
    constexpr int kScanXEnd = kSize / 2 + 8;
    constexpr CubeMapFace kFace = CubeMapFace::PositiveX;

    std::vector<VertexPositionColor> MakeDiagonalTriangle()
    {
        const Color white(255, 255, 255, 255);
        return {
            VertexPositionColor(Vector3(0.0f, 0.0f, 0.0f), white),
            VertexPositionColor(Vector3(static_cast<float>(kSize), 0.0f, 0.0f), white),
            VertexPositionColor(Vector3(0.0f, static_cast<float>(kSize), 0.0f), white),
        };
    }

    struct RenderResult
    {
        int appliedSampleCount = 0;
        std::vector<int> edgeRed;
        Color insidePixel{0, 0, 0, 0};
        bool getDataThrew = false;
    };
}

class LlglMsaaRenderTargetCubeTest : public CNA::Examples::PixelTestGame
{
public:
    // Constructs a RenderTargetCube requesting multiSampleCount, draws the diagonal triangle into
    // one face (kFace), and samples across the edge with RenderTargetCube::GetData().
    RenderResult RenderAndSample(GraphicsDevice& device, int multiSampleCount)
    {
        RenderResult out;

        RenderTargetCube cube(device, kSize, false, SurfaceFormat::Color, DepthFormat::None,
                              multiSampleCount);
        out.appliedSampleCount = cube.getMultiSampleCountProperty();

        BasicEffect effect(device);
        effect.VertexColorEnabled = true;
        effect.setTextureEnabledProperty(false);
        effect.setLightingEnabledProperty(false);
        effect.setWorldProperty(Matrix::getIdentityProperty());
        effect.setViewProperty(Matrix::getIdentityProperty());
        effect.setProjectionProperty(Matrix::CreateOrthographicOffCenter(
            0.0f, static_cast<float>(kSize), static_cast<float>(kSize), 0.0f, 0.0f, 1.0f));

        const std::vector<VertexPositionColor> triangle = MakeDiagonalTriangle();
        VertexBuffer buffer(device, VertexPositionColor::getVertexDeclarationStatic(),
                            static_cast<int>(triangle.size()), BufferUsage::None);
        buffer.SetData(triangle.data(), static_cast<int>(triangle.size()));

        device.SetRenderTarget(&cube, kFace);
        device.Clear(Color(0, 0, 0, 255));
        device.SetVertexBuffer(&buffer);
        effect.Apply();
        device.DrawPrimitives(PrimitiveType::TriangleList, 0, 1);
        device.SetRenderTarget(nullptr);

        try
        {
            for (int x = kScanXStart; x <= kScanXEnd; ++x)
            {
                const Rectangle region(x, kScanY, 1, 1);
                Color pixel(0, 0, 0, 0);
                cube.GetData(kFace, 0, &region, &pixel, 0, 1);
                out.edgeRed.push_back(pixel.getRProperty());
            }

            // Deep inside the triangle, well away from the edge -- proves the resolve path
            // returns a sane, fully-opaque colour, not just that one edge pixel looks blended.
            const Rectangle insideRegion(8, 8, 1, 1);
            cube.GetData(kFace, 0, &insideRegion, &out.insidePixel, 0, 1);
        }
        catch (const std::exception&)
        {
            out.getDataThrew = true;
        }

        return out;
    }

    void RunTest() override
    {
        auto& device = getGraphicsDeviceProperty();

        const RenderResult noMsaa = RenderAndSample(device, 0);
        ExpectTrue("MultiSampleCount=0 really applies no multisampling to a RenderTargetCube",
                  noMsaa.appliedSampleCount == 0);
        ExpectTrue("GetData() works from a non-multisampled cube face", !noMsaa.getDataThrew);

        bool hardEdge = !noMsaa.edgeRed.empty();
        for (int red : noMsaa.edgeRed)
        {
            if (red > 20 && red < 235) { hardEdge = false; break; }
        }
        ExpectTrue("without MSAA, the diagonal edge is a hard, unblended step across the cube "
                  "face (every sampled pixel across it is pure background or pure foreground)",
                  hardEdge);

        const RenderResult msaa = RenderAndSample(device, 4);
        ExpectTrue("GetData() works from a cube face requesting real MSAA", !msaa.getDataThrew);
        ExpectTrue("well away from the edge, the resolved cube face still returns a sane, "
                  "fully-opaque pixel",
                  msaa.insidePixel.getRProperty() > 235 && msaa.insidePixel.getAProperty() > 235);

        // Whether requesting a RenderTargetCube's own MultiSampleCount actually yields a
        // multisampled attachment is module/driver-dependent, the same category this project's
        // own capability table already uses for WireFrame/AnisotropicFiltering/back-buffer MSAA
        // (LLGL-23) and RenderTarget2D MSAA (LLGL-26 follow-up). The module-dependent checks
        // below are skipped (not failed) when this module does not apply it, rather than papered
        // over.
        if (msaa.appliedSampleCount > 1)
        {
            ExpectTrue("GetMultiSampleCount() reports a real, renderer-applied sample count once "
                      "MultiSampleCount is requested for a RenderTargetCube", true);

            bool blended = false;
            for (int red : msaa.edgeRed)
            {
                if (red > 20 && red < 235) { blended = true; break; }
            }
            ExpectTrue("with MSAA on, the SAME diagonal edge produces at least one genuinely "
                      "blended, mid-tone pixel in the cube face -- real multisample "
                      "antialiasing, not merely \"didn't crash\"", blended);
        }
        else
        {
            std::printf("[SKIP] this module does not apply MultiSampleCount to a RenderTargetCube "
                        "in this environment -- skipping the module-dependent MSAA cube checks "
                        "(GetMultiSampleCount() applied vs. requested, blended edge pixel)\n");
        }
    }
};

int main()
{
    return CNA::Examples::RunPixelTest<LlglMsaaRenderTargetCubeTest>();
}
