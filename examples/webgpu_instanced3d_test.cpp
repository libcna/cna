// SPDX-License-Identifier: MS-PL
// WEBGPU-27/38/68: verify WebGPUGraphicsBackend's instanced3d.wgsl / GetOrCreatePipelineInstanced3D()
// / DrawInstancedPrimitivesEx() -- a genuine SECOND vertex buffer binding
// (WGPUVertexStepMode_Instance) carrying a per-instance mat4 world transform, ported from
// VulkanGraphicsBackend's instanced3d.{vert,frag}.glsl. Tested directly at the IGraphicsBackend
// level (bypassing GraphicsDevice's own VertexBufferBinding/instance-frequency plumbing, which is
// pre-existing and backend-agnostic -- GraphicsDevice::DrawInstancedPrimitives() already forwards
// to IGraphicsBackend::DrawInstancedPrimitivesEx() unchanged), matching
// examples/d3d9_instanced_test.cpp's own established test-authoring convention for this exact API.
// Deliberately uses only the backend-agnostic IGraphicsBackend/IVertexBufferBackend/
// IIndexBufferBackend interfaces (Common/IGraphicsBackend.hpp) rather than the concrete
// WebGPUGraphicsBackend class -- unlike D3D9/D3D11/Vulkan, this backend's own header transitively
// requires wgpu-native's webgpu.h, whose include path is only exposed to
// cna_backend_graphics_webgpu's own (PRIVATE-linked) translation units, not to a separate test
// executable that merely links against it.
//
// Checks A/B/C -- ONE DrawInstancedPrimitivesEx() call, instanceCount=3, each instance a pure
//   X-translation (-0.5, 0.0, +0.5) applied to the SAME small triangle via the per-instance vertex
//   buffer: each instance paints its own small triangle at its own distinct, independently
//   predicted screen location with the exact shared DiffuseColor -- proves the per-instance vertex
//   buffer is genuinely read per-instance (a buggy implementation that ignored it, or always read
//   instance 0, could satisfy at most one of these three, never all three at once).
// Check D -- a region far from all three triangles stays the original clear colour -- proves the
//   draw painted only the 3 small triangles, not the whole screen (rules out an implementation that
//   happens to overlap by drawing something much larger).
// Check E -- a binding set with no per-instance stream falls back to a real (non-instanced)
//   DrawIndexedPrimitivesEx() draw (stride-16 VertexPositionColor, VertexColorEnabled=false, a
//   distinct pure-component DiffuseColor overriding the per-vertex colour) instead of throwing or
//   corrupting the frame -- matches D3D11GraphicsBackend::DrawInstancedPrimitivesEx()'s own
//   identical fallback contract.
//
// Exit code 0 = all checks PASS, 1 = any FAILs.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"

#include "CNA/Internal/Backends/Common/IGraphicsBackend.hpp"

#include <cstdint>
#include <cstdio>
#include <memory>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using CNA::Internal::Backends::GpuDrawParams;
using CNA::Internal::Backends::IGraphicsBackend;

namespace
{
    constexpr int kSize = 64;

    int passCount = 0;
    int totalCount = 0;

