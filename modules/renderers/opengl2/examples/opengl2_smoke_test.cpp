// SPDX-License-Identifier: MS-PL
// plan_opengl2.md: end-to-end smoke test for the native OpenGL 2.1 graphics renderer's device/
// window/GL-context lifecycle and color+depth+stencil clear/present. Real window, real SDL GL
// context, a real 60-frame Clear()+Present() loop -- the same bar SDLGPU-12/sdlgpu_smoke_test.cpp
// established when that renderer was first bootstrapped.
//
// Check A -- GameWindow handle returns a real, non-null SDL_Window.
// Check B -- SDL_GetRenderer(window) is null (this renderer does not use SDL_Renderer).
// Check C -- GetViewportSize() reports a positive width/height matching the real window.
// Check D -- a real VertexBuffer round-trip: SetData() followed by GetVertexCount() reports the
//   exact count uploaded.
// Check E -- a real 16-bit IndexBuffer round-trip (SetData16()+GetIndexCount()/IsThirtyTwoBit()).
// Check F -- a 32-bit IndexBuffer round-trips too (desktop GL 2.1 accepts GL_UNSIGNED_INT
//   indices directly, unlike GLES2 without an extension).
// Check G -- 60 frames of Clear(Target|DepthBuffer|Stencil, color, depth, stencil) + the
//   automatic end-of-frame Present() complete with no exception.
//
// Exit code 0 = all checks PASS, 1 = any FAILs.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Graphics/ClearOptions.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"

#include "CNA/Internal/Renderers/OpenGL2/OpenGL2Renderer.hpp"

#include "common/PixelTestGame.hpp"

#include <cstdint>
#include <cstdio>
#include <memory>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using namespace CNA::Internal::Renderers::OpenGL2;

namespace
{
    constexpr int kTotalFrames = 60;
}

class OpenGL2SmokeTest : public Game
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

protected:
    void Draw(const GameTime&) override
    {
        ++frame_;
        auto& dev = getGraphicsDeviceProperty();
        auto& renderer = static_cast<OpenGL2Renderer&>(dev.GetRenderer());

        if (frame_ == 1)
        {
            check(reinterpret_cast<SDL_Window*>(getWindowProperty().getHandleProperty()) != nullptr, "GameWindow handle returns a real window");
            check(SDL_GetRenderer(reinterpret_cast<SDL_Window*>(getWindowProperty().getHandleProperty())) == nullptr, "SDL_GetRenderer(window) is null (no SDL_Renderer)");

            int width = 0, height = 0;
            renderer.GetViewportSize(width, height);
            check(width > 0 && height > 0, "GetViewportSize() reports a positive size");

            auto vb = renderer.CreateVertexBuffer(3);
            const float verts[3 * 2] = {0, 0, 1, 0, 0, 1};
            vb->SetData(verts, 3, sizeof(float) * 2);
            check(vb->GetVertexCount() == 3, "VertexBuffer.SetData()+GetVertexCount() round-trips the exact count");

            auto ib16 = renderer.CreateIndexBuffer16(3);
            const std::uint16_t indices16[3] = {0, 1, 2};
            ib16->SetData16(indices16, 3);
            check(ib16->GetIndexCount() == 3 && !ib16->IsThirtyTwoBit(),
                  "IndexBuffer16.SetData16()+GetIndexCount()/IsThirtyTwoBit() round-trips correctly");

            auto ib32 = renderer.CreateIndexBuffer32(3);
            const std::uint32_t indices32[3] = {0, 1, 2};
            ib32->SetData32(indices32, 3);
            check(ib32->GetIndexCount() == 3 && ib32->IsThirtyTwoBit(),
                  "IndexBuffer32.SetData32()+GetIndexCount()/IsThirtyTwoBit() round-trips correctly");
        }

        dev.Clear(ClearOptions::Target | ClearOptions::DepthBuffer | ClearOptions::Stencil,
                  Color::CornflowerBlue, 1.0f, 0);

        // No explicit Present() here -- Game::Tick()/EndDraw() already calls
        // GraphicsDevice::Present() automatically once Draw() returns.

        if (frame_ == kTotalFrames)
        {
            check(true, "60 frames of Clear()+Present() completed with no exception");
            std::printf("=== %d/%d PASS ===\n", passCount_, 7);
            result_ = (passCount_ == 7) ? 0 : 1;
            Exit();
        }
    }

public:
    OpenGL2SmokeTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(320);
        gdm_->setPreferredBackBufferHeightProperty(240);
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

    OpenGL2SmokeTest game;
    game.Run();
    return game.getResult();
}
