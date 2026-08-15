// SPDX-License-Identifier: MS-PL
// plan_igl.md IGL-1..IGL-9: first end-to-end proof for the IGL graphics renderer. A real platform
// window, a real igl::IDevice on whichever backend the runtime selection picked, a real swap
// surface, and a real 60-frame Clear() + Present() loop.
//
// Check A -- the renderer names the backend it actually brought up.
// Check B -- GetViewportSize() keeps the requested logical height and derives the width.
// Check C -- GetDefaultViewportRect() reports a non-empty physical presentation rectangle.
// Check D -- VertexBuffer.SetData()/GetVertexCount() round-trips the exact count uploaded.
// Check E -- IndexBuffer.SetData16()/GetIndexCount()/IsThirtyTwoBit() round-trip correctly.
// Check F -- CreateTexture() produces a texture of the requested size.
// Check G -- the renderer reports ThreeD support, unlike a 2D-only family.
// Check H -- 60 frames of Clear() + the automatic end-of-frame Present() complete with no exception.
//
// Exit code 0 = all checks PASS, 1 = any FAILs, 77 = SKIP (no GPU/display).

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/ClearOptions.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"

#include "CNA/GraphicsCapability.hpp"
#include "CNA/Internal/Graphics/ImageData.hpp"
#include "CNA/Internal/Renderers/Igl/IglRenderer.hpp"

#include "common/PixelTestGame.hpp"

#include <cstdint>
#include <cstdio>
#include <memory>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using namespace CNA::Internal::Renderers::Igl;

namespace
{
    constexpr int kTotalFrames = 60;
    constexpr int kExpectedChecks = 8;
}

class IglSmokeTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    int frame_ = 0;
    int passCount_ = 0;
    int result_ = 1;

    void check(const bool ok, const char* label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label);
        if (ok)
            ++passCount_;
    }

protected:
    void Draw(const GameTime&) override
    {
        ++frame_;
        auto& device = getGraphicsDeviceProperty();
        auto& renderer = static_cast<IglRenderer&>(device.GetRenderer());

        if (frame_ == 1)
        {
            const char* backendName = Detail::GetRendererBackendName(renderer.GetBackend());
            std::printf("       IGL backend: %s\n", backendName);
            check(backendName != nullptr && backendName[0] != '\0',
                  "the renderer names the IGL backend it brought up");

            int width = 0;
            int height = 0;
            renderer.GetViewportSize(width, height);
            std::printf("       GetViewportSize(): %dx%d\n", width, height);
            // The default presentation mode is FixedHeightDynamicWidth, so only the height is the
            // requested one: the logical width follows the window's real aspect ratio.
            check(height == 240 && width > 0,
                  "GetViewportSize() keeps the requested logical height and derives the width");

            int x = 0;
            int y = 0;
            int rectWidth = 0;
            int rectHeight = 0;
            renderer.GetDefaultViewportRect(x, y, rectWidth, rectHeight);
            std::printf("       GetDefaultViewportRect(): %d,%d %dx%d\n", x, y, rectWidth,
                        rectHeight);
            check(rectWidth > 0 && rectHeight > 0,
                  "GetDefaultViewportRect() reports a real presentation rectangle");

            auto vertexBuffer = renderer.CreateVertexBuffer(3);
            const float vertices[3 * 4] = {0, 0, 0, 1, 1, 0, 0, 1, 0, 1, 0, 1};
            vertexBuffer->SetData(vertices, 3, sizeof(float) * 4);
            check(vertexBuffer->GetVertexCount() == 3,
                  "VertexBuffer.SetData()+GetVertexCount() round-trips the exact count");

            auto indexBuffer = renderer.CreateIndexBuffer16(3);
            const std::uint16_t indices[3] = {0, 1, 2};
            indexBuffer->SetData16(indices, 3);
            check(indexBuffer->GetIndexCount() == 3 && !indexBuffer->IsThirtyTwoBit(),
                  "IndexBuffer.SetData16()+GetIndexCount()/IsThirtyTwoBit() round-trip correctly");

            CNA::Internal::Graphics::ImageData image;
            image.width = 4;
            image.height = 4;
            image.pixels.assign(static_cast<std::size_t>(4 * 4 * 4), std::uint8_t{255});
            auto texture = renderer.CreateTexture(image);
            check(texture != nullptr && texture->GetWidth() == 4 && texture->GetHeight() == 4,
                  "CreateTexture() produces a texture of the requested size");

            check(renderer.SupportsCapability(CNA::GraphicsCapability::ThreeD),
                  "the renderer reports a real 3D pipeline");
        }

        device.Clear(ClearOptions::Target | ClearOptions::DepthBuffer | ClearOptions::Stencil,
                     Color::CornflowerBlue, 1.0f, 0);

        if (frame_ == kTotalFrames)
        {
            check(true, "60 frames of Clear() + Present() completed without an exception");
            result_ = passCount_ == kExpectedChecks ? 0 : 1;
            std::printf("%d/%d checks passed\n", passCount_, kExpectedChecks);
            Exit();
        }
    }

public:
    IglSmokeTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(320);
        gdm_->setPreferredBackBufferHeightProperty(240);
        gdm_->setSynchronizeWithVerticalRetraceProperty(false);
    }

    [[nodiscard]] int getResult() const { return result_; }
};

int main()
{
    if (!CNA::Examples::ProbeGpuDisplayAvailable())
        return CNA::Examples::kSkipExitCode;

    IglSmokeTest game;
    game.Run();
    return game.getResult();
}