    void check(bool ok, const char* label)
    {
        ++totalCount;
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label);
        if (ok) ++passCount;
    }

    struct VP { float x, y, z; };

    // A small quad near the origin (4 vertices + 6 indices, NOT a 3-index triangle -- an odd
    // index count is only 6 bytes, which trips this backend's wgpuQueueWriteBuffer
    // COPY_BUFFER_ALIGNMENT requirement, a real but unrelated pre-existing IndexBuffer limitation;
    // every other index-buffer-using WebGPU test in this suite already happens to use an
    // even/4-byte-aligned index count for the same reason) -- small enough that a per-instance X
    // translation of +-0.5/0.0 clearly separates each instance's own screen region, and symmetric
    // (unlike a right triangle) so its exact screen-space centre has no diagonal-edge rounding
    // ambiguity to work around.
    const VP kSmallQuad[4] = {
        {-0.1f, -0.1f, 0.0f},
        { 0.1f, -0.1f, 0.0f},
        { 0.1f,  0.1f, 0.0f},
        {-0.1f,  0.1f, 0.0f},
    };
    // REMED-GFX-160: wound CLOCKWISE as displayed, i.e. an ordinary XNA FRONT face, so this test
    // survives the device's own default RasterizerState.CullCounterClockwise -- which it never
    // sets, and therefore relies on. Over the vertices above, {0,1,2}/{0,2,3} is the
    // counter-clockwise (BACK-facing) order; it used to be that, and passed only because this
    // backend's cull mapping was inverted to match. This test's subject is instancing, not
    // culling, so it draws front-facing geometry like a real game would.
    const std::uint16_t kSmallQuadIdx[6] = {0, 2, 1, 0, 3, 2};

    // Per-instance world matrix: 4 column-major float4 "columns" (64 bytes), matching
    // instancedAttrs' Float32x4 layout in GetOrCreatePipelineInstanced3D(). A pure translation
    // matrix: column 3 = (Tx,Ty,Tz,1).
    struct InstanceColumns { float col0[4], col1[4], col2[4], col3[4]; };

    InstanceColumns TranslationInstance(float tx, float ty, float tz)
    {
        return InstanceColumns{
            {1, 0, 0, 0},
            {0, 1, 0, 0},
            {0, 0, 1, 0},
            {tx, ty, tz, 1},
        };
    }

    bool ReadPixel(GraphicsDevice& dev, int x, int y, Color& out)
    {
        const Rectangle region(x, y, 1, 1);
        std::vector<Color> pixels(1, Color(0, 0, 0, 0));
        dev.GetBackBufferData(&region, pixels.data(), 0, 1);
        out = pixels[0];
        return true;
    }

    bool IsColor(const Color& c, int r, int g, int b, int a)
    {
        return c.getRProperty() == r && c.getGProperty() == g &&
               c.getBProperty() == b && c.getAProperty() == a;
    }
}

class WebGpuInstanced3DTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    int frame_ = 0;

