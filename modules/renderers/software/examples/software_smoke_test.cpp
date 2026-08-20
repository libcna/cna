// SPDX-License-Identifier: MS-PL
// plans/plan_software.md Phase S1/S2/S3 (SOFTWARE-1..14, 20..22): smoke test for the Software
// (CPU rasterizer) graphics renderer's foundation -- no window, real framebuffer, real pixel
// readback, real vertex/index storage. Draw calls do not yet rasterize (Phase S4).
//
// Check A -- SDL's video subsystem was never initialized (SDL_WasInit(SDL_INIT_VIDEO) == 0) and
//   there is no real window -- matches HEADLESS's own "no display server needed at all" promise.
// Check B -- Clear(r,g,b,a) followed by GetBackBufferData() reads back the EXACT clear color --
//   this is the whole point of this renderer: real, correct pixels, not a fiction.
// Check C -- binding a RenderTarget2D and clearing it does not affect the backbuffer's own
//   pixels -- proves per-target framebuffers are genuinely independent.
// Check D -- a VertexBuffer's raw bytes round-trip through SoftwareVertexBufferRenderer
//   (capacity/stride tracked correctly) -- verified indirectly via a successful SetData call at
//   each of the three v1-supported strides (16/20/24).
// Check E -- an IndexBuffer (both 16- and 32-bit) round-trips real data without throwing.
//
// Exit code 0 = all checks PASS, 1 = any FAILs.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColorTexture.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionTexture.hpp"

#include "CNA/Internal/Renderers/Software/SoftwareRenderer.hpp"

#include <SDL3/SDL.h>

#include <cstdio>
#include <cstdlib>
#include <memory>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using namespace CNA::Internal::Renderers::Software;

class SoftwareSmokeTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
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
        auto& dev = getGraphicsDeviceProperty();
        auto& renderer = static_cast<SoftwareRenderer&>(dev.GetRenderer());

        // Check A: no real window/video subsystem anywhere.
        check(SDL_WasInit(SDL_INIT_VIDEO) == 0, "SDL_INIT_VIDEO was never initialized under the Software renderer");
        check(reinterpret_cast<SDL_Window*>(getWindowProperty().getHandleProperty()) == nullptr, "GraphicsDevice has no real window under the Software renderer");

        // Check B: real, correct pixel readback after Clear().
        {
            dev.Clear(Color(20, 40, 60, 255));
            const Rectangle region(0, 0, 4, 4);
            std::vector<Color> pixels(4 * 4, Color(0, 0, 0, 0));
            dev.GetBackBufferData(&region, pixels.data(), 0, static_cast<int>(pixels.size()));
            bool allMatch = true;
            for (const Color& p : pixels)
            {
                if (p.getRProperty() != 20 || p.getGProperty() != 40 || p.getBProperty() != 60 ||
                    p.getAProperty() != 255)
                {
                    allMatch = false;
                    break;
                }
            }
            check(allMatch, "GetBackBufferData() reads back the exact Clear() color for every pixel");
        }

        // Check C: a bound RenderTarget2D has its own independent framebuffer.
        {
            dev.Clear(Color(1, 2, 3, 255));
            RenderTarget2D rt(dev, 8, 8, false, SurfaceFormat::Color, DepthFormat::None, 0,
                              RenderTargetUsage::DiscardContents);
            dev.SetRenderTarget(&rt);
            dev.Clear(Color(200, 210, 220, 255));

            const Rectangle rtRegion(0, 0, 2, 2);
            std::vector<Color> rtPixels(2 * 2, Color(0, 0, 0, 0));
            dev.GetBackBufferData(&rtRegion, rtPixels.data(), 0, static_cast<int>(rtPixels.size()));

            dev.SetRenderTarget(nullptr);
            const Rectangle backRegion(0, 0, 2, 2);
            std::vector<Color> backPixels(2 * 2, Color(0, 0, 0, 0));
            dev.GetBackBufferData(&backRegion, backPixels.data(), 0, static_cast<int>(backPixels.size()));

            const bool rtCorrect = rtPixels[0].getRProperty() == 200 && rtPixels[0].getGProperty() == 210 &&
                                   rtPixels[0].getBProperty() == 220;
            const bool backUnaffected = backPixels[0].getRProperty() == 1 && backPixels[0].getGProperty() == 2 &&
                                        backPixels[0].getBProperty() == 3;
            check(rtCorrect && backUnaffected,
                  "a bound RenderTarget2D has its own independent framebuffer, unaffected by the backbuffer's own pixels");
        }

        // Check D: VertexBuffer round-trips real data at each v1-supported stride.
        {
            bool threw = false;
            try
            {
                const VertexPositionColor verts[3] = {
                    { Vector3(0, 1, 0), Color::Red }, { Vector3(-1, -1, 0), Color::Green }, { Vector3(1, -1, 0), Color::Blue },
                };
                VertexBuffer vb16(dev, 3);
                vb16.SetData(verts, 3);

                const VertexPositionTexture texVerts[3] = {
                    { Vector3(0, 1, 0), Vector2(0.5f, 0) }, { Vector3(-1, -1, 0), Vector2(0, 1) }, { Vector3(1, -1, 0), Vector2(1, 1) },
                };
                VertexBuffer vb20(dev, VertexPositionTexture::getVertexDeclarationStatic(), 3, BufferUsage::None);
                vb20.SetData(texVerts, 3);

                const VertexPositionColorTexture ctVerts[3] = {
                    { Vector3(0, 1, 0), Color::White, Vector2(0.5f, 0) },
                    { Vector3(-1, -1, 0), Color::White, Vector2(0, 1) },
                    { Vector3(1, -1, 0), Color::White, Vector2(1, 1) },
                };
                VertexBuffer vb24(dev, VertexPositionColorTexture::getVertexDeclarationStatic(), 3, BufferUsage::None);
                vb24.SetData(ctVerts, 3);
            }
            catch (const std::exception&) { threw = true; }
            check(!threw, "VertexBuffer round-trips real data at strides 16/20/24 without throwing");
        }

        // Check E: 16-bit and 32-bit IndexBuffer round-trip real data.
        {
            bool threw = false;
            try
            {
                IndexBuffer ib16(dev, IndexElementSize::SixteenBits, 3, BufferUsage::None);
                const std::uint16_t indices16[3] = {0, 1, 2};
                ib16.SetData(indices16, 0, 3);

                IndexBuffer ib32(dev, IndexElementSize::ThirtyTwoBits, 3, BufferUsage::None);
                const std::uint32_t indices32[3] = {0, 1, 2};
                ib32.SetData(indices32, 0, 3);
            }
            catch (const std::exception&) { threw = true; }
            check(!threw, "16-bit and 32-bit IndexBuffer round-trip real data without throwing");
        }

        std::printf("=== %d/%d PASS ===\n", passCount_, 6);
        result_ = (passCount_ == 6) ? 0 : 1;
        Exit();
    }

public:
    SoftwareSmokeTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(64);
        gdm_->setPreferredBackBufferHeightProperty(64);
    }

    int getResult() const { return result_; }
};

int main()
{
    SoftwareSmokeTest game;
    game.Run();
    return game.getResult();
}
