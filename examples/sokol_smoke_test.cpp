// SPDX-License-Identifier: MS-PL
// plan_sokol.md SOKOL-16: end-to-end lifecycle proof for the sokol_gfx graphics backend -- a real
// SDL window, a real GPU context, a real sg_setup(), and a 60-frame Clear()+Present() loop, with
// the cleared colour read back off the real back buffer rather than merely "did not throw".
//
// Check A -- GetWindowInternal() returns a real, non-null SDL_Window.
// Check B -- GetRendererInternal() is null (this backend does not use SDL_Renderer).
// Check C -- GetViewportSize() reports the requested logical size.
// Check D -- GetApiEXT() reports the API CNA_SOKOL_API selected at configure time.
// Check E -- a real vertex/index buffer round-trip: SetData() then GetVertexCount()/
//   GetIndexCount() reports exactly what was uploaded, for both 16- and 32-bit indices.
// Check F -- Clear(Target|DepthBuffer|Stencil) followed by GetBackBufferData() returns the exact
//   clear colour, so the pass/clear path genuinely reached the GPU.
// Check G -- the unimplemented 3D draw path throws rather than silently drawing nothing.
// Check H -- SupportsCapability() reports this baseline's documented boundary.
// Check I -- 60 frames of Clear() + the automatic end-of-frame Present() complete with no
//   exception.
//
// Exit code 0 = all checks PASS, 1 = any FAILs, 77 = skipped (no GPU/display).

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/ClearOptions.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"

#include "CNA/GraphicsBackendType.hpp"
#include "CNA/Internal/Backends/Sokol/SokolGraphicsBackend.hpp"

#include "common/PixelTestGame.hpp"

#include <cstdint>
#include <cstdio>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using namespace CNA::Internal::Backends::Sokol;

namespace
{
    constexpr int kTotalFrames = 60;
    constexpr int kExpectedChecks = 13;
    constexpr int kBackBufferWidth = 320;
    constexpr int kBackBufferHeight = 240;
}

class SokolSmokeTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    int frame_ = 0;
    int passCount_ = 0;
    int result_ = 1;

    void check(bool ok, const char* label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label);
        if (ok) ++passCount_;
    }

    void CheckBuffers(GraphicsDevice& device)
    {
        const std::vector<VertexPositionColor> vertices = {
            VertexPositionColor(Vector3(0, 0, 0), Color::Red),
            VertexPositionColor(Vector3(1, 0, 0), Color::Green),
            VertexPositionColor(Vector3(0, 1, 0), Color::Blue),
        };
        VertexBuffer vertexBuffer(device, VertexPositionColor::getVertexDeclarationStatic(),
                                  static_cast<int>(vertices.size()), BufferUsage::None);
        vertexBuffer.SetData(vertices.data(), static_cast<int>(vertices.size()));
        check(vertexBuffer.getVertexCountProperty() == 3,
              "VertexBuffer::SetData() round-trips its vertex count");

        const std::vector<std::uint16_t> indices16 = {0, 1, 2};
        IndexBuffer indexBuffer16(device, IndexElementSize::SixteenBits,
                                  static_cast<int>(indices16.size()), BufferUsage::None);
        indexBuffer16.SetData(indices16.data(), static_cast<int>(indices16.size()));
        check(indexBuffer16.getIndexCountProperty() == 3,
              "IndexBuffer::SetData() round-trips its 16-bit index count");

        const std::vector<std::uint32_t> indices32 = {0, 1, 2, 2, 1, 0};
        IndexBuffer indexBuffer32(device, IndexElementSize::ThirtyTwoBits,
                                  static_cast<int>(indices32.size()), BufferUsage::None);
        indexBuffer32.SetData(indices32.data(), static_cast<int>(indices32.size()));
        check(indexBuffer32.getIndexCountProperty() == 6,
              "IndexBuffer::SetData() round-trips its 32-bit index count");

        // The 3D path is deliberately unimplemented on this baseline. It must fail loudly: a
        // silent no-op would let a 3D game report success while rendering nothing at all. The
        // vertex buffer is bound first so the throw can only come from the missing draw path, not
        // from a missing binding.
        device.SetVertexBuffer(&vertexBuffer);
        bool threw = false;
        try
        {
            device.DrawPrimitives(PrimitiveType::TriangleList, 0, 1);
        }
        catch (const std::exception&)
        {
            threw = true;
        }
        device.SetVertexBuffer(nullptr);
        check(threw, "the unimplemented 3D draw path throws instead of silently drawing nothing");
    }

    void CheckCapabilities(SokolGraphicsBackend& backend)
    {
        using CNA::GraphicsCapability;
        const bool boundaryHolds =
            backend.SupportsCapability(GraphicsCapability::DepthStencilBuffer)
            && !backend.SupportsCapability(GraphicsCapability::ThreeD)
            && !backend.SupportsCapability(GraphicsCapability::MultipleRenderTargets)
            && !backend.SupportsCapability(GraphicsCapability::CustomEffects)
            && !backend.SupportsCapability(GraphicsCapability::Texture3D)
            && !backend.SupportsCapability(GraphicsCapability::OcclusionQuery);
        check(boundaryHolds, "SupportsCapability() reports the documented 2D baseline boundary");
    }

