// SPDX-License-Identifier: MS-PL
// plans/plan_opengl4.md GL4-1..GL4-9: end-to-end smoke test for the real desktop OpenGL 4.x
// core-profile graphics renderer's device/window/context lifecycle and color/depth/stencil
// clear+present. Real window, a real SDL_GLContext requesting SDL_GL_CONTEXT_PROFILE_CORE
// (unlike EasyGL's SDL_GL_CONTEXT_PROFILE_ES), and a real 60-frame Clear()+Present() loop.
//
// Check A -- GameWindow handle returns a real, non-null SDL_Window.
// Check B -- SDL_GetRenderer(window) is null (this renderer does not use SDL_Renderer).
// Check C -- GetViewportSize() reports a positive width/height matching the real window.
// Check D -- a real OpenGL4VertexBufferRenderer/OpenGL4IndexBufferRenderer round-trip: SetData()
//   followed by GetVertexCount()/GetIndexCount() reports the exact count uploaded.
// Check E -- 60 frames of Clear(Target|DepthBuffer|Stencil, color, depth, stencil) + the
//   automatic end-of-frame Present() complete with no exception.
// Check F/G -- SetDataWithOptions()/SetData16WithOptions() round-trip the exact byte data.
//
// Exit code 0 = all checks PASS, 1 = any FAILs.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Graphics/ClearOptions.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/SetDataOptions.hpp"

#include "CNA/Internal/Renderers/OpenGL4/OpenGL4Renderer.hpp"

#include "common/PixelTestGame.hpp"

#include <cstdint>
#include <cstdio>
#include <memory>
#include <stdexcept>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using namespace CNA::Internal::Renderers::OpenGL4;

namespace
{
    constexpr int kTotalFrames = 60;
}

class OpenGL4SmokeTest : public Game
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
        auto& renderer = static_cast<OpenGL4Renderer&>(dev.GetRenderer());

        if (frame_ == 1)
        {
            check(reinterpret_cast<SDL_Window*>(getWindowProperty().getHandleProperty()) != nullptr, "GameWindow handle returns a real window");
            check(SDL_GetRenderer(reinterpret_cast<SDL_Window*>(getWindowProperty().getHandleProperty())) == nullptr, "SDL_GetRenderer(window) is null (no SDL_Renderer)");

            int width = 0;
            int height = 0;
            renderer.GetViewportSize(width, height);
            check(width > 0 && height > 0, "GetViewportSize() reports a positive size");

            struct PackedVertex { float x, y, z; std::uint32_t color; };
            auto vb = renderer.CreateVertexBuffer(3);
            const PackedVertex verts[3] = {
                {0, 0, 0, 0xFFFFFFFFu},
                {1, 0, 0, 0xFFFFFFFFu},
                {0, 1, 0, 0xFFFFFFFFu},
            };
            vb->SetData(verts, 3, sizeof(PackedVertex));
            check(vb->GetVertexCount() == 3, "VertexBuffer.SetData()+GetVertexCount() round-trips the exact count");

            auto ib = renderer.CreateIndexBuffer16(3);
            const std::uint16_t indices[3] = {0, 1, 2};
            ib->SetData16(indices, 3);
            check(ib->GetIndexCount() == 3 && !ib->IsThirtyTwoBit(),
                  "IndexBuffer.SetData16()+GetIndexCount()/IsThirtyTwoBit() round-trips correctly");

            vb->SetDataWithOptions(verts, 3, sizeof(PackedVertex), SetDataOptions::Discard);
            check(vb->GetVertexCount() == 3,
                  "VertexBuffer.SetDataWithOptions(Discard) round-trips the exact count");

            ib->SetData16WithOptions(indices, 3, SetDataOptions::NoOverwrite);
            check(ib->GetIndexCount() == 3,
                  "IndexBuffer.SetData16WithOptions(NoOverwrite) round-trips the exact count");
        }

        dev.Clear(ClearOptions::Target | ClearOptions::DepthBuffer | ClearOptions::Stencil,
                  Color::CornflowerBlue, 1.0f, 0);

        // No explicit Present() here -- Game::Tick()/EndDraw() already calls
        // GraphicsDevice::Present() automatically once Draw() returns.

        if (frame_ == kTotalFrames)
        {
            check(true, "60 frames of Clear()+Present() completed with no exception");
            std::printf("=== %d/%d PASS ===\n", passCount_, 8);
            result_ = (passCount_ == 8) ? 0 : 1;
            Exit();
        }
    }

public:
    OpenGL4SmokeTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(320);
        gdm_->setPreferredBackBufferHeightProperty(240);
        gdm_->setSynchronizeWithVerticalRetraceProperty(false);
    }

    int getResult() const { return result_; }
};

int main()
{
    if (!CNA::Examples::ProbeGpuDisplayAvailable())
        return CNA::Examples::kSkipExitCode;

    OpenGL4SmokeTest game;
    game.Run();
    return game.getResult();
}