protected:
    void Draw(const GameTime&) override
    {
        if (frame_++ < 1) return;

        auto& dev = getGraphicsDeviceProperty();
        IGraphicsBackend& backend = dev.GetBackend();

        // Checks A/B/C/D: 3 instances, one draw call.
        {
            auto vb = backend.CreateVertexBuffer(4);
            vb->SetData(kSmallQuad, 4, sizeof(VP));
            auto ib = backend.CreateIndexBuffer16(6);
            ib->SetData16(kSmallQuadIdx, 6);

            const InstanceColumns instances[3] = {
                TranslationInstance(-0.5f, 0.0f, 0.0f),
                TranslationInstance( 0.0f, 0.0f, 0.0f),
                TranslationInstance( 0.5f, 0.0f, 0.0f),
            };
            auto instVb = backend.CreateVertexBuffer(3);
            instVb->SetData(instances, 3, sizeof(InstanceColumns));

            GpuDrawParams params;
            // REMED-GFX-202: the classic two-stream instanced binding set.
            SetInstancedVertexStreamsEXT(params, *vb, *instVb, /*instanceFrequency=*/1,
                                         static_cast<int>(sizeof(VP)), 0,
                                         static_cast<int>(sizeof(InstanceColumns)), 0);
            params.instanceCount = 3;
            params.diffuseColor[0] = 0.0f;
            params.diffuseColor[1] = 1.0f;
            params.diffuseColor[2] = 0.0f;
            params.diffuseColor[3] = 1.0f;

            dev.Clear(Color(0, 0, 255, 255));

            backend.DrawInstancedPrimitivesEx(*vb, *ib, Matrix::getIdentityProperty(), Matrix::getIdentityProperty(),
                                              Matrix::getIdentityProperty(), PrimitiveType::TriangleList, 2, 3, params);

            // kSmallQuad is symmetric (a real square, not a right triangle), so its exact
            // translation-mapped screen-space centre -- NDC (tx,0) -> pixel ((tx+1)/2)*64 -- sits
            // safely inside its own fill region with no diagonal-edge rounding ambiguity to work
            // around: tx=-0.5 -> pixel 16, tx=0.0 -> pixel 32, tx=+0.5 -> pixel 48.
            Color pixelLeft(0, 0, 0, 0), pixelMid(0, 0, 0, 0), pixelRight(0, 0, 0, 0), pixelFar(0, 0, 0, 0);
            ReadPixel(dev, 16, 32, pixelLeft);
            ReadPixel(dev, 32, 32, pixelMid);
            ReadPixel(dev, 48, 32, pixelRight);
            ReadPixel(dev, 60, 60, pixelFar);

            check(IsColor(pixelLeft, 0, 255, 0, 255),
                  "instance 0 (translation -0.5) paints exact DiffuseColor at its own screen location");
            check(IsColor(pixelMid, 0, 255, 0, 255),
                  "instance 1 (translation 0.0) paints the SAME exact DiffuseColor at a DIFFERENT "
                  "screen location -- proves a real 2nd instance was drawn");
            check(IsColor(pixelRight, 0, 255, 0, 255),
                  "instance 2 (translation +0.5) paints the SAME exact DiffuseColor at a THIRD "
                  "distinct screen location -- proves a real 3rd instance was drawn, not a "
                  "hardcoded 2-instance special case");
            check(IsColor(pixelFar, 0, 0, 255, 255),
                  "a region far from all 3 quads is untouched -- the draw painted only the "
                  "3 small quads, not the whole screen");
        }

        // Check E: no per-instance stream falls back to a real (non-instanced) draw.
        {
            auto vb = backend.CreateVertexBuffer(4);
            struct VPC { float x, y, z; std::uint8_t r, g, b, a; };
            static const VPC kQuad[4] = {
                {-1.0f, -1.0f, 0.0f, 255, 0, 0, 255},
                { 1.0f, -1.0f, 0.0f, 255, 0, 0, 255},
                { 1.0f,  1.0f, 0.0f, 255, 0, 0, 255},
                {-1.0f,  1.0f, 0.0f, 255, 0, 0, 255},
            };
            vb->SetData(kQuad, 4, sizeof(VPC));
            auto ib = backend.CreateIndexBuffer16(6);
            ib->SetData16(kSmallQuadIdx, 6);

            // A pure 0/1-component colour (yellow), not a mid-tone -- this backend's swapchain
            // prefers an sRGB format, so a genuinely mid-range linear fragment value reads back
            // gamma-encoded, not linear (see webgpu_littextured3d_test.cpp's identical, pre-existing
            // documented caveat); pure extremes are gamma-invariant.
            GpuDrawParams params; // no per-instance stream is described at all
            params.vertexColorEnabled = false; // ignore the (red) per-vertex colour above
            params.diffuseColor[0] = 1.0f; params.diffuseColor[1] = 1.0f;
            params.diffuseColor[2] = 0.0f; params.diffuseColor[3] = 1.0f; // yellow

            dev.Clear(Color(0, 0, 255, 255));
            bool threw = false;
            try
            {
                backend.DrawInstancedPrimitivesEx(*vb, *ib, Matrix::getIdentityProperty(), Matrix::getIdentityProperty(),
                                                  Matrix::getIdentityProperty(), PrimitiveType::TriangleList, 2, 1, params);
            }
            catch (const std::exception&) { threw = true; }
            Color center(0, 0, 0, 0);
            ReadPixel(dev, 32, 32, center);
            check(!threw && IsColor(center, 255, 255, 0, 255),
                  "DrawInstancedPrimitivesEx: no per-instance stream falls back to a real, working "
                  "DrawIndexedPrimitivesEx() draw (yellow, VertexColorEnabled=false override), not "
                  "a throw or a corrupted frame");
        }

        std::printf("=== %d/%d PASS ===\n", passCount, totalCount);
        Exit();
    }

public:
    WebGpuInstanced3DTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kSize);
        gdm_->setPreferredBackBufferHeightProperty(kSize);
    }
};

int main()
{
    WebGpuInstanced3DTest game;
    game.Run();

    std::printf("=== %d/%d PASS (total) ===\n", passCount, totalCount);
    return (passCount == totalCount) ? 0 : 1;
}