protected:
    void Draw(const GameTime&) override
    {
        ++frame_;
        auto& device = getGraphicsDeviceProperty();
        auto& backend = static_cast<SokolGraphicsBackend&>(device.GetBackend());

        device.Clear(static_cast<ClearOptions>(
                         static_cast<int>(ClearOptions::Target) |
                         static_cast<int>(ClearOptions::DepthBuffer) |
                         static_cast<int>(ClearOptions::Stencil)),
                     Color(32, 64, 128, 255), 1.0f, 0);

        if (frame_ == 1)
        {
            check(backend.GetWindowInternal() != nullptr,
                  "GetWindowInternal() returns a real window");
            check(backend.GetRendererInternal() == nullptr,
                  "GetRendererInternal() is null (no SDL_Renderer)");

            int width = 0;
            int height = 0;
            backend.GetViewportSize(width, height);
            check(width == kBackBufferWidth && height == kBackBufferHeight,
                  "GetViewportSize() reports the requested logical size");

            check(CNA::getCurrentGraphicsBackendName() == "SOKOL",
                  "the compiled-in backend identity is SOKOL");
            check(SokolGraphicsBackend::GetApiEXT() == SokolApiEXT::GLCore,
                  "GetApiEXT() reports the GLCore API this build selected");
            check(SokolGraphicsBackend::GetMaxSpriteQuadsPerFrameEXT() > 0,
                  "the per-frame sprite capacity is a positive, documented number");

            CheckBuffers(device);
            CheckCapabilities(backend);
        }

        if (frame_ == 2)
        {
            // Read back on the second frame, after the first frame's Present() has actually
            // submitted a pass: this proves the clear reached the GPU rather than only being
            // recorded as a pending pass action.
            const Rectangle centre(kBackBufferWidth / 2, kBackBufferHeight / 2, 1, 1);
            Color pixel(0, 0, 0, 0);
            device.GetBackBufferData(&centre, &pixel, 0, 1);
            const bool matches = pixel.getRProperty() == 32
                              && pixel.getGProperty() == 64
                              && pixel.getBProperty() == 128;
            std::printf("        clear readback = (%d,%d,%d,%d)\n",
                        pixel.getRProperty(), pixel.getGProperty(),
                        pixel.getBProperty(), pixel.getAProperty());
            check(matches, "GetBackBufferData() returns the exact colour Clear() was given");
        }

        if (frame_ == kTotalFrames)
        {
            check(true, "60 frames of Clear() + Present() complete with no exception");
            std::printf("=== %d/%d PASS ===\n", passCount_, kExpectedChecks);
            result_ = (passCount_ == kExpectedChecks) ? 0 : 1;
            Exit();
        }
    }

public:
    SokolSmokeTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kBackBufferWidth);
        gdm_->setPreferredBackBufferHeightProperty(kBackBufferHeight);
        // A virtual/headless display has no real vblank, so leaving VSync on would make every
        // frame wait about a second and blow this test's frame budget.
        gdm_->setSynchronizeWithVerticalRetraceProperty(false);
    }

    [[nodiscard]] int getResult() const { return result_; }
};

int main()
{
    if (!CNA::Examples::ProbeGpuDisplayAvailable())
        return CNA::Examples::kSkipExitCode;

    SokolSmokeTest game;
    game.Run();
    return game.getResult();
}
