// SPDX-License-Identifier: MS-PL
// plans/plan_llgl.md LLGL-1..LLGL-9: first end-to-end proof for the LLGL graphics renderer. A real SDL
// window, a real LLGL render system (whichever module the runtime selection picked), a real swap
// chain, and a real 60-frame Clear()+Present() loop.
//
// Check A -- GameWindow handle returns the real SDL window.
// Check B -- SDL_GetRenderer(window) is null; this renderer never creates an SDL_Renderer.
// Check C -- GetViewportSize() reports the logical resolution that was requested.
// Check D -- the loaded module is one that actually draws, and names itself.
// Check E -- VertexBuffer.SetData()/GetVertexCount() round-trips the exact count uploaded.
// Check F -- IndexBuffer.SetData16()/GetIndexCount()/IsThirtyTwoBit() round-trip correctly.
// Check G -- the 3D draw path fails loudly rather than silently drawing nothing.
// Check H -- 60 frames of Clear() + the automatic end-of-frame Present() complete with no exception.
//
// Exit code 0 = all checks PASS, 1 = any FAILs.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Graphics/ClearOptions.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"

#include "CNA/Internal/Renderers/Llgl/LlglRenderer.hpp"

#include "common/PixelTestGame.hpp"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <string>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using namespace CNA::Internal::Renderers::Llgl;

namespace
{
    constexpr int kTotalFrames = 60;
    constexpr int kExpectedChecks = 8;
}

class LlglSmokeTest : public Game
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
        auto& device = getGraphicsDeviceProperty();
        auto& renderer = static_cast<LlglRenderer&>(device.GetRenderer());

        if (frame_ == 1)
        {
            check(reinterpret_cast<SDL_Window*>(getWindowProperty().getHandleProperty()) != nullptr, "GameWindow handle returns a real window");
            check(SDL_GetRenderer(reinterpret_cast<SDL_Window*>(getWindowProperty().getHandleProperty())) == nullptr, "SDL_GetRenderer(window) is null (no SDL_Renderer)");

            int width = 0;
            int height = 0;
            renderer.GetViewportSize(width, height);
            std::printf("       GetViewportSize(): %dx%d\n", width, height);
            // The default presentation mode is FixedHeightDynamicWidth, so only the height is the
            // requested one: the logical width is derived from the window's real aspect ratio, and
            // a wider window is meant to show more horizontal content rather than be letterboxed.
            check(height == 240 && width > 0,
                  "GetViewportSize() keeps the requested logical height and derives the width");

            const auto module = renderer.GetRendererModule();
            const char* rendererName = renderer.GetRendererNameEXT();
            std::printf("       LLGL module: %s, renderer: %s\n",
                        Detail::GetRendererModuleName(module),
                        rendererName != nullptr ? rendererName : "<none>");
            check(module != Detail::RendererModule::Null && rendererName != nullptr &&
                  std::strlen(rendererName) > 0,
                  "a real (non-Null) renderer module was selected and names itself");

            auto vertexBuffer = renderer.CreateVertexBuffer(3);
            const float vertices[3 * 2] = {0, 0, 1, 0, 0, 1};
            vertexBuffer->SetData(vertices, 3, sizeof(float) * 2);
            check(vertexBuffer->GetVertexCount() == 3,
                  "VertexBuffer.SetData()+GetVertexCount() round-trips the exact count");

            auto indexBuffer = renderer.CreateIndexBuffer16(3);
            const std::uint16_t indices[3] = {0, 1, 2};
            indexBuffer->SetData16(indices, 3);
            check(indexBuffer->GetIndexCount() == 3 && !indexBuffer->IsThirtyTwoBit(),
                  "IndexBuffer.SetData16()+GetIndexCount()/IsThirtyTwoBit() round-trip correctly");

            bool threw = false;
            try
            {
                renderer.DrawColoredPrimitives(*vertexBuffer, Matrix::getIdentityProperty(),
                                              Matrix::getIdentityProperty(), Matrix::getIdentityProperty(),
                                              PrimitiveType::TriangleList, 1);
            }
            catch (const std::runtime_error&)
            {
                threw = true;
            }
            check(threw, "the not-yet-implemented 3D draw path throws instead of silently doing nothing");
        }

        device.Clear(ClearOptions::Target | ClearOptions::DepthBuffer | ClearOptions::Stencil,
                     Color::CornflowerBlue, 1.0f, 0);

        if (frame_ == kTotalFrames)
        {
            check(true, "60 frames of Clear()+Present() completed with no exception");
            std::printf("=== %d/%d PASS ===\n", passCount_, kExpectedChecks);
            result_ = (passCount_ == kExpectedChecks) ? 0 : 1;
            Exit();
        }
    }

public:
    LlglSmokeTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(320);
        gdm_->setPreferredBackBufferHeightProperty(240);
        // A virtual/headless display has no real vblank signal, so vsync would stall every frame
        // well past this test's budget. Requesting Immediate presentation through the property is
        // the supported way to switch it off before CreateDevice() reads it.
        gdm_->setSynchronizeWithVerticalRetraceProperty(false);
    }

    int getResult() const { return result_; }
};

int main()
{
    if (!CNA::Examples::ProbeGpuDisplayAvailable())
        return CNA::Examples::kSkipExitCode;

    LlglSmokeTest game;
    game.Run();
    return game.getResult();
}
