// SPDX-License-Identifier: MS-PL
// Supported-contract macOS smoke test. It exercises native device/window/view creation, confirms
// that no SDL_Renderer is involved, uploads vertex and index buffers through the normal and
// SetDataOptions routes, and completes 60 color/depth/stencil clear-and-present frames. It avoids
// backbuffer readback and every capability that is deliberately reported unsupported.
//
// Exit code 0 = all checks PASS, 1 = any FAILs.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Graphics/ClearOptions.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/SetDataOptions.hpp"

#include "CNA/Internal/Renderers/Metal/MetalRenderer.hpp"

#include "common/PixelTestGame.hpp"

#include <cstdint>
#include <cstdio>
#include <memory>
#include <stdexcept>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using namespace CNA::Internal::Renderers::Metal;

namespace
{
    constexpr int kTotalFrames = 60;
}

class MetalSmokeTest : public Game
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
        auto& renderer = static_cast<MetalRenderer&>(dev.GetRenderer());

        if (frame_ == 1)
        {
            check(reinterpret_cast<SDL_Window*>(getWindowProperty().getHandleProperty()) != nullptr, "GameWindow handle returns a real window");
            check(SDL_GetRenderer(reinterpret_cast<SDL_Window*>(getWindowProperty().getHandleProperty())) == nullptr, "SDL_GetRenderer(window) is null (no SDL_Renderer)");

            int width = 0;
            int height = 0;
            renderer.GetViewportSize(width, height);
            check(width > 0 && height > 0, "GetViewportSize() reports a positive size");

            auto vb = renderer.CreateVertexBuffer(3);
            const float verts[3 * 2] = {0, 0, 1, 0, 0, 1};
            vb->SetData(verts, 3, sizeof(float) * 2);
            check(vb->GetVertexCount() == 3, "VertexBuffer.SetData()+GetVertexCount() round-trips the exact count");

            auto ib = renderer.CreateIndexBuffer16(3);
            const std::uint16_t indices[3] = {0, 1, 2};
            ib->SetData16(indices, 3);
            check(ib->GetIndexCount() == 3 && !ib->IsThirtyTwoBit(),
                  "IndexBuffer.SetData16()+GetIndexCount()/IsThirtyTwoBit() round-trips correctly");

            const float vertsDiscard[3 * 2] = {2, 2, 3, 2, 2, 3};
            vb->SetDataWithOptions(vertsDiscard, 3, sizeof(float) * 2, SetDataOptions::Discard);
            const float vertsNoOverwrite[3 * 2] = {4, 4, 5, 4, 4, 5};
            vb->SetDataWithOptions(vertsNoOverwrite, 3, sizeof(float) * 2, SetDataOptions::NoOverwrite);
            check(vb->GetVertexCount() == 3,
                  "VertexBuffer.SetDataWithOptions(Discard then NoOverwrite) round-trips the exact count");

            const std::uint16_t indicesDiscard[3] = {2, 1, 0};
            ib->SetData16WithOptions(indicesDiscard, 3, SetDataOptions::Discard);
            const std::uint16_t indicesNoOverwrite[3] = {0, 2, 1};
            ib->SetData16WithOptions(indicesNoOverwrite, 3, SetDataOptions::NoOverwrite);
            check(ib->GetIndexCount() == 3,
                  "IndexBuffer.SetData16WithOptions(Discard then NoOverwrite) round-trips the exact count");
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
    MetalSmokeTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(320);
        gdm_->setPreferredBackBufferHeightProperty(240);
        // GraphicsDeviceManager.SynchronizeWithVerticalRetrace defaults to true (the XNA
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

    MetalSmokeTest game;
    game.Run();
    return game.getResult();
}
